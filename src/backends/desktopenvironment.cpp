// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopenvironment.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <PackageKit/Daemon>
#include <PackageKit/Transaction>

#include <memory>
#include <unistd.h>

namespace {
constexpr int pkexecDismissed = 126;
// desktop-session: this PC cannot switch right away
constexpr int helperNoLiveSwitch = 6;
// after leave(): the transition gives way to the apps' own questions, then
// the switch is taken back if the session is still here
constexpr int leaveSlowMs = 3000;
constexpr int leaveGiveUpMs = 20000;
const QString plasmaKey = QStringLiteral("plasma");
const QString hyprlandKey = QStringLiteral("hyprland");
const QString settingsId = QStringLiteral("org.raye.settings");
const QString busName = QStringLiteral("org.raye.settings.DesktopSwitch");

bool validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._-]+$"));
    return !id.isEmpty() && re.match(id).hasMatch();
}

QString desktopFilePath(const QString &id)
{
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id + QStringLiteral(".desktop"));
}

QString nameOf(const QString &id)
{
    const QString path = desktopFilePath(id);
    if (path.isEmpty())
        return id;
    const QString name = KDesktopFile(path).readName();
    return name.isEmpty() ? id : name;
}

// every app's (desktop id, StartupWMClass)
QList<QPair<QString, QString>> knownApps()
{
    QList<QPair<QString, QString>> out;
    QStringList seen;
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dirs) {
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QString &file : files) {
            const QString id = file.chopped(8);
            if (seen.contains(id))
                continue;
            seen << id;
            out << qMakePair(id, KDesktopFile(dir + QLatin1Char('/') + file).desktopGroup().readEntry("StartupWMClass", QString()));
        }
    }
    return out;
}

QJsonArray hyprClients()
{
    QProcess p;
    p.start(QStringLiteral("hyprctl"), {QStringLiteral("clients"), QStringLiteral("-j")});
    if (!p.waitForFinished(2000) || p.exitCode() != 0)
        return {};
    return QJsonDocument::fromJson(p.readAllStandardOutput()).array();
}

// the person's graphical session, as logind keeps it (its Display)
QString loginctlDisplay()
{
    QProcess p;
    p.start(QStringLiteral("loginctl"),
            {QStringLiteral("show-user"), QString::number(getuid()), QStringLiteral("-p"), QStringLiteral("Display"),
             QStringLiteral("--value")});
    if (!p.waitForFinished(2000) || p.exitCode() != 0)
        return {};
    return QString::fromUtf8(p.readAllStandardOutput());
}
} // namespace

QString DesktopEnvironment::sessionOrDisplay(const QString &fromEnvironment, const std::function<QString()> &display)
{
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;
    const QString id = display ? display().trimmed() : QString();
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9]+$"));
    return plain.match(id).hasMatch() ? id : QString();
}

DesktopEnvironment::DesktopEnvironment(QObject *parent)
    : DesktopEnvironment(QStringLiteral("/usr/share/wayland-sessions"),
                         {QStringLiteral("pkexec"), QStringLiteral(RAYE_LIBEXECDIR "/desktop-session")},
                         qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
                         sessionOrDisplay(qEnvironmentVariable("XDG_SESSION_ID"), loginctlDisplay),
                         QStringLiteral("raye-settingsrc"), packageKit(), {}, {}, parent)
{
    m_apps = liveApps(this);
    m_leaver = liveLeaver();
}

DesktopEnvironment::DesktopEnvironment(const QString &sessionDir, const QStringList &helper, const QString &currentDesktop,
                                       const QString &sessionId, const QString &config, PackageRunner packages, AppLister apps,
                                       Leaver leaver, QObject *parent)
    : QObject(parent)
    , m_sessionDir(sessionDir)
    , m_helper(helper)
    , m_current(desktopOf(currentDesktop))
    , m_sessionId(sessionId)
    , m_config(KSharedConfig::openConfig(config))
    , m_packages(std::move(packages))
    , m_apps(std::move(apps))
    , m_leaver(std::move(leaver))
{
    m_stillHere.setSingleShot(true);
    refresh();
}

QString DesktopEnvironment::desktopOf(const QString &xdgCurrentDesktop)
{
    const QStringList names = xdgCurrentDesktop.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &name : names) {
        if (name.compare(QLatin1String("KDE"), Qt::CaseInsensitive) == 0)
            return plasmaKey;
        if (name.compare(QLatin1String("Hyprland"), Qt::CaseInsensitive) == 0)
            return hyprlandKey;
    }
    return {};
}

QString DesktopEnvironment::packageOf(const QString &desktop)
{
    if (desktop == plasmaKey)
        return QStringLiteral("plasma-workspace");
    if (desktop == hyprlandKey)
        return QStringLiteral("raye-hyprland");
    return {};
}

QString DesktopEnvironment::sessionFileOf(const QString &desktop)
{
    if (desktop == plasmaKey)
        return QStringLiteral("plasma.desktop");
    if (desktop == hyprlandKey)
        return QStringLiteral("raye-hyprland.desktop");
    return {};
}

QStringList DesktopEnvironment::signOutCommand(const QString &desktop, const QString &sessionId)
{
    if (desktop == plasmaKey)
        return {QStringLiteral("dbus:org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"),
                QStringLiteral("logout")};
    if (desktop == hyprlandKey)
        return {QStringLiteral("hyprctl"), QStringLiteral("dispatch"), QStringLiteral("exit")};
    if (!sessionId.isEmpty())
        return {QStringLiteral("loginctl"), QStringLiteral("terminate-session"), sessionId};
    return {};
}

QString DesktopEnvironment::desktopIdForClass(const QString &windowClass, const QList<QPair<QString, QString>> &apps)
{
    if (windowClass.isEmpty())
        return {};
    // the class is the id itself (Qt and GTK apps set it so)
    for (const auto &app : apps)
        if (app.first == windowClass)
            return app.first;
    for (const auto &app : apps)
        if (!app.second.isEmpty() && app.second.compare(windowClass, Qt::CaseInsensitive) == 0)
            return app.first;
    for (const auto &app : apps)
        if (app.first.compare(windowClass, Qt::CaseInsensitive) == 0)
            return app.first;
    // "dolphin" for org.kde.dolphin
    for (const auto &app : apps)
        if (app.first.section(QLatin1Char('.'), -1).compare(windowClass, Qt::CaseInsensitive) == 0)
            return app.first;
    return {};
}

QString DesktopEnvironment::restoreFile()
{
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/raye/desktop-switch");
}

QByteArray DesktopEnvironment::restoreList(const QString &desktop, qint64 when, const QStringList &ids)
{
    QByteArray out = "# Written by Settings when switching desktops; read once by desktop-restore.\n";
    out += "to=" + desktop.toUtf8() + "\nat=" + QByteArray::number(when) + "\n";
    QStringList written;
    for (const QString &id : ids) {
        if (!validId(id) || id == settingsId || written.contains(id))
            continue;
        written << id;
        out += "app=" + id.toUtf8() + "\n";
    }
    // Settings opens again where the switch was made
    out += "app=" + settingsId.toUtf8() + "\n";
    return out;
}

QByteArray DesktopEnvironment::kwinListScript()
{
    return "var ids = [];\n"
           "var list = workspace.windowList();\n"
           "for (var i = 0; i < list.length; ++i) {\n"
           "    var w = list[i];\n"
           "    var id = w.desktopFileName;\n"
           "    if (w.normalWindow && !w.skipTaskbar && id && ids.indexOf(id) < 0)\n"
           "        ids.push(id);\n"
           "}\n"
           "callDBus(\"org.raye.settings.DesktopSwitch\", \"/DesktopSwitch\", \"org.raye.settings.DesktopSwitch\", \"apps\", ids.join(\"\\n\"));\n";
}

bool DesktopEnvironment::installed(const QString &desktop) const
{
    if (desktop == plasmaKey)
        return m_plasma;
    if (desktop == hyprlandKey)
        return m_hyprland;
    return false;
}

QString DesktopEnvironment::next() const
{
    // what was chosen here counts for the session it was chosen in; a new
    // session is the answer itself (the sign-in screen may have picked)
    const KConfigGroup group = m_config->group(QStringLiteral("Desktop"));
    const QString chosen = group.readEntry("Next", QString());
    if (!chosen.isEmpty() && installed(chosen) && group.readEntry("ChosenIn", QString()) == m_sessionId)
        return chosen;
    if (!m_current.isEmpty())
        return m_current;
    return m_plasma ? plasmaKey : (m_hyprland ? hyprlandKey : QString());
}

bool DesktopEnvironment::signOutNeeded() const
{
    return !m_current.isEmpty() && !next().isEmpty() && next() != m_current;
}

bool DesktopEnvironment::canRemoveHyprland() const
{
    return m_plasma && m_hyprland && m_current != hyprlandKey && next() != hyprlandKey;
}

void DesktopEnvironment::refresh()
{
    m_plasma = QFileInfo::exists(m_sessionDir + QLatin1Char('/') + sessionFileOf(plasmaKey));
    m_hyprland = QFileInfo::exists(m_sessionDir + QLatin1Char('/') + sessionFileOf(hyprlandKey));
    Q_EMIT changed();
}

void DesktopEnvironment::setBusy(const QString &what, const QString &desktop, int percent)
{
    m_busy = what;
    m_busyDesktop = desktop;
    m_busyPercent = percent;
    Q_EMIT busyChanged();
}

void DesktopEnvironment::rememberNext(const QString &desktop)
{
    KConfigGroup group = m_config->group(QStringLiteral("Desktop"));
    group.writeEntry("Next", desktop);
    group.writeEntry("ChosenIn", m_sessionId);
    m_config->sync();
}

void DesktopEnvironment::setOpenApps(const QStringList &ids)
{
    QStringList kept;
    QStringList names;
    for (const QString &id : ids) {
        if (!validId(id) || id == settingsId || kept.contains(id))
            continue;
        kept << id;
        names << nameOf(id);
    }
    m_openAppIds = kept;
    if (names != m_openAppNames) {
        m_openAppNames = names;
        Q_EMIT openAppsChanged();
    }
}

void DesktopEnvironment::refreshOpenApps()
{
    if (!m_apps)
        return;
    QPointer<DesktopEnvironment> self(this);
    m_apps(m_current, [self](const QStringList &ids) {
        if (self)
            self->setOpenApps(ids);
    });
}

DesktopSwitchBus::DesktopSwitchBus(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_registered = bus.registerObject(QStringLiteral("/DesktopSwitch"), this, QDBusConnection::ExportScriptableSlots)
        && bus.registerService(busName);
}

DesktopSwitchBus *DesktopSwitchBus::instance()
{
    static DesktopSwitchBus *bus = new DesktopSwitchBus(QCoreApplication::instance());
    return bus;
}

void DesktopSwitchBus::apps(const QString &list)
{
    if (!waiting)
        return;
    auto done = std::move(waiting);
    waiting = nullptr;
    done(list.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
}

void DesktopEnvironment::runHelper(const QStringList &arguments, std::function<void(int, const QString &)> done)
{
    if (m_helper.isEmpty()) {
        done(-1, {});
        return;
    }
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        process->deleteLater();
        done(status == QProcess::NormalExit ? code : -1, error);
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        done(-1, process->errorString());
    });
    process->start(m_helper.first(), m_helper.mid(1) + arguments);
}

bool DesktopEnvironment::choose(const QString &desktop)
{
    if (!m_busy.isEmpty() || !installed(desktop) || desktop == next())
        return false;
    switchTo(desktop);
    return true;
}

void DesktopEnvironment::switchTo(const QString &desktop)
{
    setBusy(QStringLiteral("switching"), desktop);
    runHelper({QStringLiteral("set"), desktop}, [this, desktop](int code, const QString &error) {
        const bool ok = code == 0;
        if (ok)
            rememberNext(desktop);
        setBusy({}, {});
        Q_EMIT changed();
        Q_EMIT finished(QStringLiteral("switch"), desktop, ok, code == pkexecDismissed, ok ? QString() : error);
    });
}

bool DesktopEnvironment::install(const QString &desktop, bool now)
{
    if (!m_busy.isEmpty() || packageOf(desktop).isEmpty() || installed(desktop) || !m_packages)
        return false;
    setBusy(QStringLiteral("installing"), desktop, 0);
    QPointer<DesktopEnvironment> self(this);
    m_packages(
        QStringLiteral("install"), {packageOf(desktop)},
        [self, desktop](int percent) {
            if (self && self->m_busy == QLatin1String("installing") && percent != self->m_busyPercent)
                self->setBusy(QStringLiteral("installing"), desktop, percent);
        },
        [self, desktop, now](bool ok, const QString &error) {
            if (!self)
                return;
            self->refresh();
            self->setBusy({}, {});
            if (!ok || !self->installed(desktop)) {
                Q_EMIT self->finished(QStringLiteral("install"), desktop, false, false, error);
                return;
            }
            Q_EMIT self->finished(QStringLiteral("install"), desktop, true, false, {});
            if (now && self->canSwitchNow() && desktop != self->m_current)
                self->switchNow(desktop);
            else
                self->choose(desktop);
        });
    return true;
}

bool DesktopEnvironment::switchNow(const QString &desktop)
{
    if (!m_busy.isEmpty() || !installed(desktop) || desktop == m_current || !canSwitchNow() || m_sessionId.isEmpty())
        return false;
    setBusy(QStringLiteral("preparing"), desktop);
    QPointer<DesktopEnvironment> self(this);
    auto armed = [self, desktop](const QStringList &ids) {
        if (!self)
            return;
        // the apps to open again, read once by the new desktop
        const QString file = restoreFile();
        QDir().mkpath(QFileInfo(file).absolutePath());
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(restoreList(desktop, QDateTime::currentSecsSinceEpoch(), ids)) < 0
            || !out.commit()) {
            self->setBusy({}, {});
            Q_EMIT self->finished(QStringLiteral("switch-now"), desktop, false, false, out.errorString());
            return;
        }
        self->runHelper({QStringLiteral("switch"), desktop, self->m_sessionId}, [self, desktop](int code, const QString &error) {
            if (!self)
                return;
            if (code == 0) {
                self->rememberNext(desktop);
                Q_EMIT self->changed();
                Q_EMIT self->readyToLeave(desktop);
                return;
            }
            QFile::remove(restoreFile());
            self->setBusy({}, {});
            if (code == helperNoLiveSwitch) {
                // not right away on this PC: the next sign-in, as before
                Q_EMIT self->switchNowUnavailable(desktop);
                self->switchTo(desktop);
                return;
            }
            Q_EMIT self->finished(QStringLiteral("switch-now"), desktop, false, code == pkexecDismissed, error);
        });
    };
    if (m_apps)
        m_apps(m_current, armed);
    else
        armed(m_openAppIds);
    return true;
}

void DesktopEnvironment::leave()
{
    if (m_busy != QLatin1String("preparing"))
        return;
    const QString desktop = m_busyDesktop;
    setBusy(QStringLiteral("leaving"), desktop);
    if (m_leaver)
        m_leaver(m_current);
    // a session that is still here: an app asks to save, or did not close
    m_stillHere.disconnect(this);
    connect(&m_stillHere, &QTimer::timeout, this, [this, desktop] {
        if (m_busy != QLatin1String("leaving"))
            return;
        runHelper({QStringLiteral("cancel")}, [](int, const QString &) {});
        QFile::remove(restoreFile());
        setBusy({}, {});
        Q_EMIT finished(QStringLiteral("switch-now"), desktop, false, false, QStringLiteral("still-open"));
    });
    m_stillHere.start(leaveGiveUpMs);
}

bool DesktopEnvironment::removeHyprland()
{
    if (!m_busy.isEmpty() || !canRemoveHyprland() || !m_packages)
        return false;
    setBusy(QStringLiteral("removing"), hyprlandKey, 0);
    QPointer<DesktopEnvironment> self(this);
    m_packages(
        QStringLiteral("remove"), {packageOf(hyprlandKey)},
        [self](int percent) {
            if (self && self->m_busy == QLatin1String("removing") && percent != self->m_busyPercent)
                self->setBusy(QStringLiteral("removing"), hyprlandKey, percent);
        },
        [self](bool ok, const QString &error) {
            if (!self)
                return;
            self->refresh();
            self->setBusy({}, {});
            Q_EMIT self->finished(QStringLiteral("remove"), hyprlandKey, ok && !self->m_hyprland, false, error);
        });
    return true;
}

void DesktopEnvironment::signOut()
{
    const QStringList command = signOutCommand(m_current, m_sessionId);
    if (command.isEmpty())
        return;
    if (command.first().startsWith(QLatin1String("dbus:"))) {
        QDBusConnection::sessionBus().send(
            QDBusMessage::createMethodCall(command.first().mid(5), command.at(1), command.at(2), command.at(3)));
        return;
    }
    if (!QProcess::startDetached(command.first(), command.mid(1)) && !m_sessionId.isEmpty())
        QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("terminate-session"), m_sessionId});
}

DesktopEnvironment::AppLister DesktopEnvironment::liveApps(DesktopEnvironment *self)
{
    return [self](const QString &desktop, std::function<void(const QStringList &)> done) {
        if (desktop == hyprlandKey) {
            const QList<QPair<QString, QString>> known = knownApps();
            QStringList ids;
            const QJsonArray clients = hyprClients();
            for (const QJsonValue &v : clients) {
                const QJsonObject c = v.toObject();
                if (!c.value(QStringLiteral("mapped")).toBool(true) || c.value(QStringLiteral("pid")).toInt() == int(::getpid()))
                    continue;
                QString id = desktopIdForClass(c.value(QStringLiteral("class")).toString(), known);
                if (id.isEmpty())
                    id = desktopIdForClass(c.value(QStringLiteral("initialClass")).toString(), known);
                if (!id.isEmpty())
                    ids << id;
            }
            done(ids);
            return;
        }
        if (desktop != plasmaKey) {
            done({});
            return;
        }
        // KWin knows the windows: a small script hands their apps to the bus
        QDBusConnection bus = QDBusConnection::sessionBus();
        DesktopSwitchBus *answers = DesktopSwitchBus::instance();
        if (!answers->registered()) {
            done({});
            return;
        }
        // a question already open: this one gets the same answer
        if (answers->waiting) {
            auto before = std::move(answers->waiting);
            answers->waiting = [before, done](const QStringList &ids) {
                before(ids);
                done(ids);
            };
            return;
        }
        const QString script = QDir::tempPath() + QStringLiteral("/raye-desktop-switch-%1.js").arg(::getpid());
        QFile f(script);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(kwinListScript()) < 0) {
            done({});
            return;
        }
        f.close();
        const QString plugin = QStringLiteral("raye-desktop-switch-%1").arg(::getpid());
        answers->waiting = done;
        const int asked = ++answers->generation;
        auto finish = [plugin, script, asked] {
            QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"),
                QStringLiteral("unloadScript")) << plugin);
            QFile::remove(script);
            // nothing came in time: an empty answer, to this question only
            if (DesktopSwitchBus::instance()->generation == asked)
                DesktopSwitchBus::instance()->apps({});
        };
        QDBusMessage load = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                                                           QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
        load << script << plugin;
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(load, 1500), self);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, self, [watcher, finish](QDBusPendingCallWatcher *) {
            watcher->deleteLater();
            QDBusPendingReply<int> reply = *watcher;
            if (reply.isError() || reply.value() < 0) {
                finish();
                return;
            }
            QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(reply.value()),
                QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run")));
        });
        // KWin answers in a few ms; never wait more than this
        QTimer::singleShot(1500, self, finish);
    };
}

DesktopEnvironment::Leaver DesktopEnvironment::liveLeaver()
{
    return [](const QString &desktop) {
        if (desktop == plasmaKey) {
            // Plasma asks every app to save and close, then ends the session
            QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"),
                QStringLiteral("logout")));
            return;
        }
        if (desktop != hyprlandKey)
            return;
        // Hyprland ends at once and takes its windows with it: ask each one
        // to close first (an app with unsaved work asks), exit when all did
        const QJsonArray clients = hyprClients();
        for (const QJsonValue &v : clients) {
            const QJsonObject c = v.toObject();
            if (c.value(QStringLiteral("pid")).toInt() == int(::getpid()))
                continue;
            QProcess::startDetached(QStringLiteral("hyprctl"), {QStringLiteral("dispatch"), QStringLiteral("closewindow"),
                                                                 QStringLiteral("address:") + c.value(QStringLiteral("address")).toString()});
        }
        auto *poll = new QTimer(QCoreApplication::instance());
        auto tries = std::make_shared<int>(0);
        QObject::connect(poll, &QTimer::timeout, poll, [poll, tries] {
            int others = 0;
            const QJsonArray now = hyprClients();
            for (const QJsonValue &v : now)
                if (v.toObject().value(QStringLiteral("pid")).toInt() != int(::getpid()))
                    ++others;
            if (others == 0) {
                poll->deleteLater();
                QProcess::startDetached(QStringLiteral("hyprctl"), {QStringLiteral("dispatch"), QStringLiteral("exit")});
                return;
            }
            // the page takes the switch back after leaveGiveUpMs
            if (++*tries * 300 >= leaveGiveUpMs)
                poll->deleteLater();
        });
        poll->start(300);
    };
}

DesktopEnvironment::PackageRunner DesktopEnvironment::packageKit()
{
    return [](const QString &operation, const QStringList &names, std::function<void(int)> progress,
              std::function<void(bool, const QString &)> done) {
        const QString service = QStringLiteral("org.freedesktop.PackageKit");
        QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
        if (!bus || (!bus->isServiceRegistered(service) && !bus->activatableServiceNames().value().contains(service))) {
            done(false, {});
            return;
        }
        const bool installing = operation == QLatin1String("install");
        auto ids = std::make_shared<QStringList>();
        PackageKit::Transaction *resolve = PackageKit::Daemon::resolve(
            names, installing ? PackageKit::Transaction::FilterNotInstalled : PackageKit::Transaction::FilterInstalled);
        QObject::connect(resolve, &PackageKit::Transaction::package, resolve,
                         [ids](PackageKit::Transaction::Info, const QString &id, const QString &) {
                             if (!ids->contains(id))
                                 ids->append(id);
                         });
        QObject::connect(resolve, &PackageKit::Transaction::finished, resolve,
                         [ids, installing, progress, done](PackageKit::Transaction::Exit exit) {
                             if (exit != PackageKit::Transaction::ExitSuccess || ids->isEmpty()) {
                                 done(false, {});
                                 return;
                             }
                             PackageKit::Transaction *t = installing ? PackageKit::Daemon::installPackages(*ids)
                                                                     // what it brought and nothing else needs goes too
                                                                     : PackageKit::Daemon::removePackages(*ids, false, true);
                             auto error = std::make_shared<QString>();
                             QObject::connect(t, &PackageKit::Transaction::percentageChanged, t, [t, progress] {
                                 const uint p = t->percentage();
                                 if (p <= 100) // 101: not known
                                     progress(int(p));
                             });
                             QObject::connect(t, &PackageKit::Transaction::errorCode, t,
                                              [error](PackageKit::Transaction::Error, const QString &details) {
                                                  *error = details;
                                              });
                             QObject::connect(t, &PackageKit::Transaction::finished, t,
                                              [error, done](PackageKit::Transaction::Exit e) {
                                                  done(e == PackageKit::Transaction::ExitSuccess, *error);
                                              });
                         });
    };
}
