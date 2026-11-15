// SPDX-License-Identifier: GPL-3.0-or-later
#include "power.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace {

const QString upower = QStringLiteral("org.freedesktop.UPower");
const QString upowerPath = QStringLiteral("/org/freedesktop/UPower");
const QString displayDevice = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
const QString deviceInterface = QStringLiteral("org.freedesktop.UPower.Device");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");

struct HistoryItem {
    uint time = 0;
    double value = 0;
    uint state = 0;
};

QDBusArgument &operator<<(QDBusArgument &arg, const HistoryItem &item)
{
    arg.beginStructure();
    arg << item.time << item.value << item.state;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, HistoryItem &item)
{
    arg.beginStructure();
    arg >> item.time >> item.value >> item.state;
    arg.endStructure();
    return arg;
}

// the power daemon's defaults (powerdevilsettingsdefaults.cpp, 6.6)
int defaultScreenOff(const QString &source)
{
    return source == QLatin1String("AC") ? 600 : source == QLatin1String("Battery") ? 300 : 120;
}

int defaultSleep(const QString &source)
{
    return source == QLatin1String("AC") ? 900 : source == QLatin1String("Battery") ? 600 : 300;
}

QString kcmshell()
{
    const QString kde = QStandardPaths::findExecutable(QStringLiteral("kcmshell6.kde"));
    return kde.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("kcmshell6")) : kde;
}

} // namespace

Q_DECLARE_METATYPE(HistoryItem)

PowerSettings::PowerSettings(QObject *parent)
    : PowerSettings(QDBusConnection::systemBus(), QStringLiteral("/"), parent)
{
}

PowerSettings::PowerSettings(const QDBusConnection &bus, const QString &root, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_root(root)
    , m_config(KSharedConfig::openConfig(QStringLiteral("powerdevilrc")))
    , m_own(KSharedConfig::openConfig(QStringLiteral("raye-settingsrc")))
{
    qDBusRegisterMetaType<HistoryItem>();
    qDBusRegisterMetaType<QList<HistoryItem>>();

    QFile cpuinfo(m_root + QStringLiteral("/proc/cpuinfo"));
    if (cpuinfo.open(QIODevice::ReadOnly))
        m_isVM = isVirtualMachine(QString::fromUtf8(cpuinfo.read(1 << 20)));

    m_bus.connect(upower, displayDevice, properties, QStringLiteral("PropertiesChanged"), this,
                  SLOT(onUPowerChanged(QString, QVariantMap, QStringList)));
    m_bus.connect(upower, upowerPath, properties, QStringLiteral("PropertiesChanged"), this,
                  SLOT(onUPowerChanged(QString, QVariantMap, QStringList)));
    for (const QString &service : {QStringLiteral("org.freedesktop.UPower.PowerProfiles"), QStringLiteral("net.hadess.PowerProfiles")}) {
        const QString path = QLatin1Char('/') + QString(service).replace(QLatin1Char('.'), QLatin1Char('/'));
        m_bus.connect(service, path, properties, QStringLiteral("PropertiesChanged"), this,
                      SLOT(onProfilesChanged(QString, QVariantMap, QStringList)));
    }
    // RAYE_POWER_PREVIEW=laptop draws a laptop on battery without asking
    // the system anything: the way to look at the page on a desktop
    if (qEnvironmentVariable("RAYE_POWER_PREVIEW") == QLatin1String("laptop")) {
        m_hasBattery = m_onBattery = m_lidPresent = true;
        m_level = 64;
        m_state = QStringLiteral("discharging");
        m_timeToEmpty = 3 * 3600 + 20 * 60;
        m_profiles = {QStringLiteral("power-saver"), QStringLiteral("balanced"), QStringLiteral("performance")};
        m_profile = QStringLiteral("balanced");
        QList<Sample> samples;
        const qint64 t = now().toSecsSinceEpoch();
        for (int i = 0; i < 24 * 4; ++i) {
            const int hour = i / 4;
            const bool charging = hour >= 9 && hour < 12;
            const double level = charging ? 30 + (hour - 9) * 22 : hour < 9 ? 95 - hour * 7 : 96 - (hour - 12) * 2.7;
            samples.append({t - qint64(24 * 3600) + qint64(i) * 900, level, charging ? 1u : 2u});
        }
        m_history = hourly(samples, now());
        return;
    }
    readBattery();
    readProfiles();
}

// --- battery ------------------------------------------------------------

QString PowerSettings::stateName(uint s)
{
    switch (s) {
    case 1:
    case 5: // pending charge
        return QStringLiteral("charging");
    case 2:
    case 3: // empty
    case 6: // pending discharge
        return QStringLiteral("discharging");
    case 4:
        return QStringLiteral("full");
    default:
        return QStringLiteral("not-charging");
    }
}

bool PowerSettings::isVirtualMachine(const QString &cpuinfo)
{
    for (const QString &line : cpuinfo.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("flags")) && line.split(QLatin1Char(' ')).contains(QLatin1String("hypervisor")))
            return true;
    }
    return false;
}

void PowerSettings::readBattery()
{
    QDBusMessage all = QDBusMessage::createMethodCall(upower, displayDevice, properties, QStringLiteral("GetAll"));
    all << deviceInterface;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(all, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *call;
        const QVariantMap p = reply.isError() ? QVariantMap() : reply.value();
        // type 2 is a battery; a desktop's display device is empty
        m_hasBattery = p.value(QStringLiteral("IsPresent")).toBool() && p.value(QStringLiteral("Type")).toUInt() == 2;
        m_level = qRound(p.value(QStringLiteral("Percentage")).toDouble());
        m_state = stateName(p.value(QStringLiteral("State")).toUInt());
        m_timeToEmpty = p.value(QStringLiteral("TimeToEmpty")).toLongLong();
        m_timeToFull = p.value(QStringLiteral("TimeToFull")).toLongLong();
        Q_EMIT batteryChanged();
        if (m_hasBattery && m_batteryPath.isEmpty())
            readHistory();
    });

    QDBusMessage daemon = QDBusMessage::createMethodCall(upower, upowerPath, properties, QStringLiteral("GetAll"));
    daemon << upower;
    auto *d = new QDBusPendingCallWatcher(m_bus.asyncCall(daemon, 5000), this);
    connect(d, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *call;
        if (reply.isError())
            return;
        m_onBattery = reply.value().value(QStringLiteral("OnBattery")).toBool();
        m_lidPresent = reply.value().value(QStringLiteral("LidIsPresent")).toBool();
        Q_EMIT batteryChanged();
    });

    QDBusMessage hibernate = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                            QStringLiteral("/org/freedesktop/login1"),
                                                            QStringLiteral("org.freedesktop.login1.Manager"),
                                                            QStringLiteral("CanHibernate"));
    auto *h = new QDBusPendingCallWatcher(m_bus.asyncCall(hibernate, 5000), this);
    connect(h, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        QDBusPendingReply<QString> reply = *call;
        const bool can = !reply.isError() && reply.value() == QLatin1String("yes");
        if (can != m_canHibernate) {
            m_canHibernate = can;
            Q_EMIT batteryChanged();
        }
    });
}

void PowerSettings::onUPowerChanged(const QString &, const QVariantMap &, const QStringList &)
{
    readBattery();
}

void PowerSettings::readHistory()
{
    // the battery that powers the PC (not a mouse): its history
    QDBusMessage list = QDBusMessage::createMethodCall(upower, upowerPath, upower, QStringLiteral("EnumerateDevices"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(list, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        QDBusPendingReply<QList<QDBusObjectPath>> reply = *call;
        if (reply.isError())
            return;
        QStringList paths;
        for (const QDBusObjectPath &path : reply.value())
            paths.append(path.path());
        findBattery(paths);
    });
}

void PowerSettings::findBattery(QStringList paths)
{
    if (paths.isEmpty())
        return;
    const QString path = paths.takeFirst();
    QDBusMessage get = QDBusMessage::createMethodCall(upower, path, properties, QStringLiteral("GetAll"));
    get << deviceInterface;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(get, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, path, paths](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *call;
        const QVariantMap p = reply.isError() ? QVariantMap() : reply.value();
        if (p.value(QStringLiteral("Type")).toUInt() != 2 || !p.value(QStringLiteral("PowerSupply")).toBool()) {
            findBattery(paths);
            return;
        }
        m_batteryPath = path;
        QDBusMessage history = QDBusMessage::createMethodCall(upower, path, deviceInterface, QStringLiteral("GetHistory"));
        history << QStringLiteral("charge") << uint(24 * 3600) << uint(200);
        auto *h = new QDBusPendingCallWatcher(m_bus.asyncCall(history, 5000), this);
        connect(h, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *c) {
            c->deleteLater();
            QDBusPendingReply<QList<HistoryItem>> items = *c;
            if (items.isError())
                return;
            QList<Sample> samples;
            for (const HistoryItem &i : items.value())
                samples.append({qint64(i.time), i.value, i.state});
            m_history = hourly(samples, now());
            Q_EMIT historyChanged();
        });
    });
}

QVariantList PowerSettings::hourly(const QList<Sample> &samples, const QDateTime &now)
{
    // 24 bars, the last one ending at the start of the next hour
    QDateTime end = now;
    end.setTime(QTime(now.time().hour(), 0));
    end = end.addSecs(3600);
    const qint64 endSecs = end.toSecsSinceEpoch();
    QVariantList bars;
    for (int i = 23; i >= 0; --i) {
        const qint64 from = endSecs - qint64(i + 1) * 3600;
        const qint64 to = from + 3600;
        double sum = 0;
        int count = 0;
        bool charging = false;
        for (const Sample &s : samples) {
            if (s.time < from || s.time >= to || s.level <= 0)
                continue;
            sum += s.level;
            ++count;
            charging = charging || s.state == 1;
        }
        bars.append(QVariantMap{
            {QStringLiteral("start"), from * 1000},
            {QStringLiteral("level"), count > 0 ? qRound(sum / count) : -1},
            {QStringLiteral("charging"), charging},
        });
    }
    return bars;
}

QString PowerSettings::durationText(qint64 seconds)
{
    const qint64 minutes = (seconds + 30) / 60;
    const qint64 h = minutes / 60;
    const qint64 m = minutes % 60;
    if (h > 0 && m > 0)
        return i18nc("@info time left, hours and minutes", "%1 h %2 min", h, m);
    if (h > 0)
        return i18nc("@info time left, hours", "%1 h", h);
    return i18nc("@info time left, minutes", "%1 min", qMax<qint64>(m, 1));
}

// UPower divides what is left by the rate of the moment. Just after the
// cable goes in, or while the battery charges slowly, that rate is close to
// zero and the estimate reads hundreds of hours. Past a day it means nothing.
bool PowerSettings::estimateShown(qint64 seconds)
{
    return seconds > 0 && seconds <= 24 * 3600;
}

QString PowerSettings::batteryText() const
{
    if (!m_hasBattery)
        return QString();
    if (m_state == QLatin1String("full"))
        return i18nc("@info battery", "Fully charged");
    if (m_state == QLatin1String("charging"))
        return estimateShown(m_timeToFull) ? i18nc("@info battery, %1 a time", "Charging, full in %1", durationText(m_timeToFull))
                                : i18nc("@info battery", "Charging");
    if (m_state == QLatin1String("discharging"))
        return estimateShown(m_timeToEmpty) ? i18nc("@info battery, %1 a time", "About %1 left", durationText(m_timeToEmpty))
                                 : i18nc("@info battery", "On battery");
    return i18nc("@info battery that is plugged in but does not charge", "Plugged in, not charging");
}

// --- power mode ---------------------------------------------------------

QStringList PowerSettings::profileNames(const QVariant &value)
{
    QStringList names;
    QList<QVariantMap> maps;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        arg >> maps;
    } else {
        for (const QVariant &v : value.toList())
            maps.append(v.toMap());
    }
    for (const QVariantMap &m : std::as_const(maps)) {
        const QString name = m.value(QStringLiteral("Profile")).toString();
        if (!name.isEmpty() && !names.contains(name))
            names.append(name);
    }
    // the order of the page, whatever the daemon's
    static const QStringList order = {QStringLiteral("power-saver"), QStringLiteral("balanced"), QStringLiteral("performance")};
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return order.indexOf(a) < order.indexOf(b);
    });
    names.erase(std::remove_if(names.begin(), names.end(), [](const QString &n) { return !order.contains(n); }), names.end());
    return names;
}

void PowerSettings::readProfiles()
{
    readProfilesFrom(0);
}

void PowerSettings::readProfilesFrom(int index)
{
    // the new name first, then the one older daemons use
    static const QStringList services = {QStringLiteral("org.freedesktop.UPower.PowerProfiles"), QStringLiteral("net.hadess.PowerProfiles")};
    if (index >= services.size()) {
        const bool had = !m_profiles.isEmpty() || !m_profile.isEmpty();
        m_profiles.clear();
        m_profile.clear();
        m_profilesService.clear();
        if (had)
            Q_EMIT profileChanged();
        return;
    }
    const QString service = services.at(index);
    const QString path = QLatin1Char('/') + QString(service).replace(QLatin1Char('.'), QLatin1Char('/'));
    QDBusMessage all = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("GetAll"));
    all << service;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(all, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, index, service, path](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *call;
        if (reply.isError()) {
            readProfilesFrom(index + 1);
            return;
        }
        const QVariantMap p = reply.value();
        m_profilesService = service;
        m_profilesPath = path;
        m_profilesInterface = service;
        m_profiles = profileNames(p.value(QStringLiteral("Profiles")));
        m_profile = p.value(QStringLiteral("ActiveProfile")).toString();
        Q_EMIT profileChanged();
    });
}

void PowerSettings::onProfilesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != m_profilesInterface && !m_profilesInterface.isEmpty())
        return;
    if (changed.contains(QStringLiteral("ActiveProfile"))) {
        m_profile = changed.value(QStringLiteral("ActiveProfile")).toString();
        Q_EMIT profileChanged();
    }
    if (changed.contains(QStringLiteral("Profiles")))
        readProfiles();
}

void PowerSettings::setProfile(const QString &profile)
{
    if (profile == m_profile || !m_profiles.contains(profile) || m_profilesService.isEmpty())
        return;
    // the mode to go back to when energy saver is turned off
    if (profile == QLatin1String("power-saver") && !m_profile.isEmpty())
        m_own->group(QStringLiteral("EnergySaver")).writeEntry("PreviousProfile", m_profile);
    m_own->sync();
    QDBusMessage set = QDBusMessage::createMethodCall(m_profilesService, m_profilesPath, properties, QStringLiteral("Set"));
    set << m_profilesInterface << QStringLiteral("ActiveProfile") << QVariant::fromValue(QDBusVariant(profile));
    const QString before = m_profile;
    m_profile = profile; // at once; the daemon confirms or the reply puts it back
    Q_EMIT profileChanged();
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(set, 10000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, before](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError()) {
            m_profile = before;
            Q_EMIT profileChanged();
        }
    });
}

void PowerSettings::setEnergySaver(bool on)
{
    if (on == energySaver())
        return;
    if (on) {
        setProfile(QStringLiteral("power-saver"));
        return;
    }
    QString back = m_own->group(QStringLiteral("EnergySaver")).readEntry("PreviousProfile", QStringLiteral("balanced"));
    if (back == QLatin1String("power-saver") || !m_profiles.contains(back))
        back = QStringLiteral("balanced");
    setProfile(back);
}

bool PowerSettings::energySaverAutoAvailable() const
{
    // the session service of the theme that switches the profile at the
    // battery level chosen here
    for (const QString &path : {QStringLiteral("/usr/lib/systemd/user/raye-energy-saver.service"),
                                QStringLiteral("/usr/libexec/raye-energy-saver"), QStringLiteral("/usr/bin/raye-energy-saver")}) {
        if (QFile::exists(m_root + path))
            return true;
    }
    return false;
}

int PowerSettings::energySaverAt() const
{
    const int at = m_own->group(QStringLiteral("EnergySaver")).readEntry("TurnOnAt", 20);
    return std::clamp(at, 0, 100);
}

void PowerSettings::setEnergySaverAt(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == energySaverAt())
        return;
    m_own->group(QStringLiteral("EnergySaver")).writeEntry("TurnOnAt", percent, KConfig::Notify);
    m_own->sync();
    Q_EMIT energySaverSettingsChanged();
}

bool PowerSettings::energySaverDims() const
{
    return m_own->group(QStringLiteral("EnergySaver")).readEntry("LowerBrightness", true);
}

void PowerSettings::setEnergySaverDims(bool on)
{
    if (on == energySaverDims())
        return;
    m_own->group(QStringLiteral("EnergySaver")).writeEntry("LowerBrightness", on, KConfig::Notify);
    m_own->sync();
    Q_EMIT energySaverSettingsChanged();
}

// --- screen, sleep, lid and power button --------------------------------

int PowerSettings::screenOff(const QString &source) const
{
    const KConfigGroup g = m_config->group(source).group(QStringLiteral("Display"));
    if (!g.readEntry("TurnOffDisplayWhenIdle", true))
        return 0;
    return qMax(1, g.readEntry("TurnOffDisplayIdleTimeoutSec", defaultScreenOff(source)));
}

void PowerSettings::setScreenOff(const QString &source, int seconds)
{
    seconds = qMax(0, seconds);
    if (seconds == screenOff(source))
        return;
    KConfigGroup g = m_config->group(source).group(QStringLiteral("Display"));
    if (seconds == 0) {
        g.writeEntry("TurnOffDisplayWhenIdle", false, KConfig::Notify);
    } else {
        g.writeEntry("TurnOffDisplayWhenIdle", true, KConfig::Notify);
        g.writeEntry("TurnOffDisplayIdleTimeoutSec", seconds, KConfig::Notify);
        // the screen dims before it goes dark, never after
        const int dim = g.readEntry("DimDisplayIdleTimeoutSec", source == QLatin1String("AC") ? 300 : 120);
        if (g.readEntry("DimDisplayWhenIdle", true) && dim >= seconds)
            g.writeEntry("DimDisplayIdleTimeoutSec", qMax(15, seconds / 2), KConfig::Notify);
    }
    // Windows has two columns: a low battery follows "on battery"
    if (source == QLatin1String("Battery")) {
        KConfigGroup low = m_config->group(QStringLiteral("LowBattery")).group(QStringLiteral("Display"));
        const int lowNow = low.readEntry("TurnOffDisplayWhenIdle", true) ? low.readEntry("TurnOffDisplayIdleTimeoutSec", 120) : 0;
        // only ever sooner than on battery
        if (seconds > 0 && (lowNow == 0 || lowNow > seconds)) {
            low.writeEntry("TurnOffDisplayWhenIdle", true, KConfig::Notify);
            low.writeEntry("TurnOffDisplayIdleTimeoutSec", seconds, KConfig::Notify);
            if (low.readEntry("DimDisplayIdleTimeoutSec", 60) >= seconds)
                low.writeEntry("DimDisplayIdleTimeoutSec", qMax(15, seconds / 2), KConfig::Notify);
        }
    }
    m_config->sync();
    notifyDaemon();
    Q_EMIT timeoutsChanged();
}

int PowerSettings::sleepAfter(const QString &source) const
{
    const KConfigGroup g = m_config->group(source).group(QStringLiteral("SuspendAndShutdown"));
    const uint fallback = m_isVM ? uint(NoAction) : uint(Sleep);
    if (g.readEntry("AutoSuspendAction", fallback) == uint(NoAction))
        return 0;
    return qMax(1, g.readEntry("AutoSuspendIdleTimeoutSec", defaultSleep(source)));
}

void PowerSettings::setSleepAfter(const QString &source, int seconds)
{
    seconds = qMax(0, seconds);
    if (seconds == sleepAfter(source))
        return;
    KConfigGroup g = m_config->group(source).group(QStringLiteral("SuspendAndShutdown"));
    if (seconds == 0) {
        g.writeEntry("AutoSuspendAction", uint(NoAction), KConfig::Notify);
    } else {
        // keep hibernate when that is what the user chose elsewhere
        const uint current = g.readEntry("AutoSuspendAction", uint(m_isVM ? NoAction : Sleep));
        g.writeEntry("AutoSuspendAction", current == uint(NoAction) ? uint(Sleep) : current, KConfig::Notify);
        g.writeEntry("AutoSuspendIdleTimeoutSec", seconds, KConfig::Notify);
    }
    m_config->sync();
    notifyDaemon();
    Q_EMIT timeoutsChanged();
}

int PowerSettings::action(const QString &source, const char *key) const
{
    const KConfigGroup g = m_config->group(source).group(QStringLiteral("SuspendAndShutdown"));
    uint fallback = uint(Ask);
    if (qstrcmp(key, "LidAction") == 0)
        fallback = m_isVM ? uint(NoAction) : uint(Sleep);
    return int(g.readEntry(key, fallback));
}

void PowerSettings::setAction(const QString &source, const char *key, int value)
{
    static const QList<int> known = {NoAction, Sleep, Hibernate, Shutdown, Ask, Lock, ScreenOff};
    if (!known.contains(value) || value == action(source, key))
        return;
    m_config->group(source).group(QStringLiteral("SuspendAndShutdown")).writeEntry(key, uint(value), KConfig::Notify);
    // the lid and the button do the same on a low battery as on battery
    if (source == QLatin1String("Battery"))
        m_config->group(QStringLiteral("LowBattery")).group(QStringLiteral("SuspendAndShutdown")).writeEntry(key, uint(value), KConfig::Notify);
    m_config->sync();
    notifyDaemon();
    Q_EMIT timeoutsChanged();
}

QVariantList PowerSettings::timeoutChoices()
{
    QVariantList list;
    auto add = [&list](int seconds, const QString &text) {
        list.append(QVariantMap{{QStringLiteral("value"), seconds}, {QStringLiteral("text"), text}});
    };
    for (int m : {1, 2, 3, 5, 10, 15, 20, 25, 30, 45})
        add(m * 60, i18ncp("@item:inlistbox time before the screen turns off or the PC sleeps", "%1 minute", "%1 minutes", m));
    for (int h : {1, 2, 3, 4, 5})
        add(h * 3600, i18ncp("@item:inlistbox time before the screen turns off or the PC sleeps", "%1 hour", "%1 hours", h));
    add(0, i18nc("@item:inlistbox the screen never turns off, the PC never sleeps", "Never"));
    return list;
}

QVariantList PowerSettings::actionChoices(bool lid, int current) const
{
    QVariantList list;
    auto add = [&list](int value, const QString &text) {
        list.append(QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}});
    };
    add(NoAction, i18nc("@item:inlistbox lid or power button", "Do nothing"));
    add(Sleep, i18nc("@item:inlistbox lid or power button", "Sleep"));
    if (m_canHibernate || current == Hibernate)
        add(Hibernate, i18nc("@item:inlistbox lid or power button", "Hibernate"));
    add(Shutdown, i18nc("@item:inlistbox lid or power button", "Shut down"));
    add(ScreenOff, i18nc("@item:inlistbox lid or power button", "Turn off the display"));
    if (!lid || current == Ask)
        add(Ask, i18nc("@item:inlistbox power button", "Ask me what to do"));
    if (current == Lock)
        add(Lock, i18nc("@item:inlistbox lid or power button", "Lock"));
    return list;
}

void PowerSettings::notifyDaemon()
{
    // the power daemon reads its file again when asked
    QDBusMessage refresh = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Solid.PowerManagement"),
                                                          QStringLiteral("/org/kde/Solid/PowerManagement"),
                                                          QStringLiteral("org.kde.Solid.PowerManagement"),
                                                          QStringLiteral("refreshStatus"));
    QDBusConnection::sessionBus().asyncCall(refresh);
}

void PowerSettings::openMoreOptions() const
{
    const QString program = kcmshell();
    if (!program.isEmpty())
        QProcess::startDetached(program, {QStringLiteral("kcm_powerdevilprofilesconfig")});
}

void PowerSettings::reload()
{
    m_config->reparseConfiguration();
    m_own->reparseConfiguration();
    readBattery();
    readProfiles();
    Q_EMIT timeoutsChanged();
    Q_EMIT energySaverSettingsChanged();
}
