// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization: the shell scripts that set the background, what the
// desktop shows today, the wallpaper and theme lists, the accent
// colours, and what the switches write; in folders of the test's own.
#include "appearance.h"
#include "wallpaper.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

namespace {

QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg
        && (qstrcmp(context.category, "qml") == 0 || qstrncmp(context.category, "qt.qml", 6) == 0
            || message.contains(QLatin1String("qrc:")))) {
        qmlWarnings << message;
    }
    fprintf(stderr, "%s\n", qPrintable(message));
}

void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

// Run a background script against a stand-in for the shell's API and
// return what it wrote: [plugin, group, key=value...] per desktop.
QStringList runScript(const QString &script, int desktops = 2)
{
    QJSEngine engine;
    const QString api = QStringLiteral(
        "var log = [];\n"
        "function desktops() {\n"
        "  var list = [];\n"
        "  for (var n = 0; n < %1; ++n) {\n"
        "    (function (n) {\n"
        "      var d = { currentConfigGroup: [] };\n"
        "      Object.defineProperty(d, 'wallpaperPlugin', { set: function (p) { log.push(n + ' plugin ' + p) } });\n"
        "      d.writeConfig = function (k, v) { log.push(n + ' ' + d.currentConfigGroup.join('/') + ' ' + k + '=' + JSON.stringify(v)) };\n"
        "      list.push(d);\n"
        "    })(n);\n"
        "  }\n"
        "  return list;\n"
        "}\n").arg(desktops);
    const QJSValue result = engine.evaluate(api + script + QStringLiteral("\nlog;"));
    if (result.isError())
        return {QStringLiteral("ERROR ") + result.toString()};
    return result.toVariant().toStringList();
}

} // namespace

class TestAppearance : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_config;
    QString m_share;

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_config = m_dir.filePath(QStringLiteral("config"));
        m_share = m_dir.filePath(QStringLiteral("share"));
        qputenv("XDG_CONFIG_HOME", m_config.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(QStringLiteral("xdg")).toUtf8());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", m_share.toUtf8());
        // no shell, no compositor, no session tools
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("PATH", "/nonexistent");
    }

    void init()
    {
        QDir(m_config).removeRecursively();
        QDir().mkpath(m_config);
        for (const char *name : {"kdeglobals", "kwinrc", "rayerc", "raye-settingsrc"})
            KSharedConfig::openConfig(QString::fromLatin1(name))->reparseConfiguration();
    }

    void fillModes()
    {
        for (const char *mode : {"fill", "fit", "stretch", "tile", "centre"})
            QCOMPARE(BackgroundSettings::fillModeName(BackgroundSettings::fillModeNumber(QString::fromLatin1(mode))), QString::fromLatin1(mode));
        QCOMPARE(BackgroundSettings::fillModeNumber(QStringLiteral("fill")), 2);
        QCOMPARE(BackgroundSettings::fillModeNumber(QStringLiteral("centre")), 6);
        QCOMPARE(BackgroundSettings::fillModeName(42), QStringLiteral("fill"));
    }

    void pictureScriptOnEveryDesktop()
    {
        const QStringList log = runScript(BackgroundSettings::pictureScript(QUrl::fromLocalFile(QStringLiteral("/home/u/Pictures/sea.png")), 1));
        QCOMPARE(log, (QStringList{QStringLiteral("0 plugin org.kde.image"),
                                   QStringLiteral("0 Wallpaper/org.kde.image/General Image=\"file:///home/u/Pictures/sea.png\""),
                                   QStringLiteral("0 Wallpaper/org.kde.image/General FillMode=1"),
                                   QStringLiteral("1 plugin org.kde.image"),
                                   QStringLiteral("1 Wallpaper/org.kde.image/General Image=\"file:///home/u/Pictures/sea.png\""),
                                   QStringLiteral("1 Wallpaper/org.kde.image/General FillMode=1")}));
    }

    void namesCannotBreakTheScript()
    {
        // quotes, a backslash, a line break and the two JS line separators
        const QString evil = QStringLiteral("/tmp/a\"); evil(\"\\b\nc\u2028d\u2029.png");
        const QStringList log = runScript(BackgroundSettings::pictureScript(QUrl::fromLocalFile(evil), 2), 1);
        QVERIFY2(!log.first().startsWith(QLatin1String("ERROR")), qPrintable(log.first()));
        QCOMPARE(log.size(), 3);
        QVERIFY(log.at(1).contains(QLatin1String("evil")));
        QCOMPARE(runScript(QStringLiteral("log.push(%1);").arg(BackgroundSettings::quoted(evil)), 0), QStringList{evil});
    }

    void colorAndSlideshowScripts()
    {
        QCOMPARE(runScript(BackgroundSettings::colorScript(QColor(0x7b, 0x3c, 0xff)), 1),
                 (QStringList{QStringLiteral("0 plugin org.kde.color"), QStringLiteral("0 Wallpaper/org.kde.color/General Color=\"123,60,255\"")}));
        QCOMPARE(runScript(BackgroundSettings::slideshowScript(QStringLiteral("/home/u/Pictures"), 600, false), 1),
                 (QStringList{QStringLiteral("0 plugin org.kde.slideshow"),
                              QStringLiteral("0 Wallpaper/org.kde.slideshow/General SlidePaths=[\"/home/u/Pictures\"]"),
                              QStringLiteral("0 Wallpaper/org.kde.slideshow/General SlideInterval=600"),
                              QStringLiteral("0 Wallpaper/org.kde.slideshow/General SlideshowMode=1")}));
        // an interval out of reason is brought back
        QVERIFY(runScript(BackgroundSettings::slideshowScript(QStringLiteral("/p"), 1, true), 1).contains(
            QStringLiteral("0 Wallpaper/org.kde.slideshow/General SlideInterval=10")));
    }

    void readCurrent()
    {
        const QString file = m_dir.filePath(QStringLiteral("appletsrc"));
        writeFile(file,
                  "[Containments][2]\nformfactor=2\nplugin=org.kde.panel\n\n"
                  "[Containments][7]\nformfactor=0\nlastScreen=1\nplugin=org.kde.plasma.folder\nwallpaperplugin=org.kde.color\n\n"
                  "[Containments][7][Wallpaper][org.kde.color][General]\nColor=10,20,30\n\n"
                  "[Containments][9]\nformfactor=0\nlastScreen=0\nplugin=org.kde.plasma.folder\nwallpaperplugin=org.kde.image\n\n"
                  "[Containments][9][Wallpaper][org.kde.image][General]\nImage=file:///usr/share/wallpapers/Raye/\nFillMode=6\n");
        BackgroundSettings::Current c = BackgroundSettings::readCurrent(file);
        QCOMPARE(c.mode, QStringLiteral("picture"));
        QCOMPARE(c.image, QUrl(QStringLiteral("file:///usr/share/wallpapers/Raye/")));
        QCOMPARE(c.fillMode, 6);

        writeFile(file,
                  "[Containments][3]\nformfactor=0\nwallpaperplugin=org.kde.slideshow\n\n"
                  "[Containments][3][Wallpaper][org.kde.slideshow][General]\nSlidePaths=/home/u/Pictures/\nSlideInterval=300\nSlideshowMode=1\n");
        c = BackgroundSettings::readCurrent(file);
        QCOMPARE(c.mode, QStringLiteral("slideshow"));
        QCOMPARE(c.folder, QStringLiteral("/home/u/Pictures/"));
        QCOMPARE(c.interval, 300);
        QVERIFY(!c.shuffle);
        // no file: a picture, the default
        QCOMPARE(BackgroundSettings::readCurrent(m_dir.filePath(QStringLiteral("none"))).mode, QStringLiteral("picture"));
    }

    void wallpapersRayeFirst()
    {
        const QString folder = m_dir.filePath(QStringLiteral("wallpapers"));
        writeFile(folder + QLatin1String("/Aurora/metadata.json"), R"({"KPlugin": {"Name": "Aurora", "Name[fr]": "Aurore"}})");
        writeFile(folder + QLatin1String("/Aurora/contents/images/1920x1080.png"), "png");
        writeFile(folder + QLatin1String("/Raye/metadata.json"), R"({"KPlugin": {"Name": "Raye"}})");
        writeFile(folder + QLatin1String("/Raye-Dune/metadata.json"), R"({"KPlugin": {"Name": "Dune"}})");
        writeFile(folder + QLatin1String("/Raye-Dune/contents/images/1920x1080.jpg"), "jpg");
        writeFile(folder + QLatin1String("/Raye/contents/images/3840x2160.png"), "png");
        writeFile(folder + QLatin1String("/Empty/metadata.json"), R"({"KPlugin": {"Name": "Empty"}})");
        writeFile(folder + QLatin1String("/loose.jpg"), "jpg");
        writeFile(folder + QLatin1String("/notes.txt"), "txt");
        const QVariantList list = BackgroundSettings::scanWallpapers({folder});
        QStringList names;
        for (const QVariant &v : list)
            names << v.toMap().value(QStringLiteral("name")).toString();
        // the default first even when another of Raye's sorts before it
        QCOMPARE(names, (QStringList{QStringLiteral("Raye"), QStringLiteral("Dune"), QStringLiteral("Aurora"), QStringLiteral("loose")}));
        QCOMPARE(list.first().toMap().value(QStringLiteral("preview")).toUrl().toLocalFile(),
                 folder + QLatin1String("/Raye/contents/images/3840x2160.png"));
    }

    void animatedScriptOnEveryDesktop()
    {
        const QStringList log = runScript(BackgroundSettings::animatedScript(QUrl::fromLocalFile(QStringLiteral("/v/sea.webm")),
                                                                             QUrl::fromLocalFile(QStringLiteral("/v/sea.jpg")), true, false), 2);
        QCOMPARE(log.size(), 10);
        QCOMPARE(log.at(0), QStringLiteral("0 plugin org.raye.animated"));
        QCOMPARE(log.at(1), QStringLiteral("0 Wallpaper/org.raye.animated/General Source=\"file:///v/sea.webm\""));
        QCOMPARE(log.at(3), QStringLiteral("0 Wallpaper/org.raye.animated/General PauseCovered=true"));
        QCOMPARE(log.at(4), QStringLiteral("0 Wallpaper/org.raye.animated/General PauseOnBattery=false"));
        QCOMPARE(log.at(5), QStringLiteral("1 plugin org.raye.animated"));
    }

    void readAnimated()
    {
        const QString file = m_dir.filePath(QStringLiteral("appletsrc-animated"));
        writeFile(file,
                  "[Containments][4]\nformfactor=0\nlastScreen=0\nwallpaperplugin=org.raye.animated\n\n"
                  "[Containments][4][Wallpaper][org.raye.animated][General]\nSource=file:///v/sea.webm\nPauseOnBattery=false\n");
        const BackgroundSettings::Current c = BackgroundSettings::readCurrent(file);
        QCOMPARE(c.mode, QStringLiteral("animated"));
        QCOMPARE(c.animation, QUrl(QStringLiteral("file:///v/sea.webm")));
        QVERIFY(c.pauseCovered);
        QVERIFY(!c.pauseOnBattery);
    }

    void lighterCopy()
    {
        QCOMPARE(BackgroundSettings::parseRate(QStringLiteral("30000/1001")), 30000.0 / 1001);
        QCOMPARE(BackgroundSettings::parseRate(QStringLiteral("60/1")), 60.0);
        QCOMPARE(BackgroundSettings::parseRate(QStringLiteral("0/0")), 0.0);
        QCOMPARE(BackgroundSettings::parseRate(QStringLiteral("junk")), 0.0);
        QVERIFY(!BackgroundSettings::needsLighterCopy(29.97, 1080));
        QVERIFY(!BackgroundSettings::needsLighterCopy(24, 720));
        QVERIFY(BackgroundSettings::needsLighterCopy(60, 1080));
        QVERIFY(BackgroundSettings::needsLighterCopy(24, 2160));
        const QStringList args = BackgroundSettings::lighterArguments(QStringLiteral("/v/in.mkv"), QStringLiteral("/o/out.mp4"));
        QVERIFY(args.contains(QStringLiteral("-an")));
        QCOMPARE(args.last(), QStringLiteral("/o/out.mp4"));
        const QString filter = args.at(args.indexOf(QStringLiteral("-vf")) + 1);
        QVERIFY(filter.contains(QLatin1String("min(1080,ih)")));
        QVERIFY(filter.contains(QLatin1String("min(30,source_fps)")));
    }

    void videoReadyAfterANewerChoiceIsDropped()
    {
        const QString bin = m_dir.filePath(QStringLiteral("videobin"));
        QDir().mkpath(bin);
        auto tool = [&](const char *name, const QByteArray &body) {
            QFile f(bin + QLatin1Char('/') + QLatin1String(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n" + body);
            f.close();
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        };
        // a 4K video at 60 frames: a lighter copy is made first, slowly
        tool("ffprobe", "printf 'avg_frame_rate=60/1\\nheight=2160\\n'\n");
        tool("ffmpeg", "sleep 1\nfor last; do :; done\n: > \"$last\"\n");
        const QString video = m_dir.filePath(QStringLiteral("sea.mp4"));
        QFile v(video);
        QVERIFY(v.open(QIODevice::WriteOnly));
        v.close();
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", bin.toUtf8());

        BackgroundSettings background;
        background.setAnimation(QUrl::fromLocalFile(video));
        QVERIFY(background.preparing());
        background.setColor(QColor(0x20, 0x20, 0x20));
        QCOMPARE(background.mode(), QStringLiteral("color"));
        QVERIFY(!background.preparing());
        QTest::qWait(3000);
        qputenv("PATH", path);
        QCOMPARE(background.mode(), QStringLiteral("color"));

        // and with nothing chosen meanwhile, the video does go on
        qputenv("PATH", bin.toUtf8());
        background.setAnimation(QUrl::fromLocalFile(video));
        QTRY_COMPARE_WITH_TIMEOUT(background.mode(), QStringLiteral("animated"), 5000);
        qputenv("PATH", path);
        QVERIFY(!background.preparing());
    }

    void animationsWithNames()
    {
        const QString folder = m_dir.filePath(QStringLiteral("motion"));
        writeFile(folder + QLatin1String("/metadata.json"), R"({"raye-strata": {"Name": "Strata in motion", "Name[fr]": "Strates"}})");
        writeFile(folder + QLatin1String("/raye-strata.webm"), "webm");
        writeFile(folder + QLatin1String("/raye-strata.jpg"), "jpg");
        writeFile(folder + QLatin1String("/cat.gif"), "gif");
        writeFile(folder + QLatin1String("/notes.txt"), "txt");
        const QVariantList list = BackgroundSettings::scanAnimations({folder});
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("cat"));
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("preview")).toUrl().toLocalFile(), folder + QLatin1String("/cat.gif"));
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Strata in motion"));
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("preview")).toUrl().toLocalFile(), folder + QLatin1String("/raye-strata.jpg"));
    }

    void recentImages()
    {
        QStringList recent;
        for (int i = 0; i < 7; ++i)
            recent = BackgroundSettings::pushRecent(recent, QStringLiteral("/p/%1.png").arg(i));
        QCOMPARE(recent.size(), 5);
        QCOMPARE(recent.first(), QStringLiteral("/p/6.png"));
        recent = BackgroundSettings::pushRecent(recent, QStringLiteral("/p/4.png"));
        QCOMPARE(recent.first(), QStringLiteral("/p/4.png"));
        QCOMPARE(recent.size(), 5);
        QCOMPARE(recent.count(QStringLiteral("/p/4.png")), 1);
    }

    void swatches()
    {
        const QStringList colors = AppearanceSettings::swatchColors();
        QCOMPARE(colors.size(), 24);
        QCOMPARE(colors.first(), QStringLiteral("#7b3cff"));
        QCOMPARE(QSet<QString>(colors.cbegin(), colors.cend()).size(), 24);
        for (const QString &c : colors) {
            const QColor color(c);
            QVERIFY2(color.isValid(), qPrintable(c));
            // white text on it stays readable (WCAG contrast 4:1 or more)
            auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            const double l = 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
            QVERIFY2(1.05 / (l + 0.05) >= 4.0, qPrintable(c));
        }
        AppearanceSettings a;
        QCOMPARE(a.swatchIndex(QColor(123, 60, 255)), 0);
        QCOMPARE(a.swatchIndex(QColor(124, 61, 254)), 0);
        QCOMPARE(a.swatchIndex(QColor(1, 2, 3)), -1);
    }

    void themes()
    {
        const QString folder = m_dir.filePath(QStringLiteral("lnf"));
        writeFile(folder + QLatin1String("/org.kde.breeze.desktop/metadata.json"), R"({"KPlugin": {"Id": "org.kde.breeze.desktop", "Name": "Breeze"}})");
        writeFile(folder + QLatin1String("/org.raye.day.desktop/metadata.json"),
                  R"({"KPlugin": {"Id": "org.raye.day.desktop", "Name": "Raye Day", "Name[fr]": "Raye Jour"}})");
        writeFile(folder + QLatin1String("/org.raye.day.desktop/contents/previews/preview.png"), "png");
        writeFile(folder + QLatin1String("/org.raye.night.desktop/metadata.json"), R"({"KPlugin": {"Id": "org.raye.night.desktop", "Name": "Raye Night"}})");
        writeFile(folder + QLatin1String("/broken/metadata.json"), "nope");
        const QVariantList list = AppearanceSettings::scanThemes({folder}, QStringLiteral("org.raye.day.desktop"));
        QStringList ids;
        for (const QVariant &v : list)
            ids << v.toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(ids, (QStringList{QStringLiteral("org.raye.night.desktop"), QStringLiteral("org.raye.day.desktop"), QStringLiteral("org.kde.breeze.desktop")}));
        QVERIFY(list.at(1).toMap().value(QStringLiteral("current")).toBool());
        QVERIFY(list.at(1).toMap().value(QStringLiteral("preview")).toUrl().isLocalFile());
        QVERIFY(!list.at(0).toMap().value(QStringLiteral("preview")).toUrl().isValid());
        QVERIFY(list.at(0).toMap().value(QStringLiteral("raye")).toBool());
        QVERIFY(!list.at(2).toMap().value(QStringLiteral("raye")).toBool());
    }

    void modes()
    {
        QCOMPARE(AppearanceSettings::modeOf(QStringLiteral("RayeNight"), QColor()), QStringLiteral("dark"));
        QCOMPARE(AppearanceSettings::modeOf(QStringLiteral("RayeDay"), QColor()), QStringLiteral("light"));
        QCOMPARE(AppearanceSettings::modeOf(QStringLiteral("Something"), QColor(250, 250, 250)), QStringLiteral("light"));
        QCOMPARE(AppearanceSettings::modeOf(QStringLiteral("Something"), QColor(20, 20, 20)), QStringLiteral("dark"));
        QCOMPARE(AppearanceSettings::themeForMode(QStringLiteral("light")), QStringLiteral("org.raye.day.desktop"));
        QCOMPARE(AppearanceSettings::themeForMode(QStringLiteral("dark")), QStringLiteral("org.raye.night.desktop"));
        QVERIFY(AppearanceSettings::themeForMode(QStringLiteral("custom")).isEmpty());

        writeFile(m_config + QLatin1String("/kdeglobals"), "[General]\nColorScheme=RayeDay\n");
        AppearanceSettings a;
        QCOMPARE(a.mode(), QStringLiteral("light"));
        QCOMPARE(a.currentTheme(), QStringLiteral("org.raye.night.desktop"));
        // without the tool nothing is applied, and nothing breaks
        QVERIFY(!a.applyTheme(QStringLiteral("org.raye.day.desktop")));
    }

    void switchesWrite()
    {
        AppearanceSettings a;
        QVERIFY(a.transparency());
        a.setTransparency(false);
        KConfig globals(m_config + QLatin1String("/kdeglobals"), KConfig::SimpleConfig);
        QCOMPARE(globals.group(QStringLiteral("Raye")).readEntry("TransparencyEffects", true), false);
        KConfig kwin(m_config + QLatin1String("/kwinrc"), KConfig::SimpleConfig);
        QCOMPARE(kwin.group(QStringLiteral("Plugins")).readEntry("blurEnabled", true), false);
        QCOMPARE(kwin.group(QStringLiteral("Plugins")).readEntry("contrastEnabled", true), false);
        QVERIFY(!a.transparency());
        a.setTransparency(true);
        KConfig kwin2(m_config + QLatin1String("/kwinrc"), KConfig::SimpleConfig);
        // back to the system's default, not a value of the user's
        QVERIFY(!kwin2.group(QStringLiteral("Plugins")).hasKey("blurEnabled"));
        QVERIFY(a.transparency());

        QVERIFY(!a.accentOnBorders());
        a.setAccentOnBorders(true);
        KConfig rayerc(m_config + QLatin1String("/rayerc"), KConfig::SimpleConfig);
        QCOMPARE(rayerc.group(QStringLiteral("Windeco")).readEntry("AccentBorder", false), true);
        QVERIFY(a.rayeBorders());

        a.setAccentColor(QColor(QStringLiteral("#2672d9")));
        QCOMPARE(a.accentColor(), QColor(QStringLiteral("#2672d9")));
        QVERIFY(!a.accentFromWallpaper());
        a.setAccentFromWallpaper(true);
        QVERIFY(a.accentFromWallpaper());
        a.setAccentFromWallpaper(false);
        QVERIFY(!a.accentFromWallpaper());
        // the colour shown stays when going back to manual
        QCOMPARE(a.accentColor(), QColor(QStringLiteral("#2672d9")));
    }

    // Raye writes a picked accent as a whole scheme, without AccentColor:
    // the accent is then the selection colour
    void accentShown()
    {
        KConfig globals(m_config + QLatin1String("/accent-kdeglobals"), KConfig::SimpleConfig);
        KConfigGroup general = globals.group(QStringLiteral("General"));
        KConfigGroup selection = globals.group(QStringLiteral("Colors:Selection"));
        QCOMPARE(AppearanceSettings::accentOf(general, selection), QColor(QStringLiteral("#7b3cff")));
        selection.writeEntry("BackgroundNormal", QColor(38, 114, 217));
        QCOMPARE(AppearanceSettings::accentOf(general, selection), QColor(QStringLiteral("#2672d9")));
        // Plasma's own accent (another scheme, or from the wallpaper) wins
        general.writeEntry("AccentColor", QColor(214, 51, 108));
        QCOMPARE(AppearanceSettings::accentOf(general, selection), QColor(QStringLiteral("#d6336c")));
    }

    // the pages of this package load as Settings shows them: no QML
    // warning, and every row the search knows is on its page
    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        QTest::newRow("landing") << QStringLiteral("pages/personalization/PersonalizationPage.qml") << QStringLiteral("personalization");
        QTest::newRow("background") << QStringLiteral("pages/personalization/BackgroundPage.qml") << QStringLiteral("personalization/background");
        QTest::newRow("colors") << QStringLiteral("pages/personalization/ColorsPage.qml") << QStringLiteral("personalization/colors");
        QTest::newRow("themes") << QStringLiteral("pages/personalization/ThemesPage.qml") << QStringLiteral("personalization/themes");
        QTest::newRow("fonts") << QStringLiteral("pages/personalization/FontsPage.qml") << QStringLiteral("personalization/fonts");
        QTest::newRow("sounds") << QStringLiteral("pages/personalization/SoundsPage.qml") << QString();
    }

    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);

        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/personalization.json"));
        QVERIFY(fragment.open(QIODevice::ReadOnly));
        QStringList indexed;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        QVERIFY(!entries.isEmpty());
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
            if (!page.isEmpty() && id.startsWith(page + QLatin1Char('#')))
                indexed << id.section(QLatin1Char('#'), 1);
        }

        // one picture, so the Background page draws its tiles too
        QDir().mkpath(m_share + QLatin1String("/wallpapers"));
        QImage tile(16, 9, QImage::Format_RGB32);
        tile.fill(Qt::darkGray);
        QVERIFY(tile.save(m_share + QLatin1String("/wallpapers/raye-tile.png")));

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            QQuickView view(&engine, nullptr);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1120, 760);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + file));
            QCOMPARE(view.status(), QQuickView::Ready);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(500);

            if (!page.isEmpty()) {
                QVariant ids;
                QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
                const QStringList drawn = ids.toStringList();
                for (const QString &id : std::as_const(indexed))
                    QVERIFY2(drawn.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not on the page")));
                for (const QString &id : drawn)
                    QVERIFY2(indexed.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not in data/search")));
            }
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestAppearance)
#include "test_appearance.moc"
