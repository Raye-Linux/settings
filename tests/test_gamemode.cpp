// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Game Mode: the choices file the watcher reads, the games it
// listed, turning one game off or one Windows program on, and the page.
#include "gamemode.h"
#include "systeminfo.h"

#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(text);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QQuickItem *withProperty(QQuickItem *item, const char *property, const QString &value)
{
    if (!item)
        return nullptr;
    if (item->property(property).toString() == value)
        return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        if (QQuickItem *found = withProperty(child, property, value))
            return found;
    return nullptr;
}

QVariantMap gameOf(const GameModeSettings &gm, const QString &key)
{
    for (const QVariant &v : gm.games())
        if (v.toMap().value(QStringLiteral("key")).toString() == key)
            return v.toMap();
    return {};
}

} // namespace

class TestGameMode : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString path(const QString &name) const { return m_dir.filePath(name); }
    QString conf() const { return path(QStringLiteral("config/raye/game-mode.conf")); }
    QString games() const { return path(QStringLiteral("state/raye/game-mode/games")); }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", path(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", path(QStringLiteral("data")).toUtf8());
        qputenv("XDG_STATE_HOME", path(QStringLiteral("state")).toUtf8());
        qputenv("XDG_RUNTIME_DIR", path(QStringLiteral("run")).toUtf8());
        qputenv("HOME", path(QStringLiteral("home")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        // no profile reader: every Windows program starts off
        qputenv("RAYE_WINDOWS_PROFILE", path(QStringLiteral("no-reader")).toUtf8());
        // no lister either: the tests say what is installed
        qputenv("RAYE_WINDOWS_LIST", path(QStringLiteral("no-lister")).toUtf8());
        qputenv("RAYE_GAME_FLATPAK_SYSTEM", path(QStringLiteral("flatpak")).toUtf8());
        // the watcher is installed
        writeFile(path(QStringLiteral("raye-game-watch")), "#!/bin/sh\n");
        qputenv("RAYE_GAME_WATCH", path(QStringLiteral("raye-game-watch")).toUtf8());
        QDir().mkpath(path(QStringLiteral("home")));
    }

    void init()
    {
        QFile::remove(conf());
        QFile::remove(games());
        QDir(path(QStringLiteral("data"))).removeRecursively();
        QDir(path(QStringLiteral("home/.steam"))).removeRecursively();
        QDir(path(QStringLiteral("flatpak"))).removeRecursively();
        QFile::remove(path(QStringLiteral("run/raye-game-mode/active")));
        QDir().mkpath(path(QStringLiteral("run")));
        QFile::setPermissions(path(QStringLiteral("run")),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    void onWithoutAFile()
    {
        GameModeSettings gm;
        QVERIFY(gm.enabled());
        QVERIFY(gm.effects());
        QVERIFY(gm.scheduler());
        QVERIFY(gm.power());
        QVERIFY(gm.games().isEmpty());
    }

    void choicesAreWrittenForTheWatcher()
    {
        GameModeSettings gm;
        QSignalSpy spy(&gm, &GameModeSettings::changed);
        gm.setEffects(false);
        QCOMPARE(spy.count(), 1);
        QVERIFY(readFile(conf()).contains("effects=false\n"));
        // the shell reads key=value lines, nothing else
        const QRegularExpression line(QStringLiteral(
            "^(enabled|effects|scheduler|power)=(true|false)$|^(on|off)=([a-z]+:[A-Za-z0-9._-]+( [a-z]+:[A-Za-z0-9._-]+)*)?$"));
        for (const QByteArray &l : readFile(conf()).split('\n')) {
            if (l.isEmpty() || l.startsWith('#'))
                continue;
            QVERIFY2(line.match(QString::fromUtf8(l)).hasMatch(), l.constData());
        }
        gm.setEnabled(false);
        gm.setScheduler(false);
        gm.setPower(false);
        GameModeSettings again;
        QVERIFY(!again.enabled());
        QVERIFY(!again.effects());
        QVERIFY(!again.scheduler());
        QVERIFY(!again.power());
        // the same answer twice writes nothing and says nothing
        QSignalSpy quiet(&again, &GameModeSettings::changed);
        again.setEnabled(false);
        QCOMPARE(quiet.count(), 0);
    }

    void steamGame(const QString &id, const QString &library = {})
    {
        writeFile((library.isEmpty() ? path(QStringLiteral("data/Steam")) : library)
                      + QStringLiteral("/steamapps/appmanifest_") + id + QStringLiteral(".acf"),
                  "\"AppState\" {}\n");
    }

    void storeApp(const QString &id)
    {
        QDir().mkpath(path(QStringLiteral("flatpak/app/")) + id + QStringLiteral("/x86_64/stable/active"));
        QFile::link(QStringLiteral("x86_64/stable"), path(QStringLiteral("flatpak/app/")) + id + QStringLiteral("/current"));
    }

    void playedGamesAreListedLastFirst()
    {
        steamGame(QStringLiteral("570"));
        steamGame(QStringLiteral("1091500"));
        storeApp(QStringLiteral("org.vinegarhq.Sober"));
        writeFile(games(), "steam:570\tDota 2\napp:org.vinegarhq.Sober\tSober\nsteam:1091500\t\n");
        GameModeSettings gm;
        const QVariantList list = gm.games();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("steam:1091500"));
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Sober"));
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("store"));
        QCOMPARE(list.at(2).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("steam"));
        // a Steam game without a name says which one it is
        QVERIFY(list.at(0).toMap().value(QStringLiteral("name")).toString().contains(QLatin1String("1091500")));
        QVERIFY(list.at(0).toMap().value(QStringLiteral("on")).toBool());
    }

    void onlyInstalledGamesAreListed()
    {
        // played once, then uninstalled: Steam removed its manifest, the
        // Store app is gone, the Windows space holds no program any more
        writeFile(games(), "steam:570\tDota 2\napp:org.vinegarhq.Sober\tSober\nwindows:fl-studio\tFL Studio\n"
                           "steam:730\tCounter-Strike 2\n");
        steamGame(QStringLiteral("730"), path(QStringLiteral("games-disk/SteamLibrary")));
        writeFile(path(QStringLiteral("home/.steam/steam/steamapps/libraryfolders.vdf")),
                  QStringLiteral("\"libraryfolders\" { \"1\" { \"path\" \"%1\" } }\n")
                      .arg(path(QStringLiteral("games-disk/SteamLibrary"))).toUtf8());
        for (const char *space : {"default", "fl-studio", "roblox-studio", "winrar", "notepad-plus"})
            QDir().mkpath(path(QStringLiteral("data/raye/windows/")) + QLatin1String(space) + QStringLiteral("/drive_c"));
        GameModeSettings gm;
        gm.setWindowsKinds(QByteArray(
            "{\"fl-studio\": {\"name\": \"FL Studio\", \"game\": null}, \"roblox-studio\": {\"name\": \"Roblox Studio\", \"game\": null},"
            " \"winrar\": {\"name\": \"WinRAR\", \"game\": null}, \"default\": {\"name\": null, \"game\": null}}"));
        // no program installed in any space yet
        gm.setWindowsPrograms("[]");
        QVariantList list = gm.games();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("steam:730"));
        // WinRAR installed (and something in the default space): only WinRAR
        gm.setWindowsPrograms(QByteArray("[{\"space\": \"winrar\", \"key\": \"WinRAR archiver\", \"name\": \"WinRAR 7.0 (64-bit)\"},"
                                         " {\"space\": \"default\", \"key\": \"x\", \"name\": \"Something\"},"
                                         " {\"space\": \"notepad-plus\", \"key\": \"Notepad++\", \"name\": \"Notepad++\"}]"));
        list = gm.games();
        QStringList keys;
        for (const QVariant &v : list)
            keys << v.toMap().value(QStringLiteral("key")).toString();
        QCOMPARE(keys, (QStringList{QStringLiteral("steam:730"), QStringLiteral("windows:notepad-plus"), QStringLiteral("windows:winrar")}));
        // the profile's name first, the program's own otherwise
        QCOMPARE(gameOf(gm, QStringLiteral("windows:winrar")).value(QStringLiteral("name")).toString(), QStringLiteral("WinRAR"));
        QCOMPARE(gameOf(gm, QStringLiteral("windows:notepad-plus")).value(QStringLiteral("name")).toString(), QStringLiteral("Notepad++"));
        QVERIFY(gameOf(gm, QStringLiteral("windows:default")).isEmpty());
        QVERIFY(gameOf(gm, QStringLiteral("steam:570")).isEmpty());
        QVERIFY(gameOf(gm, QStringLiteral("app:org.vinegarhq.Sober")).isEmpty());
    }

    void oneGameCanBeTurnedOff()
    {
        steamGame(QStringLiteral("570"));
        writeFile(games(), "steam:570\tDota 2\n");
        GameModeSettings gm;
        gm.setGameOn(QStringLiteral("steam:570"), false);
        QVERIFY(readFile(conf()).contains("off=steam:570\n"));
        QVERIFY(!gameOf(gm, QStringLiteral("steam:570")).value(QStringLiteral("on")).toBool());
        gm.setGameOn(QStringLiteral("steam:570"), true);
        QVERIFY(!readFile(conf()).contains("steam:570"));
        QVERIFY(gameOf(gm, QStringLiteral("steam:570")).value(QStringLiteral("on")).toBool());
    }

    void windowsProgramsAreListedOffUnlessAGame()
    {
        QDir().mkpath(path(QStringLiteral("data/raye/windows/some-tool/drive_c")));
        QDir().mkpath(path(QStringLiteral("data/raye/windows/battle-net/drive_c")));
        QDir().mkpath(path(QStringLiteral("data/raye/windows/.template/drive_c")));
        GameModeSettings gm;
        gm.setWindowsPrograms(QByteArray("[{\"space\": \"some-tool\", \"key\": \"t\", \"name\": \"Some Tool\"},"
                                         " {\"space\": \"battle-net\", \"key\": \"b\", \"name\": \"Battle.net\"}]"));
        gm.setWindowsKinds(QByteArray(
            "{\"some-tool\": {\"profile\": null, \"name\": null, \"optimized\": false, \"game\": null},"
            " \"battle-net\": {\"profile\": \"battle-net\", \"name\": \"Battle.net\", \"optimized\": false, \"game\": \"launcher\"}}"));
        const QVariantMap tool = gameOf(gm, QStringLiteral("windows:some-tool"));
        QCOMPARE(tool.value(QStringLiteral("source")).toString(), QStringLiteral("windows"));
        QVERIFY(!tool.value(QStringLiteral("on")).toBool());
        const QVariantMap launcher = gameOf(gm, QStringLiteral("windows:battle-net"));
        QVERIFY(launcher.value(QStringLiteral("on")).toBool());
        QCOMPARE(launcher.value(QStringLiteral("name")).toString(), QStringLiteral("Battle.net"));
        QVERIFY(gameOf(gm, QStringLiteral("windows:.template")).isEmpty());
        // turned on by hand: the watcher finds it in on=
        gm.setGameOn(QStringLiteral("windows:some-tool"), true);
        QVERIFY(readFile(conf()).contains("on=windows:some-tool\n"));
        QVERIFY(gameOf(gm, QStringLiteral("windows:some-tool")).value(QStringLiteral("on")).toBool());
        // a launcher turned off goes in off=
        gm.setGameOn(QStringLiteral("windows:battle-net"), false);
        QVERIFY(readFile(conf()).contains("off=windows:battle-net\n"));
        gm.setGameOn(QStringLiteral("windows:some-tool"), false);
        QVERIFY(!readFile(conf()).contains("some-tool"));
    }

    void oddKeysAreRefused()
    {
        GameModeSettings gm;
        gm.setGameOn(QStringLiteral("steam:570 enabled=false"), false);
        gm.setGameOn(QStringLiteral("steam:570\nenabled=false"), false);
        gm.setGameOn(QStringLiteral("nothing"), false);
        QVERIFY(!readFile(conf()).contains("570"));
        QVERIFY(GameModeSettings().enabled());
    }

    void playingNowIsShown()
    {
        GameModeSettings gm;
        QVERIFY(!gm.active());
        writeFile(path(QStringLiteral("run/raye-game-mode/active")), "");
        gm.refresh();
        QVERIFY(gm.active());
    }

    void pageLoads_data()
    {
        QTest::addColumn<QByteArray>("lang");
        QTest::newRow("en") << QByteArray("en");
        QTest::newRow("fr") << QByteArray("fr");
    }

    void pageLoads()
    {
        QFETCH(QByteArray, lang);
        qputenv("LANGUAGE", lang);
        steamGame(QStringLiteral("570"));
        writeFile(games(), "steam:570\tDota 2\n");
        SystemInfo system;
        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQuickView view;
            view.engine()->rootContext()->setContextObject(new KLocalizedContext(view.engine()));
            view.engine()->rootContext()->setContextProperty(QStringLiteral("system"), &system);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1120, 760);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/gaming/GamingPage.qml")));
            QCOMPARE(view.status(), QQuickView::Ready);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(100);
            QVERIFY(withProperty(view.rootObject(), "title", QStringLiteral("Dota 2")));
        }
        qInstallMessageHandler(previous);
        qunsetenv("LANGUAGE");
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestGameMode)
#include "test_gamemode.moc"
