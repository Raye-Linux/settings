// SPDX-License-Identifier: GPL-3.0-or-later
// System › Display without touching a real screen: the resolution and
// refresh lists, the suggested scale, where a dragged screen lands, the
// 15 second "Keep these display settings?" countdown on a configuration
// made by hand, the brightness of each screen (a fake PowerDevil on a
// private bus), night light's settings files and the per-app graphics
// card choice.
#include "brightness.h"
#include "display.h"
#include "gpupreference.h"
#include "nightlight.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KScreen/Config>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/Screen>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDBusServer>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace DisplayLogic;
using KScreen::Output;

namespace
{
KScreen::ModePtr makeMode(const QString &id, int w, int h, float hz)
{
    auto m = KScreen::ModePtr::create();
    m->setId(id);
    m->setSize(QSize(w, h));
    m->setRefreshRate(hz);
    return m;
}

KScreen::ModeList laptopModes()
{
    KScreen::ModeList modes;
    for (const auto &m : {makeMode(QStringLiteral("a"), 1920, 1080, 60.0f), makeMode(QStringLiteral("b"), 1920, 1080, 144.0f),
                          makeMode(QStringLiteral("c"), 1920, 1080, 59.94f), makeMode(QStringLiteral("d"), 1280, 720, 60.0f),
                          makeMode(QStringLiteral("e"), 2560, 1440, 59.95f), makeMode(QStringLiteral("f"), 800, 600, 60.0f)})
        modes.insert(m->id(), m);
    return modes;
}

KScreen::OutputPtr makeOutput(int id, const QString &name, Output::Type type, const QPoint &pos)
{
    auto o = KScreen::OutputPtr::create();
    o->setId(id);
    o->setName(name);
    o->setType(type);
    o->setModes(laptopModes());
    o->setPreferredModes({QStringLiteral("a")});
    o->setCurrentModeId(QStringLiteral("a"));
    o->setConnected(true);
    o->setEnabled(true);
    o->setPos(pos);
    o->setScale(1.0);
    o->setSizeMm(QSize(344, 194));
    return o;
}

KScreen::ConfigPtr twoScreens()
{
    auto config = KScreen::ConfigPtr::create();
    // a real configuration always has its screen (the whole desktop)
    auto screen = KScreen::ScreenPtr::create();
    screen->setMaxSize(QSize(16384, 16384));
    config->setScreen(screen);
    config->setValid(true);
    KScreen::OutputList outputs;
    auto laptop = makeOutput(1, QStringLiteral("eDP-1"), Output::Panel, QPoint(0, 0));
    auto monitor = makeOutput(2, QStringLiteral("HDMI-A-1"), Output::HDMI, QPoint(1920, 0));
    monitor->setSizeMm(QSize(597, 336));
    outputs.insert(1, laptop);
    outputs.insert(2, monitor);
    config->setOutputs(outputs);
    config->setOutputPriority(laptop, 1);
    return config;
}
} // namespace

// org.kde.ScreenBrightness and one display, as PowerDevil shows them
class FakeBrightnessRoot : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.ScreenBrightness")
    Q_PROPERTY(QStringList DisplaysDBusNames MEMBER names)
public:
    QStringList names{QStringLiteral("display0"), QStringLiteral("display1")};
};

class FakeBrightnessDisplay : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.ScreenBrightness.Display")
    Q_PROPERTY(QString Label MEMBER label)
    Q_PROPERTY(bool IsInternal MEMBER internal)
    Q_PROPERTY(int MaxBrightness MEMBER maximum)
    Q_PROPERTY(int Brightness MEMBER brightness)
public:
    QString label;
    bool internal = false;
    int maximum = 100;
    int brightness = 50;
    uint lastFlags = 0;
public Q_SLOTS:
    void SetBrightness(int value, uint flags)
    {
        brightness = value;
        lastFlags = flags;
    }
};

class FakePowerDevil : public QObject
{
    Q_OBJECT
public:
    QString address;
    FakeBrightnessDisplay *external = nullptr;
    FakeBrightnessDisplay *builtIn = nullptr;
public Q_SLOTS:
    void start()
    {
        m_server = new QDBusServer(this);
        address = m_server->address();
        connect(m_server, &QDBusServer::newConnection, this, [this](const QDBusConnection &connection) {
            m_connection = connection;
            const auto how = QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots;
            auto *root = new FakeBrightnessRoot;
            root->setParent(this);
            external = new FakeBrightnessDisplay;
            external->setParent(this);
            external->label = QStringLiteral("DELL U2720Q");
            builtIn = new FakeBrightnessDisplay;
            builtIn->setParent(this);
            builtIn->label = QStringLiteral("Built-in display");
            builtIn->internal = true;
            builtIn->maximum = 19200;
            builtIn->brightness = 9600;
            m_connection.registerObject(QStringLiteral("/org/kde/ScreenBrightness"), root, how);
            // listed second, shown first: the built-in screen leads
            m_connection.registerObject(QStringLiteral("/org/kde/ScreenBrightness/display0"), external, how);
            m_connection.registerObject(QStringLiteral("/org/kde/ScreenBrightness/display1"), builtIn, how);
        });
    }

private:
    QDBusServer *m_server = nullptr;
    QDBusConnection m_connection{QString()};
};

class TestDisplay : public QObject
{
    Q_OBJECT
    QThread m_thread;

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain("raye-settings");
        m_thread.start();
    }

    void cleanupTestCase()
    {
        m_thread.quit();
        m_thread.wait();
    }

    void resolutionList()
    {
        const auto list = resolutions(laptopModes(), QStringLiteral("a"));
        QCOMPARE(list.size(), 4);
        QCOMPARE(list.at(0).size, QSize(2560, 1440));
        QCOMPARE(list.at(1).size, QSize(1920, 1080));
        QVERIFY(list.at(1).recommended);
        QVERIFY(!list.at(0).recommended);
        QCOMPARE(list.last().size, QSize(800, 600));
        QCOMPARE(resolutionText(QSize(1920, 1080), true), QStringLiteral("1920 × 1080 (Recommended)"));
        QCOMPARE(resolutionText(QSize(1280, 720), false), QStringLiteral("1280 × 720"));
    }

    void refreshList()
    {
        const auto rates = refreshRates(laptopModes(), QSize(1920, 1080));
        QCOMPARE(rates.size(), 3);
        QCOMPARE(rates.at(0), 144.0);
        QCOMPARE(rates.at(1), 60.0);
        QCOMPARE(rates.at(2), 59.94);
        QCOMPARE(refreshText(60.0), QStringLiteral("60 Hz"));
        QCOMPARE(refreshText(59.94), QStringLiteral("59.94 Hz"));
        QCOMPARE(refreshText(143.9998), QStringLiteral("144 Hz"));
        QCOMPARE(modeFor(laptopModes(), QSize(1920, 1080), 144.0), QStringLiteral("b"));
        QCOMPARE(modeFor(laptopModes(), QSize(1920, 1080), 59.9), QStringLiteral("c"));
        // the resolution has no 144: the closest rate it has
        QCOMPARE(modeFor(laptopModes(), QSize(2560, 1440), 144.0), QStringLiteral("e"));
        QCOMPARE(modeFor(laptopModes(), QSize(3840, 2160), 60.0), QString());
    }

    void scales()
    {
        // 15.6" 1080p laptop, 13.3" 1080p, 24" 1080p desk, 27" 4K, a TV
        QCOMPARE(recommendedScale(QSize(1920, 1080), QSize(344, 194)), 125);
        QCOMPARE(recommendedScale(QSize(1920, 1080), QSize(294, 165)), 150);
        QCOMPARE(recommendedScale(QSize(1920, 1080), QSize(531, 299)), 100);
        QCOMPARE(recommendedScale(QSize(3840, 2160), QSize(597, 336)), 150);
        QCOMPARE(recommendedScale(QSize(3840, 2160), QSize(16, 9)), 100);
        QCOMPARE(recommendedScale(QSize(1920, 1080), QSize()), 100);
        QCOMPARE(scaleSteps(150).size(), 9);
        QCOMPARE(scaleSteps(110).size(), 10);
        QCOMPARE(scaleSteps(110).at(1), 110);
    }

    void rotation()
    {
        for (int d : {0, 90, 180, 270})
            QCOMPARE(degreesOf(rotationOf(d)), d);
        QCOMPARE(rotationOf(-90), int(Output::Right));
        QCOMPARE(rotationOf(450), int(Output::Left));
    }

    void dropping()
    {
        const QRect a(0, 0, 1920, 1080), b(1920, 0, 1920, 1080);
        // dragged below the first screen: snaps under it
        auto placed = placeDropped({a, b}, 1, QRect(300, 1200, 1920, 1080));
        QCOMPARE(placed.at(1).topLeft(), QPoint(300, 1080));
        QCOMPARE(placed.at(0).topLeft(), QPoint(0, 0));
        // dropped over the first: pushed to its nearest free edge
        placed = placeDropped({a, b}, 1, QRect(100, 50, 1920, 1080));
        QVERIFY(!placed.at(1).intersects(placed.at(0)));
        // dragged to the left: everything moves so the left one starts at 0
        placed = placeDropped({a, b}, 1, QRect(-2500, 0, 1920, 1080));
        QCOMPARE(placed.at(1).topLeft(), QPoint(0, 0));
        QCOMPARE(placed.at(0).topLeft(), QPoint(1920, 0));
        // far away: still touches the other screen
        placed = placeDropped({a, b}, 1, QRect(9000, 9000, 1920, 1080));
        const QRect grown = placed.at(0).adjusted(-1, -1, 1, 1);
        QVERIFY(grown.intersects(placed.at(1)));
        // one screen alone sits at 0,0
        placed = placeDropped({a}, 0, QRect(500, 500, 1920, 1080));
        QCOMPARE(placed.at(0).topLeft(), QPoint(0, 0));
    }

    void config()
    {
        QList<KScreen::ConfigPtr> applied;
        DisplayConfig display(twoScreens(), [&applied](const KScreen::ConfigPtr &c) { applied.append(c); return true; });
        QVERIFY(display.ready());
        QCOMPARE(display.selected(), 1);
        const QVariantList outputs = display.outputs();
        QCOMPARE(outputs.size(), 2);
        QCOMPARE(outputs.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Built-in display"));
        QCOMPARE(outputs.at(0).toMap().value(QStringLiteral("number")).toInt(), 1);
        QVERIFY(outputs.at(0).toMap().value(QStringLiteral("primary")).toBool());
        QCOMPARE(outputs.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("HDMI-A-1"));
        QCOMPARE(display.resolutionIndex(), 1);
        QCOMPARE(display.refreshIndex(), 1);
        QCOMPARE(display.recommendedScale(), 125);
        QCOMPARE(display.mode(), QStringLiteral("extend"));
        QVERIFY(!display.hdrCapable());

        // a new resolution asks to be kept and counts down
        QSignalSpy confirming(&display, &DisplayConfig::confirmingChanged);
        display.setResolution(0);
        QCOMPARE(applied.size(), 1);
        QVERIFY(display.confirming());
        QCOMPARE(display.secondsLeft(), 15);
        QCOMPARE(display.resolutionIndex(), 0);
        // the bigger screen pushed its neighbour along: they still touch
        {
            const QVariantList now = display.outputs();
            const QVariantMap a = now.at(0).toMap(), b = now.at(1).toMap();
            QCOMPARE(QRect(a.value(QStringLiteral("x")).toInt(), a.value(QStringLiteral("y")).toInt(), a.value(QStringLiteral("width")).toInt(),
                           a.value(QStringLiteral("height")).toInt())
                         .adjusted(-1, -1, 1, 1)
                         .intersects(QRect(b.value(QStringLiteral("x")).toInt(), b.value(QStringLiteral("y")).toInt(),
                                           b.value(QStringLiteral("width")).toInt(), b.value(QStringLiteral("height")).toInt())),
                     true);
            QCOMPARE(a.value(QStringLiteral("width")).toInt(), 2560);
        }
        // nothing pressed for 15 seconds: back to what it was
        for (int i = 0; i < 15; ++i)
            display.tick();
        QVERIFY(!display.confirming());
        QCOMPARE(display.resolutionIndex(), 1);
        QCOMPARE(applied.size(), 2);

        // kept: stays
        display.setRefreshRate(0);
        QCOMPARE(display.refreshIndex(), 0);
        display.keepChanges();
        QVERIFY(!display.confirming());
        display.tick();
        QCOMPARE(display.refreshIndex(), 0);

        // two changes in a row go back together
        display.setResolution(2);
        display.setOrientation(90);
        QCOMPARE(display.orientation(), 90);
        display.revertChanges();
        QCOMPARE(display.orientation(), 0);
        QCOMPARE(display.refreshIndex(), 0);

        // scale applies at once, no question; the screen beside follows
        const int before = int(applied.size());
        display.setScale(150);
        QCOMPARE(display.scale(), 150);
        QVERIFY(!display.confirming());
        QCOMPARE(int(applied.size()), before + 1);
        const QVariantList after = display.outputs();
        QCOMPARE(after.at(1).toMap().value(QStringLiteral("x")).toInt(), after.at(0).toMap().value(QStringLiteral("width")).toInt());

        // HDR and adaptive sync on a screen without them: nothing happens
        display.setHdrEnabled(true);
        display.setVrrEnabled(true);
        QVERIFY(!display.hdrEnabled());
        QVERIFY(!display.vrrEnabled());
        QCOMPARE(int(applied.size()), before + 1);
    }

    void modes()
    {
        QList<KScreen::ConfigPtr> applied;
        DisplayConfig display(twoScreens(), [&applied](const KScreen::ConfigPtr &c) { applied.append(c); return true; });
        display.setMode(QStringLiteral("only"), 2);
        QCOMPARE(display.mode(), QStringLiteral("only"));
        QVERIFY(display.confirming());
        QCOMPARE(display.selected(), 2);
        display.revertChanges();
        QCOMPARE(display.mode(), QStringLiteral("extend"));
        display.setMode(QStringLiteral("duplicate"));
        QCOMPARE(display.mode(), QStringLiteral("duplicate"));
        display.keepChanges();
        display.setMode(QStringLiteral("extend"));
        QCOMPARE(display.mode(), QStringLiteral("extend"));
        // nonsense does nothing
        const int count = int(applied.size());
        display.setMode(QStringLiteral("mirror-everything"));
        display.setMode(QStringLiteral("only"), 77);
        QCOMPARE(int(applied.size()), count);
    }

    void dragAndPrimary()
    {
        QList<KScreen::ConfigPtr> applied;
        DisplayConfig display(twoScreens(), [&applied](const KScreen::ConfigPtr &c) { applied.append(c); return true; });
        display.moveOutput(2, 200, 1300);
        const QVariantList outputs = display.outputs();
        QCOMPARE(outputs.at(1).toMap().value(QStringLiteral("y")).toInt(), 1080);
        QCOMPARE(outputs.at(1).toMap().value(QStringLiteral("x")).toInt(), 200);
        QVERIFY(!display.confirming());
        display.makePrimary(2);
        QVERIFY(display.outputs().at(1).toMap().value(QStringLiteral("primary")).toBool());
        QVERIFY(!display.outputs().at(0).toMap().value(QStringLiteral("primary")).toBool());
    }

    // "Make this my main display": applied at once, the old main one
    // second; asking for the main one again, or an unknown one, does nothing
    void mainDisplay()
    {
        QList<KScreen::ConfigPtr> applied;
        DisplayConfig display(twoScreens(), [&applied](const KScreen::ConfigPtr &c) { applied.append(c); return true; });
        QCOMPARE(display.selected(), 1); // the page opens on the main display
        display.makePrimary(1);
        QCOMPARE(applied.size(), 0);
        display.makePrimary(77);
        QCOMPARE(applied.size(), 0);

        display.makePrimary(2);
        QCOMPARE(applied.size(), 1);
        QVERIFY(!display.confirming());
        QCOMPARE(applied.last()->primaryOutput()->name(), QStringLiteral("HDMI-A-1"));
        QCOMPARE(applied.last()->output(2)->priority(), 1u);
        QCOMPARE(applied.last()->output(1)->priority(), 2u);
        int primaries = 0;
        for (const QVariant &o : display.outputs())
            primaries += o.toMap().value(QStringLiteral("primary")).toBool() ? 1 : 0;
        QCOMPARE(primaries, 1);

        display.makePrimary(1);
        QCOMPARE(applied.size(), 2);
        QCOMPARE(applied.last()->primaryOutput()->name(), QStringLiteral("eDP-1"));
        QCOMPARE(applied.last()->output(2)->priority(), 2u);
    }

    void brightnessMath()
    {
        QCOMPARE(ScreenBrightness::percentOf(9600, 19200), 50);
        QCOMPARE(ScreenBrightness::percentOf(5, 0), 0);
        QCOMPARE(ScreenBrightness::brightnessOf(50, 19200, true), 9600);
        QCOMPARE(ScreenBrightness::brightnessOf(0, 19200, true), 1);
        QCOMPARE(ScreenBrightness::brightnessOf(0, 100, false), 0);
        QCOMPARE(ScreenBrightness::brightnessOf(150, 100, false), 100);
        QVERIFY(ScreenBrightness::isDisplayName(QStringLiteral("display0")));
        QVERIFY(!ScreenBrightness::isDisplayName(QStringLiteral("../x")));
        QVERIFY(!ScreenBrightness::isDisplayName(QString()));
    }

    void brightnessTitles()
    {
        const auto screen = [](int number, const QString &name) {
            return QVariantMap{{QStringLiteral("number"), number}, {QStringLiteral("name"), name}};
        };
        const QString qemu = QStringLiteral("Red Hat, Inc. QEMU Monitor");
        const QVariantList screens{screen(1, qemu), screen(2, qemu)};
        QCOMPARE(ScreenBrightness::titles({qemu, qemu}, screens),
                 QStringList({qemu + QStringLiteral(" (Display 1)"), qemu + QStringLiteral(" (Display 2)")}));
        // different models keep their plain names
        const QVariantList mixed{screen(1, QStringLiteral("Built-in display")), screen(2, QStringLiteral("HP 23bw"))};
        QCOMPARE(ScreenBrightness::titles({QStringLiteral("Built-in display"), QStringLiteral("HP 23bw")}, mixed),
                 QStringList({QStringLiteral("Built-in display"), QStringLiteral("HP 23bw")}));
        // the twins are numbered as the Display page numbers them, not by their place here
        const QVariantList three{screen(1, QStringLiteral("Built-in display")), screen(2, qemu), screen(3, qemu)};
        QCOMPARE(ScreenBrightness::titles({qemu, qemu}, three),
                 QStringList({qemu + QStringLiteral(" (Display 2)"), qemu + QStringLiteral(" (Display 3)")}));
        // no screen list yet: their place in the list
        QCOMPARE(ScreenBrightness::titles({qemu, qemu}, {}),
                 QStringList({qemu + QStringLiteral(" (Display 1)"), qemu + QStringLiteral(" (Display 2)")}));
    }

    void brightnessService()
    {
        auto *fake = new FakePowerDevil;
        fake->moveToThread(&m_thread);
        QMetaObject::invokeMethod(fake, &FakePowerDevil::start, Qt::BlockingQueuedConnection);
        QDBusConnection connection = QDBusConnection::connectToPeer(fake->address, QStringLiteral("powerdevil"));
        QVERIFY(connection.isConnected());
        QTRY_VERIFY(fake->builtIn);
        ScreenBrightness brightness(connection);
        QVERIFY(brightness.available());
        const QVariantList displays = brightness.displays();
        QCOMPARE(displays.size(), 2);
        QCOMPARE(displays.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Built-in display"));
        QCOMPARE(displays.at(0).toMap().value(QStringLiteral("percent")).toInt(), 50);
        brightness.setBrightness(QStringLiteral("display1"), 25);
        QTRY_COMPARE(fake->builtIn->brightness, 4800);
        // no on-screen display for a change made with the slider
        QCOMPARE(fake->builtIn->lastFlags, 1u);
        // a name that is not in the list is never called
        brightness.setBrightness(QStringLiteral("display9"), 10);
        connection.disconnectFromPeer(QStringLiteral("powerdevil"));
        fake->deleteLater();
    }

    void nightLight()
    {
        for (const char *file : {"kwinrc", "knighttimerc"})
            QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1Char('/')
                          + QLatin1String(file));
        NightLightSettings night;
        // KWin's defaults
        QVERIFY(!night.enabled());
        QVERIFY(night.scheduled());
        QCOMPARE(night.schedule(), QStringLiteral("sunset"));
        QCOMPARE(night.strength(), NightLightSettings::strengthOf(4500));

        QSignalSpy changed(&night, &NightLightSettings::changed);
        night.setEnabled(true);
        night.setScheduled(false);
        night.setStrength(80);
        night.setSchedule(QStringLiteral("times"));
        night.setTurnOn(QStringLiteral("22:30"));
        night.setTurnOff(QStringLiteral("6:15"));
        night.setTurnOff(QStringLiteral("25:99"));
        night.setSchedule(QStringLiteral("whenever"));
        QVERIFY(changed.count() >= 6);

        // what KWin and the day/night schedule read
        KSharedConfig::Ptr kwin = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
        kwin->reparseConfiguration();
        const KConfigGroup nc = kwin->group(QStringLiteral("NightColor"));
        QCOMPARE(nc.readEntry("Active", false), true);
        QCOMPARE(nc.readEntry("Mode", QString()), QStringLiteral("Constant"));
        QCOMPARE(nc.readEntry("NightTemperature", 0), NightLightSettings::temperatureOf(80));
        KSharedConfig::Ptr schedule = KSharedConfig::openConfig(QStringLiteral("knighttimerc"));
        schedule->reparseConfiguration();
        const KConfigGroup g = schedule->group(QStringLiteral("General"));
        QCOMPARE(g.readEntry("Source", QString()), QStringLiteral("Times"));
        QCOMPARE(g.readEntry("SunsetStart", QTime()), QTime(22, 30));
        QCOMPARE(g.readEntry("SunriseStart", QTime()), QTime(6, 15));

        NightLightSettings again;
        QCOMPARE(again.turnOn(), QStringLiteral("22:30"));
        QCOMPARE(again.turnOff(), QStringLiteral("06:15"));
        QCOMPARE(again.strength(), 80);
        QVERIFY(!again.scheduled());

        QCOMPARE(NightLightSettings::temperatureOf(0), 6500);
        QCOMPARE(NightLightSettings::temperatureOf(100), 1800);
        QCOMPARE(NightLightSettings::temperatureOf(250), 1800);
        for (int s : {0, 10, 48, 100})
            QCOMPARE(NightLightSettings::strengthOf(NightLightSettings::temperatureOf(s)), s);
        QVERIFY(!NightLightSettings::parseTime(QStringLiteral("noon")).isValid());
    }

    void graphics()
    {
        QTemporaryDir home, system;
        auto write = [](const QString &path, const QByteArray &text) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        write(system.filePath(QStringLiteral("org.raye.game.desktop")),
              "[Desktop Entry]\nType=Application\nName=Space Game\nExec=spacegame\nIcon=spacegame\n");
        write(system.filePath(QStringLiteral("hidden.desktop")),
              "[Desktop Entry]\nType=Application\nName=Hidden\nExec=x\nNoDisplay=true\n");
        write(system.filePath(QStringLiteral("kde/org.kde.editor.desktop")),
              "[Desktop Entry]\nType=Application\nName=Editor\nExec=editor\nPrefersNonDefaultGPU=true\n");
        const QString user = home.filePath(QStringLiteral("applications"));

        GpuApps apps(user, {system.path()});
        QCOMPARE(apps.rowCount(), 2);
        QCOMPARE(apps.data(apps.index(0), GpuApps::NameRole).toString(), QStringLiteral("Editor"));
        QCOMPARE(apps.data(apps.index(0), GpuApps::IdRole).toString(), QStringLiteral("kde-org.kde.editor.desktop"));
        QVERIFY(apps.data(apps.index(0), GpuApps::DedicatedRole).toBool());
        QVERIFY(!apps.data(apps.index(1), GpuApps::DedicatedRole).toBool());

        // on: a marked copy in the user's folder, the system's untouched
        QVERIFY(apps.setDedicated(1, true));
        const QString copy = user + QStringLiteral("/org.raye.game.desktop");
        QVERIFY(QFile::exists(copy));
        QFile original(system.filePath(QStringLiteral("org.raye.game.desktop")));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY(!original.readAll().contains("PrefersNonDefaultGPU"));
        GpuApps reread(user, {system.path()});
        QVERIFY(reread.data(reread.index(1), GpuApps::DedicatedRole).toBool());
        // off: our copy goes away
        QVERIFY(reread.setDedicated(1, false));
        QVERIFY(!QFile::exists(copy));

        // a copy the user made themselves is kept, only the key changes
        write(copy, "[Desktop Entry]\nType=Application\nName=Space Game\nExec=spacegame --fast\n");
        GpuApps own(user, {system.path()});
        QVERIFY(own.setDedicated(1, true));
        QVERIFY(own.setDedicated(1, false));
        QVERIFY(QFile::exists(copy));
        QFile mine(copy);
        QVERIFY(mine.open(QIODevice::ReadOnly));
        const QByteArray text = mine.readAll();
        QVERIFY(text.contains("--fast"));
        QVERIFY(!text.contains("PrefersNonDefaultGPU"));
        QVERIFY(!own.setDedicated(9, true));
    }
};

QTEST_GUILESS_MAIN(TestDisplay)
#include "test_display.moc"
