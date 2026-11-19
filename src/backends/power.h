// SPDX-License-Identifier: GPL-3.0-or-later
// System > Power & battery.
//
// - Battery: UPower on the system bus (the display device for level,
//   state and time left; the battery itself for the 24 hour history,
//   GetHistory "charge").
// - Power mode and Energy saver: power-profiles-daemon
//   (org.freedesktop.UPower.PowerProfiles, or the older
//   net.hadess.PowerProfiles name): power-saver, balanced, performance.
//   Energy saver on is the power-saver profile; turning it off goes
//   back to the mode in use before.
// - Screen and sleep, lid and power button: the power daemon's
//   powerdevilrc, one group per power source ([AC], [Battery],
//   [LowBattery]) with [Display] and [SuspendAndShutdown] under it; the
//   daemon is told to reload after a change. The lock follows the
//   screen (LockBeforeTurnOffDisplay, written by the theme): Settings
//   shows the one number.
// - Energy saver turning on by itself at a battery level is done by the
//   session's energy saver service; its settings are here, in
//   raye-settingsrc [EnergySaver] TurnOnAt (percent, 0 = never) and
//   LowerBrightness.
// - Sleep turned off on a PC where it does not wake up: Raye's sleep guard
//   (raye-base) keeps its state in /var/lib/raye/sleep-guard/state.json,
//   readable by anyone; "Turn sleep back on" runs its "enable" through
//   pkexec. While sleep is off, the lid's default is to turn the screen
//   off (the power daemon's own rule when the PC cannot sleep) and Sleep
//   is not offered for the lid or the power button.
#pragma once

#include <KSharedConfig>
#include <QDBusConnection>
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class PowerSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // battery
    Q_PROPERTY(bool hasBattery READ hasBattery NOTIFY batteryChanged)
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryChanged)
    // "charging", "discharging", "full", "not-charging"
    Q_PROPERTY(QString batteryState READ batteryState NOTIFY batteryChanged)
    Q_PROPERTY(qint64 timeToEmpty READ timeToEmpty NOTIFY batteryChanged)
    Q_PROPERTY(qint64 timeToFull READ timeToFull NOTIFY batteryChanged)
    // "About 3 h 20 min left", "Full in 45 min", "Fully charged"
    Q_PROPERTY(QString batteryText READ batteryText NOTIFY batteryChanged)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY batteryChanged)
    Q_PROPERTY(bool lidPresent READ lidPresent NOTIFY batteryChanged)
    // the last 24 hours, one entry an hour: { start (ms), level (-1 when
    // nothing is known), charging }
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)

    // power mode
    Q_PROPERTY(bool profilesAvailable READ profilesAvailable NOTIFY profileChanged)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY profileChanged)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(bool energySaver READ energySaver WRITE setEnergySaver NOTIFY profileChanged)
    // the service that turns energy saver on by itself is installed
    Q_PROPERTY(bool energySaverAutoAvailable READ energySaverAutoAvailable CONSTANT)
    Q_PROPERTY(int energySaverAt READ energySaverAt WRITE setEnergySaverAt NOTIFY energySaverSettingsChanged)
    Q_PROPERTY(bool energySaverDims READ energySaverDims WRITE setEnergySaverDims NOTIFY energySaverSettingsChanged)

    // timeouts in seconds, 0 = never
    Q_PROPERTY(int screenOffAc READ screenOffAc WRITE setScreenOffAc NOTIFY timeoutsChanged)
    Q_PROPERTY(int screenOffBattery READ screenOffBattery WRITE setScreenOffBattery NOTIFY timeoutsChanged)
    Q_PROPERTY(int sleepAc READ sleepAc WRITE setSleepAc NOTIFY timeoutsChanged)
    Q_PROPERTY(int sleepBattery READ sleepBattery WRITE setSleepBattery NOTIFY timeoutsChanged)
    // actions (the daemon's numbers: 0 nothing, 1 sleep, 2 hibernate,
    // 8 shut down, 16 ask, 32 lock, 64 turn off the screen)
    Q_PROPERTY(int lidAc READ lidAc WRITE setLidAc NOTIFY timeoutsChanged)
    Q_PROPERTY(int lidBattery READ lidBattery WRITE setLidBattery NOTIFY timeoutsChanged)
    Q_PROPERTY(int buttonAc READ buttonAc WRITE setButtonAc NOTIFY timeoutsChanged)
    Q_PROPERTY(int buttonBattery READ buttonBattery WRITE setButtonBattery NOTIFY timeoutsChanged)
    Q_PROPERTY(bool canHibernate READ canHibernate NOTIFY batteryChanged)

    // sleep turned off on this PC by Raye's sleep guard
    Q_PROPERTY(bool sleepOff READ sleepOff NOTIFY sleepGuardChanged)
    // "failed" (it did not wake up twice) or "model" (a PC known not to)
    Q_PROPERTY(QString sleepOffReason READ sleepOffReason NOTIFY sleepGuardChanged)
    Q_PROPERTY(bool sleepBusy READ sleepBusy NOTIFY sleepGuardChanged)
    // the last "turn back on" did not go through (not the password closed)
    Q_PROPERTY(bool sleepFailed READ sleepFailed NOTIFY sleepGuardChanged)
    // turned back on in this window: Plasma shows Sleep after signing in again
    Q_PROPERTY(bool sleepTurnedOn READ sleepTurnedOn NOTIFY sleepGuardChanged)

public:
    enum Action { NoAction = 0, Sleep = 1, Hibernate = 2, Shutdown = 8, Ask = 16, Lock = 32, ScreenOff = 64 };
    Q_ENUM(Action)

    explicit PowerSettings(QObject *parent = nullptr);
    // `bus` is for the tests (a private bus with a fake UPower and a
    // fake power-profiles-daemon); `root` is where /proc is read.
    PowerSettings(const QDBusConnection &bus, const QString &root, QObject *parent);

    bool hasBattery() const { return m_hasBattery; }
    int batteryLevel() const { return m_level; }
    QString batteryState() const { return m_state; }
    qint64 timeToEmpty() const { return m_timeToEmpty; }
    qint64 timeToFull() const { return m_timeToFull; }
    QString batteryText() const;
    bool onBattery() const { return m_onBattery; }
    bool lidPresent() const { return m_lidPresent; }
    QVariantList history() const { return m_history; }

    bool profilesAvailable() const { return !m_profiles.isEmpty(); }
    QStringList profiles() const { return m_profiles; }
    QString profile() const { return m_profile; }
    void setProfile(const QString &profile);
    bool energySaver() const { return m_profile == QLatin1String("power-saver"); }
    void setEnergySaver(bool on);
    bool energySaverAutoAvailable() const;
    int energySaverAt() const;
    void setEnergySaverAt(int percent);
    bool energySaverDims() const;
    void setEnergySaverDims(bool on);

    int screenOffAc() const { return screenOff(QStringLiteral("AC")); }
    int screenOffBattery() const { return screenOff(QStringLiteral("Battery")); }
    void setScreenOffAc(int s) { setScreenOff(QStringLiteral("AC"), s); }
    void setScreenOffBattery(int s) { setScreenOff(QStringLiteral("Battery"), s); }
    int sleepAc() const { return sleepAfter(QStringLiteral("AC")); }
    int sleepBattery() const { return sleepAfter(QStringLiteral("Battery")); }
    void setSleepAc(int s) { setSleepAfter(QStringLiteral("AC"), s); }
    void setSleepBattery(int s) { setSleepAfter(QStringLiteral("Battery"), s); }
    int lidAc() const { return action(QStringLiteral("AC"), "LidAction"); }
    int lidBattery() const { return action(QStringLiteral("Battery"), "LidAction"); }
    void setLidAc(int a) { setAction(QStringLiteral("AC"), "LidAction", a); }
    void setLidBattery(int a) { setAction(QStringLiteral("Battery"), "LidAction", a); }
    int buttonAc() const { return action(QStringLiteral("AC"), "PowerButtonAction"); }
    int buttonBattery() const { return action(QStringLiteral("Battery"), "PowerButtonAction"); }
    void setButtonAc(int a) { setAction(QStringLiteral("AC"), "PowerButtonAction", a); }
    void setButtonBattery(int a) { setAction(QStringLiteral("Battery"), "PowerButtonAction", a); }
    bool canHibernate() const { return m_canHibernate; }

    bool sleepOff() const { return !m_sleepOffReason.isEmpty(); }
    QString sleepOffReason() const { return m_sleepOffReason; }
    bool sleepBusy() const { return m_sleepBusy; }
    bool sleepFailed() const { return m_sleepFailed; }
    bool sleepTurnedOn() const { return m_sleepTurnedOn; }
    // false when it is already running
    Q_INVOKABLE bool turnSleepBackOn();
    Q_INVOKABLE void readSleepGuard();
    // the guard's state.json to the reason sleep is off, empty when it is on
    static QString sleepOffReasonOf(const QByteArray &stateJson);
    // for the tests
    void setSleepGuardCommand(const QStringList &command) { m_sleepCommand = command; }
    void setSleepRestoreCommand(const QStringList &command) { m_sleepRestore = command; }

    // per source ("AC", "Battery")
    int screenOff(const QString &source) const;
    void setScreenOff(const QString &source, int seconds);
    int sleepAfter(const QString &source) const;
    void setSleepAfter(const QString &source, int seconds);
    int action(const QString &source, const char *key) const;
    void setAction(const QString &source, const char *key, int action);

    // [{ value (seconds, 0 = never), text }] as Windows lists them
    Q_INVOKABLE static QVariantList timeoutChoices();
    // [{ value, text }] for the lid or the power button; `current` is
    // kept in the list even when it is not one of the usual ones
    Q_INVOKABLE QVariantList actionChoices(bool lid, int current) const;
    // the power daemon's own settings, in a window of their own
    Q_INVOKABLE void openMoreOptions() const;
    Q_INVOKABLE void reload();

    // The parsing, public for the tests.
    // "3 h 20 min", "45 min", "1 h"
    static QString durationText(qint64 seconds);
    static bool estimateShown(qint64 seconds);
    struct Sample {
        qint64 time = 0; // seconds since 1970
        double level = 0;
        uint state = 0; // UPower: 1 charging, 2 discharging, 4 full
    };
    static QVariantList hourly(const QList<Sample> &samples, const QDateTime &now);
    // aa{sv} of the daemon to the profile names it offers, in order
    static QStringList profileNames(const QVariant &profiles);
    static QString stateName(uint upowerState);
    // a virtual machine never sleeps by default (the power daemon's rule)
    static bool isVirtualMachine(const QString &cpuinfo);

    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

Q_SIGNALS:
    void batteryChanged();
    void historyChanged();
    void profileChanged();
    void energySaverSettingsChanged();
    void timeoutsChanged();
    void sleepGuardChanged();

private Q_SLOTS:
    void readBattery();
    void readProfiles();
    void onUPowerChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onProfilesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void readHistory();
    void readProfilesFrom(int index);
    void findBattery(QStringList paths);
    void notifyDaemon();
    QDateTime now() const { return m_clock ? m_clock() : QDateTime::currentDateTime(); }

    QDBusConnection m_bus;
    QString m_root;
    KSharedConfigPtr m_config; // powerdevilrc
    KSharedConfigPtr m_own;    // raye-settingsrc
    bool m_isVM = false;

    bool m_hasBattery = false;
    int m_level = 0;
    QString m_state;
    qint64 m_timeToEmpty = 0;
    qint64 m_timeToFull = 0;
    bool m_onBattery = false;
    bool m_lidPresent = false;
    bool m_canHibernate = false;
    QString m_sleepOffReason;
    bool m_sleepBusy = false;
    bool m_sleepFailed = false;
    bool m_sleepTurnedOn = false;
    QStringList m_sleepCommand;
    QStringList m_sleepRestore;
    QString m_batteryPath;
    QVariantList m_history;

    QString m_profilesService;
    QString m_profilesPath;
    QString m_profilesInterface;
    QStringList m_profiles;
    QString m_profile;
    std::function<QDateTime()> m_clock;
};
