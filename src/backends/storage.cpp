// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <fts.h>
#include <sys/stat.h>

#include <algorithm>
#include <set>
#include <utility>

namespace
{
const QStringList kCleanupItems{QStringLiteral("trash"), QStringLiteral("thumbnails"), QStringLiteral("apt"),
                                QStringLiteral("logs"), QStringLiteral("snapshots")};
const QString kTimer = QStringLiteral("raye-storage-sense.timer");
const QString kService = QStringLiteral("raye-storage-sense.service");
constexpr int kCacheVersion = 1;
constexpr int kHelperTimeout = 120000;

QString defaultHelper()
{
    return QStringLiteral(RAYE_LIBEXECDIR "/storage-clean");
}

// "/home/" is under "/", "/homework" is not under "/home"
bool isUnder(const QString &path, const QString &mount)
{
    if (mount == QLatin1String("/"))
        return path.startsWith(QLatin1Char('/'));
    return path == mount || path.startsWith(mount + QLatin1Char('/'));
}
} // namespace

namespace StorageScan
{
Sources Sources::forThisUser()
{
    Sources s;
    s.home = QDir::homePath();
    const QList<QPair<QString, QStandardPaths::StandardLocation>> folders{
        {QStringLiteral("documents"), QStandardPaths::DocumentsLocation},
        {QStringLiteral("pictures"), QStandardPaths::PicturesLocation},
        {QStringLiteral("music"), QStandardPaths::MusicLocation},
        {QStringLiteral("videos"), QStandardPaths::MoviesLocation},
        {QStringLiteral("downloads"), QStandardPaths::DownloadLocation},
        {QStringLiteral("desktop"), QStandardPaths::DesktopLocation},
    };
    for (const auto &[id, location] : folders)
        s.folders.append({id, QStandardPaths::writableLocation(location)});
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    s.flatpak = {QStringLiteral("/var/lib/flatpak"), data + QStringLiteral("/flatpak")};
    s.temporary = {{QStringLiteral("trash"), data + QStringLiteral("/Trash")},
                   {QStringLiteral("thumbnails"), cache + QStringLiteral("/thumbnails")},
                   {QStringLiteral("apt"), QStringLiteral("/var/cache/apt/archives")},
                   {QStringLiteral("logs"), QStringLiteral("/var/log")}};
    return s;
}

bool isUserVolume(const Volume &volume)
{
    static const QSet<QString> fileSystems{
        QStringLiteral("ext2"),  QStringLiteral("ext3"),     QStringLiteral("ext4"),  QStringLiteral("btrfs"),
        QStringLiteral("xfs"),   QStringLiteral("f2fs"),     QStringLiteral("vfat"),  QStringLiteral("exfat"),
        QStringLiteral("ntfs"),  QStringLiteral("ntfs3"),    QStringLiteral("fuseblk"), QStringLiteral("jfs"),
        QStringLiteral("reiserfs"), QStringLiteral("bcachefs"), QStringLiteral("hfsplus"), QStringLiteral("udf"),
    };
    if (!fileSystems.contains(volume.fsType) || volume.total <= 0)
        return false;
    if (!volume.device.startsWith(QLatin1String("/dev/")) || volume.device.startsWith(QLatin1String("/dev/loop")))
        return false;
    const QString &m = volume.mountPoint;
    for (const char *hidden : {"/boot", "/efi", "/snap", "/var/snap", "/run", "/proc", "/sys"}) {
        if (isUnder(m, QLatin1String(hidden)) && !isUnder(m, QStringLiteral("/run/media")))
            return false;
    }
    return true;
}

QList<Disk> groupDisks(const QList<Volume> &volumes)
{
    QList<Disk> disks;
    for (const Volume &v : volumes) {
        if (!isUserVolume(v))
            continue;
        auto it = std::find_if(disks.begin(), disks.end(), [&](const Disk &d) { return d.id == v.device; });
        if (it == disks.end()) {
            Disk d;
            d.id = v.device;
            d.fsType = v.fsType;
            d.total = v.total;
            d.available = v.available;
            d.reserved = qMax<qint64>(0, v.free - v.available);
            disks.append(d);
            it = disks.end() - 1;
        }
        it->mountPoints.append(v.mountPoint);
    }
    for (Disk &d : disks) {
        // the main mount point: / when it is there, else the shortest
        std::sort(d.mountPoints.begin(), d.mountPoints.end(), [](const QString &a, const QString &b) {
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        });
    }
    // the system drive first, then the order of the mounts
    std::stable_sort(disks.begin(), disks.end(), [](const Disk &a, const Disk &b) {
        return a.mountPoints.contains(QStringLiteral("/")) && !b.mountPoints.contains(QStringLiteral("/"));
    });
    return disks;
}

int volumeOf(const QString &path, const QList<Volume> &volumes)
{
    if (!path.startsWith(QLatin1Char('/')))
        return -1;
    int best = -1;
    for (int i = 0; i < volumes.size(); ++i) {
        const QString &m = volumes[i].mountPoint;
        if (isUnder(path, m) && (best < 0 || m.size() > volumes[best].mountPoint.size()))
            best = i;
    }
    return best;
}

int diskOf(const QString &path, const QList<Volume> &volumes, const QList<Disk> &disks)
{
    const int v = volumeOf(path, volumes);
    if (v < 0)
        return -1;
    for (int i = 0; i < disks.size(); ++i) {
        if (disks[i].id == volumes[v].device && disks[i].mountPoints.contains(volumes[v].mountPoint))
            return i;
    }
    return -1;
}

void nameDisk(Disk &disk, const QString &label, const QString &sysRoot)
{
    if (disk.mountPoints.contains(QStringLiteral("/"))) {
        disk.kind = QStringLiteral("system");
        disk.name = QStringLiteral("Raye");
        return;
    }
    const QString main = disk.mountPoints.value(0);
    if (!main.isEmpty() && QFileInfo(main + QStringLiteral("/Windows/System32")).isDir()) {
        disk.kind = QStringLiteral("windows");
        disk.name = QStringLiteral("Windows");
        return;
    }
    // a drive on a USB bus: its block device sits under one in sysfs
    const QString block = QDir(sysRoot).filePath(QStringLiteral("sys/class/block/") + QFileInfo(disk.id).fileName());
    const QString real = QFileInfo(block).canonicalFilePath();
    const bool usb = real.contains(QRegularExpression(QStringLiteral("/usb[0-9]*/")));
    disk.kind = usb ? QStringLiteral("usb") : QStringLiteral("local");
    if (!label.trimmed().isEmpty())
        disk.name = label.trimmed();
    else
        disk.name = usb ? i18nc("@label a USB drive without a name", "USB Drive")
                        : i18nc("@label a drive without a name", "Local Disk");
}

qint64 directoryUsage(const QString &path, const std::atomic_bool *cancel)
{
    const QByteArray encoded = QFile::encodeName(path);
    struct stat top {};
    if (path.isEmpty() || ::lstat(encoded.constData(), &top) != 0 || !S_ISDIR(top.st_mode))
        return 0;
    char *roots[] = {const_cast<char *>(encoded.constData()), nullptr};
    FTS *fts = ::fts_open(roots, FTS_PHYSICAL | FTS_XDEV | FTS_NOCHDIR, nullptr);
    if (!fts)
        return 0;
    qint64 total = 0;
    std::set<std::pair<dev_t, ino_t>> seen;
    while (FTSENT *entry = ::fts_read(fts)) {
        if (cancel && cancel->load())
            break;
        switch (entry->fts_info) {
        case FTS_DP:
        case FTS_DNR:
        case FTS_ERR:
        case FTS_NS:
        case FTS_DC:
            continue;
        default:
            break;
        }
        const struct stat *st = entry->fts_statp;
        if (!st)
            continue;
        if (!S_ISDIR(st->st_mode) && st->st_nlink > 1 && !seen.insert({st->st_dev, st->st_ino}).second)
            continue;
        total += qint64(st->st_blocks) * 512;
    }
    ::fts_close(fts);
    return total;
}

PackageSizes packageSizes(const QString &statusFile, const QString &infoDir, const std::atomic_bool *cancel)
{
    PackageSizes sizes;
    QFile file(statusFile);
    if (!file.open(QIODevice::ReadOnly))
        return sizes;

    // a package that puts an entry in the menu is an app
    auto isApp = [&](const QString &package, const QString &arch) {
        QStringList names{package + QStringLiteral(".list")};
        if (!arch.isEmpty())
            names.prepend(package + QLatin1Char(':') + arch + QStringLiteral(".list"));
        for (const QString &name : names) {
            QFile list(infoDir + QLatin1Char('/') + name);
            if (!list.open(QIODevice::ReadOnly))
                continue;
            while (!list.atEnd()) {
                const QByteArray line = list.readLine().trimmed();
                if (line.startsWith("/usr/share/applications/") && line.endsWith(".desktop"))
                    return true;
            }
            return false;
        }
        return false;
    };

    QString package, arch;
    qint64 kib = 0;
    bool installed = false;
    auto flush = [&] {
        if (!package.isEmpty() && installed && kib > 0) {
            if (isApp(package, arch)) {
                sizes.apps += kib * 1024;
                ++sizes.appPackages;
            } else {
                sizes.system += kib * 1024;
            }
        }
        package.clear();
        arch.clear();
        kib = 0;
        installed = false;
    };
    while (!file.atEnd()) {
        if (cancel && cancel->load())
            break;
        const QByteArray line = file.readLine();
        if (line.trimmed().isEmpty()) {
            flush();
            continue;
        }
        if (line.startsWith(' ') || line.startsWith('\t'))
            continue;
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray key = line.left(colon);
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (key == "Package")
            package = QString::fromUtf8(value);
        else if (key == "Architecture")
            arch = QString::fromUtf8(value);
        else if (key == "Status")
            installed = value.endsWith(" installed");
        else if (key == "Installed-Size")
            kib = value.toLongLong();
    }
    flush();
    return sizes;
}

HelperResult parseHelper(const QByteArray &output, const QString &kind)
{
    HelperResult result;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> f = line.split('\t');
        if (f.size() == 3 && f[0] == "error" && kCleanupItems.contains(QString::fromLatin1(f[1]))) {
            result.errors.append(QString::fromLatin1(f[1]) + QLatin1Char(' ') + QString::fromLatin1(f[2]));
            continue;
        }
        if (f.size() != 4 || QString::fromLatin1(f[0]) != kind || !kCleanupItems.contains(QString::fromLatin1(f[1])))
            continue;
        bool okBytes = false, okCount = false;
        const qint64 bytes = f[2].toLongLong(&okBytes);
        const qint64 count = f[3].toLongLong(&okCount);
        if (okBytes && okCount)
            result.items.insert(QString::fromLatin1(f[1]), {qMax<qint64>(-1, bytes), qMax<qint64>(-1, count)});
    }
    return result;
}

QHash<QString, QList<Category>> categorize(const QList<Volume> &volumes, const QList<Disk> &disks,
                                           const QList<Piece> &pieces)
{
    QHash<QString, QList<Category>> result;
    QVector<QHash<QString, Category>> byDisk(disks.size());
    for (const Piece &piece : pieces) {
        const int d = diskOf(piece.path, volumes, disks);
        if (d < 0 || piece.bytes <= 0)
            continue;
        Category &c = byDisk[d][piece.category];
        c.id = piece.category;
        c.bytes += piece.bytes;
        // a folder of the user's opens; apps and temporary files go
        // to their own pages
        static const QSet<QString> folders{QStringLiteral("documents"), QStringLiteral("pictures"),
                                           QStringLiteral("music"),     QStringLiteral("videos"),
                                           QStringLiteral("downloads"), QStringLiteral("desktop")};
        if (folders.contains(piece.category) && c.path.isEmpty())
            c.path = piece.path;
    }
    for (int d = 0; d < disks.size(); ++d) {
        QHash<QString, Category> &cats = byDisk[d];
        if (disks[d].reserved > 0) {
            Category &system = cats[QStringLiteral("system")];
            system.id = QStringLiteral("system");
            system.bytes += disks[d].reserved;
        }
        qint64 counted = 0;
        for (const Category &c : std::as_const(cats))
            counted += c.bytes;
        QList<Category> list = cats.values();
        list.append(Category{QStringLiteral("other"), qMax<qint64>(0, disks[d].used() - counted), {}});
        std::sort(list.begin(), list.end(), [](const Category &a, const Category &b) {
            return a.bytes != b.bytes ? a.bytes > b.bytes : a.id < b.id;
        });
        result.insert(disks[d].id, list);
    }
    return result;
}
} // namespace StorageScan

using namespace StorageScan;

struct Storage::ScanResult {
    QList<Disk> disks;
    QHash<QString, QList<Category>> categories;
};

Storage::Storage(QObject *parent)
    : QObject(parent)
    , m_helper(defaultHelper())
{
    m_cacheFile = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                + QStringLiteral("/raye-settings/storage.json");
    m_configFile = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                 + QStringLiteral("/raye-settingsrc");
    m_unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/systemd/user");
    readSense();
    // asked once the page has set everything up
    QTimer::singleShot(0, this, &Storage::querySense);
}

Storage::~Storage()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_thread) {
        m_thread->disconnect(this);
        // a count stops at the next file it looks at
        m_thread->wait();
        delete m_thread;
    }
    // the helper, pkexec and systemctl: nobody is left to hear them
    const auto processes = findChildren<QProcess *>();
    for (QProcess *p : processes) {
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(2000);
        }
    }
}

void Storage::setVolumes(const QList<Volume> &volumes)
{
    m_volumes = volumes;
    m_volumesSet = true;
}

void Storage::setSources(const Sources &sources)
{
    m_sources = sources;
    m_sourcesSet = true;
}

QList<Volume> Storage::currentVolumes() const
{
    if (m_volumesSet)
        return m_volumes;
    QList<Volume> volumes;
    const auto mounted = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &info : mounted) {
        if (!info.isValid() || !info.isReady())
            continue;
        Volume v;
        v.mountPoint = info.rootPath();
        v.device = QString::fromLocal8Bit(info.device());
        v.fsType = QString::fromLatin1(info.fileSystemType());
        v.label = info.name();
        v.total = info.bytesTotal();
        v.available = info.bytesAvailable();
        v.free = info.bytesFree();
        v.readOnly = info.isReadOnly();
        volumes.append(v);
    }
    return volumes;
}

Sources Storage::currentSources() const
{
    return m_sourcesSet ? m_sources : Sources::forThisUser();
}

// Pictures of the page (the reference renders) need drives that look
// like a real PC's: RAYE_STORAGE_PREVIEW names a JSON file with
// { "disks": [...], "cleanup": [...] } in the shape of the properties.
// Nothing is counted and nothing can be cleaned then.
QJsonObject Storage::preview()
{
    const QString path = qEnvironmentVariable("RAYE_STORAGE_PREVIEW");
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void Storage::refresh()
{
    const QJsonObject fixture = preview();
    if (!fixture.isEmpty()) {
        m_disks = fixture.value(QStringLiteral("disks")).toArray().toVariantList();
        Q_EMIT disksChanged();
        return;
    }
    const QList<Volume> volumes = currentVolumes();
    QList<Disk> disks = groupDisks(volumes);
    for (Disk &d : disks) {
        QString label;
        for (const Volume &v : volumes) {
            if (v.device == d.id && v.mountPoint == d.mountPoints.value(0))
                label = v.label;
        }
        nameDisk(d, label, m_sysRoot);
    }
    // the sizes of the drives are known now; what fills them comes from
    // the last count until the new one is done
    publish(disks, {}, false);
    loadCache();

    if (m_thread)
        return; // the count running now publishes when it is done
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    const Sources sources = currentSources();
    const QString helper = m_helper;
    auto result = std::make_shared<ScanResult>();
    result->disks = disks;

    m_thread = QThread::create([=] {
        QList<Piece> pieces;
        // the user's folders, each once, never the home itself
        QSet<QString> done;
        const QString home = QDir::cleanPath(sources.home);
        for (const auto &[id, folder] : sources.folders) {
            const QString path = QDir::cleanPath(folder);
            if (folder.isEmpty() || path == home || done.contains(path))
                continue;
            done.insert(path);
            pieces.append({id, path, directoryUsage(path, cancel.get())});
        }
        for (const QString &dir : sources.flatpak)
            pieces.append({QStringLiteral("apps"), dir, directoryUsage(dir, cancel.get())});
        const PackageSizes packages = packageSizes(sources.dpkgStatus, sources.dpkgInfo, cancel.get());
        pieces.append({QStringLiteral("apps"), sources.packages, packages.apps});
        pieces.append({QStringLiteral("system"), sources.packages, packages.system});

        // what Clean up could free, measured by the helper itself
        if (!cancel->load()) {
            QProcess process;
            process.start(helper, {QStringLiteral("trash"), QStringLiteral("thumbnails"), QStringLiteral("apt"),
                                   QStringLiteral("logs")});
            // a page closed meanwhile does not wait for the helper
            QElapsedTimer waited;
            waited.start();
            while (!process.waitForFinished(200)) {
                if (cancel->load() || waited.elapsed() > kHelperTimeout || process.state() == QProcess::NotRunning)
                    break;
            }
            if (process.state() == QProcess::NotRunning && !cancel->load()) {
                const HelperResult r = parseHelper(process.readAllStandardOutput(), QStringLiteral("size"));
                for (auto it = r.items.cbegin(); it != r.items.cend(); ++it) {
                    const QString path = sources.temporary.value(it.key());
                    if (!path.isEmpty())
                        pieces.append({QStringLiteral("temporary"), path, it.value().first});
                }
            } else {
                process.kill();
                process.waitForFinished(1000);
            }
        }
        QList<Volume> vols = volumes;
        result->categories = categorize(vols, result->disks, pieces);
    });
    connect(m_thread, &QThread::finished, this, [this, result] { scanDone(result); });
    m_scanning = true;
    Q_EMIT scanningChanged();
    m_thread->start(QThread::LowPriority);
}

void Storage::scanDone(const std::shared_ptr<ScanResult> &result)
{
    m_thread->deleteLater();
    m_thread = nullptr;
    const bool cancelled = m_cancel && m_cancel->load();
    m_cancel.reset();
    m_scanning = false;
    if (!cancelled) {
        publish(result->disks, result->categories, true);
        saveCache();
    }
    Q_EMIT scanningChanged();
    Q_EMIT scanFinished();
}

bool Storage::waitForScan(int msecs)
{
    if (!m_thread)
        return true;
    QElapsedTimer timer;
    timer.start();
    while (m_thread && timer.elapsed() < msecs) {
        m_thread->wait(20);
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
    return !m_thread;
}

void Storage::publish(const QList<Disk> &disks, const QHash<QString, QList<Category>> &categories, bool counted)
{
    // the drives shown in full: the system's, and the one the home is on
    const int homeDisk = diskOf(QDir::cleanPath(currentSources().home), currentVolumes(), disks);
    QVariantList list;
    for (int i = 0; i < disks.size(); ++i) {
        const Disk &d = disks[i];
        QVariantList cats;
        for (const Category &c : categories.value(d.id)) {
            cats.append(QVariantMap{{QStringLiteral("id"), c.id},
                                    {QStringLiteral("bytes"), c.bytes},
                                    {QStringLiteral("path"), c.path}});
        }
        list.append(QVariantMap{
            {QStringLiteral("id"), d.id},
            {QStringLiteral("name"), d.name},
            {QStringLiteral("kind"), d.kind},
            {QStringLiteral("primary"), d.kind == QLatin1String("system") || i == homeDisk},
            {QStringLiteral("mountPoint"), d.mountPoints.value(0)},
            {QStringLiteral("fsType"), d.fsType},
            {QStringLiteral("total"), d.total},
            {QStringLiteral("used"), d.used()},
            {QStringLiteral("free"), d.available},
            {QStringLiteral("categories"), cats},
            {QStringLiteral("counted"), counted && categories.contains(d.id)},
        });
    }
    m_disks = list;
    Q_EMIT disksChanged();
}

// The last count, for the drives that are still here: categories only,
// the sizes of the drives are always the ones of now.
void Storage::loadCache()
{
    QFile file(m_cacheFile);
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != kCacheVersion)
        return;
    const QJsonObject drives = root.value(QStringLiteral("drives")).toObject();
    bool changed = false;
    for (QVariant &value : m_disks) {
        QVariantMap disk = value.toMap();
        const QJsonArray cached = drives.value(disk.value(QStringLiteral("id")).toString()).toArray();
        if (cached.isEmpty() || !disk.value(QStringLiteral("categories")).toList().isEmpty())
            continue;
        QVariantList cats;
        for (const QJsonValue &c : cached) {
            const QJsonObject o = c.toObject();
            cats.append(QVariantMap{{QStringLiteral("id"), o.value(QStringLiteral("id")).toString()},
                                    {QStringLiteral("bytes"), qint64(o.value(QStringLiteral("bytes")).toDouble())},
                                    {QStringLiteral("path"), o.value(QStringLiteral("path")).toString()}});
        }
        disk.insert(QStringLiteral("categories"), cats);
        value = disk;
        changed = true;
    }
    if (changed)
        Q_EMIT disksChanged();
}

void Storage::saveCache() const
{
    QJsonObject drives;
    for (const QVariant &value : m_disks) {
        const QVariantMap disk = value.toMap();
        QJsonArray cats;
        for (const QVariant &c : disk.value(QStringLiteral("categories")).toList()) {
            const QVariantMap m = c.toMap();
            cats.append(QJsonObject{{QStringLiteral("id"), m.value(QStringLiteral("id")).toString()},
                                    {QStringLiteral("bytes"), double(m.value(QStringLiteral("bytes")).toLongLong())},
                                    {QStringLiteral("path"), m.value(QStringLiteral("path")).toString()}});
        }
        drives.insert(disk.value(QStringLiteral("id")).toString(), cats);
    }
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile file(m_cacheFile);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), kCacheVersion}, {QStringLiteral("drives"), drives}})
                   .toJson(QJsonDocument::Compact));
    file.commit();
}

bool Storage::isSystemItem(const QString &item)
{
    return item == QLatin1String("apt") || item == QLatin1String("logs") || item == QLatin1String("snapshots");
}

qint64 Storage::cleanupTotal() const
{
    qint64 total = 0;
    for (const QVariant &item : m_cleanupItems)
        total += qMax<qint64>(0, item.toMap().value(QStringLiteral("bytes")).toLongLong());
    return total;
}

void Storage::estimateCleanup()
{
    if (m_estimate)
        return;
    const QJsonObject fixture = preview();
    if (!fixture.isEmpty()) {
        m_cleanupItems = fixture.value(QStringLiteral("cleanup")).toArray().toVariantList();
        Q_EMIT cleanupChanged();
        return;
    }
    m_estimate = new QProcess(this);
    connect(m_estimate, &QProcess::finished, this, [this] {
        const HelperResult r = parseHelper(m_estimate->readAllStandardOutput(), QStringLiteral("size"));
        m_estimate->deleteLater();
        m_estimate = nullptr;
        QVariantList items;
        for (const QString &id : kCleanupItems) {
            if (!r.items.contains(id))
                continue;
            items.append(QVariantMap{{QStringLiteral("id"), id},
                                     {QStringLiteral("bytes"), r.items.value(id).first},
                                     {QStringLiteral("count"), r.items.value(id).second},
                                     {QStringLiteral("system"), isSystemItem(id)}});
        }
        m_cleanupItems = items;
        Q_EMIT cleanupChanged();
    });
    connect(m_estimate, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_estimate) {
            m_estimate->deleteLater();
            m_estimate = nullptr;
            m_cleanupItems.clear();
            Q_EMIT cleanupChanged();
        }
    });
    m_estimate->start(m_helper, kCleanupItems);
    Q_EMIT cleanupChanged();
}

void Storage::clean(const QStringList &items)
{
    if (m_cleaning)
        return;
    QStringList user, system;
    for (const QString &id : kCleanupItems) {
        if (!items.contains(id))
            continue;
        (isSystemItem(id) ? system : user).append(id);
    }
    if ((user.isEmpty() && system.isEmpty()) || !preview().isEmpty()) {
        Q_EMIT cleanupFinished(0, QString());
        return;
    }
    m_cleaning = true;
    Q_EMIT cleaningChanged();
    runClean(user, system, 0, QString());
}

// The user's things first, as the user; then the system's, through
// pkexec, which asks for the password once.
void Storage::runClean(QStringList user, QStringList system, qint64 freed, const QString &problem)
{
    const bool asUser = !user.isEmpty();
    if (!asUser && system.isEmpty()) {
        finishClean(freed, problem);
        return;
    }
    const QStringList items = asUser ? user : system;
    QString program = m_helper;
    QStringList args{QStringLiteral("--apply")};
    args += items;
    if (!asUser) {
        args.prepend(m_helper);
        program = m_pkexec;
    }
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        const HelperResult r = parseHelper(process->readAllStandardOutput(), QStringLiteral("freed"));
        qint64 total = freed;
        for (const auto &value : r.items)
            total += qMax<qint64>(0, value.first);
        QString next = problem;
        if (!asUser && status == QProcess::NormalExit && (code == 126 || code == 127) && r.items.isEmpty())
            next = QStringLiteral("cancelled");
        else if (status != QProcess::NormalExit)
            next = QStringLiteral("partial");
        for (const QString &error : r.errors) {
            const QString reason = error.section(QLatin1Char(' '), 1);
            if (next.isEmpty() || reason == QLatin1String("busy"))
                next = reason == QLatin1String("missing") ? next : reason;
        }
        if (asUser)
            runClean({}, system, total, next);
        else
            finishClean(total, next);
    });
    connect(process, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        if (asUser)
            runClean({}, system, freed, QStringLiteral("partial"));
        else
            finishClean(freed, QStringLiteral("denied"));
    });
    process->start(program, args);
}

void Storage::finishClean(qint64 freed, const QString &problem)
{
    m_cleaning = false;
    Q_EMIT cleaningChanged();
    Q_EMIT cleanupFinished(freed, problem);
}

// --- Storage Sense -------------------------------------------------------

void Storage::setConfigFile(const QString &path)
{
    m_configFile = path;
    readSense();
    querySense();
}

void Storage::querySense()
{
    systemctl({QStringLiteral("--user"), QStringLiteral("is-enabled"), kTimer}, [this](int, const QByteArray &out) {
        const bool on = out.trimmed() == "enabled";
        if (on != m_senseEnabled) {
            m_senseEnabled = on;
            Q_EMIT senseChanged();
        }
    });
    systemctl({QStringLiteral("--user"), QStringLiteral("show"), kTimer, QStringLiteral("--property=LastTriggerUSec"),
               QStringLiteral("--value"), QStringLiteral("--timestamp=unix")},
              [this](int, const QByteArray &out) {
                  const QByteArray v = out.trimmed();
                  bool ok = false;
                  const qint64 secs = v.startsWith('@') ? v.mid(1).toLongLong(&ok) : 0;
                  const QDateTime when = ok && secs > 0 ? QDateTime::fromSecsSinceEpoch(secs) : QDateTime();
                  if (when != m_senseLastRun) {
                      m_senseLastRun = when;
                      Q_EMIT senseChanged();
                  }
              });
}

void Storage::readSense()
{
    const KConfigGroup group = KSharedConfig::openConfig(m_configFile, KConfig::SimpleConfig)->group(QStringLiteral("StorageSense"));
    const int days = group.readEntry("TrashDays", 30);
    m_senseTrashDays = days >= 0 && days <= 9999 ? days : 30;
    m_senseThumbnails = group.readEntry("Thumbnails", true);
    const QString frequency = group.readEntry("Frequency", QStringLiteral("week"));
    m_senseFrequency = senseCalendar(frequency).isEmpty() ? QStringLiteral("week") : frequency;
    const int downloads = group.readEntry("DownloadsDays", 0);
    m_senseDownloadsDays = downloads >= 0 && downloads <= 9999 ? downloads : 0;
    Q_EMIT senseChanged();
}

void Storage::writeSense()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(m_configFile, KConfig::SimpleConfig);
    config->reparseConfiguration();
    KConfigGroup group = config->group(QStringLiteral("StorageSense"));
    group.writeEntry("TrashDays", m_senseTrashDays);
    group.writeEntry("Thumbnails", m_senseThumbnails);
    group.writeEntry("Frequency", m_senseFrequency);
    group.writeEntry("DownloadsDays", m_senseDownloadsDays);
    config->sync();
}

void Storage::setSenseEnabled(bool on)
{
    if (on == m_senseEnabled)
        return;
    m_senseEnabled = on;
    Q_EMIT senseChanged();
    systemctl({QStringLiteral("--user"), on ? QStringLiteral("enable") : QStringLiteral("disable"), QStringLiteral("--now"), kTimer});
}

void Storage::setSenseTrashDays(int days)
{
    if (days < 0 || days > 9999 || days == m_senseTrashDays)
        return;
    m_senseTrashDays = days;
    writeSense();
    Q_EMIT senseChanged();
}

void Storage::setSenseThumbnails(bool on)
{
    if (on == m_senseThumbnails)
        return;
    m_senseThumbnails = on;
    writeSense();
    Q_EMIT senseChanged();
}

void Storage::setSenseFrequency(const QString &frequency)
{
    const QString calendar = senseCalendar(frequency);
    if (calendar.isEmpty() || frequency == m_senseFrequency)
        return;
    m_senseFrequency = frequency;
    writeSense();
    Q_EMIT senseChanged();

    // the empty OnCalendar= drops the weekly one of the shipped timer
    QDir().mkpath(m_unitDir + QStringLiteral("/raye-storage-sense.timer.d"));
    QSaveFile file(m_unitDir + QStringLiteral("/raye-storage-sense.timer.d/frequency.conf"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write("# Settings > System > Storage > Storage Sense: Run Storage Sense\n[Timer]\nOnCalendar=\nOnCalendar=");
        file.write(calendar.toLatin1() + "\n");
        file.commit();
    }
    systemctl({QStringLiteral("--user"), QStringLiteral("daemon-reload")}, [this](int, const QByteArray &) {
        if (m_senseEnabled)
            systemctl({QStringLiteral("--user"), QStringLiteral("restart"), kTimer});
    });
}

void Storage::setSenseDownloadsDays(int days)
{
    if (days < 0 || days > 9999 || days == m_senseDownloadsDays)
        return;
    m_senseDownloadsDays = days;
    writeSense();
    Q_EMIT senseChanged();
}

QString Storage::senseCalendar(const QString &frequency)
{
    if (frequency == QLatin1String("day"))
        return QStringLiteral("daily");
    if (frequency == QLatin1String("week"))
        return QStringLiteral("weekly");
    if (frequency == QLatin1String("month"))
        return QStringLiteral("monthly");
    if (frequency == QLatin1String("lowspace"))
        return QStringLiteral("hourly");
    return {};
}

void Storage::runSenseNow()
{
    if (m_senseRunning)
        return;
    m_senseRunning = true;
    Q_EMIT senseChanged();
    systemctl({QStringLiteral("--user"), QStringLiteral("start"), kService}, [this](int, const QByteArray &) {
        m_senseRunning = false;
        m_senseLastRun = QDateTime::currentDateTime();
        Q_EMIT senseChanged();
        refresh();
    });
}

void Storage::systemctl(const QStringList &args, std::function<void(int, const QByteArray &)> done)
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus) {
        process->deleteLater();
        if (done)
            done(code, process->readAllStandardOutput());
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        if (done)
            done(-1, {});
    });
    process->start(m_systemctl, args);
}

// --- small things --------------------------------------------------------

bool Storage::disksToolAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("partitionmanager")).isEmpty();
}

void Storage::openDisksTool()
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("partitionmanager"));
    if (!tool.isEmpty())
        QProcess::startDetached(tool, {});
}

void Storage::openFolder(const QString &path)
{
    // only the folders this page showed
    for (const QVariant &disk : std::as_const(m_disks)) {
        for (const QVariant &c : disk.toMap().value(QStringLiteral("categories")).toList()) {
            if (!path.isEmpty() && c.toMap().value(QStringLiteral("path")).toString() == path) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                return;
            }
        }
    }
}

QString Storage::formatSize(qint64 bytes) const
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
    // three figures, the way Windows shows sizes: 1.24 GB, 45.3 GB, 512 MB
    const int decimals = unit == 0 || value >= 100 ? 0 : value >= 10 ? 1 : 2;
    QString number = QLocale().toString(value, 'f', decimals);
    if (decimals > 0) {
        const QChar point = QLocale().decimalPoint().at(0);
        while (number.endsWith(QLatin1Char('0')))
            number.chop(1);
        if (number.endsWith(point))
            number.chop(1);
    }
    return i18nc("@label a size: number and unit", "%1 %2", number, units[unit]);
}
