// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsimport.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <QCoreApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

#include <cerrno>
#include <csignal>

namespace
{
constexpr qint64 kGiB = 1024LL * 1024 * 1024;
// left free on this PC after the copy, as the helper does
constexpr qint64 kMargin = kGiB;
const QStringList kKinds{QStringLiteral("desktop"), QStringLiteral("documents"), QStringLiteral("downloads"),
                         QStringLiteral("music"), QStringLiteral("pictures"), QStringLiteral("videos")};

QString libexec(const char *name)
{
    return QStringLiteral(RAYE_LIBEXECDIR "/") + QLatin1String(name);
}

void markOffered()
{
    KConfig config(QStringLiteral("raye-settingsrc"), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("WindowsImport"));
    group.writeEntry("Offered", true);
    config.sync();
}
} // namespace

namespace WindowsImportData
{
QVariantList parseVolumes(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object().value(QStringLiteral("volumes")).toArray().toVariantList();
}

QVariantMap parseScan(const QByteArray &json)
{
    QVariantMap scan = QJsonDocument::fromJson(json).object().toVariantMap();
    QVariantList accounts = scan.value(QStringLiteral("accounts")).toList();
    for (QVariant &value : accounts) {
        QVariantMap account = value.toMap();
        QVariantList folders = account.value(QStringLiteral("folders")).toList();
        for (QVariant &folder : folders) {
            QVariantMap map = folder.toMap();
            map.insert(QStringLiteral("checked"), true);
            folder = map;
        }
        account.insert(QStringLiteral("folders"), folders);
        account.insert(QStringLiteral("wallpaperChecked"), account.value(QStringLiteral("wallpaper")).toLongLong() > 0);
        account.insert(QStringLiteral("bookmarksChecked"), !account.value(QStringLiteral("browsers")).toList().isEmpty());
        value = account;
    }
    // with several accounts, only the first is ticked to start with: it is
    // most often the person's own
    for (int i = 1; i < accounts.size(); ++i) {
        QVariantMap account = accounts[i].toMap();
        QVariantList folders = account.value(QStringLiteral("folders")).toList();
        for (QVariant &folder : folders) {
            QVariantMap map = folder.toMap();
            map.insert(QStringLiteral("checked"), false);
            folder = map;
        }
        account.insert(QStringLiteral("folders"), folders);
        account.insert(QStringLiteral("wallpaperChecked"), false);
        account.insert(QStringLiteral("bookmarksChecked"), false);
        accounts[i] = account;
    }
    scan.insert(QStringLiteral("accounts"), accounts);
    return scan;
}

QPair<QString, QString> parseHoldLine(const QByteArray &line)
{
    const QString text = QString::fromUtf8(line).trimmed();
    for (const char *kind : {"mounted", "refused", "warning"}) {
        const QString prefix = QLatin1String(kind) + QLatin1Char(' ');
        if (text.startsWith(prefix) && text.size() > prefix.size())
            return {QLatin1String(kind), text.mid(prefix.size())};
    }
    return {};
}

QVariantMap selection(const QVariantList &accounts)
{
    qint64 bytes = 0;
    int files = 0;
    int folders = 0;
    for (const QVariant &value : accounts) {
        const QVariantMap account = value.toMap();
        const QVariantList list = account.value(QStringLiteral("folders")).toList();
        for (const QVariant &folder : list) {
            const QVariantMap map = folder.toMap();
            if (!map.value(QStringLiteral("checked")).toBool())
                continue;
            bytes += map.value(QStringLiteral("bytes")).toLongLong();
            files += map.value(QStringLiteral("files")).toInt();
            ++folders;
        }
        if (account.value(QStringLiteral("wallpaperChecked")).toBool()) {
            bytes += account.value(QStringLiteral("wallpaper")).toLongLong();
            ++files;
        }
    }
    return {{QStringLiteral("bytes"), bytes}, {QStringLiteral("files"), files}, {QStringLiteral("folders"), folders}};
}

QVariantMap plan(const QString &root, const QVariantList &accounts, const QVariantMap &dest, const QString &lang,
                 const QVariantMap &source)
{
    QVariantList chosen;
    QVariantMap used;
    for (const QVariant &value : accounts) {
        const QVariantMap account = value.toMap();
        QVariantList folders;
        const QVariantList list = account.value(QStringLiteral("folders")).toList();
        for (const QVariant &folder : list) {
            const QVariantMap map = folder.toMap();
            if (!map.value(QStringLiteral("checked")).toBool())
                continue;
            const QString kind = map.value(QStringLiteral("kind")).toString();
            folders << QVariantMap{{QStringLiteral("kind"), kind}, {QStringLiteral("path"), map.value(QStringLiteral("path"))}};
            used.insert(kind, dest.value(kind));
        }
        const bool wallpaper = account.value(QStringLiteral("wallpaperChecked")).toBool();
        const bool bookmarks = account.value(QStringLiteral("bookmarksChecked")).toBool();
        if (wallpaper)
            used.insert(QStringLiteral("pictures"), dest.value(QStringLiteral("pictures")));
        if (bookmarks)
            used.insert(QStringLiteral("documents"), dest.value(QStringLiteral("documents")));
        if (folders.isEmpty() && !wallpaper && !bookmarks)
            continue;
        chosen << QVariantMap{{QStringLiteral("name"), account.value(QStringLiteral("name"))},
                              {QStringLiteral("path"), account.value(QStringLiteral("path"))},
                              {QStringLiteral("folders"), folders},
                              {QStringLiteral("wallpaper"), wallpaper},
                              {QStringLiteral("browsers"), bookmarks ? account.value(QStringLiteral("browsers")) : QVariantList()}};
    }
    return {{QStringLiteral("root"), root},
            {QStringLiteral("lang"), lang},
            {QStringLiteral("dest"), used},
            {QStringLiteral("accounts"), chosen},
            {QStringLiteral("source"), source}};
}

int secondsLeft(qint64 done, qint64 total, qint64 elapsedMs)
{
    // the first seconds of a copy say little about its pace
    if (done <= 0 || elapsedMs < 3000 || total <= done)
        return total > 0 && done >= total ? 0 : -1;
    const double rate = double(done) / double(elapsedMs);
    return int((total - done) / rate / 1000.0) + 1;
}

bool offerable(const QVariantList &volumes)
{
    for (const QVariant &value : volumes) {
        const QVariantMap volume = value.toMap();
        if (volume.value(QStringLiteral("windows")).toBool()
            && volume.value(QStringLiteral("state")).toString() == QLatin1String("ok"))
            return true;
    }
    return false;
}
} // namespace WindowsImportData

using namespace WindowsImportData;

WindowsImport::WindowsImport(QObject *parent)
    : QObject(parent)
    , m_helper(libexec("windows-import"))
    , m_mountHelper(libexec("windows-import-mount"))
{
    QTimer::singleShot(0, this, &WindowsImport::readState);
}

WindowsImport::~WindowsImport()
{
    // nothing reaches the page any more while this goes
    for (QProcess *process : findChildren<QProcess *>())
        process->disconnect(this);
    if (m_copy) {
        // the copy stops cleanly, and is picked up next time
        m_copy->terminate();
        if (!m_copy->waitForFinished(5000))
            m_copy->kill();
    }
    release();
    // root's helper cannot be killed from here: give it the time to unmount
    for (QProcess *process : std::as_const(m_released)) {
        if (process->state() != QProcess::NotRunning)
            process->waitForFinished(15000);
    }
}

void WindowsImport::setStage(const QString &stage, const QString &refusal)
{
    m_stage = stage;
    m_refusal = refusal;
    Q_EMIT stageChanged();
}

void WindowsImport::setStateHome(const QString &path)
{
    m_stateHome = path;
    readState();
}

QString WindowsImport::stateDir() const
{
    QString base = m_stateHome;
    if (base.isEmpty())
        base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/raye-settings/windows-import");
}

QStringList WindowsImport::helperEnvironment() const
{
    QStringList env = QProcess::systemEnvironment();
    if (!m_stateHome.isEmpty())
        env << QStringLiteral("XDG_STATE_HOME=") + m_stateHome;
    return env;
}

QVariantMap WindowsImport::destinations() const
{
    if (!m_dest.isEmpty())
        return m_dest;
    const QHash<QString, QStandardPaths::StandardLocation> where{
        {QStringLiteral("desktop"), QStandardPaths::DesktopLocation},
        {QStringLiteral("documents"), QStandardPaths::DocumentsLocation},
        {QStringLiteral("downloads"), QStandardPaths::DownloadLocation},
        {QStringLiteral("music"), QStandardPaths::MusicLocation},
        {QStringLiteral("pictures"), QStandardPaths::PicturesLocation},
        {QStringLiteral("videos"), QStandardPaths::MoviesLocation}};
    QVariantMap dest;
    for (const QString &kind : kKinds) {
        QString path = QStandardPaths::writableLocation(where.value(kind));
        // a folder turned off in user-dirs is the home itself: never fill the home
        if (path.isEmpty() || QDir(path) == QDir::home())
            path = QDir::homePath() + QLatin1Char('/') + kind.left(1).toUpper() + kind.mid(1);
        dest.insert(kind, path);
    }
    return dest;
}

void WindowsImport::readState()
{
    QFile file(stateDir() + QStringLiteral("/state.json"));
    m_canResume = false;
    if (file.open(QIODevice::ReadOnly)) {
        const QVariantMap state = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        const QString status = state.value(QStringLiteral("status")).toString();
        const qint64 pid = state.value(QStringLiteral("pid")).toLongLong();
        const bool gone = pid <= 0 || (::kill(pid_t(pid), 0) != 0 && errno == ESRCH);
        m_canResume = QFile::exists(stateDir() + QStringLiteral("/plan.json"))
                      && (status == QLatin1String("stopped") || (status == QLatin1String("running") && gone));
        if (m_canResume) {
            m_totalBytes = state.value(QStringLiteral("total")).toLongLong();
            m_doneBytes = state.value(QStringLiteral("done")).toLongLong();
            m_totalFiles = state.value(QStringLiteral("totalFiles")).toInt();
            Q_EMIT progressChanged();
        }
    }
    Q_EMIT stageChanged();
}

void WindowsImport::refresh()
{
    if (m_list)
        return;
    m_listing = true;
    Q_EMIT volumesChanged();
    m_list = new QProcess(this);
    m_list->setEnvironment(helperEnvironment());
    connect(m_list, &QProcess::finished, this, [this] {
        m_volumes = parseVolumes(m_list->readAllStandardOutput());
        m_list->deleteLater();
        m_list = nullptr;
        m_listing = false;
        Q_EMIT volumesChanged();
    });
    connect(m_list, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_list)
            return;
        m_list->deleteLater();
        m_list = nullptr;
        m_listing = false;
        Q_EMIT volumesChanged();
    });
    m_list->start(m_helper, {QStringLiteral("volumes")});
}

void WindowsImport::openVolume(const QString &device)
{
    QVariantMap volume;
    for (const QVariant &value : std::as_const(m_volumes)) {
        if (value.toMap().value(QStringLiteral("device")).toString() == device)
            volume = value.toMap();
    }
    if (volume.isEmpty())
        return;
    m_dirty = false;
    m_source = {{QStringLiteral("device"), device},
                {QStringLiteral("uuid"), volume.value(QStringLiteral("uuid"))},
                {QStringLiteral("label"), volume.value(QStringLiteral("label"))}};
    m_sourceName = volume.value(QStringLiteral("label")).toString();
    if (volume.value(QStringLiteral("state")).toString() == QLatin1String("bitlocker")) {
        setStage(QStringLiteral("refused"), QStringLiteral("bitlocker"));
        return;
    }
    const QString mounted = volume.value(QStringLiteral("mountPoint")).toString();
    if (!mounted.isEmpty()) {
        m_source.insert(QStringLiteral("mountPoint"), mounted);
        scan(mounted);
        return;
    }
    hold(device, [this](const QString &path) { scan(path); });
}

void WindowsImport::openFolder(const QUrl &folder)
{
    if (!folder.isLocalFile())
        return;
    const QString path = QDir::cleanPath(folder.toLocalFile());
    m_dirty = false;
    m_source = {{QStringLiteral("folder"), path}};
    m_sourceName = QFileInfo(path).fileName();
    scan(path);
}

void WindowsImport::hold(const QString &device, std::function<void(const QString &)> mounted)
{
    release();
    setStage(QStringLiteral("opening"));
    auto *process = new QProcess(this);
    m_hold = process;
    auto seen = std::make_shared<bool>(false);
    connect(process, &QProcess::readyReadStandardOutput, this, [=, this] {
        while (process->canReadLine()) {
            const auto [kind, value] = parseHoldLine(process->readLine());
            if (kind == QLatin1String("warning")) {
                m_dirty = value == QLatin1String("dirty");
            } else if (kind == QLatin1String("mounted")) {
                *seen = true;
                m_root = value;
                mounted(value);
            } else if (kind == QLatin1String("refused")) {
                *seen = true;
                release();
                setStage(QStringLiteral("refused"), value);
            }
        }
    });
    connect(process, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus) {
        if (m_hold == process)
            m_hold = nullptr;
        process->deleteLater();
        if (*seen || m_stage != QLatin1String("opening"))
            return;
        // pkexec: 126 the password box was closed, 127 not allowed
        setStage(QStringLiteral("pick"), code == 126 || code == 127 ? QStringLiteral("denied") : QStringLiteral("unreadable"));
    });
    connect(process, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_hold == process)
            m_hold = nullptr;
        process->deleteLater();
        setStage(QStringLiteral("pick"), QStringLiteral("denied"));
    });
    process->start(m_pkexec, {m_mountHelper, QStringLiteral("hold"), device});
}

void WindowsImport::release()
{
    if (!m_hold)
        return;
    // the helper runs as root and cannot be signalled: closing its input
    // is what makes it unmount and go
    QProcess *process = m_hold;
    m_hold = nullptr;
    process->closeWriteChannel();
    m_released << process;
    connect(process, &QProcess::finished, this, [this, process] { m_released.removeAll(process); });
}

void WindowsImport::scan(const QString &root)
{
    m_root = root;
    setStage(QStringLiteral("scanning"));
    if (m_scan)
        m_scan->deleteLater();
    m_scan = new QProcess(this);
    m_scan->setEnvironment(helperEnvironment());
    QProcess *process = m_scan;
    connect(process, &QProcess::finished, this, [=, this] {
        if (m_scan != process)
            return;
        const QVariantMap found = parseScan(process->readAllStandardOutput());
        m_scan->deleteLater();
        m_scan = nullptr;
        const QString state = found.value(QStringLiteral("state")).toString();
        if (state != QLatin1String("ok")) {
            release();
            setStage(QStringLiteral("refused"), state.isEmpty() ? QStringLiteral("unreadable") : state);
            return;
        }
        m_accounts = found.value(QStringLiteral("accounts")).toList();
        Q_EMIT accountsChanged();
        setStage(QStringLiteral("choose"));
    });
    connect(process, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_scan != process)
            return;
        m_scan->deleteLater();
        m_scan = nullptr;
        release();
        setStage(QStringLiteral("refused"), QStringLiteral("unreadable"));
    });
    process->start(m_helper, {QStringLiteral("scan"), root});
}

qint64 WindowsImport::selectedBytes() const
{
    return selection(m_accounts).value(QStringLiteral("bytes")).toLongLong();
}

int WindowsImport::selectedFiles() const
{
    return selection(m_accounts).value(QStringLiteral("files")).toInt();
}

qint64 WindowsImport::freeBytes() const
{
    QString at = destinations().value(QStringLiteral("documents")).toString();
    while (!at.isEmpty() && !QFileInfo::exists(at) && at != QLatin1String("/"))
        at = QFileInfo(at).path();
    QStorageInfo storage(at.isEmpty() ? QDir::homePath() : at);
    return storage.isValid() ? storage.bytesAvailable() : 0;
}

void WindowsImport::setFolderChecked(int account, int folder, bool checked)
{
    if (account < 0 || account >= m_accounts.size())
        return;
    if (m_refusal == QLatin1String("room"))
        setStage(m_stage);
    QVariantMap map = m_accounts[account].toMap();
    QVariantList folders = map.value(QStringLiteral("folders")).toList();
    if (folder < 0 || folder >= folders.size())
        return;
    QVariantMap item = folders[folder].toMap();
    item.insert(QStringLiteral("checked"), checked);
    folders[folder] = item;
    map.insert(QStringLiteral("folders"), folders);
    m_accounts[account] = map;
    Q_EMIT accountsChanged();
}

void WindowsImport::setWallpaperChecked(int account, bool checked)
{
    if (account < 0 || account >= m_accounts.size())
        return;
    // one wallpaper at a time: ticking one unticks the others
    for (int i = 0; i < m_accounts.size(); ++i) {
        QVariantMap map = m_accounts[i].toMap();
        map.insert(QStringLiteral("wallpaperChecked"), i == account ? checked : false);
        m_accounts[i] = map;
    }
    Q_EMIT accountsChanged();
}

void WindowsImport::setBookmarksChecked(int account, bool checked)
{
    if (account < 0 || account >= m_accounts.size())
        return;
    QVariantMap map = m_accounts[account].toMap();
    map.insert(QStringLiteral("bookmarksChecked"), checked);
    m_accounts[account] = map;
    Q_EMIT accountsChanged();
}

void WindowsImport::start()
{
    if (m_stage != QLatin1String("choose") || m_copy)
        return;
    const QVariantMap chosen = selection(m_accounts);
    const QVariantMap planMap = plan(m_root, m_accounts, destinations(), QLocale().name().left(2), m_source);
    if (planMap.value(QStringLiteral("accounts")).toList().isEmpty())
        return;
    m_needed = chosen.value(QStringLiteral("bytes")).toLongLong();
    if (m_needed + kMargin > freeBytes()) {
        // said on the page of choices, where folders can be unticked
        setStage(QStringLiteral("choose"), QStringLiteral("room"));
        return;
    }
    QDir().mkpath(stateDir());
    QFile::setPermissions(stateDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile file(stateDir() + QStringLiteral("/plan-new.json"));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject::fromVariantMap(planMap)).toJson()) < 0 || !file.commit()) {
        setStage(QStringLiteral("refused"), QStringLiteral("failed"));
        return;
    }
    m_resumedBytes = 0;
    runCopy(file.fileName());
}

void WindowsImport::resume()
{
    if (m_copy)
        return;
    QFile file(stateDir() + QStringLiteral("/plan.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        m_canResume = false;
        Q_EMIT stageChanged();
        return;
    }
    QVariantMap planMap = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    file.close();
    planMap.insert(QStringLiteral("resume"), true);
    m_source = planMap.value(QStringLiteral("source")).toMap();
    m_sourceName = m_source.value(QStringLiteral("label")).toString();
    if (m_sourceName.isEmpty())
        m_sourceName = QFileInfo(m_source.value(QStringLiteral("folder")).toString()).fileName();
    m_resumedBytes = m_doneBytes;

    auto go = [this, planMap](const QString &root) mutable {
        planMap.insert(QStringLiteral("root"), root);
        QSaveFile out(stateDir() + QStringLiteral("/plan-new.json"));
        if (!out.open(QIODevice::WriteOnly)
            || out.write(QJsonDocument(QJsonObject::fromVariantMap(planMap)).toJson()) < 0 || !out.commit()) {
            release();
            setStage(QStringLiteral("refused"), QStringLiteral("failed"));
            return;
        }
        m_root = root;
        runCopy(out.fileName());
    };
    const QString device = m_source.value(QStringLiteral("device")).toString();
    const QString root = planMap.value(QStringLiteral("root")).toString();
    const bool held = root.startsWith(QStringLiteral("/run/raye-windows-import/"));
    if (!device.isEmpty() && (held || !QFileInfo(root).isDir())) {
        // the Windows drive was let go of when the copy stopped: open it again
        hold(device, go);
        return;
    }
    if (!QFileInfo(root).isDir()) {
        setStage(QStringLiteral("refused"), QStringLiteral("gone"));
        return;
    }
    go(root);
}

void WindowsImport::runCopy(const QString &planFile)
{
    m_copy = new QProcess(this);
    m_copy->setEnvironment(helperEnvironment());
    m_copy->setProcessChannelMode(QProcess::SeparateChannels);
    m_copyBuffer.clear();
    m_report.clear();
    m_doneBytes = 0;
    m_doneFiles = 0;
    m_secondsLeft = -1;
    m_currentFolder.clear();
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    Q_EMIT progressChanged();
    QProcess *process = m_copy;
    connect(process, &QProcess::readyReadStandardOutput, this, [=, this] {
        m_copyBuffer += process->readAllStandardOutput();
        int end;
        while ((end = m_copyBuffer.indexOf('\n')) >= 0) {
            copyLine(m_copyBuffer.left(end));
            m_copyBuffer.remove(0, end + 1);
        }
    });
    connect(process, &QProcess::finished, this, [=, this] {
        if (m_copy == process)
            m_copy = nullptr;
        process->deleteLater();
        // ended without a word: a crash, picked up next time like a stop
        if (m_stage == QLatin1String("copying")) {
            m_canResume = true;
            setStage(QStringLiteral("stopped"));
        }
        release();
    });
    connect(process, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_copy == process)
            m_copy = nullptr;
        process->deleteLater();
        release();
        setStage(QStringLiteral("refused"), QStringLiteral("failed"));
    });
    m_canResume = false;
    setStage(QStringLiteral("copying"));
    process->start(m_helper, {QStringLiteral("copy"), planFile});
}

void WindowsImport::copyLine(const QByteArray &line)
{
    const QVariantMap event = QJsonDocument::fromJson(line).object().toVariantMap();
    const QString kind = event.value(QStringLiteral("event")).toString();
    if (kind == QLatin1String("total")) {
        m_totalBytes = event.value(QStringLiteral("bytes")).toLongLong();
        m_totalFiles = event.value(QStringLiteral("files")).toInt();
        Q_EMIT progressChanged();
    } else if (kind == QLatin1String("progress")) {
        m_doneBytes = event.value(QStringLiteral("bytes")).toLongLong();
        m_doneFiles = event.value(QStringLiteral("files")).toInt();
        // what a copy picking up finds already there is not its pace
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startedAt;
        m_secondsLeft = WindowsImportData::secondsLeft(m_doneBytes - qMin(m_doneBytes, m_resumedBytes),
                                                       m_totalBytes - qMin(m_doneBytes, m_resumedBytes), elapsed);
        Q_EMIT progressChanged();
    } else if (kind == QLatin1String("folder")) {
        m_currentFolder = event.value(QStringLiteral("kind")).toString();
        Q_EMIT progressChanged();
    } else if (kind == QLatin1String("done")) {
        m_report = event.value(QStringLiteral("report")).toMap();
        m_doneBytes = m_totalBytes;
        m_doneFiles = m_totalFiles;
        m_secondsLeft = 0;
        Q_EMIT progressChanged();
        markOffered();
        setStage(QStringLiteral("done"));
    } else if (kind == QLatin1String("stopped")) {
        m_report = event.value(QStringLiteral("report")).toMap();
        m_canResume = true;
        setStage(QStringLiteral("stopped"));
    } else if (kind == QLatin1String("refused")) {
        m_needed = event.value(QStringLiteral("needed")).toLongLong();
        setStage(QStringLiteral("refused"), event.value(QStringLiteral("reason")).toString());
    } else if (kind == QLatin1String("error")) {
        setStage(QStringLiteral("refused"), QStringLiteral("failed"));
    }
}

void WindowsImport::stop()
{
    if (m_copy)
        m_copy->terminate();
}

void WindowsImport::back()
{
    if (m_copy)
        return;
    release();
    m_accounts.clear();
    m_root.clear();
    m_report.clear();
    Q_EMIT accountsChanged();
    setStage(QStringLiteral("pick"));
    readState();
    refresh();
}

void WindowsImport::showInFiles(const QString &path)
{
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString WindowsImport::formatSize(qint64 bytes) const
{
    if (bytes < 0)
        return {};
    const QStringList units{i18nc("@label size unit, bytes", "B"), i18nc("@label size unit", "KB"),
                            i18nc("@label size unit", "MB"), i18nc("@label size unit", "GB"),
                            i18nc("@label size unit", "TB")};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1000 && unit < units.size() - 1) {
        value /= 1024;
        ++unit;
    }
    const int decimals = unit == 0 || value >= 100 ? 0 : value >= 10 ? 1 : 2;
    QString number = QLocale().toString(value, 'f', decimals);
    if (decimals > 0) {
        const QChar point = QLocale().decimalPoint().at(0);
        while (number.endsWith(QLatin1Char('0')))
            number.chop(1);
        if (number.endsWith(point))
            number.chop(1);
    }
    return number + QLatin1Char(' ') + units[unit];
}

bool WindowsImport::waitFor(const std::function<bool()> &done, int msecs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > msecs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

int WindowsImport::offerAtSignIn()
{
    // never from the USB stick: Setup is the place to ask there
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (cmdline.open(QIODevice::ReadOnly) && cmdline.readAll().contains("boot=casper"))
        return 0;
    {
        KConfig config(QStringLiteral("raye-settingsrc"), KConfig::SimpleConfig);
        if (config.group(QStringLiteral("WindowsImport")).readEntry("Offered", false))
            return 0;
    }
    QProcess list;
    list.start(libexec("windows-import"), {QStringLiteral("volumes")});
    if (!list.waitForFinished(30000) || !offerable(parseVolumes(list.readAllStandardOutput())))
        return 0;
    markOffered();

    QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"),
                                 QStringLiteral("/org/freedesktop/Notifications"),
                                 QStringLiteral("org.freedesktop.Notifications"));
    const QStringList actions{QStringLiteral("default"), QString(), QStringLiteral("open"),
                              i18nc("@action:button", "Bring my files"), QStringLiteral("later"),
                              i18nc("@action:button", "Not now")};
    const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("org.raye.settings")}};
    const QDBusReply<uint> reply = notifications.call(
        QStringLiteral("Notify"), i18nc("@title", "Settings"), uint(0), QStringLiteral("org.raye.settings"),
        i18nc("@title notification", "Bring your files from Windows"),
        i18nc("@info notification", "Windows is on this PC. Copy your documents, pictures and more to Raye. Windows stays as it is."),
        actions, hints, int(0));
    if (!reply.isValid())
        return 1;
    const uint id = reply.value();

    QEventLoop loop;
    auto *listener = new WindowsImportOfferListener(id, &loop);
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("ActionInvoked"), listener,
                SLOT(actionInvoked(uint, QString)));
    bus.connect(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("NotificationClosed"), listener,
                SLOT(closed(uint, uint)));
    // the notification stays in the history; nobody waits on it all day
    QTimer::singleShot(30 * 60 * 1000, &loop, &QEventLoop::quit);
    loop.exec();
    if (listener->opened)
        QProcess::startDetached(QStringLiteral("raye-settings"), {QStringLiteral("accounts/windows-files")});
    return 0;
}

WindowsImportOfferListener::WindowsImportOfferListener(uint id, QEventLoop *loop)
    : QObject(loop)
    , m_id(id)
    , m_loop(loop)
{
}

void WindowsImportOfferListener::actionInvoked(uint id, const QString &action)
{
    if (id != m_id)
        return;
    opened = action == QLatin1String("open") || action == QLatin1String("default");
    m_loop->quit();
}

void WindowsImportOfferListener::closed(uint id, uint)
{
    if (id == m_id)
        m_loop->quit();
}
