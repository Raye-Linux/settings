// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility: every setting read and written with the key names of
// the desktop's own modules (Plasma 6.6 kcfg files), in config folders
// of the test's own. Then the nine pages load as the window loads them:
// no QML warning, and every row the search knows is on its page.
#include "accessibility.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
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

QString entry(const QString &path, const QString &group, const QString &key)
{
    KConfig config(path, KConfig::SimpleConfig);
    return config.group(group).readEntry(key, QStringLiteral("<unset>"));
}

qreal pointSize(const QString &font)
{
    QFont f;
    f.fromString(font);
    return f.pointSizeF();
}

// what Raye ships in /etc/xdg/kdeglobals
const QByteArray rayeFonts =
    "[General]\n"
    "font=Inter,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1\n"
    "menuFont=Inter,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1\n"
    "toolBarFont=Inter,9,-1,5,400,0,0,0,0,0,0,0,0,0,0,1\n"
    "smallestReadableFont=Inter,9,-1,5,400,0,0,0,0,0,0,0,0,0,0,1\n"
    "fixed=Cascadia Mono,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1\n"
    "[WM]\n"
    "activeFont=Inter,9,-1,5,500,0,0,0,0,0,0,0,0,0,0,1\n";

} // namespace

class TestAccessibility : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_home;
    QString m_system;
    QString m_data;

    QString user(const QString &file) const { return m_home + QLatin1Char('/') + file; }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_home = m_dir.filePath(QStringLiteral("config"));
        m_system = m_dir.filePath(QStringLiteral("xdg"));
        m_data = m_dir.filePath(QStringLiteral("share"));
        qputenv("XDG_CONFIG_HOME", m_home.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_system.toUtf8());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", m_data.toUtf8());
        qputenv("HOME", m_dir.filePath(QStringLiteral("home")).toUtf8());
        // no session: nothing is started or told, only the files change
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void init()
    {
        for (const QString &dir : {m_home, m_system, m_data}) {
            QDir(dir).removeRecursively();
            QDir().mkpath(dir);
        }
        for (const char *name : {"kdeglobals", "kwinrc", "rayeshellrc", "kcminputrc", "kaccessrc"})
            KSharedConfig::openConfig(QString::fromLatin1(name))->reparseConfiguration();
    }

    // ---- Text size --------------------------------------------------

    void scaledFont()
    {
        const QString inter = QStringLiteral("Inter,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1");
        QCOMPARE(pointSize(TextSizeSettings::scaledFont(inter, 100, 150)), 15.0);
        QCOMPARE(pointSize(TextSizeSettings::scaledFont(inter, 100, 225)), 22.5);
        QCOMPARE(pointSize(TextSizeSettings::scaledFont(inter, 125, 100)), 8.0);
        // the family, weight and the rest are kept
        QFont f;
        f.fromString(TextSizeSettings::scaledFont(inter, 100, 175));
        QCOMPARE(f.family(), QStringLiteral("Inter"));
        QCOMPARE(f.weight(), QFont::Normal);
        // 9 pt there and back, through every step, ends at 9 pt
        QString font = QStringLiteral("Inter,9,-1,5,500,0,0,0,0,0,0,0,0,0,0,1");
        int at = 100;
        for (int to : {105, 130, 225, 160, 115, 100}) {
            font = TextSizeSettings::scaledFont(font, at, to);
            at = to;
        }
        QCOMPARE(pointSize(font), 9.0);
        // nothing to scale: left as it is
        QCOMPARE(TextSizeSettings::scaledFont(QString(), 100, 150), QString());
        QCOMPARE(TextSizeSettings::scaledFont(QStringLiteral("garbage"), 0, 150), QStringLiteral("garbage"));
    }

    void clampPercent()
    {
        QCOMPARE(TextSizeSettings::clampPercent(50), 100);
        QCOMPARE(TextSizeSettings::clampPercent(300), 225);
        QCOMPARE(TextSizeSettings::clampPercent(132), 130);
        QCOMPARE(TextSizeSettings::clampPercent(133), 135);
    }

    void textSizeApply()
    {
        writeFile(m_system + QStringLiteral("/kdeglobals"), rayeFonts);
        TextSizeSettings t;
        QCOMPARE(t.percent(), 100);
        QCOMPARE(t.basePointSize(), 10.0);
        QCOMPARE(t.family(), QStringLiteral("Inter"));

        QSignalSpy changed(&t, &TextSizeSettings::changed);
        t.apply(150);
        QCOMPARE(changed.count(), 1);
        const QString globals = user(QStringLiteral("kdeglobals"));
        QCOMPARE(entry(globals, QStringLiteral("Raye"), QStringLiteral("TextSize")), QStringLiteral("150"));
        QCOMPARE(pointSize(entry(globals, QStringLiteral("General"), QStringLiteral("font"))), 15.0);
        QCOMPARE(pointSize(entry(globals, QStringLiteral("General"), QStringLiteral("menuFont"))), 15.0);
        QCOMPARE(pointSize(entry(globals, QStringLiteral("General"), QStringLiteral("toolBarFont"))), 13.5);
        QCOMPARE(pointSize(entry(globals, QStringLiteral("General"), QStringLiteral("smallestReadableFont"))), 13.5);
        QCOMPARE(pointSize(entry(globals, QStringLiteral("General"), QStringLiteral("fixed"))), 15.0);
        QCOMPARE(pointSize(entry(globals, QStringLiteral("WM"), QStringLiteral("activeFont"))), 13.5);
        QCOMPARE(t.percent(), 150);
        // the preview still knows the size at 100 %
        QCOMPARE(t.basePointSize(), 10.0);

        // the same size again: nothing written
        t.apply(150);
        QCOMPARE(changed.count(), 1);

        t.apply(100);
        QCOMPARE(pointSize(entry(globals, QStringLiteral("General"), QStringLiteral("font"))), 10.0);
        QCOMPARE(pointSize(entry(globals, QStringLiteral("WM"), QStringLiteral("activeFont"))), 9.0);
        QCOMPARE(entry(globals, QStringLiteral("Raye"), QStringLiteral("TextSize")), QStringLiteral("100"));
    }

    // a font nobody set stays the toolkit's
    void textSizeWithoutFonts()
    {
        TextSizeSettings t;
        t.apply(200);
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("General"), QStringLiteral("font")), QStringLiteral("<unset>"));
        QCOMPARE(t.percent(), 200);
    }

    // ---- Visual effects ---------------------------------------------

    void visualEffects()
    {
        VisualEffectsSettings v;
        QVERIFY(v.transparency());
        QVERIFY(v.animations());
        QCOMPARE(v.toastSeconds(), 5);
        QVERIFY(!v.toastAvailable());

        v.setTransparency(false);
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("Raye"), QStringLiteral("TransparencyEffects")), QStringLiteral("false"));
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Plugins"), QStringLiteral("blurEnabled")), QStringLiteral("false"));
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Plugins"), QStringLiteral("contrastEnabled")), QStringLiteral("false"));
        v.setTransparency(true);
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("Raye"), QStringLiteral("TransparencyEffects")), QStringLiteral("true"));
        // back to the window manager's own default
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Plugins"), QStringLiteral("blurEnabled")), QStringLiteral("<unset>"));

        v.setAnimations(false);
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("KDE"), QStringLiteral("AnimationDurationFactor")), QStringLiteral("0"));
        QVERIFY(!v.animations());
        v.setAnimations(true);
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("KDE"), QStringLiteral("AnimationDurationFactor")), QStringLiteral("1"));

        writeFile(m_system + QStringLiteral("/rayeshellrc"), "[Notifications]\nToastSeconds=5\n");
        v.reload();
        QVERIFY(v.toastAvailable());
        v.setToastSeconds(30);
        QCOMPARE(entry(user(QStringLiteral("rayeshellrc")), QStringLiteral("Notifications"), QStringLiteral("ToastSeconds")), QStringLiteral("30"));
        QCOMPARE(v.toastSeconds(), 30);
        // only the times offered
        v.setToastSeconds(31);
        QCOMPARE(v.toastSeconds(), 30);
        QCOMPARE(VisualEffectsSettings::toastIndex(300), 5);
        QCOMPARE(VisualEffectsSettings::toastIndex(8), 1);
    }

    // ---- Mouse pointer ----------------------------------------------

    void pointerThemes()
    {
        writeFile(m_data + QStringLiteral("/icons/breeze_cursors/cursors/left_ptr"), "x");
        writeFile(m_data + QStringLiteral("/icons/breeze_cursors/index.theme"), "[Icon Theme]\nName=Breeze\n");
        writeFile(m_data + QStringLiteral("/icons/Raye/cursors/left_ptr"), "x");
        writeFile(m_data + QStringLiteral("/icons/Raye/cursors_scalable/default/default.svg"), "<svg/>");
        writeFile(m_data + QStringLiteral("/icons/Raye/index.theme"), "[Icon Theme]\nName=Raye\n");
        writeFile(m_data + QStringLiteral("/icons/Raye-Dark/cursors/left_ptr"), "x");
        // an icon theme without cursors is not a pointer
        writeFile(m_data + QStringLiteral("/icons/Raye-Icons/index.theme"), "[Icon Theme]\nName=Raye icons\n");
        // a folder named like an option is never offered
        writeFile(m_data + QStringLiteral("/icons/--help/cursors/left_ptr"), "x");

        const auto themes = PointerSettings::findThemes();
        QStringList ids;
        for (const auto &t : themes)
            ids << t.id;
        QCOMPARE(ids, (QStringList{QStringLiteral("Raye"), QStringLiteral("Raye-Dark"), QStringLiteral("breeze_cursors")}));
        QVERIFY(themes.first().preview.endsWith(QLatin1String("default/default.svg")));
        QCOMPARE(themes.last().name, QStringLiteral("Breeze"));
        QVERIFY(themes.at(1).preview.isEmpty());

        PointerSettings p;
        QCOMPARE(p.theme(), QStringLiteral("breeze_cursors"));
        QCOMPARE(p.size(), 24);
        p.setTheme(QStringLiteral("Raye"));
        QCOMPARE(entry(user(QStringLiteral("kcminputrc")), QStringLiteral("Mouse"), QStringLiteral("cursorTheme")), QStringLiteral("Raye"));
        QCOMPARE(entry(user(QStringLiteral("kcminputrc")), QStringLiteral("Mouse"), QStringLiteral("cursorSize")), QStringLiteral("24"));
        p.setSize(48);
        QCOMPARE(entry(user(QStringLiteral("kcminputrc")), QStringLiteral("Mouse"), QStringLiteral("cursorSize")), QStringLiteral("48"));
        p.setSize(500);
        QCOMPARE(p.size(), PointerSettings::maxSize);
        // only an installed theme
        p.setTheme(QStringLiteral("../evil"));
        QCOMPARE(p.theme(), QStringLiteral("Raye"));

        QVERIFY(p.shakeToFind());
        p.setShakeToFind(false);
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Plugins"), QStringLiteral("shakecursorEnabled")), QStringLiteral("false"));
    }

    // ---- Magnifier --------------------------------------------------

    void magnifier()
    {
        MagnifierSettings m;
        QVERIFY(m.enabled()); // the window manager's default
        QCOMPARE(m.view(), 0);
        QCOMPARE(MagnifierSettings::zoomSteps.at(m.zoomStep()), 1.2);
        QVERIFY(m.followPointer());
        QCOMPARE(m.tracking(), 0);

        const QString kwin = user(QStringLiteral("kwinrc"));
        m.setView(1);
        QCOMPARE(entry(kwin, QStringLiteral("Plugins"), QStringLiteral("zoomEnabled")), QStringLiteral("false"));
        QCOMPARE(entry(kwin, QStringLiteral("Plugins"), QStringLiteral("magnifierEnabled")), QStringLiteral("true"));
        QCOMPARE(m.view(), 1);
        m.setEnabled(false);
        QCOMPARE(entry(kwin, QStringLiteral("Plugins"), QStringLiteral("magnifierEnabled")), QStringLiteral("false"));
        QVERIFY(!m.enabled());
        m.setEnabled(true);
        QCOMPARE(entry(kwin, QStringLiteral("Plugins"), QStringLiteral("zoomEnabled")), QStringLiteral("true"));
        QCOMPARE(entry(kwin, QStringLiteral("Plugins"), QStringLiteral("magnifierEnabled")), QStringLiteral("false"));

        m.setZoomStep(4);
        QCOMPARE(entry(kwin, QStringLiteral("Effect-zoom"), QStringLiteral("ZoomFactor")), QStringLiteral("2"));
        m.setZoomStep(9);
        QCOMPARE(m.zoomStep(), 4);
        QCOMPARE(MagnifierSettings::stepIndex(1.3), 2);

        m.setTracking(1);
        QCOMPARE(entry(kwin, QStringLiteral("Effect-zoom"), QStringLiteral("MouseTracking")), QStringLiteral("1"));
        m.setFollowPointer(false);
        QCOMPARE(entry(kwin, QStringLiteral("Effect-zoom"), QStringLiteral("MouseTracking")), QStringLiteral("3"));
        QVERIFY(!m.followPointer());
        m.setFollowPointer(true);
        // back to the way it followed before
        QCOMPARE(entry(kwin, QStringLiteral("Effect-zoom"), QStringLiteral("MouseTracking")), QStringLiteral("1"));

        m.setFollowFocus(true);
        m.setFollowTextCursor(true);
        QCOMPARE(entry(kwin, QStringLiteral("Effect-zoom"), QStringLiteral("EnableFocusTracking")), QStringLiteral("true"));
        QCOMPARE(entry(kwin, QStringLiteral("Effect-zoom"), QStringLiteral("EnableTextCaretTracking")), QStringLiteral("true"));
    }

    // ---- Colour filters ---------------------------------------------

    void colorFilters()
    {
        ColorFilterSettings c;
        QVERIFY(!c.enabled());
        QCOMPARE(c.mode(), 0);
        QCOMPARE(c.intensity(), 100);
        const QString kwin = user(QStringLiteral("kwinrc"));
        c.setEnabled(true);
        QCOMPARE(entry(kwin, QStringLiteral("Plugins"), QStringLiteral("colorblindnesscorrectionEnabled")), QStringLiteral("true"));
        c.setMode(3);
        QCOMPARE(entry(kwin, QStringLiteral("Effect-colorblindnesscorrection"), QStringLiteral("Mode")), QStringLiteral("3"));
        c.setMode(4);
        QCOMPARE(c.mode(), 3);
        c.setIntensity(40);
        QCOMPARE(entry(kwin, QStringLiteral("Effect-colorblindnesscorrection"), QStringLiteral("Intensity")), QStringLiteral("0.4"));
        QCOMPARE(c.intensity(), 40);
        c.setIntensity(-5);
        QCOMPARE(c.intensity(), 0);
    }

    // ---- Screen reader, audio, keyboard, mouse ----------------------

    void accessService()
    {
        AccessSettings a;
        const QString file = user(QStringLiteral("kaccessrc"));
        QVERIFY(!a.screenReader());
        QVERIFY(a.stickyKeysLatch());
        QVERIFY(!a.filterKeys());

        a.setScreenReader(true);
        QCOMPARE(entry(file, QStringLiteral("ScreenReader"), QStringLiteral("Enabled")), QStringLiteral("true"));
        a.setVisibleBell(true);
        QCOMPARE(entry(file, QStringLiteral("Bell"), QStringLiteral("VisibleBell")), QStringLiteral("true"));

        a.setStickyKeys(true);
        a.setStickyKeysLatch(false);
        a.setStickyKeysAutoOff(true);
        a.setStickyKeysBeep(true);
        a.setGestures(true);
        a.setToggleKeys(true);
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("StickyKeys")), QStringLiteral("true"));
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("StickyKeysLatch")), QStringLiteral("false"));
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("StickyKeysAutoOff")), QStringLiteral("true"));
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("StickyKeysBeep")), QStringLiteral("true"));
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("Gestures")), QStringLiteral("true"));
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("ToggleKeysBeep")), QStringLiteral("true"));

        // filter keys on: repeated keystrokes ignored first, as Windows
        a.setFilterKeys(true);
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("BounceKeys")), QStringLiteral("true"));
        QVERIFY(a.filterKeys());
        a.setSlowKeys(true);
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("SlowKeys")), QStringLiteral("true"));
        a.setFilterKeys(false);
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("SlowKeys")), QStringLiteral("false"));
        QCOMPARE(entry(file, QStringLiteral("Keyboard"), QStringLiteral("BounceKeys")), QStringLiteral("false"));

        a.setMouseKeys(true);
        a.setMouseKeysSpeed(7);
        a.setMouseKeysAcceleration(3);
        QCOMPARE(entry(file, QStringLiteral("Mouse"), QStringLiteral("MouseKeys")), QStringLiteral("true"));
        QCOMPARE(entry(file, QStringLiteral("Mouse"), QStringLiteral("MKMaxSpeed")), QStringLiteral("70"));
        QCOMPARE(entry(file, QStringLiteral("Mouse"), QStringLiteral("MKTimeToMax")), QStringLiteral("80"));

        AccessSettings again;
        QVERIFY(again.screenReader());
        QVERIFY(again.stickyKeys());
        QCOMPARE(again.mouseKeysSpeed(), 7);
        QCOMPARE(again.mouseKeysAcceleration(), 3);
        // no session: the on-screen keyboard is not offered
        QVERIFY(!again.onScreenKeyboardAvailable());
    }

    void mouseKeysLevels()
    {
        // the service's defaults (30, 30) sit on the scale
        QCOMPARE(AccessSettings::maxSpeedToSpeed(30), 3);
        QCOMPARE(AccessSettings::timeToAcceleration(30), 8);
        for (int level = 1; level <= 10; ++level) {
            QCOMPARE(AccessSettings::maxSpeedToSpeed(AccessSettings::speedToMaxSpeed(level)), level);
            QCOMPARE(AccessSettings::timeToAcceleration(AccessSettings::accelerationToTime(level)), level);
        }
        QCOMPARE(AccessSettings::maxSpeedToSpeed(2000), 10);
        QCOMPARE(AccessSettings::timeToAcceleration(5000), 1);
    }

    // ---- the pages, loaded as the window loads them -----------------

    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        const QList<QPair<const char *, const char *>> pages = {
            {"TextSize", "text-size"}, {"VisualEffects", "visual-effects"}, {"MousePointer", "mouse-pointer"},
            {"Magnifier", "magnifier"}, {"ColorFilters", "color-filters"},   {"ScreenReader", "screen-reader"},
            {"Audio", "audio"},         {"Keyboard", "keyboard"},             {"MouseKeys", "mouse"}};
        for (const auto &[name, id] : pages)
            QTest::newRow(id) << QStringLiteral("pages/accessibility/%1Page.qml").arg(QString::fromLatin1(name))
                              << QStringLiteral("accessibility/") + QString::fromLatin1(id);
    }

    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);
        // every row present
        writeFile(m_system + QStringLiteral("/rayeshellrc"), "[Notifications]\nToastSeconds=5\n");
        writeFile(m_system + QStringLiteral("/kdeglobals"), rayeFonts);

        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/accessibility.json"));
        QVERIFY(fragment.open(QIODevice::ReadOnly));
        QStringList indexed;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        QVERIFY(!entries.isEmpty());
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
            if (id.startsWith(page + QLatin1Char('#')))
                indexed << id.section(QLatin1Char('#'), 1);
        }
        QVERIFY2(!indexed.isEmpty(), qPrintable(page));

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
            QTest::qWait(300);

            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            const QStringList drawn = ids.toStringList();
            for (const QString &id : std::as_const(indexed))
                QVERIFY2(drawn.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not on the page")));
            for (const QString &id : drawn)
                QVERIFY2(indexed.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not in data/search")));
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestAccessibility)
#include "test_accessibility.moc"
