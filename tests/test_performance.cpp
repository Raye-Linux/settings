// SPDX-License-Identifier: GPL-3.0-or-later
// Performance options: every effect read and written in config folders of
// the test's own, the four choices, the low-end guess, and the page loads
// with every searchable row on it.
#include "accessibility.h"
#include "performance.h"
#include "windoweffects.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
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

// what Raye ships in /etc/xdg/kwinrc, the part that matters here: its own
// window effect replaces the stock scale one, which is shipped off
const QByteArray rayeKwin =
    "[Plugins]\n"
    "scaleEnabled=false\nraye_windowsEnabled=true\nsquashEnabled=true\nslideEnabled=true\nslidingpopupsEnabled=true\n"
    "blurEnabled=true\ncontrastEnabled=true\nraye_popupsEnabled=true\nraye_maximizeEnabled=true\n";

struct FakeBlur : WindowEffects::Backend {
    bool lastEnable = false;
    bool available() const override { return true; }
    void setBlur(QWindow *, bool enable, const QRect &) override { lastEnable = enable; }
};

} // namespace

class TestPerformance : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_home;
    QString m_system;

    QString user(const QString &file) const { return m_home + QLatin1Char('/') + file; }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_home = m_dir.filePath(QStringLiteral("config"));
        m_system = m_dir.filePath(QStringLiteral("xdg"));
        qputenv("XDG_CONFIG_HOME", m_home.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_system.toUtf8());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        qputenv("HOME", m_dir.filePath(QStringLiteral("home")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void init()
    {
        for (const QString &dir : {m_home, m_system}) {
            QDir(dir).removeRecursively();
            QDir().mkpath(dir);
        }
        writeFile(m_system + QStringLiteral("/kwinrc"), rayeKwin);
        for (const char *name : {"kdeglobals", "kwinrc", "rayerc"})
            KSharedConfig::openConfig(QString::fromLatin1(name))->reparseConfiguration();
    }

    void effectiveMode()
    {
        // nothing chosen yet: the files say it
        QCOMPARE(PerformanceSettings::effectiveMode(QString(), 7, 7, false), QStringLiteral("auto"));
        QCOMPARE(PerformanceSettings::effectiveMode(QString(), 7, 7, true), QStringLiteral("appearance"));
        QCOMPARE(PerformanceSettings::effectiveMode(QString(), 0, 7, false), QStringLiteral("performance"));
        QCOMPARE(PerformanceSettings::effectiveMode(QString(), 0, 7, true), QStringLiteral("auto"));
        QCOMPARE(PerformanceSettings::effectiveMode(QString(), 3, 7, false), QStringLiteral("custom"));
        // a choice holds while the files agree with it
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("appearance"), 7, 7, false), QStringLiteral("appearance"));
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("performance"), 0, 7, false), QStringLiteral("performance"));
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("auto"), 0, 7, true), QStringLiteral("auto"));
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("custom"), 7, 7, false), QStringLiteral("custom"));
        // and becomes Custom when a switch was flipped elsewhere
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("performance"), 1, 7, false), QStringLiteral("custom"));
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("appearance"), 6, 7, false), QStringLiteral("custom"));
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("auto"), 7, 7, true), QStringLiteral("custom"));
        QCOMPARE(PerformanceSettings::effectiveMode(QStringLiteral("garbage"), 7, 7, false), QStringLiteral("auto"));
    }

    void lowEnd()
    {
        const qint64 eight = 8 * 1024 * 1024;
        const QString gpu = QStringLiteral("Compositing Type: OpenGL\nOpenGL renderer string: AMD Radeon RX 6600 (radeonsi)\n");
        QVERIFY(!PerformanceSettings::isLowEnd(gpu, eight));
        QVERIFY(PerformanceSettings::isLowEnd(QStringLiteral("OpenGL renderer string: llvmpipe (LLVM 19.1.7, 256 bits)\n"), eight));
        QVERIFY(PerformanceSettings::isLowEnd(QStringLiteral("OpenGL renderer string: softpipe\n"), eight));
        QVERIFY(PerformanceSettings::isLowEnd(QStringLiteral("Compositing Type: QPainter\n"), eight));
        // a "4 GB" PC reports a little less
        QVERIFY(PerformanceSettings::isLowEnd(gpu, 3900 * 1024));
        QVERIFY(PerformanceSettings::isLowEnd(gpu, 1024 * 1024));
        QVERIFY(!PerformanceSettings::isLowEnd(gpu, 6 * 1024 * 1024));
        // nothing known: not low-end
        QVERIFY(!PerformanceSettings::isLowEnd(QString(), 0));
    }

    void effectsDefault()
    {
        PerformanceSettings p;
        for (const QString &id : PerformanceSettings::effectIds)
            QVERIFY2(p.effect(id), qPrintable(id));
        QCOMPARE(p.mode(), QStringLiteral("auto"));
        QVERIFY(!p.effect(QStringLiteral("no-such-effect")));
    }

    void effectsWrite()
    {
        PerformanceSettings p;
        QSignalSpy changed(&p, &PerformanceSettings::changed);
        const QString kwin = user(QStringLiteral("kwinrc"));
        const QString globals = user(QStringLiteral("kdeglobals"));
        const QString rayerc = user(QStringLiteral("rayerc"));
        const QString plugins = QStringLiteral("Plugins");

        p.setEffect(QStringLiteral("window-open"), false);
        QCOMPARE(entry(kwin, plugins, QStringLiteral("raye_windowsEnabled")), QStringLiteral("false"));
        QCOMPARE(entry(kwin, plugins, QStringLiteral("scaleEnabled")), QStringLiteral("<unset>"));
        QVERIFY(!p.windowOpen());
        QCOMPARE(p.mode(), QStringLiteral("custom"));
        QCOMPARE(entry(globals, QStringLiteral("Raye"), QStringLiteral("VisualEffectsMode")), QStringLiteral("custom"));

        p.setEffect(QStringLiteral("minimize"), false);
        QCOMPARE(entry(kwin, plugins, QStringLiteral("squashEnabled")), QStringLiteral("false"));
        QCOMPARE(entry(kwin, plugins, QStringLiteral("raye_maximizeEnabled")), QStringLiteral("false"));
        p.setEffect(QStringLiteral("menus"), false);
        QCOMPARE(entry(kwin, plugins, QStringLiteral("raye_popupsEnabled")), QStringLiteral("false"));
        QCOMPARE(entry(kwin, plugins, QStringLiteral("slidingpopupsEnabled")), QStringLiteral("false"));
        p.setEffect(QStringLiteral("desktops"), false);
        QCOMPARE(entry(kwin, plugins, QStringLiteral("slideEnabled")), QStringLiteral("false"));
        p.setEffect(QStringLiteral("shadows"), false);
        QCOMPARE(entry(rayerc, QStringLiteral("Common"), QStringLiteral("ShadowSize")), QStringLiteral("ShadowNone"));
        QVERIFY(!p.shadows());
        p.setEffect(QStringLiteral("animations"), false);
        QCOMPARE(entry(globals, QStringLiteral("KDE"), QStringLiteral("AnimationDurationFactor")), QStringLiteral("0"));
        p.setEffect(QStringLiteral("transparency"), false);
        QCOMPARE(entry(globals, QStringLiteral("Raye"), QStringLiteral("TransparencyEffects")), QStringLiteral("false"));
        QCOMPARE(entry(kwin, plugins, QStringLiteral("blurEnabled")), QStringLiteral("false"));
        QCOMPARE(entry(kwin, plugins, QStringLiteral("contrastEnabled")), QStringLiteral("false"));
        QCOMPARE(changed.count(), 7);

        // on: back to the system's own value
        p.setEffect(QStringLiteral("shadows"), true);
        QCOMPARE(entry(rayerc, QStringLiteral("Common"), QStringLiteral("ShadowSize")), QStringLiteral("<unset>"));
        QVERIFY(p.shadows());
        p.setEffect(QStringLiteral("minimize"), true);
        QCOMPARE(entry(kwin, plugins, QStringLiteral("squashEnabled")), QStringLiteral("<unset>"));
        QCOMPARE(entry(kwin, plugins, QStringLiteral("raye_maximizeEnabled")), QStringLiteral("<unset>"));
        p.setEffect(QStringLiteral("animations"), true);
        QCOMPARE(entry(globals, QStringLiteral("KDE"), QStringLiteral("AnimationDurationFactor")), QStringLiteral("1"));
        QVERIFY(p.animations());

        // the same value again: nothing happens
        const int before = changed.count();
        p.setEffect(QStringLiteral("animations"), true);
        p.setEffect(QStringLiteral("nothing"), false);
        QCOMPARE(changed.count(), before);
    }

    // the window border's own value for "no shadow" reads as off too
    void shadowsFromBorderSettings()
    {
        writeFile(user(QStringLiteral("rayerc")), "[Common]\nShadowSize=0\n");
        PerformanceSettings p;
        QVERIFY(!p.shadows());
        writeFile(user(QStringLiteral("rayerc")), "[Common]\nShadowSize=ShadowSmall\n");
        p.reload();
        QVERIFY(p.shadows());
    }

    void modes()
    {
        PerformanceSettings p;
        p.setMode(QStringLiteral("performance"));
        for (const QString &id : PerformanceSettings::effectIds)
            QVERIFY2(!p.effect(id), qPrintable(id));
        QCOMPARE(p.mode(), QStringLiteral("performance"));
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("Raye"), QStringLiteral("VisualEffectsMode")),
                 QStringLiteral("performance"));

        p.setMode(QStringLiteral("appearance"));
        for (const QString &id : PerformanceSettings::effectIds)
            QVERIFY2(p.effect(id), qPrintable(id));
        QCOMPARE(p.mode(), QStringLiteral("appearance"));
        // every key of the window manager went back to the system's
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Plugins"), QStringLiteral("blurEnabled")), QStringLiteral("<unset>"));

        // custom keeps what is there
        p.setMode(QStringLiteral("custom"));
        QCOMPARE(p.mode(), QStringLiteral("custom"));
        QVERIFY(p.transparency());

        // a PC that is slow at drawing: Raye's choice is speed
        p.setLowEnd(true);
        p.setMode(QStringLiteral("auto"));
        QVERIFY(!p.transparency());
        QVERIFY(!p.animations());
        QCOMPARE(p.mode(), QStringLiteral("auto"));
        p.setLowEnd(false);
        p.setMode(QStringLiteral("auto"));
        QVERIFY(p.transparency());
        QCOMPARE(p.mode(), QStringLiteral("auto"));

        // an unknown choice changes nothing
        p.setMode(QStringLiteral("turbo"));
        QCOMPARE(p.mode(), QStringLiteral("auto"));
    }

    // Colours and Visual effects flip the same switch the same way
    void transparencyShared()
    {
        PerformanceSettings p;
        p.setMode(QStringLiteral("appearance"));
        VisualEffectsSettings v;
        v.setTransparency(false);
        p.reload();
        QVERIFY(!p.transparency());
        QCOMPARE(p.mode(), QStringLiteral("custom"));
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Plugins"), QStringLiteral("blurEnabled")), QStringLiteral("false"));
        p.setEffect(QStringLiteral("transparency"), true);
        v.reload();
        QVERIFY(v.transparency());
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Plugins"), QStringLiteral("contrastEnabled")), QStringLiteral("<unset>"));
    }

    // the Settings window itself goes opaque with the switch
    void settingsWindow()
    {
        QWindow window;
        WindowEffects effects;
        auto *fake = new FakeBlur;
        effects.setBackend(fake);
        effects.setWindow(&window);
        effects.componentComplete();
        QVERIFY(effects.translucent());
        QVERIFY(fake->lastEnable);

        PerformanceSettings::writeTransparency(false);
        effects.refresh();
        QVERIFY(!effects.transparencyEffects());
        QVERIFY(!effects.translucent());
        QVERIFY(!fake->lastEnable);

        PerformanceSettings::writeTransparency(true);
        effects.refresh();
        QVERIFY(effects.translucent());
        QVERIFY(fake->lastEnable);
    }

    // a list set up with currentIndex: indexOfValue(...) shows the value,
    // whatever order QML sets the properties in
    void comboCardShowsValue()
    {
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport org.raye.settings\n"
                          "ComboCard { property string mode: \"light\"\n"
                          "  currentIndex: Math.max(0, indexOfValue(mode))\n"
                          "  textRole: \"text\"; valueRole: \"value\"\n"
                          "  model: [{ text: \"Dark\", value: \"dark\" }, { text: \"Light\", value: \"light\" }] }",
                          QUrl());
        std::unique_ptr<QObject> card(component.create());
        QVERIFY2(card, qPrintable(component.errorString()));
        QCOMPARE(card->property("currentIndex").toInt(), 1);
        card->setProperty("mode", QStringLiteral("dark"));
        QCOMPARE(card->property("currentIndex").toInt(), 0);
    }

    void page()
    {
        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/system-misc.json"));
        QVERIFY(fragment.open(QIODevice::ReadOnly));
        QStringList indexed;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        const QString page = QStringLiteral("system/performance");
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (id.startsWith(page + QLatin1Char('#'))) {
                QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
                indexed << id.section(QLatin1Char('#'), 1);
            }
        }
        QCOMPARE(indexed.size(), PerformanceSettings::effectIds.size() + 1);
        // the list shows the choice in the files, not its first line
        PerformanceSettings().setMode(QStringLiteral("performance"));

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            QQuickView view(&engine, nullptr);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1120, 760);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/system/PerformancePage.qml")));
            QCOMPARE(view.status(), QQuickView::Ready);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(300);

            auto *combo = view.rootObject()->findChild<QObject *>(QStringLiteral("visualEffectsMode"));
            QVERIFY(combo);
            QCOMPARE(combo->property("currentIndex").toInt(), 2);

            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            const QStringList drawn = ids.toStringList();
            for (const QString &id : std::as_const(indexed))
                QVERIFY2(drawn.contains(id), qPrintable(id + QStringLiteral(" is not on the page")));
            for (const QString &id : drawn)
                QVERIFY2(indexed.contains(id), qPrintable(id + QStringLiteral(" is not in data/search")));
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestPerformance)
#include "test_performance.moc"
