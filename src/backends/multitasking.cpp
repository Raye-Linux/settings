// SPDX-License-Identifier: GPL-3.0-or-later
#include "multitasking.h"

#include <KConfig>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KLocalizedString>
#include <QCollator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

namespace {

const QString kwinWindows = QStringLiteral("Windows");
const QString kwinSnap = QStringLiteral("Script-org.raye.snap");
const QString kwinTabBox = QStringLiteral("TabBox");
const QString shellTaskbar = QStringLiteral("Taskbar");

// A call that failed before it left (no session bus) is finished before
// anyone listens: say so once the caller is connected.
void finishLate(QDBusPendingCallWatcher *watcher)
{
    if (watcher->isFinished())
        QMetaObject::invokeMethod(watcher, [watcher] { Q_EMIT watcher->finished(watcher); }, Qt::QueuedConnection);
}

void writeNotify(const KSharedConfigPtr &config, const QString &group, const QString &key, const QVariant &value)
{
    KConfigGroup g = config->group(group);
    g.writeEntry(key, value, KConfig::Notify);
    config->sync();
}

} // namespace

// ---- Multitasking -----------------------------------------------------

MultitaskingSettings::MultitaskingSettings(QObject *parent)
    : QObject(parent)
    , m_kwin(KSharedConfig::openConfig(QStringLiteral("kwinrc")))
    , m_shell(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
{
    reload();
}

void MultitaskingSettings::reload()
{
    m_kwin->reparseConfiguration();
    m_shell->reparseConfiguration();
    Q_EMIT changed();
}

bool MultitaskingSettings::snapWindows() const
{
    return m_kwin->group(kwinWindows).readEntry("ElectricBorderTiling", true);
}

void MultitaskingSettings::setSnapWindows(bool on)
{
    if (on == snapWindows())
        return;
    // drag to an edge to fill half, and to the top to fill the screen
    KConfigGroup g = m_kwin->group(kwinWindows);
    g.writeEntry("ElectricBorderMaximize", on, KConfig::Notify);
    writeKwin(kwinWindows, QStringLiteral("ElectricBorderTiling"), on);
}

bool MultitaskingSettings::snapLayoutsOnHover() const
{
    return m_kwin->group(kwinSnap).readEntry("SnapLayoutsOnHover", true);
}

void MultitaskingSettings::setSnapLayoutsOnHover(bool on)
{
    if (on != snapLayoutsOnHover())
        writeKwin(kwinSnap, QStringLiteral("SnapLayoutsOnHover"), on);
}

bool MultitaskingSettings::snapBar() const
{
    return m_kwin->group(kwinSnap).readEntry("SnapBar", true);
}

void MultitaskingSettings::setSnapBar(bool on)
{
    if (on != snapBar())
        writeKwin(kwinSnap, QStringLiteral("SnapBar"), on);
}

bool MultitaskingSettings::snapAssist() const
{
    return m_kwin->group(kwinSnap).readEntry("SnapAssist", true);
}

void MultitaskingSettings::setSnapAssist(bool on)
{
    if (on != snapAssist())
        writeKwin(kwinSnap, QStringLiteral("SnapAssist"), on);
}

bool MultitaskingSettings::altTabAllDesktops() const
{
    // 0 every desktop, 1 the current one (the window manager's default)
    return m_kwin->group(kwinTabBox).readEntry("DesktopMode", 1) == 0;
}

void MultitaskingSettings::setAltTabAllDesktops(bool on)
{
    if (on != altTabAllDesktops())
        writeKwin(kwinTabBox, QStringLiteral("DesktopMode"), on ? 0 : 1);
}

bool MultitaskingSettings::altTabShowsDesktop() const
{
    return m_kwin->group(kwinTabBox).readEntry("ShowDesktopMode", 0) == 1;
}

void MultitaskingSettings::setAltTabShowsDesktop(bool on)
{
    if (on != altTabShowsDesktop())
        writeKwin(kwinTabBox, QStringLiteral("ShowDesktopMode"), on ? 1 : 0);
}

bool MultitaskingSettings::taskbarAllDesktops() const
{
    return m_shell->group(shellTaskbar).readEntry("AllDesktops", false);
}

void MultitaskingSettings::setTaskbarAllDesktops(bool on)
{
    if (on == taskbarAllDesktops())
        return;
    // the taskbar watches the file and follows at once
    writeNotify(m_shell, shellTaskbar, QStringLiteral("AllDesktops"), on);
    Q_EMIT changed();
}

bool MultitaskingSettings::taskbarAllDesktopsAvailable() const
{
    return systemDefines(QStringLiteral("rayeshellrc"), shellTaskbar, QStringLiteral("AllDesktops"));
}

bool MultitaskingSettings::systemDefines(const QString &file, const QString &group, const QString &key)
{
    const QString mine = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    for (const QString &dir : dirs) {
        if (dir == mine)
            continue;
        const QString path = dir + QLatin1Char('/') + file;
        if (!QFileInfo::exists(path))
            continue;
        KConfig config(path, KConfig::SimpleConfig);
        if (config.group(group).hasKey(key))
            return true;
    }
    return false;
}

void MultitaskingSettings::writeKwin(const QString &group, const QString &key, const QVariant &value)
{
    writeNotify(m_kwin, group, key, value);
    // the window manager reads its file again on this signal
    QDBusConnection::sessionBus().send(
        QDBusMessage::createSignal(QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reloadConfig")));
    Q_EMIT changed();
}

void MultitaskingSettings::openDesktops()
{
    // Settings' relay hands module ids it has a page for back to the
    // window; RAYE_SETTINGS_KCMSHELL (set by main) lets this one through
    QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_kwin_virtualdesktops")});
}

// ---- Advanced ---------------------------------------------------------

AdvancedSettings::AdvancedSettings(QObject *parent)
    : QObject(parent)
    , m_files(KSharedConfig::openConfig(QStringLiteral("dolphinrc")))
    , m_shell(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
    , m_globals(KSharedConfig::openConfig(QStringLiteral("kdeglobals")))
{
    reload();
}

void AdvancedSettings::reload()
{
    m_files->reparseConfiguration();
    m_shell->reparseConfiguration();
    m_globals->reparseConfiguration();
    m_terminals = findTerminals();
    Q_EMIT changed();
}

bool AdvancedSettings::showExtensions() const
{
    return m_files->group(QStringLiteral("Raye")).readEntry("ShowExtensions", false);
}

void AdvancedSettings::setShowExtensions(bool on)
{
    if (on == showExtensions())
        return;
    // Files watches its file: open windows follow at once
    writeNotify(m_files, QStringLiteral("Raye"), QStringLiteral("ShowExtensions"), on);
    Q_EMIT changed();
}

bool AdvancedSettings::showHidden() const
{
    return m_files->group(QStringLiteral("Raye")).readEntry("ShowHidden", false);
}

void AdvancedSettings::setShowHidden(bool on)
{
    if (on == showHidden())
        return;
    writeNotify(m_files, QStringLiteral("Raye"), QStringLiteral("ShowHidden"), on);
    Q_EMIT changed();
}

bool AdvancedSettings::endTask() const
{
    return m_shell->group(shellTaskbar).readEntry("EndTask", false);
}

void AdvancedSettings::setEndTask(bool on)
{
    if (on == endTask())
        return;
    writeNotify(m_shell, shellTaskbar, QStringLiteral("EndTask"), on);
    Q_EMIT changed();
}

QVariantList AdvancedSettings::terminals() const
{
    QVariantList list;
    for (const Terminal &t : m_terminals) {
        list.append(QVariantMap{{QStringLiteral("id"), t.id},
                                {QStringLiteral("name"), t.name},
                                {QStringLiteral("icon"), t.icon}});
    }
    return list;
}

QString AdvancedSettings::terminal() const
{
    const QString chosen = m_globals->group(QStringLiteral("General")).readEntry("TerminalService");
    QString fallback;
    for (const Terminal &t : m_terminals) {
        if (t.id == chosen)
            return chosen;
        if (t.id == QLatin1String("org.kde.konsole.desktop"))
            fallback = t.id;
    }
    if (fallback.isEmpty() && !m_terminals.isEmpty())
        fallback = m_terminals.first().id;
    return fallback;
}

void AdvancedSettings::setTerminal(const QString &id)
{
    for (const Terminal &t : std::as_const(m_terminals)) {
        if (t.id != id)
            continue;
        if (id == m_globals->group(QStringLiteral("General")).readEntry("TerminalService"))
            return;
        // what apps that open a terminal read (the KDE libraries, Files)
        KConfigGroup g = m_globals->group(QStringLiteral("General"));
        g.writeEntry("TerminalApplication", commandOf(t.exec), KConfig::Notify);
        g.writeEntry("TerminalService", t.id, KConfig::Notify);
        m_globals->sync();
        Q_EMIT changed();
        return;
    }
}

QString AdvancedSettings::commandOf(const QString &exec)
{
    static const QRegularExpression fieldCode(QStringLiteral("\\s*(?<!%)%[fFuUdDnNickvm](?=\\s|$)"));
    QString command = exec;
    command.remove(fieldCode);
    command.replace(QLatin1String("%%"), QLatin1String("%"));
    return command.trimmed();
}

QList<AdvancedSettings::Terminal> AdvancedSettings::findTerminals()
{
    QList<Terminal> found;
    QSet<QString> seen;
    // the user's own folder comes first: their entry hides the system's
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("applications"),
                                                       QStandardPaths::LocateDirectory);
    for (const QString &dir : dirs) {
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = QDir(dir).relativeFilePath(path).replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seen.contains(id))
                continue;
            seen.insert(id);
            KDesktopFile file(path);
            const KConfigGroup g = file.desktopGroup();
            if (file.readType() != QLatin1String("Application") || g.readEntry("Hidden", false) || file.noDisplay())
                continue;
            if (!g.readXdgListEntry("Categories").contains(QLatin1String("TerminalEmulator")))
                continue;
            const QString exec = g.readEntry("Exec");
            if (exec.isEmpty() || !file.tryExec())
                continue;
            found.append({id, file.readName(), file.readIcon(), exec});
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(found.begin(), found.end(), [&collator](const Terminal &a, const Terminal &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    return found;
}

// ---- Remote Desktop ---------------------------------------------------

const QString RemoteDesktop::unit = QStringLiteral("app-org.kde.krdpserver.service");

namespace {
const QString systemdService = QStringLiteral("org.freedesktop.systemd1");
const QString systemdPath = QStringLiteral("/org/freedesktop/systemd1");
const QString systemdManager = QStringLiteral("org.freedesktop.systemd1.Manager");
}

RemoteDesktop::RemoteDesktop(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("krdpserverrc")))
{
    refresh();
}

bool RemoteDesktop::hasUsers() const
{
    const KConfigGroup g = m_config->group(QStringLiteral("General"));
    return !g.readEntry("Users", QStringList()).isEmpty() || g.readEntry("SystemUserEnabled", false);
}

int RemoteDesktop::port() const
{
    const int port = m_config->group(QStringLiteral("General")).readEntry("ListenPort", 3389);
    return port > 0 && port < 65536 ? port : 3389;
}

void RemoteDesktop::readState()
{
    m_config->reparseConfiguration();
    m_installed = !QStandardPaths::findExecutable(QStringLiteral("krdpserver")).isEmpty();
    m_addresses = formatAddresses(localAddresses(), port());
}

void RemoteDesktop::refresh()
{
    readState();
    Q_EMIT changed();
    // is the service running? A unit that is not loaded is not running
    QDBusMessage get = QDBusMessage::createMethodCall(systemdService, systemdPath, systemdManager, QStringLiteral("GetUnit"));
    get << unit;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(get), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            if (m_running) {
                m_running = false;
                Q_EMIT changed();
            }
            return;
        }
        QDBusMessage prop = QDBusMessage::createMethodCall(systemdService, reply.value().path(),
                                                           QStringLiteral("org.freedesktop.DBus.Properties"),
                                                           QStringLiteral("Get"));
        prop << QStringLiteral("org.freedesktop.systemd1.Unit") << QStringLiteral("ActiveState");
        auto *state = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(prop), this);
        connect(state, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *s) {
            s->deleteLater();
            const QDBusPendingReply<QDBusVariant> value = *s;
            const QString active = value.isError() ? QString() : value.value().variant().toString();
            const bool running = active == QLatin1String("active") || active == QLatin1String("activating")
                || active == QLatin1String("reloading");
            if (running != m_running) {
                m_running = running;
                Q_EMIT changed();
            }
        });
        finishLate(state);
    });
    finishLate(watcher);
}

void RemoteDesktop::setEnabled(bool on)
{
    if (m_busy || (on && (!m_installed || !hasUsers())))
        return;
    m_busy = true;
    m_error.clear();
    m_pending = 2;
    Q_EMIT changed();
    // on: now and at every sign-in, as Windows' switch does
    KConfigGroup g = m_config->group(QStringLiteral("General"));
    g.writeEntry("Autostart", on, KConfig::Notify);
    m_config->sync();
    if (on) {
        callSystemd(QStringLiteral("EnableUnitFiles"), {QStringList{unit}, false, true});
        callSystemd(QStringLiteral("StartUnit"), {unit, QStringLiteral("replace")});
    } else {
        callSystemd(QStringLiteral("StopUnit"), {unit, QStringLiteral("replace")});
        callSystemd(QStringLiteral("DisableUnitFiles"), {QStringList{unit}, false});
    }
}

void RemoteDesktop::callSystemd(const QString &method, const QVariantList &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(systemdService, systemdPath, systemdManager, method);
    call.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError() && m_error.isEmpty())
            m_error = i18n("Remote Desktop could not be changed. Try again, or restart your PC.");
        if (--m_pending > 0)
            return;
        m_busy = false;
        // the service takes a moment to be up
        QTimer::singleShot(800, this, &RemoteDesktop::refresh);
        Q_EMIT changed();
    });
    finishLate(watcher);
}

void RemoteDesktop::openOptions()
{
    QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_krdpserver")});
}

QStringList RemoteDesktop::formatAddresses(const QStringList &ips, int port)
{
    QStringList list;
    for (const QString &ip : ips) {
        list.append(ip.contains(QLatin1Char(':')) ? QStringLiteral("[%1]:%2").arg(ip).arg(port)
                                                   : QStringLiteral("%1:%2").arg(ip).arg(port));
    }
    return list;
}

QStringList RemoteDesktop::localAddresses()
{
    QStringList v4;
    QStringList v6;
    ifaddrs *all = nullptr;
    if (getifaddrs(&all) != 0)
        return {};
    for (ifaddrs *a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || !(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_RUNNING) || (a->ifa_flags & IFF_LOOPBACK))
            continue;
        // containers and virtual machines on this PC, not the network
        const QString name = QString::fromLocal8Bit(a->ifa_name);
        if (name.startsWith(QLatin1String("docker")) || name.startsWith(QLatin1String("virbr"))
            || name.startsWith(QLatin1String("veth")) || name.startsWith(QLatin1String("br-"))
            || name.startsWith(QLatin1String("lxcbr")))
            continue;
        char text[INET6_ADDRSTRLEN] = {};
        if (a->ifa_addr->sa_family == AF_INET) {
            const auto *in = reinterpret_cast<const sockaddr_in *>(a->ifa_addr);
            const quint32 host = ntohl(in->sin_addr.s_addr);
            if ((host >> 24) == 127 || (host >> 16) == 0xa9fe) // loopback, link-local
                continue;
            if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof text)) {
                const QString ip = QString::fromLatin1(text);
                if (!v4.contains(ip))
                    v4.append(ip);
            }
        } else if (a->ifa_addr->sa_family == AF_INET6) {
            const auto *in6 = reinterpret_cast<const sockaddr_in6 *>(a->ifa_addr);
            if (IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr) || IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr))
                continue;
            if (inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof text)) {
                const QString ip = QString::fromLatin1(text);
                if (!v6.contains(ip))
                    v6.append(ip);
            }
        }
    }
    freeifaddrs(all);
    // the short IPv4 address is the one people type; IPv6 only without it
    return v4.isEmpty() ? v6 : v4;
}
