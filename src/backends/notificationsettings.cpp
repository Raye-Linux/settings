// SPDX-License-Identifier: GPL-3.0-or-later
#include "notificationsettings.h"

#include <KConfigGroup>
#include <KConfigWatcher>
#include <KDesktopFile>
#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

namespace {

const QString dndGroup = QStringLiteral("DoNotDisturb");
const QString appsGroup = QStringLiteral("Applications");

KConfigGroup appGroup(const KSharedConfigPtr &config, const QString &id)
{
    return config->group(appsGroup).group(id);
}

// the desktop entry of an app, when it is one a user sees
QString desktopFileOf(const QString &id)
{
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id + QStringLiteral(".desktop"));
}

// KDE's own program, never our relay (which would send the module back
// to Raye Settings): the one the package moved aside, else the one on PATH
QString kcmshell()
{
    const QString kde = QStandardPaths::findExecutable(QStringLiteral("kcmshell6.kde"));
    return kde.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("kcmshell6")) : kde;
}

} // namespace

NotificationSettings::NotificationSettings(QObject *parent)
    : QObject(parent)
    , m_notify(KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc")))
    , m_shell(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
    , m_own(KSharedConfig::openConfig(QStringLiteral("raye-settingsrc")))
    , m_expiry(new QTimer(this))
{
    // changes made elsewhere (the notification centre, the shell)
    m_watchers = {KConfigWatcher::create(m_notify), KConfigWatcher::create(m_shell)};
    for (const auto &w : m_watchers) {
        connect(w.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
            if (group.name() == appsGroup || group.parent().name() == appsGroup)
                loadApps();
            scheduleExpiry();
            Q_EMIT changed();
        });
    }
    m_expiry->setSingleShot(true);
    connect(m_expiry, &QTimer::timeout, this, [this]() {
        scheduleExpiry();
        Q_EMIT changed();
    });
    loadApps();
    scheduleExpiry();
}

NotificationSettings::~NotificationSettings() = default;

void NotificationSettings::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    scheduleExpiry();
    Q_EMIT changed();
}

QDateTime NotificationSettings::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

QDateTime NotificationSettings::offMarker()
{
    return QDateTime(QDate(9999, 12, 31), QTime(0, 0));
}

bool NotificationSettings::isAppId(const QString &id)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.+-]{0,254}$"));
    return valid.match(id).hasMatch() && !id.contains(QStringLiteral(".."));
}

void NotificationSettings::reload()
{
    m_notify->reparseConfiguration();
    m_shell->reparseConfiguration();
    m_own->reparseConfiguration();
    loadApps();
    scheduleExpiry();
    Q_EMIT changed();
}

// The next moment something turns off by itself (do not disturb, a
// focus session): the page is told then.
void NotificationSettings::scheduleExpiry()
{
    const QDateTime t = now();
    qint64 next = -1;
    for (const QDateTime &end : {rawUntil(), focusUntil()}) {
        if (end.isValid() && end > t && end.date().year() < 9000) {
            const qint64 ms = t.msecsTo(end);
            if (next < 0 || ms < next)
                next = ms;
        }
    }
    if (next < 0 || next > 24LL * 3600 * 1000)
        m_expiry->stop();
    else
        m_expiry->start(int(next) + 500);
}

// --- notifications ------------------------------------------------------

QDateTime NotificationSettings::rawUntil() const
{
    return m_notify->group(dndGroup).readEntry("Until", QDateTime());
}

void NotificationSettings::writeUntil(const QDateTime &until)
{
    KConfigGroup g = m_notify->group(dndGroup);
    if (until.isValid())
        g.writeEntry("Until", until, KConfig::Notify);
    else
        g.deleteEntry("Until", KConfig::Notify);
    m_notify->sync();
    scheduleExpiry();
    Q_EMIT changed();
}

void NotificationSettings::writeDnd(const char *key, bool value)
{
    KConfigGroup g = m_notify->group(dndGroup);
    if (g.readEntry(key, !value) == value && g.hasKey(key))
        return;
    g.writeEntry(key, value, KConfig::Notify);
    m_notify->sync();
    Q_EMIT changed();
}

bool NotificationSettings::enabled() const
{
    const QDateTime until = rawUntil();
    return !(until.isValid() && until.date().year() >= 9000);
}

void NotificationSettings::setEnabled(bool on)
{
    if (on == enabled())
        return;
    writeUntil(on ? QDateTime() : offMarker());
}

bool NotificationSettings::doNotDisturb() const
{
    const QDateTime until = rawUntil();
    return until.isValid() && until > now();
}

void NotificationSettings::setDoNotDisturb(bool on)
{
    // with notifications off, the master switch decides
    if (!enabled() || on == doNotDisturb())
        return;
    // the desktop's "until I turn it off" is a year from now
    writeUntil(on ? now().addYears(1) : QDateTime());
}

QDateTime NotificationSettings::doNotDisturbUntil() const
{
    if (!doNotDisturb())
        return QDateTime();
    const QDateTime until = rawUntil();
    // more than a month away is "until I turn it off"
    return until > now().addDays(31) ? QDateTime() : until;
}

bool NotificationSettings::whenFullscreen() const
{
    return m_notify->group(dndGroup).readEntry("WhenFullscreen", true);
}

void NotificationSettings::setWhenFullscreen(bool on)
{
    writeDnd("WhenFullscreen", on);
}

bool NotificationSettings::whenScreensMirrored() const
{
    return m_notify->group(dndGroup).readEntry("WhenScreensMirrored", true);
}

void NotificationSettings::setWhenScreensMirrored(bool on)
{
    writeDnd("WhenScreensMirrored", on);
}

bool NotificationSettings::whenScreenSharing() const
{
    return m_notify->group(dndGroup).readEntry("WhenScreenSharing", true);
}

void NotificationSettings::setWhenScreenSharing(bool on)
{
    writeDnd("WhenScreenSharing", on);
}

bool NotificationSettings::sounds() const
{
    return !m_notify->group(dndGroup).readEntry("NotificationSoundsMuted", false);
}

void NotificationSettings::setSounds(bool on)
{
    writeDnd("NotificationSoundsMuted", !on);
}

bool NotificationSettings::lockScreenAvailable() const
{
    // the shell lists every key it reads in its defaults file under
    // /etc/xdg; the user's own file does not count
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation)) {
        if (dir == QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
            continue;
        KConfig shell(dir + QStringLiteral("/rayeshellrc"), KConfig::SimpleConfig);
        if (shell.group(QStringLiteral("Notifications")).hasKey("LockBadges"))
            return true;
    }
    return false;
}

bool NotificationSettings::lockScreen() const
{
    return m_shell->group(QStringLiteral("Notifications")).readEntry("LockBadges", true);
}

void NotificationSettings::setLockScreen(bool on)
{
    if (on == lockScreen())
        return;
    m_shell->group(QStringLiteral("Notifications")).writeEntry("LockBadges", on, KConfig::Notify);
    m_shell->sync();
    Q_EMIT changed();
}

// --- apps ---------------------------------------------------------------

void NotificationSettings::loadApps()
{
    QSet<QString> ids;
    // apps the server has seen, or that have defaults
    for (const QString &id : m_notify->group(appsGroup).groupList())
        ids.insert(id);
    // apps that say they send notifications, and apps with event sounds
    // of their own (a notifyrc naming the app): the files are read once
    if (!m_scanned) {
        m_scanned = true;
        const QStringList appDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        for (const QString &dir : appDirs) {
            QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files);
            while (it.hasNext()) {
                const QString path = it.next();
                KDesktopFile file(path);
                if (file.desktopGroup().readEntry("X-GNOME-UsesNotifications", false))
                    m_senders.insert(it.fileInfo().completeBaseName());
            }
        }
        const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        for (const QString &dir : dataDirs) {
            QDirIterator it(dir + QStringLiteral("/knotifications6"), {QStringLiteral("*.notifyrc")}, QDir::Files);
            while (it.hasNext()) {
                KConfig rc(it.next(), KConfig::SimpleConfig);
                const QString entry = rc.group(QStringLiteral("Global")).readEntry("DesktopEntry");
                if (!entry.isEmpty())
                    m_senders.insert(entry);
            }
        }
    }
    ids.unite(m_senders);

    QVariantList apps;
    for (const QString &id : std::as_const(ids)) {
        if (!isAppId(id))
            continue;
        const QString path = desktopFileOf(id);
        if (path.isEmpty())
            continue;
        KDesktopFile file(path);
        if (file.noDisplay() || file.desktopGroup().readEntry("Hidden", false))
            continue;
        const QString name = file.readName();
        if (name.isEmpty())
            continue;
        const KConfigGroup g = appGroup(m_notify, id);
        apps.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("icon"), file.readIcon()},
            {QStringLiteral("enabled"), g.readEntry("ShowPopups", true) || g.readEntry("ShowInHistory", true)},
        });
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(apps.begin(), apps.end(), [&collator](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(),
                                b.toMap().value(QStringLiteral("name")).toString())
            < 0;
    });
    if (apps != m_apps) {
        m_apps = apps;
        Q_EMIT appsChanged();
    }
}

QVariantMap NotificationSettings::app(const QString &id) const
{
    if (!isAppId(id))
        return {};
    QVariantMap result;
    for (const QVariant &v : m_apps) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id)
            result = v.toMap();
    }
    if (result.isEmpty())
        result = {{QStringLiteral("id"), id}, {QStringLiteral("name"), id}, {QStringLiteral("icon"), QString()}};
    const KConfigGroup g = appGroup(m_notify, id);
    const bool popups = g.readEntry("ShowPopups", true);
    const bool history = g.readEntry("ShowInHistory", true);
    result.insert(QStringLiteral("popups"), popups);
    result.insert(QStringLiteral("history"), history);
    result.insert(QStringLiteral("enabled"), popups || history);
    result.insert(QStringLiteral("dnd"), g.readEntry("ShowPopupsInDndMode", false));
    result.insert(QStringLiteral("badges"), g.readEntry("ShowBadges", true));
    return result;
}

void NotificationSettings::setAppOption(const QString &id, const QString &option, bool on)
{
    if (!isAppId(id))
        return;
    QStringList keys;
    if (option == QLatin1String("enabled"))
        keys = {QStringLiteral("ShowPopups"), QStringLiteral("ShowInHistory")};
    else if (option == QLatin1String("popups"))
        keys = {QStringLiteral("ShowPopups")};
    else if (option == QLatin1String("history"))
        keys = {QStringLiteral("ShowInHistory")};
    else if (option == QLatin1String("dnd"))
        keys = {QStringLiteral("ShowPopupsInDndMode")};
    else if (option == QLatin1String("badges"))
        keys = {QStringLiteral("ShowBadges")};
    else
        return;
    KConfigGroup g = appGroup(m_notify, id);
    for (const QString &key : std::as_const(keys))
        g.writeEntry(key, on, KConfig::Notify);
    m_notify->sync();
    loadApps();
    Q_EMIT changed();
}

void NotificationSettings::openMoreOptions(const QString &id) const
{
    const QString program = kcmshell();
    if (program.isEmpty())
        return;
    QStringList args{QStringLiteral("kcm_notifications")};
    if (isAppId(id))
        args << QStringLiteral("--args") << QStringLiteral("--desktop-entry ") + id;
    QProcess::startDetached(program, args);
}

// --- focus --------------------------------------------------------------

QDateTime NotificationSettings::focusUntil() const
{
    const QString text = m_shell->group(QStringLiteral("Focus")).readEntry("Until", QString()).trimmed();
    if (text.isEmpty())
        return QDateTime();
    const QDateTime until = QDateTime::fromString(text, Qt::ISODate);
    return until.isValid() && until > now() ? until : QDateTime();
}

bool NotificationSettings::focusActive() const
{
    return focusUntil().isValid();
}

int NotificationSettings::focusMinutes() const
{
    const int m = m_own->group(QStringLiteral("Focus")).readEntry("Minutes", int(defaultFocusMinutes));
    return std::clamp(m, int(minFocusMinutes), int(maxFocusMinutes));
}

void NotificationSettings::setFocusMinutes(int minutes)
{
    const int m = std::clamp(minutes, int(minFocusMinutes), int(maxFocusMinutes));
    if (m == focusMinutes() && m_own->group(QStringLiteral("Focus")).hasKey("Minutes"))
        return;
    m_own->group(QStringLiteral("Focus")).writeEntry("Minutes", m);
    m_own->sync();
    Q_EMIT changed();
}

bool NotificationSettings::focusDoNotDisturb() const
{
    return m_own->group(QStringLiteral("Focus")).readEntry("DoNotDisturb", true);
}

void NotificationSettings::setFocusDoNotDisturb(bool on)
{
    if (on == focusDoNotDisturb())
        return;
    m_own->group(QStringLiteral("Focus")).writeEntry("DoNotDisturb", on);
    m_own->sync();
    Q_EMIT changed();
}

void NotificationSettings::startFocus()
{
    // whole seconds: the shell reads the time back from text
    QDateTime end = now().addSecs(qint64(focusMinutes()) * 60);
    end.setTime(QTime(end.time().hour(), end.time().minute(), end.time().second()));
    m_shell->group(QStringLiteral("Focus")).writeEntry("Until", end.toString(Qt::ISODate), KConfig::Notify);
    m_shell->sync();
    // do not disturb for the session, unless it is already on for longer
    if (focusDoNotDisturb() && enabled() && !(doNotDisturb() && rawUntil() > end))
        writeUntil(end);
    scheduleExpiry();
    Q_EMIT changed();
}

void NotificationSettings::stopFocus()
{
    const QDateTime end = focusUntil();
    m_shell->group(QStringLiteral("Focus")).writeEntry("Until", QString(), KConfig::Notify);
    m_shell->sync();
    // do not disturb that the session started ends with it
    if (end.isValid() && rawUntil() == end)
        writeUntil(QDateTime());
    scheduleExpiry();
    Q_EMIT changed();
}
