// SPDX-License-Identifier: GPL-3.0-or-later
// System > Power & battery: the battery, the power mode and energy
// saver against a fake UPower and a fake power-profiles-daemon on a
// private bus, and screen, sleep, lid and power button as the power
// daemon reads them in powerdevilrc.
#include "power.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

struct Item {
    uint time = 0;
    double value = 0;
    uint state = 0;
};

QDBusArgument &operator<<(QDBusArgument &arg, const Item &item)
{
    arg.beginStructure();
    arg << item.time << item.value << item.state;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, Item &item)
{
    arg.beginStructure();
    arg >> item.time >> item.value >> item.state;
    arg.endStructure();
    return arg;
}

} // namespace

Q_DECLARE_METATYPE(Item)

using Profiles = QList<QVariantMap>;

// org.freedesktop.UPower on /org/freedesktop/UPower
class FakeDaemon : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower")
    Q_PROPERTY(bool OnBattery READ onBattery)
    Q_PROPERTY(bool LidIsPresent READ lid)

public:
    explicit FakeDaemon(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }
    bool onBattery() const { return battery; }
    bool lid() const { return true; }
    bool battery = true;

public Q_SLOTS:
    QList<QDBusObjectPath> EnumerateDevices()
    {
        return {QDBusObjectPath(u"/org/freedesktop/UPower/devices/mouse_0"_s),
                QDBusObjectPath(u"/org/freedesktop/UPower/devices/battery_BAT0"_s)};
    }
};

// org.freedesktop.UPower.Device on the display device, a mouse and the battery
class FakeDevice : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower.Device")
    Q_PROPERTY(bool IsPresent READ present)
    Q_PROPERTY(uint Type READ type)
    Q_PROPERTY(double Percentage READ percentage)
    Q_PROPERTY(uint State READ state)
    Q_PROPERTY(qlonglong TimeToEmpty READ timeToEmpty)
    Q_PROPERTY(qlonglong TimeToFull READ timeToFull)
    Q_PROPERTY(bool PowerSupply READ powerSupply)

public:
    FakeDevice(QObject *parent, uint type, bool supply)
        : QDBusAbstractAdaptor(parent)
        , m_type(type)
        , m_supply(supply)
    {
    }
    bool present() const { return true; }
    uint type() const { return m_type; }
    double percentage() const { return 64.4; }
    uint state() const { return 2; }
    qlonglong timeToEmpty() const { return 3 * 3600 + 20 * 60; }
    qlonglong timeToFull() const { return 0; }
    bool powerSupply() const { return m_supply; }
    QList<Item> history;

public Q_SLOTS:
    QList<Item> GetHistory(const QString &type, uint timespan, uint resolution)
    {
        Q_UNUSED(timespan)
        Q_UNUSED(resolution)
        return type == QLatin1String("charge") ? history : QList<Item>();
    }

private:
    uint m_type;
    bool m_supply;
};

// org.freedesktop.UPower.PowerProfiles
class FakeProfiles : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower.PowerProfiles")
    Q_PROPERTY(QString ActiveProfile READ active WRITE setActive)
    Q_PROPERTY(Profiles Profiles READ profiles)

public:
    explicit FakeProfiles(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }
    QString active() const { return m_active; }
    void setActive(const QString &p) { m_active = p; }
    Profiles profiles() const
    {
        return {QVariantMap{{u"Profile"_s, u"performance"_s}, {u"Driver"_s, u"platform_profile"_s}},
                QVariantMap{{u"Profile"_s, u"balanced"_s}}, QVariantMap{{u"Profile"_s, u"power-saver"_s}}};
    }
    QString m_active = u"performance"_s;
};

class TestPower : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QProcess m_busDaemon;
    QString m_address;
    std::unique_ptr<QObject> m_root, m_display, m_mouse, m_battery, m_profilesObject;
    FakeDevice *m_batteryDevice = nullptr;
    FakeProfiles *m_profiles = nullptr;

    QDBusConnection client() { return QDBusConnection::connectToBus(m_address, u"client"_s); }
    QString home(const QString &f) const { return m_dir.filePath(u"config/"_s + f); }

    QString entry(const QStringList &groups, const QString &key) const
    {
        KConfig config(home(u"powerdevilrc"_s), KConfig::SimpleConfig);
        KConfigGroup g = config.group(groups.first());
        for (int i = 1; i < groups.size(); ++i)
            g = g.group(groups.at(i));
        return g.readEntry(key, u"<unset>"_s);
    }

    void resetConfig()
    {
        QFile::remove(home(u"powerdevilrc"_s));
        QFile::remove(home(u"raye-settingsrc"_s));
        KSharedConfig::openConfig(u"powerdevilrc"_s)->reparseConfiguration();
        KSharedConfig::openConfig(u"raye-settingsrc"_s)->reparseConfiguration();
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(u"config"_s).toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(u"xdg"_s).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qDBusRegisterMetaType<Item>();
        qDBusRegisterMetaType<QList<Item>>();
        qDBusRegisterMetaType<Profiles>();

        // a private bus of our own
        const QString config = m_dir.filePath(u"bus.conf"_s);
        QFile conf(config);
        QVERIFY(conf.open(QIODevice::WriteOnly));
        conf.write(QStringLiteral("<busconfig><type>session</type><listen>unix:dir=%1</listen>"
                                  "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                                  "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>")
                       .arg(m_dir.path())
                       .toUtf8());
        conf.close();
        m_busDaemon.start(u"dbus-daemon"_s, {u"--config-file="_s + config, u"--print-address"_s, u"--nofork"_s});
        if (!m_busDaemon.waitForStarted(5000))
            QSKIP("no dbus-daemon here");
        QTRY_VERIFY_WITH_TIMEOUT(m_busDaemon.canReadLine(), 5000);
        m_address = QString::fromUtf8(m_busDaemon.readLine()).trimmed();

        QDBusConnection server = QDBusConnection::connectToBus(m_address, u"server"_s);
        QVERIFY(server.isConnected());
        m_root = std::make_unique<QObject>();
        new FakeDaemon(m_root.get());
        m_display = std::make_unique<QObject>();
        new FakeDevice(m_display.get(), 2, true);
        m_mouse = std::make_unique<QObject>();
        new FakeDevice(m_mouse.get(), 5, false);
        m_battery = std::make_unique<QObject>();
        m_batteryDevice = new FakeDevice(m_battery.get(), 2, true);
        const uint now = uint(QDateTime(QDate(2026, 10, 31), QTime(14, 30)).toSecsSinceEpoch());
        m_batteryDevice->history = {{now - 600, 70, 2}, {now - 300, 66, 2}, {now - 2 * 3600, 40, 1}, {now - 30 * 3600, 99, 4}};
        m_profilesObject = std::make_unique<QObject>();
        m_profiles = new FakeProfiles(m_profilesObject.get());
        const auto flags = QDBusConnection::ExportAdaptors;
        QVERIFY(server.registerObject(u"/org/freedesktop/UPower"_s, m_root.get(), flags));
        QVERIFY(server.registerObject(u"/org/freedesktop/UPower/devices/DisplayDevice"_s, m_display.get(), flags));
        QVERIFY(server.registerObject(u"/org/freedesktop/UPower/devices/mouse_0"_s, m_mouse.get(), flags));
        QVERIFY(server.registerObject(u"/org/freedesktop/UPower/devices/battery_BAT0"_s, m_battery.get(), flags));
        QVERIFY(server.registerObject(u"/org/freedesktop/UPower/PowerProfiles"_s, m_profilesObject.get(), flags));
        QVERIFY(server.registerService(u"org.freedesktop.UPower"_s));
        QVERIFY(server.registerService(u"org.freedesktop.UPower.PowerProfiles"_s));
    }

    void cleanupTestCase()
    {
        m_busDaemon.kill();
        m_busDaemon.waitForFinished();
    }

    void init() { resetConfig(); }

    void durations()
    {
        QCOMPARE(PowerSettings::durationText(3 * 3600 + 20 * 60), u"3 h 20 min"_s);
        QCOMPARE(PowerSettings::durationText(3600), u"1 h"_s);
        QCOMPARE(PowerSettings::durationText(45 * 60 + 10), u"45 min"_s);
        QCOMPARE(PowerSettings::durationText(5), u"1 min"_s);
    }

    void estimatesPastADayAreHidden()
    {
        // seen on a Dell just after plugging in: "full in 978 h"
        QVERIFY(!PowerSettings::estimateShown(978 * 3600));
        QVERIFY(!PowerSettings::estimateShown(24 * 3600 + 1));
        QVERIFY(!PowerSettings::estimateShown(0));
        QVERIFY(PowerSettings::estimateShown(24 * 3600));
        QVERIFY(PowerSettings::estimateShown(45 * 60));
    }

    void states()
    {
        QCOMPARE(PowerSettings::stateName(1), u"charging"_s);
        QCOMPARE(PowerSettings::stateName(2), u"discharging"_s);
        QCOMPARE(PowerSettings::stateName(4), u"full"_s);
        QCOMPARE(PowerSettings::stateName(5), u"charging"_s);
        QCOMPARE(PowerSettings::stateName(0), u"not-charging"_s);
    }

    void hourlyBars()
    {
        const QDateTime now(QDate(2026, 10, 31), QTime(14, 30));
        const qint64 t = now.toSecsSinceEpoch();
        const QVariantList bars = PowerSettings::hourly({{t - 600, 70, 2}, {t - 300, 66, 2}, {t - 7200, 40, 1}, {t - 30 * 3600, 99, 4}}, now);
        QCOMPARE(bars.size(), 24);
        const QVariantMap last = bars.last().toMap();
        // the last bar is the hour 14:00-15:00, the average of its samples
        QCOMPARE(last.value(u"start"_s).toLongLong(), QDateTime(QDate(2026, 10, 31), QTime(14, 0)).toMSecsSinceEpoch());
        QCOMPARE(last.value(u"level"_s).toInt(), 68);
        QCOMPARE(last.value(u"charging"_s).toBool(), false);
        const QVariantMap twoAgo = bars.at(21).toMap(); // 12:00-13:00
        QCOMPARE(twoAgo.value(u"level"_s).toInt(), 40);
        QVERIFY(twoAgo.value(u"charging"_s).toBool());
        QCOMPARE(bars.at(0).toMap().value(u"level"_s).toInt(), -1); // nothing known; 30 h ago is out
    }

    void profileOrder()
    {
        const QVariantList list = {QVariantMap{{u"Profile"_s, u"performance"_s}}, QVariantMap{{u"Profile"_s, u"power-saver"_s}},
                                   QVariantMap{{u"Profile"_s, u"balanced"_s}}, QVariantMap{{u"Profile"_s, u"odd"_s}}};
        QCOMPARE(PowerSettings::profileNames(list), (QStringList{u"power-saver"_s, u"balanced"_s, u"performance"_s}));
    }

    void virtualMachines()
    {
        QVERIFY(PowerSettings::isVirtualMachine(u"flags\t\t: fpu vme hypervisor lm\n"_s));
        QVERIFY(!PowerSettings::isVirtualMachine(u"flags\t\t: fpu vme lm\n"_s));
    }

    void battery()
    {
        if (m_address.isEmpty())
            QSKIP("no bus");
        PowerSettings p(client(), m_dir.path(), nullptr);
        p.setClock([]() { return QDateTime(QDate(2026, 10, 31), QTime(14, 30)); });
        QTRY_VERIFY_WITH_TIMEOUT(p.hasBattery(), 5000);
        QCOMPARE(p.batteryLevel(), 64);
        QCOMPARE(p.batteryState(), u"discharging"_s);
        QCOMPARE(p.batteryText(), u"About 3 h 20 min left"_s);
        QTRY_VERIFY_WITH_TIMEOUT(p.onBattery(), 5000);
        QVERIFY(p.lidPresent());
        // the history of the battery, not of the mouse
        QTRY_COMPARE_WITH_TIMEOUT(p.history().size(), 24, 5000);
        QCOMPARE(p.history().last().toMap().value(u"level"_s).toInt(), 68);
    }

    void powerMode()
    {
        if (m_address.isEmpty())
            QSKIP("no bus");
        m_profiles->m_active = u"performance"_s;
        PowerSettings p(client(), m_dir.path(), nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(p.profilesAvailable(), 5000);
        QCOMPARE(p.profiles(), (QStringList{u"power-saver"_s, u"balanced"_s, u"performance"_s}));
        QCOMPARE(p.profile(), u"performance"_s);
        QVERIFY(!p.energySaver());

        // energy saver on, then off: back to the mode in use before
        p.setEnergySaver(true);
        QTRY_COMPARE_WITH_TIMEOUT(m_profiles->m_active, u"power-saver"_s, 5000);
        QVERIFY(p.energySaver());
        p.setEnergySaver(false);
        QTRY_COMPARE_WITH_TIMEOUT(m_profiles->m_active, u"performance"_s, 5000);

        p.setProfile(u"balanced"_s);
        QTRY_COMPARE_WITH_TIMEOUT(m_profiles->m_active, u"balanced"_s, 5000);
        // not one of the daemon's: nothing sent
        p.setProfile(u"turbo"_s);
        QCOMPARE(p.profile(), u"balanced"_s);
    }

    void noDaemons()
    {
        // nothing answers: no battery, no power mode, no crash
        QDBusConnection empty = QDBusConnection::connectToBus(m_address, u"empty"_s);
        if (m_address.isEmpty())
            QSKIP("no bus");
        QDBusConnection server = QDBusConnection::connectToBus(m_address, u"server"_s);
        server.unregisterService(u"org.freedesktop.UPower.PowerProfiles"_s);
        PowerSettings p(empty, m_dir.path(), nullptr);
        QTest::qWait(300);
        QVERIFY(!p.profilesAvailable());
        QVERIFY(server.registerService(u"org.freedesktop.UPower.PowerProfiles"_s));
    }

    void energySaverSettings()
    {
        PowerSettings p(client(), m_dir.path(), nullptr);
        QCOMPARE(p.energySaverAt(), 20);
        QVERIFY(p.energySaverDims());
        p.setEnergySaverAt(30);
        p.setEnergySaverDims(false);
        KConfig own(home(u"raye-settingsrc"_s), KConfig::SimpleConfig);
        QCOMPARE(own.group(u"EnergySaver"_s).readEntry("TurnOnAt", 0), 30);
        QCOMPARE(own.group(u"EnergySaver"_s).readEntry("LowerBrightness", true), false);
        p.setEnergySaverAt(500);
        QCOMPARE(p.energySaverAt(), 100);
        QVERIFY(!p.energySaverAutoAvailable()); // nothing installed under the test root
    }

    void timeoutDefaults()
    {
        PowerSettings p(client(), m_dir.path(), nullptr);
        QCOMPARE(p.screenOffAc(), 600);
        QCOMPARE(p.screenOffBattery(), 300);
        QCOMPARE(p.sleepAc(), 900);
        QCOMPARE(p.sleepBattery(), 600);
        QCOMPARE(p.lidAc(), int(PowerSettings::Sleep));
        QCOMPARE(p.buttonAc(), int(PowerSettings::Ask));
    }

    void screenOff()
    {
        PowerSettings p(client(), m_dir.path(), nullptr);
        QSignalSpy changed(&p, &PowerSettings::timeoutsChanged);
        p.setScreenOffAc(120);
        QCOMPARE(p.screenOffAc(), 120);
        QCOMPARE(entry({u"AC"_s, u"Display"_s}, u"TurnOffDisplayIdleTimeoutSec"_s), u"120"_s);
        QCOMPARE(entry({u"AC"_s, u"Display"_s}, u"TurnOffDisplayWhenIdle"_s), u"true"_s);
        // the screen dimmed at 5 minutes: now it dims before going dark
        QCOMPARE(entry({u"AC"_s, u"Display"_s}, u"DimDisplayIdleTimeoutSec"_s), u"60"_s);
        QCOMPARE(changed.count(), 1);

        p.setScreenOffAc(0);
        QCOMPARE(p.screenOffAc(), 0);
        QCOMPARE(entry({u"AC"_s, u"Display"_s}, u"TurnOffDisplayWhenIdle"_s), u"false"_s);

        // on battery: a low battery is never later than that
        p.setScreenOffBattery(60);
        QCOMPARE(entry({u"LowBattery"_s, u"Display"_s}, u"TurnOffDisplayIdleTimeoutSec"_s), u"60"_s);
        p.setScreenOffBattery(1800);
        QCOMPARE(entry({u"LowBattery"_s, u"Display"_s}, u"TurnOffDisplayIdleTimeoutSec"_s), u"60"_s);
        QCOMPARE(p.screenOffBattery(), 1800);
    }

    void sleep()
    {
        PowerSettings p(client(), m_dir.path(), nullptr);
        p.setSleepAc(0);
        QCOMPARE(p.sleepAc(), 0);
        QCOMPARE(entry({u"AC"_s, u"SuspendAndShutdown"_s}, u"AutoSuspendAction"_s), u"0"_s);
        p.setSleepAc(1800);
        QCOMPARE(p.sleepAc(), 1800);
        QCOMPARE(entry({u"AC"_s, u"SuspendAndShutdown"_s}, u"AutoSuspendAction"_s), u"1"_s);
        QCOMPARE(entry({u"AC"_s, u"SuspendAndShutdown"_s}, u"AutoSuspendIdleTimeoutSec"_s), u"1800"_s);
    }

    void virtualMachineDoesNotSleep()
    {
        QTemporaryDir vm;
        QDir().mkpath(vm.filePath(u"proc"_s));
        QFile f(vm.filePath(u"proc/cpuinfo"_s));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("flags\t\t: fpu hypervisor\n");
        f.close();
        PowerSettings p(client(), vm.path(), nullptr);
        QCOMPARE(p.sleepAc(), 0);
        QCOMPARE(p.lidAc(), int(PowerSettings::NoAction));
    }

    void lidAndButton()
    {
        PowerSettings p(client(), m_dir.path(), nullptr);
        p.setLidBattery(PowerSettings::Hibernate);
        p.setButtonAc(PowerSettings::Shutdown);
        QCOMPARE(p.lidBattery(), int(PowerSettings::Hibernate));
        QCOMPARE(entry({u"Battery"_s, u"SuspendAndShutdown"_s}, u"LidAction"_s), u"2"_s);
        QCOMPARE(entry({u"LowBattery"_s, u"SuspendAndShutdown"_s}, u"LidAction"_s), u"2"_s);
        QCOMPARE(entry({u"AC"_s, u"SuspendAndShutdown"_s}, u"PowerButtonAction"_s), u"8"_s);
        // a number the daemon does not know is not written
        p.setButtonAc(12345);
        QCOMPARE(p.buttonAc(), int(PowerSettings::Shutdown));
    }

    void choices()
    {
        const QVariantList t = PowerSettings::timeoutChoices();
        QCOMPARE(t.first().toMap().value(u"text"_s).toString(), u"1 minute"_s);
        QCOMPARE(t.last().toMap().value(u"value"_s).toInt(), 0);
        QCOMPARE(t.last().toMap().value(u"text"_s).toString(), u"Never"_s);
        PowerSettings p(client(), m_dir.path(), nullptr);
        auto values = [](const QVariantList &l) {
            QList<int> v;
            for (const QVariant &x : l)
                v << x.toMap().value(u"value"_s).toInt();
            return v;
        };
        // no hibernate here (logind does not answer); the lid never asks
        QVERIFY(!values(p.actionChoices(true, 1)).contains(PowerSettings::Ask));
        QVERIFY(values(p.actionChoices(false, 16)).contains(PowerSettings::Ask));
        QVERIFY(!values(p.actionChoices(true, 1)).contains(PowerSettings::Hibernate));
        // what the user chose elsewhere stays in the list
        QVERIFY(values(p.actionChoices(true, 32)).contains(PowerSettings::Lock));
        QVERIFY(values(p.actionChoices(true, 2)).contains(PowerSettings::Hibernate));
    }

    void sleepGuardState()
    {
        QCOMPARE(PowerSettings::sleepOffReasonOf(R"({"status": "off", "reason": "model"})"), u"model"_s);
        QCOMPARE(PowerSettings::sleepOffReasonOf(R"({"status": "off", "reason": "failed"})"), u"failed"_s);
        QCOMPARE(PowerSettings::sleepOffReasonOf(R"({"status": "off"})"), u"failed"_s);
        QVERIFY(PowerSettings::sleepOffReasonOf(R"({"status": "warned", "reason": ""})").isEmpty());
        QVERIFY(PowerSettings::sleepOffReasonOf(R"({"status": "ok", "user_on": true})").isEmpty());
        QVERIFY(PowerSettings::sleepOffReasonOf("not json").isEmpty());
    }

    void sleepTurnedOffByRaye()
    {
        QTemporaryDir root;
        writeGuardState(root.path(), R"({"status": "off", "reason": "model", "event": 1})");
        PowerSettings p(client(), root.path(), nullptr);
        QVERIFY(p.sleepOff());
        QCOMPARE(p.sleepOffReason(), u"model"_s);
        // the lid turns the screen off (the daemon's rule when it cannot sleep)
        QCOMPARE(p.lidAc(), int(PowerSettings::ScreenOff));
        auto values = [](const QVariantList &l) {
            QList<int> v;
            for (const QVariant &x : l)
                v << x.toMap().value(u"value"_s).toInt();
            return v;
        };
        QVERIFY(!values(p.actionChoices(true, PowerSettings::ScreenOff)).contains(PowerSettings::Sleep));
        QVERIFY(!values(p.actionChoices(false, PowerSettings::Ask)).contains(PowerSettings::Sleep));
        // a choice already made stays shown
        QVERIFY(values(p.actionChoices(true, PowerSettings::Sleep)).contains(PowerSettings::Sleep));

        PowerSettings normal(client(), m_dir.path(), nullptr);
        QVERIFY(!normal.sleepOff());
        QVERIFY(values(normal.actionChoices(true, 1)).contains(PowerSettings::Sleep));
    }

    void turnSleepBackOn()
    {
        QTemporaryDir root;
        writeGuardState(root.path(), R"({"status": "off", "reason": "failed"})");
        const QString state = root.filePath(u"var/lib/raye/sleep-guard/state.json"_s);
        PowerSettings p(client(), root.path(), nullptr);
        p.setSleepRestoreCommand({});

        // the password box closed: nothing changes, no error
        p.setSleepGuardCommand({u"sh"_s, u"-c"_s, u"exit 126"_s});
        QSignalSpy spy(&p, &PowerSettings::sleepGuardChanged);
        QVERIFY(p.turnSleepBackOn());
        QVERIFY(p.sleepBusy());
        QVERIFY(!p.turnSleepBackOn());
        QTRY_VERIFY(!p.sleepBusy());
        QVERIFY(p.sleepOff());
        QVERIFY(!p.sleepFailed());

        // the helper failed
        p.setSleepGuardCommand({u"sh"_s, u"-c"_s, u"exit 1"_s});
        QVERIFY(p.turnSleepBackOn());
        QTRY_VERIFY(!p.sleepBusy());
        QVERIFY(p.sleepFailed());
        QVERIFY(p.sleepOff());

        // it went through: the guard wrote "ok"
        p.setSleepGuardCommand({u"sh"_s, u"-c"_s, u"printf '{\"status\": \"ok\", \"user_on\": true}' > \"$0\""_s, state});
        QVERIFY(p.turnSleepBackOn());
        QTRY_VERIFY(!p.sleepBusy());
        QVERIFY(!p.sleepFailed());
        QVERIFY(!p.sleepOff());
        QVERIFY(p.sleepTurnedOn());
        QCOMPARE(p.lidAc(), int(PowerSettings::Sleep));
    }

private:
    static void writeGuardState(const QString &root, const QByteArray &json)
    {
        QDir().mkpath(root + u"/var/lib/raye/sleep-guard"_s);
        QFile f(root + u"/var/lib/raye/sleep-guard/state.json"_s);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(json);
    }
};

QTEST_GUILESS_MAIN(TestPower)
#include "test_power.moc"
