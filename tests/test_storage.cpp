// SPDX-License-Identifier: GPL-3.0-or-later
// System > Storage: drives, what fills them, Clean up and Storage
// Sense, on folders made for the test and with stand-ins for the
// helper, pkexec and systemctl.
#include "storage.h"

#include <KLocalizedString>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

using namespace StorageScan;

namespace
{
void writeFile(const QString &path, qint64 bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray chunk(4096, 'x');
    for (qint64 left = bytes; left > 0; left -= chunk.size())
        file.write(chunk.constData(), qMin<qint64>(left, chunk.size()));
}

qint64 onDisk(const QString &path)
{
    struct stat st {};
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
        return 0;
    return qint64(st.st_blocks) * 512;
}

void writeScript(const QString &path, const QByteArray &body)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n" + body);
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

Volume volume(const QString &mount, const QString &device, const QString &fs, qint64 total, qint64 available)
{
    Volume v;
    v.mountPoint = mount;
    v.device = device;
    v.fsType = fs;
    v.total = total;
    v.available = available;
    v.free = available;
    return v;
}

qint64 categoryBytes(const QList<Category> &list, const QString &id)
{
    for (const auto &c : list) {
        if (c.id == id)
            return c.bytes;
    }
    return -1;
}

QVariantMap categoryOf(const QVariantMap &disk, const QString &id)
{
    const QVariantList list = disk.value(QStringLiteral("categories")).toList();
    for (const QVariant &c : list) {
        if (c.toMap().value(QStringLiteral("id")).toString() == id)
            return c.toMap();
    }
    return {};
}
} // namespace

class TestStorage : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain("raye-settings");
    }

    void userVolumes()
    {
        QVERIFY(isUserVolume(volume(QStringLiteral("/"), QStringLiteral("/dev/nvme0n1p2"), QStringLiteral("btrfs"), 100, 50)));
        QVERIFY(isUserVolume(volume(QStringLiteral("/media/alex/KEY"), QStringLiteral("/dev/sdb1"), QStringLiteral("vfat"), 100, 50)));
        QVERIFY(isUserVolume(volume(QStringLiteral("/run/media/alex/Data"), QStringLiteral("/dev/sdc1"), QStringLiteral("ntfs3"), 100, 50)));
        QVERIFY(isUserVolume(volume(QStringLiteral("/mnt/data"), QStringLiteral("/dev/mapper/luks-1"), QStringLiteral("ext4"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/tmp"), QStringLiteral("tmpfs"), QStringLiteral("tmpfs"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/boot"), QStringLiteral("/dev/nvme0n1p1"), QStringLiteral("ext4"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/boot/efi"), QStringLiteral("/dev/nvme0n1p1"), QStringLiteral("vfat"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/snap/core/1"), QStringLiteral("/dev/loop3"), QStringLiteral("squashfs"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/"), QStringLiteral("overlay"), QStringLiteral("overlay"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/data"), QStringLiteral("/dev/loop0"), QStringLiteral("ext4"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/run/user/1000/doc"), QStringLiteral("portal"), QStringLiteral("fuse.portal"), 100, 50)));
        QVERIFY(!isUserVolume(volume(QStringLiteral("/empty"), QStringLiteral("/dev/sdd1"), QStringLiteral("ext4"), 0, 0)));
    }

    void grouping()
    {
        const QList<Volume> volumes{
            volume(QStringLiteral("/media/alex/KEY"), QStringLiteral("/dev/sdb1"), QStringLiteral("vfat"), 32, 16),
            volume(QStringLiteral("/home"), QStringLiteral("/dev/nvme0n1p2"), QStringLiteral("btrfs"), 500, 300),
            volume(QStringLiteral("/"), QStringLiteral("/dev/nvme0n1p2"), QStringLiteral("btrfs"), 500, 300),
            volume(QStringLiteral("/tmp"), QStringLiteral("tmpfs"), QStringLiteral("tmpfs"), 8, 8),
        };
        const QList<Disk> disks = groupDisks(volumes);
        QCOMPARE(disks.size(), 2);
        QCOMPARE(disks[0].id, QStringLiteral("/dev/nvme0n1p2"));
        QCOMPARE(disks[0].mountPoints, (QStringList{QStringLiteral("/"), QStringLiteral("/home")}));
        QCOMPARE(disks[0].total, 500);
        QCOMPARE(disks[0].used(), 200);
        QCOMPARE(disks[1].mountPoints, QStringList{QStringLiteral("/media/alex/KEY")});
    }

    void pathsOnDrives()
    {
        const QList<Volume> volumes{
            volume(QStringLiteral("/"), QStringLiteral("/dev/a"), QStringLiteral("ext4"), 10, 5),
            volume(QStringLiteral("/home"), QStringLiteral("/dev/b"), QStringLiteral("ext4"), 10, 5),
        };
        QCOMPARE(volumeOf(QStringLiteral("/home/alex/Documents"), volumes), 1);
        QCOMPARE(volumeOf(QStringLiteral("/home"), volumes), 1);
        QCOMPARE(volumeOf(QStringLiteral("/homework/x"), volumes), 0);
        QCOMPARE(volumeOf(QStringLiteral("/usr"), volumes), 0);
        QCOMPARE(volumeOf(QStringLiteral("relative"), volumes), -1);
        const QList<Disk> disks = groupDisks(volumes);
        QCOMPARE(diskOf(QStringLiteral("/home/alex"), volumes, disks), 1);
        QCOMPARE(diskOf(QStringLiteral("/var/log"), volumes, disks), 0);
    }

    void names()
    {
        QTemporaryDir dir;
        // a Windows partition holds Windows/System32
        QDir().mkpath(dir.filePath(QStringLiteral("win/Windows/System32")));
        // a USB stick: its block device sits under a usb bus in sysfs
        QDir().mkpath(dir.filePath(QStringLiteral("sys/devices/pci0000:00/usb2/2-1/block/sdb/sdb1")));
        QDir().mkpath(dir.filePath(QStringLiteral("sys/devices/pci0000:00/ata1/block/sdc/sdc1")));
        QDir().mkpath(dir.filePath(QStringLiteral("sys/class/block")));
        QFile::link(dir.filePath(QStringLiteral("sys/devices/pci0000:00/usb2/2-1/block/sdb/sdb1")),
                    dir.filePath(QStringLiteral("sys/class/block/sdb1")));
        QFile::link(dir.filePath(QStringLiteral("sys/devices/pci0000:00/ata1/block/sdc/sdc1")),
                    dir.filePath(QStringLiteral("sys/class/block/sdc1")));

        Disk system;
        system.mountPoints = QStringList{QStringLiteral("/")};
        nameDisk(system, QStringLiteral("raye"), dir.path());
        QCOMPARE(system.name, QStringLiteral("Raye"));
        QCOMPARE(system.kind, QStringLiteral("system"));

        Disk windows;
        windows.id = QStringLiteral("/dev/sda3");
        windows.mountPoints = QStringList{dir.filePath(QStringLiteral("win"))};
        nameDisk(windows, QStringLiteral("OS"), dir.path());
        QCOMPARE(windows.name, QStringLiteral("Windows"));
        QCOMPARE(windows.kind, QStringLiteral("windows"));

        Disk usb;
        usb.id = QStringLiteral("/dev/sdb1");
        usb.mountPoints = QStringList{QStringLiteral("/media/alex/x")};
        nameDisk(usb, QString(), dir.path());
        QCOMPARE(usb.name, QStringLiteral("USB Drive"));
        QCOMPARE(usb.kind, QStringLiteral("usb"));
        nameDisk(usb, QStringLiteral("KEY"), dir.path());
        QCOMPARE(usb.name, QStringLiteral("KEY"));

        Disk local;
        local.id = QStringLiteral("/dev/sdc1");
        local.mountPoints = QStringList{QStringLiteral("/mnt/data")};
        nameDisk(local, QString(), dir.path());
        QCOMPARE(local.name, QStringLiteral("Local Disk"));
        QCOMPARE(local.kind, QStringLiteral("local"));
        nameDisk(local, QStringLiteral("Data"), dir.path());
        QCOMPARE(local.name, QStringLiteral("Data"));
    }

    void usage()
    {
        QTemporaryDir dir;
        QTemporaryDir outside;
        writeFile(dir.filePath(QStringLiteral("a")), 10000);
        writeFile(dir.filePath(QStringLiteral("sub/b")), 50000);
        writeFile(outside.filePath(QStringLiteral("big")), 400000);
        // a second name for b and a link out of the folder count nothing more
        QVERIFY(::link(QFile::encodeName(dir.filePath(QStringLiteral("sub/b"))).constData(),
                       QFile::encodeName(dir.filePath(QStringLiteral("b-again"))).constData()) == 0);
        QVERIFY(QFile::link(outside.filePath(QStringLiteral("big")), dir.filePath(QStringLiteral("link"))));
        const qint64 want = onDisk(dir.filePath(QStringLiteral("a"))) + onDisk(dir.filePath(QStringLiteral("sub/b")))
                          + onDisk(dir.filePath(QStringLiteral("link")));
        const qint64 got = directoryUsage(dir.path());
        // folders take a few blocks of their own
        QVERIFY2(got >= want && got < want + 64 * 1024, qPrintable(QStringLiteral("%1 vs %2").arg(got).arg(want)));
        QCOMPARE(directoryUsage(dir.filePath(QStringLiteral("missing"))), 0);
        std::atomic_bool stop{true};
        QVERIFY(directoryUsage(dir.path(), &stop) < want);
    }

    void packages()
    {
        QTemporaryDir dir;
        QFile status(dir.filePath(QStringLiteral("status")));
        QVERIFY(status.open(QIODevice::WriteOnly));
        status.write("Package: firefox\nStatus: install ok installed\nArchitecture: amd64\nInstalled-Size: 250000\n"
                     "Description: web\n continued line\n\n"
                     "Package: libc6\nStatus: install ok installed\nArchitecture: amd64\nInstalled-Size: 13000\n\n"
                     "Package: gone\nStatus: deinstall ok config-files\nInstalled-Size: 999999\n\n"
                     "Package: kate\nStatus: install ok installed\nArchitecture: amd64\nInstalled-Size: 7000\n");
        status.close();
        QDir().mkpath(dir.filePath(QStringLiteral("info")));
        auto list = [&](const QString &name, const QByteArray &body) {
            QFile f(dir.filePath(QStringLiteral("info/") + name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(body);
        };
        list(QStringLiteral("firefox.list"), "/usr/bin/firefox\n/usr/share/applications/firefox.desktop\n");
        list(QStringLiteral("libc6:amd64.list"), "/usr/lib/x86_64-linux-gnu/libc.so.6\n");
        list(QStringLiteral("kate.list"), "/usr/share/applications/org.kde.kate.desktop\n");
        const PackageSizes sizes = packageSizes(dir.filePath(QStringLiteral("status")), dir.filePath(QStringLiteral("info")));
        QCOMPARE(sizes.apps, qint64(257000) * 1024);
        QCOMPARE(sizes.system, qint64(13000) * 1024);
        QCOMPARE(sizes.appPackages, 2);
        const PackageSizes none = packageSizes(dir.filePath(QStringLiteral("nothing")), dir.filePath(QStringLiteral("info")));
        QCOMPARE(none.apps + none.system, 0);
    }

    void helperOutput()
    {
        const HelperResult r = parseHelper("size\ttrash\t4096\t2\nsize\tsnapshots\t-1\t-1\nerror\tapt\tdenied\n"
                                           "freed\tlogs\t10\t1\ngarbage\nsize\tbad\tx\t1\n",
                                           QStringLiteral("size"));
        QCOMPARE(r.items.size(), 2);
        QCOMPARE(r.items.value(QStringLiteral("trash")), qMakePair(qint64(4096), qint64(2)));
        QCOMPARE(r.items.value(QStringLiteral("snapshots")).first, -1);
        QCOMPARE(r.errors, QStringList{QStringLiteral("apt denied")});
        QCOMPARE(parseHelper("freed\tlogs\t10\t1\n", QStringLiteral("freed")).items.value(QStringLiteral("logs")).first, 10);
    }

    void categories()
    {
        const QList<Volume> volumes{
            volume(QStringLiteral("/"), QStringLiteral("/dev/a"), QStringLiteral("btrfs"), 1000, 400),
            volume(QStringLiteral("/home"), QStringLiteral("/dev/a"), QStringLiteral("btrfs"), 1000, 400),
            volume(QStringLiteral("/mnt/data"), QStringLiteral("/dev/b"), QStringLiteral("ext4"), 500, 100),
        };
        QList<Volume> withReserve = volumes;
        withReserve[2].free = 125; // 25 for root only
        const QList<Disk> disks = groupDisks(withReserve);
        const QList<Piece> pieces{
            {QStringLiteral("documents"), QStringLiteral("/home/alex/Documents"), 100},
            {QStringLiteral("pictures"), QStringLiteral("/home/alex/Pictures"), 50},
            {QStringLiteral("apps"), QStringLiteral("/usr"), 120},
            {QStringLiteral("system"), QStringLiteral("/usr"), 200},
            {QStringLiteral("apps"), QStringLiteral("/var/lib/flatpak"), 30},
            {QStringLiteral("temporary"), QStringLiteral("/var/log"), 10},
            {QStringLiteral("temporary"), QStringLiteral("/home/alex/.cache/thumbnails"), 5},
            {QStringLiteral("videos"), QStringLiteral("/mnt/data/Videos"), 300},
            {QStringLiteral("music"), QStringLiteral("nowhere"), 7},
        };
        const auto result = categorize(withReserve, disks, pieces);
        const QList<Category> a = result.value(QStringLiteral("/dev/a"));
        QCOMPARE(categoryBytes(a, QStringLiteral("apps")), 150);
        QCOMPARE(categoryBytes(a, QStringLiteral("system")), 200);
        QCOMPARE(categoryBytes(a, QStringLiteral("temporary")), 15);
        QCOMPARE(categoryBytes(a, QStringLiteral("documents")), 100);
        // used 600 - 515 counted
        QCOMPARE(categoryBytes(a, QStringLiteral("other")), 85);
        QCOMPARE(categoryBytes(a, QStringLiteral("music")), -1);
        // largest first
        for (int i = 1; i < a.size(); ++i)
            QVERIFY(a[i - 1].bytes >= a[i].bytes);
        QCOMPARE(a.first().id, QStringLiteral("system"));
        for (const auto &c : a) {
            if (c.id == QLatin1String("documents"))
                QCOMPARE(c.path, QStringLiteral("/home/alex/Documents"));
        }
        const QList<Category> b = result.value(QStringLiteral("/dev/b"));
        QCOMPARE(categoryBytes(b, QStringLiteral("videos")), 300);
        // root's reserve shows as system; Other is what is left
        QCOMPARE(categoryBytes(b, QStringLiteral("system")), 25);
        QCOMPARE(categoryBytes(b, QStringLiteral("other")), 75);

        // counted more than the drive says it uses: Other is 0, never below
        const auto over = categorize(volumes, groupDisks(volumes),
                                     {{QStringLiteral("documents"), QStringLiteral("/home/x"), 5000}});
        QCOMPARE(categoryBytes(over.value(QStringLiteral("/dev/a")), QStringLiteral("other")), 0);
    }

    void sizes()
    {
        Storage storage;
        QCOMPARE(storage.formatSize(0), QStringLiteral("0 B"));
        QCOMPARE(storage.formatSize(qint64(45.3 * 1024 * 1024 * 1024)), QStringLiteral("45.3 GB"));
        QCOMPARE(storage.formatSize(qint64(512) * 1024 * 1024), QStringLiteral("512 MB"));
        QCOMPARE(storage.formatSize(-1), QString());
        QVERIFY(Storage::isSystemItem(QStringLiteral("apt")));
        QVERIFY(Storage::isSystemItem(QStringLiteral("snapshots")));
        QVERIFY(!Storage::isSystemItem(QStringLiteral("trash")));
    }

    // the whole page backend, on folders that stand for a computer
    void scanAndCache()
    {
        QTemporaryDir dir;
        const QString home = dir.filePath(QStringLiteral("home"));
        writeFile(home + QStringLiteral("/Documents/a.odt"), 200000);
        writeFile(home + QStringLiteral("/Pictures/b.png"), 100000);
        writeFile(dir.filePath(QStringLiteral("flatpak/app/x")), 30000);
        writeScript(dir.filePath(QStringLiteral("helper")),
                    "echo \"$@\" >> " + QFile::encodeName(dir.filePath(QStringLiteral("helper.log"))) + "\n"
                    "printf 'size\\ttrash\\t8192\\t1\\nsize\\tapt\\t40960\\t2\\n'\n");

        Volume v = volume(dir.path(), QStringLiteral("/dev/fake1"), QStringLiteral("ext4"),
                          qint64(100) * 1024 * 1024, qint64(60) * 1024 * 1024);
        Sources sources;
        sources.home = home;
        sources.folders = {{QStringLiteral("documents"), home + QStringLiteral("/Documents")},
                           {QStringLiteral("pictures"), home + QStringLiteral("/Pictures")},
                           {QStringLiteral("music"), home + QStringLiteral("/Music")},
                           {QStringLiteral("desktop"), home}};
        sources.flatpak = {dir.filePath(QStringLiteral("flatpak"))};
        sources.dpkgStatus = dir.filePath(QStringLiteral("status"));
        sources.dpkgInfo = dir.filePath(QStringLiteral("info"));
        sources.packages = dir.filePath(QStringLiteral("usr"));
        sources.temporary = {{QStringLiteral("trash"), home + QStringLiteral("/.local/share/Trash")},
                             {QStringLiteral("apt"), dir.filePath(QStringLiteral("var/cache/apt/archives"))}};
        const QString cache = dir.filePath(QStringLiteral("cache/storage.json"));

        {
            Storage storage;
            storage.setVolumes({v});
            storage.setSources(sources);
            storage.setHelper(dir.filePath(QStringLiteral("helper")));
            storage.setCacheFile(cache);
            storage.setSysRoot(dir.path());
            storage.refresh();
            QVERIFY(storage.scanning());
            QVERIFY(storage.waitForScan(10000));
            QVERIFY(!storage.scanning());
            QCOMPARE(storage.disks().size(), 1);
            const QVariantMap disk = storage.disks().first().toMap();
            QCOMPARE(disk.value(QStringLiteral("total")).toLongLong(), qint64(100) * 1024 * 1024);
            QCOMPARE(disk.value(QStringLiteral("free")).toLongLong(), qint64(60) * 1024 * 1024);
            QVERIFY(disk.value(QStringLiteral("counted")).toBool());
            // the home is on it: shown in full
            QVERIFY(disk.value(QStringLiteral("primary")).toBool());
            QVERIFY(categoryOf(disk, QStringLiteral("documents")).value(QStringLiteral("bytes")).toLongLong() >= 200000);
            QCOMPARE(categoryOf(disk, QStringLiteral("documents")).value(QStringLiteral("path")).toString(),
                     home + QStringLiteral("/Documents"));
            QVERIFY(categoryOf(disk, QStringLiteral("apps")).value(QStringLiteral("bytes")).toLongLong() >= 30000);
            QCOMPARE(categoryOf(disk, QStringLiteral("temporary")).value(QStringLiteral("bytes")).toLongLong(), 49152);
            // a folder that is the home itself is not a category
            QVERIFY(categoryOf(disk, QStringLiteral("desktop")).isEmpty());
            // the helper was only asked, never told to delete
            QVERIFY(!readAll(dir.filePath(QStringLiteral("helper.log"))).contains(QStringLiteral("--apply")));
            QVERIFY(QFile::exists(cache));
        }
        {
            // the next time, the last count shows before anything is counted
            Storage storage;
            storage.setVolumes({v});
            storage.setSources(sources);
            storage.setHelper(dir.filePath(QStringLiteral("helper")));
            storage.setCacheFile(cache);
            storage.setSysRoot(dir.path());
            storage.refresh();
            QCOMPARE(storage.disks().size(), 1);
            const QVariantMap disk = storage.disks().first().toMap();
            QVERIFY(!disk.value(QStringLiteral("counted")).toBool());
            QVERIFY(categoryOf(disk, QStringLiteral("documents")).value(QStringLiteral("bytes")).toLongLong() >= 200000);
            QVERIFY(storage.waitForScan(10000));
        }
        // a broken cache is ignored
        QFile broken(cache);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{not json");
        broken.close();
        Storage storage;
        storage.setVolumes({v});
        storage.setCacheFile(cache);
        storage.setHelper(dir.filePath(QStringLiteral("helper")));
        storage.setSources(sources);
        storage.refresh();
        QCOMPARE(storage.disks().size(), 1);
        QVERIFY(categoryOf(storage.disks().first().toMap(), QStringLiteral("documents")).isEmpty());
        QVERIFY(storage.waitForScan(10000));
    }

    // closing the page does not wait for a slow helper
    void closeWhileCounting()
    {
        QTemporaryDir dir;
        writeScript(dir.filePath(QStringLiteral("helper")), "sleep 30\n");
        QElapsedTimer timer;
        timer.start();
        {
            Storage storage;
            storage.setVolumes({volume(dir.path(), QStringLiteral("/dev/fake2"), QStringLiteral("ext4"), 1000, 500)});
            Sources sources;
            sources.home = dir.path();
            storage.setSources(sources);
            storage.setHelper(dir.filePath(QStringLiteral("helper")));
            storage.setCacheFile(dir.filePath(QStringLiteral("cache.json")));
            storage.refresh();
            QTest::qWait(300);
        }
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    }

    void estimate()
    {
        QTemporaryDir dir;
        writeScript(dir.filePath(QStringLiteral("helper")),
                    "echo \"$@\" > " + QFile::encodeName(dir.filePath(QStringLiteral("args"))) + "\n"
                    "printf 'size\\ttrash\\t1000\\t3\\nsize\\tthumbnails\\t2000\\t9\\nsize\\tapt\\t3000\\t2\\n"
                    "size\\tlogs\\t500\\t4\\nsize\\tsnapshots\\t-1\\t5\\n'\n");
        Storage storage;
        storage.setHelper(dir.filePath(QStringLiteral("helper")));
        QSignalSpy changed(&storage, &Storage::cleanupChanged);
        storage.estimateCleanup();
        QVERIFY(storage.estimating());
        QTRY_VERIFY_WITH_TIMEOUT(!storage.estimating(), 5000);
        const QVariantList items = storage.cleanupItems();
        QCOMPARE(items.size(), 5);
        QCOMPARE(items[0].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("trash"));
        QCOMPARE(items[2].toMap().value(QStringLiteral("system")).toBool(), true);
        QCOMPARE(items[4].toMap().value(QStringLiteral("bytes")).toLongLong(), -1);
        QCOMPARE(items[4].toMap().value(QStringLiteral("count")).toLongLong(), 5);
        // an unknown size does not count in the total
        QCOMPARE(storage.cleanupTotal(), 6500);
        QVERIFY(!readAll(dir.filePath(QStringLiteral("args"))).contains(QStringLiteral("--apply")));
        // a helper that says nothing (snapper missing): items missing are left out
        writeScript(dir.filePath(QStringLiteral("helper")), "printf 'size\\ttrash\\t10\\t1\\nerror\\tsnapshots\\tmissing\\n'\nexit 2\n");
        storage.estimateCleanup();
        QTRY_VERIFY_WITH_TIMEOUT(!storage.estimating(), 5000);
        QCOMPARE(storage.cleanupItems().size(), 1);
    }

    void cleanUp()
    {
        QTemporaryDir dir;
        const QByteArray log = QFile::encodeName(dir.filePath(QStringLiteral("log")));
        writeScript(dir.filePath(QStringLiteral("helper")),
                    "echo \"helper $*\" >> " + log + "\n"
                    "for a do case $a in --apply) ;; *) printf 'freed\\t%s\\t1000\\t1\\n' \"$a\" ;; esac; done\n");
        writeScript(dir.filePath(QStringLiteral("pkexec")), "echo \"pkexec $*\" >> " + log + "\nexec \"$@\"\n");
        Storage storage;
        storage.setHelper(dir.filePath(QStringLiteral("helper")));
        storage.setPkexec(dir.filePath(QStringLiteral("pkexec")));
        QSignalSpy done(&storage, &Storage::cleanupFinished);
        storage.clean({QStringLiteral("apt"), QStringLiteral("trash"), QStringLiteral("rm -rf"), QStringLiteral("logs")});
        QVERIFY(storage.cleaning());
        QVERIFY(done.wait(5000));
        QCOMPARE(done.first().at(0).toLongLong(), 3000);
        QCOMPARE(done.first().at(1).toString(), QString());
        QVERIFY(!storage.cleaning());
        const QStringList lines = readAll(dir.filePath(QStringLiteral("log"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        // the user's things as the user, the system's through pkexec, nothing unknown
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0], QStringLiteral("helper --apply trash"));
        QCOMPARE(lines[1], QStringLiteral("pkexec ") + dir.filePath(QStringLiteral("helper")) + QStringLiteral(" --apply apt logs"));
        QCOMPARE(lines[2], QStringLiteral("helper --apply apt logs"));

        // the password prompt closed: pkexec says 126
        writeScript(dir.filePath(QStringLiteral("pkexec")), "exit 126\n");
        done.clear();
        storage.clean({QStringLiteral("apt")});
        QVERIFY(done.wait(5000));
        QCOMPARE(done.first().at(0).toLongLong(), 0);
        QCOMPARE(done.first().at(1).toString(), QStringLiteral("cancelled"));

        // apt busy: the helper says so
        writeScript(dir.filePath(QStringLiteral("pkexec")), "exec \"$@\"\n");
        writeScript(dir.filePath(QStringLiteral("helper")), "printf 'freed\\tapt\\t0\\t0\\nerror\\tapt\\tbusy\\n'\nexit 2\n");
        done.clear();
        storage.clean({QStringLiteral("apt")});
        QVERIFY(done.wait(5000));
        QCOMPARE(done.first().at(1).toString(), QStringLiteral("busy"));

        // nothing to do
        done.clear();
        storage.clean({});
        QCOMPARE(done.size(), 1);
    }

    // the reference renders show a made-up PC, and nothing can be cleaned
    void preview()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("fixture.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"disks":[{"id":"/dev/x","name":"Raye","total":100,"used":40,"free":60,"primary":true,"categories":[]}],
                    "cleanup":[{"id":"trash","bytes":10,"count":1,"system":false}]})");
        f.close();
        writeScript(dir.filePath(QStringLiteral("helper")), "touch " + QFile::encodeName(dir.filePath(QStringLiteral("ran"))) + "\n");
        qputenv("RAYE_STORAGE_PREVIEW", QFile::encodeName(dir.filePath(QStringLiteral("fixture.json"))));
        Storage storage;
        storage.setHelper(dir.filePath(QStringLiteral("helper")));
        storage.setPkexec(dir.filePath(QStringLiteral("helper")));
        storage.refresh();
        QVERIFY(!storage.scanning());
        QCOMPARE(storage.disks().size(), 1);
        QCOMPARE(storage.disks().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Raye"));
        storage.estimateCleanup();
        QCOMPARE(storage.cleanupTotal(), 10);
        QSignalSpy done(&storage, &Storage::cleanupFinished);
        storage.clean({QStringLiteral("trash"), QStringLiteral("apt")});
        QCOMPARE(done.size(), 1);
        qunsetenv("RAYE_STORAGE_PREVIEW");
        QTest::qWait(100);
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("ran"))));
    }

    void storageSense()
    {
        QTemporaryDir dir;
        const QByteArray log = QFile::encodeName(dir.filePath(QStringLiteral("systemctl.log")));
        writeScript(dir.filePath(QStringLiteral("systemctl")),
                    "echo \"$*\" >> " + log + "\n"
                    "case \"$*\" in *is-enabled*) echo enabled ;; *LastTriggerUSec*) echo @1761400000 ;; esac\n");
        const QString config = dir.filePath(QStringLiteral("raye-settingsrc"));
        QFile f(config);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Home]\nHidden=bluetooth\n\n[StorageSense]\nTrashDays=60\nThumbnails=false\n");
        f.close();

        Storage storage;
        storage.setSystemctl(dir.filePath(QStringLiteral("systemctl")));
        QSignalSpy changed(&storage, &Storage::senseChanged);
        storage.setConfigFile(config);
        QCOMPARE(storage.senseTrashDays(), 60);
        QCOMPARE(storage.senseThumbnails(), false);
        QTRY_VERIFY_WITH_TIMEOUT(storage.senseEnabled(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(storage.senseLastRun().toSecsSinceEpoch(), qint64(1761400000), 5000);

        storage.setSenseTrashDays(14);
        storage.setSenseThumbnails(true);
        storage.setSenseTrashDays(-5); // refused
        QCOMPARE(storage.senseTrashDays(), 14);
        const QString written = readAll(config);
        QVERIFY(written.contains(QStringLiteral("TrashDays=14")));
        QVERIFY(written.contains(QStringLiteral("Thumbnails=true")));
        QVERIFY(written.contains(QStringLiteral("Hidden=bluetooth")));

        storage.setSenseEnabled(false);
        QVERIFY(!storage.senseEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(readAll(dir.filePath(QStringLiteral("systemctl.log")))
                                     .contains(QStringLiteral("--user disable --now raye-storage-sense.timer")), 5000);
        storage.setSenseEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(readAll(dir.filePath(QStringLiteral("systemctl.log")))
                                     .contains(QStringLiteral("--user enable --now raye-storage-sense.timer")), 5000);
        storage.runSenseNow();
        QVERIFY(storage.senseRunning());
        QTRY_VERIFY_WITH_TIMEOUT(!storage.senseRunning(), 5000);
        QVERIFY(readAll(dir.filePath(QStringLiteral("systemctl.log")))
                    .contains(QStringLiteral("--user start raye-storage-sense.service")));
    }
};

QTEST_MAIN(TestStorage)
#include "test_storage.moc"
