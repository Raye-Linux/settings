// SPDX-License-Identifier: GPL-3.0-or-later
// About and the launch screen: what the files of /proc, /sys and /etc
// say about a PC, read from folders that look like a laptop and like a
// desktop with junk in its firmware, and the name of the PC as the
// rename dialog checks it.
#include "deviceinfo.h"
#include "hostname.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void put(const QString &root, const QString &path, const QByteArray &text)
{
    const QString file = root + QLatin1Char('/') + path;
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

const QByteArray laptopCpuinfo =
    "processor\t: 0\nvendor_id\t: GenuineIntel\nmodel name\t: 11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz\n"
    "physical id\t: 0\ncore id\t\t: 0\nflags\t\t: fpu vme lm\n\n"
    "processor\t: 1\nmodel name\t: 11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz\nphysical id\t: 0\ncore id\t\t: 1\n\n"
    "processor\t: 2\nmodel name\t: 11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz\nphysical id\t: 0\ncore id\t\t: 0\n\n"
    "processor\t: 3\nmodel name\t: 11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz\nphysical id\t: 0\ncore id\t\t: 1\n\n";

const QByteArray pciIds =
    "# comment\n"
    "8086  Intel Corporation\n"
    "\t9a49  TigerLake-LP GT2 [Iris Xe Graphics]\n"
    "\t9a60  TigerLake-H GT1 [UHD Graphics]\n"
    "10de  NVIDIA Corporation\n"
    "\t2484  GA104 [GeForce RTX 3070]\n"
    "\t\t1043 87b8  subsystem line\n"
    "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n"
    "\t73df  Navi 22 [Radeon RX 6700/6700 XT/6750 XT / 6800M/6850M XT]\n"
    "1234  Technical Corp.\n"
    "\t1111  QEMU Virtual Video Controller\n"
    "C 00  Unclassified device\n";

const QByteArray touchscreen =
    "I: Bus=0011 Vendor=0001 Product=0001 Version=ab83\n"
    "N: Name=\"AT Translated Set 2 keyboard\"\n"
    "B: PROP=0\nB: EV=120013\n\n"
    "I: Bus=0018 Vendor=04f3 Product=2a1c Version=0100\n"
    "N: Name=\"ELAN Touchscreen\"\n"
    "B: PROP=2\nB: EV=1b\nB: KEY=400 0 0 0 0 0\nB: ABS=3273800000000003\n\n";

const QByteArray penTouchDevices = touchscreen
    + "I: Bus=0018 Vendor=04f3 Product=2a1c Version=0100\n"
      "N: Name=\"ELAN Touchscreen Stylus\"\n"
      "B: PROP=2\nB: EV=1b\nB: KEY=c03 1 0 0 0 0\nB: ABS=3000003\n\n";

void makeLaptop(const QString &r)
{
    put(r, QStringLiteral("sys/class/dmi/id/sys_vendor"), "LENOVO\n");
    put(r, QStringLiteral("sys/class/dmi/id/product_name"), "20XWCTO1WW\n");
    put(r, QStringLiteral("sys/class/dmi/id/product_version"), "ThinkPad X1 Carbon Gen 9\n");
    put(r, QStringLiteral("sys/class/dmi/id/chassis_type"), "10\n");
    put(r, QStringLiteral("proc/cpuinfo"), laptopCpuinfo);
    put(r, QStringLiteral("proc/meminfo"), "MemTotal:       16114848 kB\nMemFree:  100 kB\n");
    put(r, QStringLiteral("proc/bus/input/devices"), touchscreen);
    put(r, QStringLiteral("proc/sys/kernel/osrelease"), "7.0.14-2-raye\n");
    put(r, QStringLiteral("etc/machine-id"), "3f2a9c1e0b7d4e8f9a6b5c4d3e2f1a0b\n");
    put(r, QStringLiteral("etc/os-release"),
        "PRETTY_NAME=\"Raye Linux 0.5 Beta Alpenglow\"\nNAME=\"Raye Linux\"\nVERSION_ID=\"0.5\"\nVERSION=\"0.5 Beta Alpenglow\"\nID=raye\n");
    put(r, QStringLiteral("usr/share/misc/pci.ids"), pciIds);
    put(r, QStringLiteral("sys/class/drm/card1/device/vendor"), "0x8086\n");
    put(r, QStringLiteral("sys/class/drm/card1/device/device"), "0x9a49\n");
    put(r, QStringLiteral("sys/class/drm/card1-eDP-1/status"), "connected\n");
    put(r, QStringLiteral("sys/block/nvme0n1/size"), "1000215216\n");
    put(r, QStringLiteral("sys/block/nvme0n1/removable"), "0\n");
    put(r, QStringLiteral("sys/block/nvme0n1/queue/rotational"), "0\n");
    put(r, QStringLiteral("sys/block/nvme0n1/device/model"), "SAMSUNG MZVL2512HCJQ-00B00              \n");
    put(r, QStringLiteral("sys/block/loop0/size"), "1000\n");
    put(r, QStringLiteral("sys/block/loop0/removable"), "0\n");
    put(r, QStringLiteral("sys/block/sda/size"), "61440000\n");
    put(r, QStringLiteral("sys/block/sda/removable"), "1\n");
    QDir().mkpath(r + QStringLiteral("/var/log/installer"));
}

} // namespace

class TestDeviceInfo : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        qputenv("LANGUAGE", "en");
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    }

    void vendors_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("clean");
        QTest::newRow("dell") << u"Dell Inc."_s << u"Dell"_s;
        QTest::newRow("lenovo") << u"LENOVO"_s << u"Lenovo"_s;
        QTest::newRow("hp") << u"Hewlett-Packard"_s << u"HP"_s;
        QTest::newRow("asus") << u"ASUSTeK COMPUTER INC."_s << u"ASUS"_s;
        QTest::newRow("msi") << u"Micro-Star International Co., Ltd."_s << u"MSI"_s;
        QTest::newRow("gigabyte") << u"Gigabyte Technology Co., Ltd."_s << u"Gigabyte"_s;
        QTest::newRow("framework") << u"Framework"_s << u"Framework"_s;
        QTest::newRow("junk") << u"To be filled by O.E.M."_s << u""_s;
        QTest::newRow("system manufacturer") << u"System manufacturer"_s << u""_s;
    }
    void vendors()
    {
        QFETCH(QString, raw);
        QFETCH(QString, clean);
        QCOMPARE(DeviceInfo::cleanVendor(raw), clean);
    }

    void models_data()
    {
        QTest::addColumn<QStringList>("dmi"); // vendor, product, version, board vendor, board name
        QTest::addColumn<QString>("model");
        QTest::newRow("dell") << QStringList{u"Dell Inc."_s, u"XPS 13 9310"_s, u""_s, u""_s, u""_s} << u"Dell XPS 13 9310"_s;
        QTest::newRow("lenovo uses the version") << QStringList{u"LENOVO"_s, u"20XWCTO1WW"_s, u"ThinkPad X1 Carbon Gen 9"_s, u""_s, u""_s}
                                                 << u"Lenovo ThinkPad X1 Carbon Gen 9"_s;
        QTest::newRow("vendor already in the name") << QStringList{u"HP"_s, u"HP EliteBook 840 G8"_s, u""_s, u""_s, u""_s} << u"HP EliteBook 840 G8"_s;
        QTest::newRow("junk falls back to the board")
            << QStringList{u"System manufacturer"_s, u"System Product Name"_s, u"System Version"_s, u"ASUSTeK COMPUTER INC."_s, u"ROG STRIX B550-F GAMING"_s}
            << u"ASUS ROG STRIX B550-F GAMING"_s;
        QTest::newRow("all junk") << QStringList{u"To be filled by O.E.M."_s, u"To be filled by O.E.M."_s, u""_s, u"Default string"_s, u"Default string"_s}
                                  << u""_s;
        QTest::newRow("qemu") << QStringList{u"QEMU"_s, u"Standard PC (Q35 + ICH9, 2009)"_s, u"pc-q35-9.2"_s, u""_s, u""_s}
                              << u"QEMU Standard PC (Q35 + ICH9, 2009)"_s;
    }
    void models()
    {
        QFETCH(QStringList, dmi);
        QFETCH(QString, model);
        QCOMPARE(DeviceInfo::modelOf(dmi[0], dmi[1], dmi[2], dmi[3], dmi[4]), model);
    }

    void chassis()
    {
        QCOMPARE(DeviceInfo::chassisOf(3), QStringLiteral("desktop"));
        QCOMPARE(DeviceInfo::chassisOf(7), QStringLiteral("desktop"));
        QCOMPARE(DeviceInfo::chassisOf(9), QStringLiteral("laptop"));
        QCOMPARE(DeviceInfo::chassisOf(10), QStringLiteral("laptop"));
        QCOMPARE(DeviceInfo::chassisOf(31), QStringLiteral("laptop"));
        QCOMPARE(DeviceInfo::chassisOf(30), QStringLiteral("tablet"));
        QCOMPARE(DeviceInfo::chassisOf(32), QStringLiteral("tablet"));
        QCOMPARE(DeviceInfo::chassisOf(13), QStringLiteral("all-in-one"));
        QCOMPARE(DeviceInfo::chassisOf(1), QStringLiteral("desktop"));
        QCOMPARE(DeviceInfo::chassisOf(0), QStringLiteral("desktop"));
    }

    void processor()
    {
        const DeviceInfo::Cpu cpu = DeviceInfo::parseCpuinfo(QString::fromLatin1(laptopCpuinfo));
        QCOMPARE(cpu.threads, 4);
        QCOMPARE(cpu.cores, 2);
        QCOMPARE(cpu.name, QStringLiteral("11th Gen Intel Core i7-1165G7 @ 2.80GHz"));
        QCOMPARE(DeviceInfo::cleanProcessor(QStringLiteral("AMD Ryzen 7 5800X 8-Core Processor   ")),
                 QStringLiteral("AMD Ryzen 7 5800X 8-Core Processor"));
        // no core ids (some ARM boards, some VMs): one core per thread
        const DeviceInfo::Cpu arm = DeviceInfo::parseCpuinfo(
            QStringLiteral("processor\t: 0\nModel\t: Raspberry Pi 5 Model B Rev 1.0\n\nprocessor\t: 1\n\n"));
        QCOMPARE(arm.threads, 2);
        QCOMPARE(arm.cores, 2);
        QCOMPARE(arm.name, QStringLiteral("Raspberry Pi 5 Model B Rev 1.0"));
    }

    void memory()
    {
        QCOMPARE(DeviceInfo::parseMemTotal(QStringLiteral("MemTotal:       16114848 kB\n")), qint64(16114848) * 1024);
        QCOMPARE(DeviceInfo::parseMemTotal(QStringLiteral("nothing\n")), qint64(0));
        const qint64 gib = qint64(1) << 30;
        QCOMPARE(DeviceInfo::installedMemory(qint64(15.4 * gib)), 16 * gib);
        QCOMPARE(DeviceInfo::installedMemory(qint64(7.6 * gib)), 8 * gib);
        QCOMPARE(DeviceInfo::installedMemory(qint64(11.5 * gib)), 12 * gib);
        QCOMPARE(DeviceInfo::installedMemory(qint64(31.2 * gib)), 32 * gib);
        QCOMPARE(DeviceInfo::installedMemory(0), qint64(0));
    }

    void sizes()
    {
        const qint64 gib = qint64(1) << 30;
        QCOMPARE(DeviceInfo::formatSize(16 * gib), QStringLiteral("16.0 GB"));
        QCOMPARE(DeviceInfo::formatSize(qint64(15.6 * gib)), QStringLiteral("15.6 GB"));
        QCOMPARE(DeviceInfo::formatSize(qint64(476.9 * gib)), QStringLiteral("477 GB"));
        QCOMPARE(DeviceInfo::formatSize(qint64(1863 * gib)), QStringLiteral("1.82 TB"));
        QCOMPARE(DeviceInfo::formatSize(512 * (qint64(1) << 20)), QStringLiteral("512 MB"));
    }

    void penAndTouch()
    {
        QCOMPARE(DeviceInfo::parseInputDevices(QString::fromLatin1(touchscreen)), QStringLiteral("touch"));
        QCOMPARE(DeviceInfo::parseInputDevices(QString::fromLatin1(penTouchDevices)), QStringLiteral("pen-touch"));
        QCOMPARE(DeviceInfo::parseInputDevices(
                     QStringLiteral("N: Name=\"Keyboard\"\nB: PROP=0\nB: KEY=1000 0\n\nN: Name=\"Touchpad\"\nB: PROP=5\nB: KEY=e520 10000 0 0 0 0\n\n")),
                 QString()); // a touchpad is not direct input
    }

    void deviceId()
    {
        QCOMPARE(DeviceInfo::deviceIdOf(QStringLiteral("3f2a9c1e0b7d4e8f9a6b5c4d3e2f1a0b\n")),
                 QStringLiteral("3F2A9C1E-0B7D-4E8F-9A6B-5C4D3E2F1A0B"));
        QCOMPARE(DeviceInfo::deviceIdOf(QStringLiteral("not an id")), QString());
    }

    void osRelease()
    {
        const auto os = DeviceInfo::parseOsRelease(
            QStringLiteral("# comment\nNAME=\"Raye Linux\"\nVERSION_ID=0.1\nPRETTY_NAME='Raye Linux 0.1'\nBAD LINE\n"));
        QCOMPARE(os.value(QStringLiteral("NAME")), QStringLiteral("Raye Linux"));
        QCOMPARE(os.value(QStringLiteral("VERSION_ID")), QStringLiteral("0.1"));
        QCOMPARE(os.value(QStringLiteral("PRETTY_NAME")), QStringLiteral("Raye Linux 0.1"));
    }

    void gpus()
    {
        const QString ids = QString::fromLatin1(pciIds);
        QCOMPARE(DeviceInfo::gpuName(QStringLiteral("8086"), QStringLiteral("9a49"), ids), QStringLiteral("Intel Iris Xe Graphics"));
        QCOMPARE(DeviceInfo::gpuName(QStringLiteral("10de"), QStringLiteral("2484"), ids), QStringLiteral("NVIDIA GeForce RTX 3070"));
        QCOMPARE(DeviceInfo::gpuName(QStringLiteral("1002"), QStringLiteral("73df"), ids),
                 QStringLiteral("AMD Radeon RX 6700/6700 XT/6750 XT / 6800M/6850M XT"));
        QCOMPARE(DeviceInfo::gpuName(QStringLiteral("1234"), QStringLiteral("1111"), ids), QStringLiteral("QEMU Virtual Video Controller"));
        // a card pci.ids does not know still has its maker
        QCOMPARE(DeviceInfo::gpuName(QStringLiteral("10de"), QStringLiteral("ffff"), ids), QStringLiteral("NVIDIA graphics"));
        QCOMPARE(DeviceInfo::gpuName(QStringLiteral("10de"), QStringLiteral("2484"), QString()), QStringLiteral("NVIDIA graphics"));
    }

    void laptop()
    {
        QTemporaryDir dir;
        makeLaptop(dir.path());
        const DeviceInfo::Facts f = DeviceInfo::read(dir.path());
        QCOMPARE(f.model, QStringLiteral("Lenovo ThinkPad X1 Carbon Gen 9"));
        QCOMPARE(f.manufacturer, QStringLiteral("Lenovo"));
        QCOMPARE(f.chassis, QStringLiteral("laptop"));
        QCOMPARE(f.threads, 4);
        QCOMPARE(f.memoryInstalled, qint64(16) << 30);
        QCOMPARE(f.graphics, QStringList{QStringLiteral("Intel Iris Xe Graphics")});
        // the internal disk only: not the loop device, not the USB stick
        QCOMPARE(f.disks, QStringList{QStringLiteral("477 GB SSD SAMSUNG MZVL2512HCJQ-00B00")});
        QCOMPARE(f.penAndTouch, QStringLiteral("touch"));
        QCOMPARE(f.deviceId, QStringLiteral("3F2A9C1E-0B7D-4E8F-9A6B-5C4D3E2F1A0B"));
        QCOMPARE(f.edition, QStringLiteral("Raye Linux"));
        QCOMPARE(f.version, QStringLiteral("0.5 Beta Alpenglow"));
        QCOMPARE(f.kernelRelease, QStringLiteral("7.0.14-2-raye"));
        QVERIFY(f.installedOn.isValid());
    }

    void emptyRoot()
    {
        // nothing readable: no crash, nothing made up
        QTemporaryDir dir;
        const DeviceInfo::Facts f = DeviceInfo::read(dir.path());
        QVERIFY(f.model.isEmpty());
        QCOMPARE(f.chassis, QStringLiteral("desktop"));
        QVERIFY(f.processor.isEmpty());
        QVERIFY(f.graphics.isEmpty());
        QVERIFY(f.disks.isEmpty());
        QVERIFY(f.deviceId.isEmpty());
    }

    void objectReadsInAThread()
    {
        QTemporaryDir dir;
        makeLaptop(dir.path());
        DeviceInfo info(dir.path());
        if (!info.ready()) {
            QSignalSpy changed(&info, &DeviceInfo::changed);
            QVERIFY(changed.wait(5000));
        }
        QVERIFY(info.ready());
        QCOMPARE(info.chassis(), QStringLiteral("laptop"));
        QCOMPARE(info.memory(), QStringLiteral("16.0 GB (15.4 GB usable)"));
        QCOMPARE(info.kernelKind(), QStringLiteral("raye"));
        QCOMPARE(info.penAndTouch(), QStringLiteral("Touch support"));
        QVERIFY(info.systemType().startsWith(QLatin1String("64-bit operating system")));
        const QString text = info.specificationsText(QStringLiteral("iris"));
        QVERIFY(text.contains(QLatin1String("Device name\tiris")));
        QVERIFY(text.contains(QLatin1String("Processor\t11th Gen Intel Core i7-1165G7 @ 2.80GHz")));
        QVERIFY(text.contains(QLatin1String("Installed RAM\t16.0 GB (15.4 GB usable)")));
        QVERIFY(info.rayeSpecificationsText().contains(QLatin1String("Edition\tRaye Linux")));
    }

    void names_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("problem");
        QTest::newRow("ok") << u"Alex-PC"_s << u""_s;
        QTest::newRow("digits inside") << u"desk2"_s << u""_s;
        QTest::newRow("empty") << u""_s << u"empty"_s;
        QTest::newRow("spaces") << u"Alex PC"_s << u"characters"_s;
        QTest::newRow("accents") << u"Bureau-Élodie"_s << u"characters"_s;
        QTest::newRow("dot") << u"pc.local"_s << u"characters"_s;
        QTest::newRow("only digits") << u"12345"_s << u"digits"_s;
        QTest::newRow("hyphen first") << u"-pc"_s << u"hyphen"_s;
        QTest::newRow("hyphen last") << u"pc-"_s << u"hyphen"_s;
        QTest::newRow("15 is the most") << u"abcdefghijklmno"_s << u""_s;
        QTest::newRow("16 is too long") << u"abcdefghijklmnop"_s << u"too-long"_s;
        QTest::newRow("shell") << u"a;rm -rf"_s << u"characters"_s;
    }
    void names()
    {
        QFETCH(QString, name);
        QFETCH(QString, problem);
        QCOMPARE(Hostname::problemWith(name), problem);
        QCOMPARE(Hostname::problemText(problem).isEmpty(), problem.isEmpty());
    }

    void staticNames()
    {
        QCOMPARE(Hostname::staticNameOf(QStringLiteral("Alex-PC")), QStringLiteral("alex-pc"));
    }
};

QTEST_GUILESS_MAIN(TestDeviceInfo)
#include "test_deviceinfo.moc"
