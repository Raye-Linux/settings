// SPDX-License-Identifier: GPL-3.0-or-later
// What Raye Update is doing, read from the state file it keeps, plus the
// things the page can ask for: check now, restart and install, pause and
// resume.
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class UpdateStatus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QStringList packages READ packages NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    // why updates that need no restart are not installing yet: "battery",
    // "metered", "game", "busy"; empty when they install on their own
    Q_PROPERTY(QString waitReason READ waitReason NOTIFY changed)
    Q_PROPERTY(qint64 checked READ checked NOTIFY changed)
    Q_PROPERTY(qint64 pausedUntil READ pausedUntil NOTIFY changed)
    // downloaded updates wait for a restart, or something installed
    // outside Raye Update (a kernel from unattended-upgrades) asks for one
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
    // updates that need no restart are installing now, installPercent done
    Q_PROPERTY(bool installingNow READ installingNow NOTIFY changed)
    Q_PROPERTY(int installPercent READ installPercent NOTIFY changed)
    Q_PROPERTY(bool restarting READ restarting NOTIFY restartingChanged)
    // version 2 of the state file (raye-update docs/state.md)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(qint64 downloadedBytes READ downloadedBytes NOTIFY changed)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY changed)
    // 0-100 while it checks; 0 = cannot be told (or a file from before the key)
    Q_PROPERTY(int checkPercent READ checkPercent NOTIFY changed)
    // while it downloads; secondsLeft 0 means "cannot be told", never "done"
    Q_PROPERTY(qint64 bytesPerSecond READ bytesPerSecond NOTIFY changed)
    Q_PROPERTY(qint64 secondsLeft READ secondsLeft NOTIFY changed)
    // what the last install in use put in, by name (lastInstall.packages)
    Q_PROPERTY(QStringList installedPackages READ installedPackages NOTIFY changed)
    // [{ name, from, to, origin, kind, restart, state }]; restart missing =
    // true; state ("queued", "installing", "installed", "failed") only
    // while an install in use runs
    Q_PROPERTY(QVariantList pending READ pending NOTIFY changed)
    // [{ device, from, to, summary, size, needsPower }]
    Q_PROPERTY(QVariantList firmware READ firmware NOTIFY changed)
    // { time, count, firmware, ok, packages }, empty before the first install
    Q_PROPERTY(QVariantMap lastInstall READ lastInstall NOTIFY changed)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
    // the last check read every place updates come from; a file from before
    // this key was written by a client that only knew the all-clear case
    Q_PROPERTY(bool checkedEverything READ checkedEverything NOTIFY changed)
    // what it could not read: [{ uri, problem }]
    Q_PROPERTY(QVariantList brokenSources READ brokenSources NOTIFY changed)
    // "restart" or "shutdown" once someone picked "Update and restart", else empty
    Q_PROPERTY(QString armedThen READ armedThen NOTIFY changed)
    Q_PROPERTY(int activeHoursStart READ activeHoursStart NOTIFY changed)
    Q_PROPERTY(int activeHoursEnd READ activeHoursEnd NOTIFY changed)
    Q_PROPERTY(bool autoRestart READ autoRestart NOTIFY changed)
    // updates installed so far, newest first: [{ time, packages: [{ name, from, to, kind }] }]
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)

public:
    // the restart itself: call done(false) when it could not be asked for
    using RestartRequest = std::function<void(std::function<void(bool)> done)>;

    explicit UpdateStatus(const QString &path, QObject *parent = nullptr);

    QString state() const { return m_state; }
    int count() const { return m_count; }
    QStringList packages() const { return m_packages; }
    QString reason() const { return m_reason; }
    QString waitReason() const { return m_waitReason; }
    qint64 checked() const { return m_checked; } // seconds since 1970, 0 = never
    qint64 pausedUntil() const { return m_pausedUntil; } // 0 = not paused
    // the set that is downloaded needs one (raye-update says so in
    // needsRestart), not just "something is downloaded"
    bool restartNeeded() const { return (m_phase == QLatin1String("ready") && m_needsRestart) || m_rebootRequired; }
    bool installingNow() const { return m_phase == QLatin1String("installing") && m_armedThen.isEmpty(); }
    int installPercent() const { return m_installPercent; }
    bool restarting() const { return m_restarting; }
    QString phase() const { return m_phase; }
    qint64 downloadedBytes() const { return m_downloadedBytes; }
    qint64 totalBytes() const { return m_totalBytes; }
    int checkPercent() const { return m_checkPercent; }
    qint64 bytesPerSecond() const { return m_bytesPerSecond; }
    qint64 secondsLeft() const { return m_secondsLeft; }
    QStringList installedPackages() const { return m_installedPackages; }
    QVariantList pending() const { return m_pending; }
    QVariantList firmware() const { return m_firmware; }
    QVariantMap lastInstall() const { return m_lastInstall; }
    QString errorCode() const { return m_errorCode; }
    bool checkedEverything() const { return m_checkedEverything; }
    QVariantList brokenSources() const { return m_brokenSources; }
    QString armedThen() const { return m_armedThen; }
    int activeHoursStart() const { return m_activeStart; }
    int activeHoursEnd() const { return m_activeEnd; }
    bool autoRestart() const { return m_autoRestart; }
    // read from apt's log the first time someone asks (the history page)
    QVariantList history();

    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void restartAndInstall();
    // installs now what needs no restart; the rest waits for one
    Q_INVOKABLE void installNow();
    Q_INVOKABLE void pause(int weeks);
    Q_INVOKABLE void resume();
    // undo "Update and restart": the updates stay downloaded
    Q_INVOKABLE void cancelInstall();
    // hours 0-23, end excluded, 18 hours at most (raye-update checks it too)
    Q_INVOKABLE void setActiveHours(int start, int end);
    Q_INVOKABLE void setAutoRestart(bool on);
    Q_INVOKABLE void reloadHistory();

    static bool validActiveHours(int start, int end);
    // "kernel", "driver" or "system", as raye-update sorts a package
    static QString kindOf(const QString &package);
    // The updates in apt's history log: blocks with an Upgrade line, newest
    // first, at most `limit` of them. Programs installed by hand are left out.
    static QVariantList parseAptHistory(const QByteArray &log, int limit = 60);

    // public so the tests can feed it a file
    void reload();
    // for the tests: the raye-update program, the file a package drops to
    // ask for a restart, and the restart itself
    void setToolPath(const QString &path) { m_toolPath = path; }
    void setRebootRequiredPath(const QString &path);
    void setHistoryPath(const QString &path);
    void setRestartRequest(RestartRequest request) { m_restart = std::move(request); }

    static constexpr int RestartGrace = 60000; // ms

Q_SIGNALS:
    void changed();
    void restartingChanged();
    void historyChanged();
    // "cancelled": the password was not given, "failed": the updates could
    // not be set up to install, "restart": the restart itself was refused
    void restartFailed(const QString &reason);

private:
    void checkRebootRequired();
    void restart();
    void finish(const QString &failure);
    void runTool(const QStringList &arguments);

    QString m_path;
    QString m_toolPath = QStringLiteral("/usr/bin/raye-update");
    QString m_rebootRequiredPath = QStringLiteral("/run/reboot-required");
    QString m_historyPath = QStringLiteral("/var/log/apt/history.log");
    QFileSystemWatcher m_watcher;
    QString m_state = QStringLiteral("idle");
    int m_count = 0;
    QStringList m_packages;
    QString m_reason;
    QString m_waitReason;
    qint64 m_checked = 0;
    qint64 m_pausedUntil = 0;
    bool m_rebootRequired = false;
    QString m_phase = QStringLiteral("idle");
    bool m_needsRestart = false;
    int m_installPercent = 0;
    qint64 m_downloadedBytes = 0;
    qint64 m_totalBytes = 0;
    int m_checkPercent = 0;
    qint64 m_bytesPerSecond = 0;
    qint64 m_secondsLeft = 0;
    QStringList m_installedPackages;
    QVariantList m_pending;
    QVariantList m_firmware;
    QVariantMap m_lastInstall;
    QString m_errorCode;
    bool m_checkedEverything = true;
    QVariantList m_brokenSources;
    QString m_armedThen;
    int m_activeStart = 8;
    int m_activeEnd = 17;
    bool m_autoRestart = false;
    QVariantList m_history;
    qint64 m_historyStamp = -1;
    bool m_historyLoaded = false;
    bool m_restarting = false;
    RestartRequest m_restart;
};
