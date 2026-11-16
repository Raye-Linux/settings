// SPDX-License-Identifier: GPL-3.0-or-later
#include "updatestatus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>

namespace {

void callLogind(std::function<void(bool)> done)
{
    // interactive: polkit may still ask, as it does for the lock screen
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                              QStringLiteral("/org/freedesktop/login1"),
                                              QStringLiteral("org.freedesktop.login1.Manager"),
                                              QStringLiteral("Reboot"));
    msg << true;
    auto *watch = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg));
    QObject::connect(watch, &QDBusPendingCallWatcher::finished, watch, [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        done(!w->isError());
    });
}

// Plasma's own restart first, so apps get to save their work; logind
// when there is no Plasma session to ask (or it refuses).
void requestRestart(std::function<void(bool)> done)
{
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
                                              QStringLiteral("org.kde.Shutdown"), QStringLiteral("logoutAndReboot"));
    auto *watch = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg));
    QObject::connect(watch, &QDBusPendingCallWatcher::finished, watch, [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError())
            callLogind(done);
        else
            done(true);
    });
}

} // namespace

UpdateStatus::UpdateStatus(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_restart(requestRestart)
{
    // the file is replaced on every write, so watch the folder too
    m_watcher.addPath(QFileInfo(path).absolutePath());
    if (QFile::exists(path))
        m_watcher.addPath(path);
    m_watcher.addPath(QFileInfo(m_rebootRequiredPath).absolutePath());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &dir) {
        if (dir == QFileInfo(m_path).absolutePath())
            reload();
        else
            checkRebootRequired();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &UpdateStatus::reload);
    reload();
}

void UpdateStatus::checkRebootRequired()
{
    const bool required = QFile::exists(m_rebootRequiredPath);
    if (required == m_rebootRequired)
        return;
    m_rebootRequired = required;
    Q_EMIT changed();
}

void UpdateStatus::reload()
{
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_state = o.value(QStringLiteral("state")).toString(QStringLiteral("idle"));
        m_count = o.value(QStringLiteral("updates")).toInt();
        m_reason = o.value(QStringLiteral("reason")).toString();
        m_waitReason = o.value(QStringLiteral("waitReason")).toString();
        m_checked = qint64(o.value(QStringLiteral("checked")).toDouble());
        m_pausedUntil = qint64(o.value(QStringLiteral("paused_until")).toDouble());
        m_packages.clear();
        for (const QJsonValue &v : o.value(QStringLiteral("packages")).toArray())
            m_packages.append(v.toString());
        m_phase = o.value(QStringLiteral("phase")).toString(m_state);
        // a file without the key had every downloaded set wait for a restart
        m_needsRestart = o.value(QStringLiteral("needsRestart")).toBool(m_state == QLatin1String("ready"));
        m_installPercent = qBound(0, o.value(QStringLiteral("installPercent")).toInt(), 100);
        m_downloadedBytes = qint64(o.value(QStringLiteral("downloadedBytes")).toDouble());
        m_totalBytes = qint64(o.value(QStringLiteral("totalBytes")).toDouble());
        m_pending = o.value(QStringLiteral("pending")).toArray().toVariantList();
        m_firmware = o.value(QStringLiteral("firmware")).toArray().toVariantList();
        m_lastInstall = o.value(QStringLiteral("lastInstall")).toObject().toVariantMap();
        m_errorCode = o.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
        m_armedThen = o.value(QStringLiteral("armed")).toObject().value(QStringLiteral("then")).toString();
        m_checkedEverything = o.value(QStringLiteral("checkedEverything")).toBool(true);
        m_brokenSources = o.value(QStringLiteral("brokenSources")).toArray().toVariantList();
        const QJsonObject hours = o.value(QStringLiteral("activeHours")).toObject();
        m_activeStart = hours.value(QStringLiteral("start")).toInt(8);
        m_activeEnd = hours.value(QStringLiteral("end")).toInt(17);
        m_autoRestart = o.value(QStringLiteral("autoRestart")).toBool();
        if (!m_watcher.files().contains(m_path))
            m_watcher.addPath(m_path);
    }
    m_rebootRequired = QFile::exists(m_rebootRequiredPath);
    Q_EMIT changed();
    // an install that just ended has written to apt's log
    const qint64 stamp = qint64(m_lastInstall.value(QStringLiteral("time")).toDouble());
    if (stamp != m_historyStamp) {
        m_historyStamp = stamp;
        if (m_historyLoaded)
            reloadHistory();
    }
}

QVariantList UpdateStatus::history()
{
    if (!m_historyLoaded)
        reloadHistory();
    return m_history;
}

void UpdateStatus::reloadHistory()
{
    m_historyLoaded = true;
    QFile f(m_historyPath);
    m_history = f.open(QIODevice::ReadOnly) ? parseAptHistory(f.readAll()) : QVariantList();
    // firmware is not in apt's log: the last install says how many devices
    const int devices = m_lastInstall.value(QStringLiteral("firmware")).toInt();
    if (devices > 0) {
        const qint64 when = qint64(m_lastInstall.value(QStringLiteral("time")).toDouble());
        QVariantMap entry{{QStringLiteral("time"), when},
                          {QStringLiteral("firmware"), devices},
                          {QStringLiteral("packages"), QVariantList()}};
        auto at = std::find_if(m_history.begin(), m_history.end(), [when](const QVariant &e) {
            return e.toMap().value(QStringLiteral("time")).toLongLong() <= when;
        });
        m_history.insert(at, entry);
    }
    Q_EMIT historyChanged();
}

void UpdateStatus::setHistoryPath(const QString &path)
{
    m_historyPath = path;
    if (m_historyLoaded)
        reloadHistory();
}

QString UpdateStatus::kindOf(const QString &package)
{
    // raye-kernel only points at a kernel: it is not one of its own
    static const QRegularExpression kernel(QStringLiteral("^linux-(image|modules|headers|generic|signed|raye)"));
    static const QRegularExpression driver(
        QStringLiteral("^(nvidia-|libnvidia-|linux-firmware|firmware-|intel-microcode|amd64-microcode)"));
    const QString base = package.section(QLatin1Char(':'), 0, 0);
    if (kernel.match(base).hasMatch())
        return QStringLiteral("kernel");
    if (driver.match(base).hasMatch())
        return QStringLiteral("driver");
    return QStringLiteral("system");
}

QVariantList UpdateStatus::parseAptHistory(const QByteArray &log, int limit)
{
    // "Upgrade: firefox:amd64 (140.0, 141.0), libfoo:amd64 (1.0, 1.1)"
    static const QRegularExpression item(QStringLiteral("([^\\s,(]+) \\(([^,)]*)(?:, ([^)]*))?\\)"));
    QVariantList entries;
    qint64 time = 0;
    QVariantList packages;
    const auto close = [&] {
        if (!packages.isEmpty())
            entries.prepend(QVariantMap{{QStringLiteral("time"), time}, {QStringLiteral("packages"), packages}});
        packages.clear();
    };
    for (const QByteArray &raw : log.split('\n')) {
        const QString line = QString::fromUtf8(raw);
        if (line.startsWith(QLatin1String("Start-Date: "))) {
            close();
            const QDateTime when = QDateTime::fromString(line.mid(12).simplified(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            time = when.isValid() ? when.toSecsSinceEpoch() : 0;
        } else if (line.startsWith(QLatin1String("Upgrade: "))) {
            auto it = item.globalMatch(line.mid(9));
            while (it.hasNext()) {
                const auto m = it.next();
                const QString name = m.captured(1).section(QLatin1Char(':'), 0, 0);
                packages.append(QVariantMap{{QStringLiteral("name"), name},
                                            {QStringLiteral("from"), m.captured(2).trimmed()},
                                            {QStringLiteral("to"), m.captured(3).trimmed()},
                                            {QStringLiteral("kind"), kindOf(name)}});
            }
        }
    }
    close();
    if (entries.size() > limit)
        entries.erase(entries.begin() + limit, entries.end());
    return entries;
}

void UpdateStatus::checkNow()
{
    // through pkexec like the rest: no password for the people who run this
    // computer, and anyone else may take their time to type one
    runTool({QStringLiteral("check-now")});
}

void UpdateStatus::installNow()
{
    // the install runs in a unit of its own: closing Settings or signing out
    // never cuts dpkg
    runTool({QStringLiteral("install-now")});
}

void UpdateStatus::setRebootRequiredPath(const QString &path)
{
    const QString stateDir = QFileInfo(m_path).absolutePath();
    const QString oldDir = QFileInfo(m_rebootRequiredPath).absolutePath();
    if (oldDir != stateDir)
        m_watcher.removePath(oldDir);
    m_rebootRequiredPath = path;
    m_watcher.addPath(QFileInfo(path).absolutePath());
    reload();
}

void UpdateStatus::restartAndInstall()
{
    if (m_restarting)
        return;
    m_restarting = true;
    Q_EMIT restartingChanged();
    reload();
    if (m_state != QLatin1String("ready")) {
        // nothing downloaded to set up: a plain restart
        restart();
        return;
    }
    // arm the downloaded set (the program goes through pkexec), then restart
    auto *p = new QProcess(this);
    p->setProgram(m_toolPath);
    p->setArguments({QStringLiteral("arm"), QStringLiteral("--then"), QStringLiteral("restart")});
    p->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        if (status != QProcess::NormalExit) {
            finish(QStringLiteral("failed"));
            return;
        }
        switch (code) {
        case 0: // armed
        case 3: // nothing downloaded after all: the restart is still wanted
        case 4: // another tool already set an install up for the next start
            restart();
            return;
        case 126: // pkexec: no password given
        case 127:
            finish(QStringLiteral("cancelled"));
            return;
        default:
            finish(QStringLiteral("failed"));
        }
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        p->deleteLater();
        finish(QStringLiteral("failed"));
    });
    p->start();
}

void UpdateStatus::restart()
{
    QPointer<UpdateStatus> self(this);
    m_restart([self](bool ok) {
        if (self)
            self->finish(ok ? QString() : QStringLiteral("restart"));
    });
}

void UpdateStatus::finish(const QString &failure)
{
    // on success the session is going away: stay "restarting" until then,
    // unless an app that would not close kept it
    if (failure.isEmpty()) {
        QTimer::singleShot(RestartGrace, this, [this] {
            m_restarting = false;
            Q_EMIT restartingChanged();
        });
        return;
    }
    m_restarting = false;
    Q_EMIT restartingChanged();
    Q_EMIT restartFailed(failure);
}

// raye-update runs these through pkexec, allowed without a password for
// the people who run this computer (polkit rule shipped with raye-update).
// The state file changes when they are done, and the page follows it.
void UpdateStatus::runTool(const QStringList &arguments)
{
    QProcess::startDetached(m_toolPath, arguments);
}

void UpdateStatus::pause(int weeks)
{
    if (weeks < 1 || weeks > 5)
        return;
    runTool({QStringLiteral("pause"), QString::number(weeks)});
}

void UpdateStatus::resume()
{
    runTool({QStringLiteral("resume")});
}

void UpdateStatus::cancelInstall()
{
    runTool({QStringLiteral("disarm")});
}

bool UpdateStatus::validActiveHours(int start, int end)
{
    if (start < 0 || start > 23 || end < 0 || end > 23 || start == end)
        return false;
    return (end - start + 24) % 24 <= 18;
}

void UpdateStatus::setActiveHours(int start, int end)
{
    if (!validActiveHours(start, end))
        return;
    runTool({QStringLiteral("active-hours"), QString::number(start), QString::number(end)});
}

void UpdateStatus::setAutoRestart(bool on)
{
    runTool({QStringLiteral("auto-restart"), on ? QStringLiteral("on") : QStringLiteral("off")});
}
