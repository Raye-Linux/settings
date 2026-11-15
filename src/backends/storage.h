// SPDX-License-Identifier: GPL-3.0-or-later
// System > Storage: how full each drive is and what fills it, the
// clean-up of files nobody needs, and Storage Sense.
//
// The drives come from the mounted file systems (statvfs). What fills
// them is counted in a thread of its own, folder by folder: the user's
// folders, the apps (Flatpak and the packages that put an app in the
// menu), the system (every other package), the files Clean up can free.
// The last count is kept in ~/.cache/raye-settings/storage.json, so the
// page shows it at once and replaces it when the new count is done.
//
// Clean up runs data/libexec/storage-clean: as the user for the trash
// and the thumbnails, through pkexec for the system files. Storage
// Sense is the user timer raye-storage-sense.timer, which runs the same
// helper with the choices kept in raye-settingsrc [StorageSense].
#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <memory>

class QJsonObject;
class QProcess;
class QThread;

namespace StorageScan
{
// A mounted file system, as statvfs and /proc/self/mountinfo see it.
struct Volume {
    QString mountPoint;
    QString device;
    QString fsType;
    QString label;
    qint64 total = 0;
    qint64 available = 0; // what a user may still write
    qint64 free = 0; // what is free, root's reserve included
    bool readOnly = false;
};

// One drive on the page: the volumes of one device (btrfs mounts the
// same device at / and /home).
struct Disk {
    QString id; // the device
    QString name;
    QString kind; // system, windows, usb, local
    QStringList mountPoints; // the main one first
    QString fsType;
    qint64 total = 0;
    qint64 available = 0;
    qint64 reserved = 0;
    qint64 used() const { return qMax<qint64>(0, total - available); }
};

// What the categories are made of, before they are counted. Each
// path is put on the drive it lives on.
struct Sources {
    QString home;
    // (category, folder): documents, pictures, music, videos, downloads, desktop
    QList<QPair<QString, QString>> folders;
    QStringList flatpak; // installations, system and user
    QString dpkgStatus = QStringLiteral("/var/lib/dpkg/status");
    QString dpkgInfo = QStringLiteral("/var/lib/dpkg/info");
    QString packages = QStringLiteral("/usr"); // where packages put their files
    // where each clean-up item lives
    QHash<QString, QString> temporary;
    static Sources forThisUser();
};

// A size and the place it lives.
struct Piece {
    QString category;
    QString path;
    qint64 bytes = 0;
};

// A category of one drive.
struct Category {
    QString id; // apps, system, temporary, documents, ..., other
    qint64 bytes = 0;
    QString path; // the folder "open" shows, empty when there is none
};

// The file systems a user keeps files on; the rest (memory, boot and
// firmware partitions, app images, the live image) are left out.
bool isUserVolume(const Volume &volume);
// Volumes grouped by device, the system drive first.
QList<Disk> groupDisks(const QList<Volume> &volumes);
// The volume a path lives on: the longest mount point it is under.
int volumeOf(const QString &path, const QList<Volume> &volumes);
// The drive (index in disks) a path lives on, -1 when none.
int diskOf(const QString &path, const QList<Volume> &volumes, const QList<Disk> &disks);
// "Raye", "Windows", the label, "USB Drive" or "Local Disk".
// kind is set at the same time. sysRoot stands for / in the tests.
void nameDisk(Disk &disk, const QString &label, const QString &sysRoot = QStringLiteral("/"));

// Space taken on disk by everything under path: one file system,
// links not followed, a file with several names counted once. Stops
// early (with what it counted so far) when cancel turns true.
qint64 directoryUsage(const QString &path, const std::atomic_bool *cancel = nullptr);

// Installed sizes from dpkg: packages that put an app in the menu, and
// all the others. Bytes.
struct PackageSizes {
    qint64 apps = 0;
    qint64 system = 0;
    int appPackages = 0;
};
PackageSizes packageSizes(const QString &statusFile, const QString &infoDir,
                          const std::atomic_bool *cancel = nullptr);

// The lines of `storage-clean`: item -> {bytes, count}; bytes -1 when
// unknown. errors gets "item reason" for each failure.
struct HelperResult {
    QHash<QString, QPair<qint64, qint64>> items;
    QStringList errors;
};
HelperResult parseHelper(const QByteArray &output, const QString &kind);

// Every category of every drive, by drive id, largest first. Other is
// what is left of the space used; root's reserve counts as system.
QHash<QString, QList<Category>> categorize(const QList<Volume> &volumes, const QList<Disk> &disks,
                                           const QList<Piece> &pieces);
} // namespace StorageScan

class Storage : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{ id, name, kind, primary, mountPoint, fsType, total, used, free,
    //    categories: [{ id, bytes, path }], counted }]
    Q_PROPERTY(QVariantList disks READ disks NOTIFY disksChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // [{ id, bytes, count, system }], bytes -1 when not known
    Q_PROPERTY(QVariantList cleanupItems READ cleanupItems NOTIFY cleanupChanged)
    Q_PROPERTY(qint64 cleanupTotal READ cleanupTotal NOTIFY cleanupChanged)
    Q_PROPERTY(bool estimating READ estimating NOTIFY cleanupChanged)
    Q_PROPERTY(bool cleaning READ cleaning NOTIFY cleaningChanged)
    Q_PROPERTY(bool senseEnabled READ senseEnabled WRITE setSenseEnabled NOTIFY senseChanged)
    // 0 = never
    Q_PROPERTY(int senseTrashDays READ senseTrashDays WRITE setSenseTrashDays NOTIFY senseChanged)
    Q_PROPERTY(bool senseThumbnails READ senseThumbnails WRITE setSenseThumbnails NOTIFY senseChanged)
    Q_PROPERTY(bool senseRunning READ senseRunning NOTIFY senseChanged)
    Q_PROPERTY(QDateTime senseLastRun READ senseLastRun NOTIFY senseChanged)
    Q_PROPERTY(bool disksToolAvailable READ disksToolAvailable CONSTANT)

public:
    explicit Storage(QObject *parent = nullptr);
    ~Storage() override;

    QVariantList disks() const { return m_disks; }
    bool scanning() const { return m_scanning; }
    QVariantList cleanupItems() const { return m_cleanupItems; }
    qint64 cleanupTotal() const;
    bool estimating() const { return m_estimate != nullptr; }
    bool cleaning() const { return m_cleaning; }
    bool senseEnabled() const { return m_senseEnabled; }
    void setSenseEnabled(bool on);
    int senseTrashDays() const { return m_senseTrashDays; }
    void setSenseTrashDays(int days);
    bool senseThumbnails() const { return m_senseThumbnails; }
    void setSenseThumbnails(bool on);
    bool senseRunning() const { return m_senseRunning; }
    QDateTime senseLastRun() const { return m_senseLastRun; }
    bool disksToolAvailable() const;

    // counts again, in the background
    Q_INVOKABLE void refresh();
    // asks the helper what Clean up would free
    Q_INVOKABLE void estimateCleanup();
    // trash, thumbnails, apt, logs, snapshots; cleanupFinished() after
    Q_INVOKABLE void clean(const QStringList &items);
    Q_INVOKABLE void runSenseNow();
    Q_INVOKABLE void openDisksTool();
    // a category's folder in the file manager
    Q_INVOKABLE void openFolder(const QString &path);
    // "45.3 GB", the way Windows counts (1024, GB)
    Q_INVOKABLE QString formatSize(qint64 bytes) const;

    static bool isSystemItem(const QString &item);

    // For the tests: the volumes, the folders, the programs run and the
    // files read, instead of the computer's own. Set before refresh().
    void setVolumes(const QList<StorageScan::Volume> &volumes);
    void setSources(const StorageScan::Sources &sources);
    void setHelper(const QString &path) { m_helper = path; }
    void setPkexec(const QString &path) { m_pkexec = path; }
    void setSystemctl(const QString &path) { m_systemctl = path; }
    void setSysRoot(const QString &path) { m_sysRoot = path; }
    void setCacheFile(const QString &path) { m_cacheFile = path; }
    void setConfigFile(const QString &path);
    // waits for the count running now (tests)
    bool waitForScan(int msecs);

Q_SIGNALS:
    void disksChanged();
    void scanningChanged();
    void cleanupChanged();
    void cleaningChanged();
    void senseChanged();
    // freed: bytes; problem: "", "cancelled", "busy", "partial", "denied"
    void cleanupFinished(qint64 freed, const QString &problem);
    void scanFinished();

private:
    struct ScanResult;
    static QJsonObject preview();
    void loadCache();
    void saveCache() const;
    void publish(const QList<StorageScan::Disk> &disks, const QHash<QString, QList<StorageScan::Category>> &categories,
                 bool counted);
    void scanDone(const std::shared_ptr<ScanResult> &result);
    QList<StorageScan::Volume> currentVolumes() const;
    StorageScan::Sources currentSources() const;
    void runClean(QStringList user, QStringList system, qint64 freed, const QString &problem);
    void finishClean(qint64 freed, const QString &problem);
    void readSense();
    void querySense();
    void writeSense();
    void systemctl(const QStringList &args, std::function<void(int, const QByteArray &)> done = {});

    QVariantList m_disks;
    bool m_scanning = false;
    QVariantList m_cleanupItems;
    bool m_cleaning = false;
    bool m_senseEnabled = false;
    int m_senseTrashDays = 30;
    bool m_senseThumbnails = true;
    bool m_senseRunning = false;
    QDateTime m_senseLastRun;

    bool m_volumesSet = false;
    QList<StorageScan::Volume> m_volumes;
    bool m_sourcesSet = false;
    StorageScan::Sources m_sources;
    QString m_helper;
    QString m_pkexec = QStringLiteral("pkexec");
    QString m_systemctl = QStringLiteral("systemctl");
    QString m_sysRoot = QStringLiteral("/");
    QString m_cacheFile;
    QString m_configFile;

    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QProcess *m_estimate = nullptr;
};
