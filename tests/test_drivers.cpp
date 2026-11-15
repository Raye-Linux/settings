// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update > Optional updates: what ubuntu-drivers and dpkg print,
// read into drivers. Installing needs PackageKit and the administrator:
// that is tried on a real system, not here.
#include "drivers.h"

#include <QTest>

namespace {
const char *devices =
    "udevadm hwdb is deprecated. Use systemd-hwdb instead.\n"
    "== /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0 ==\n"
    "modalias : pci:v000010DEd00002503sv00001458sd00004074bc03sc00i00\n"
    "vendor   : NVIDIA Corporation\n"
    "model    : GA106 [GeForce RTX 3060]\n"
    "driver   : nvidia-driver-570 - distro non-free\n"
    "driver   : nvidia-driver-580-open - distro non-free recommended\n"
    "driver   : xserver-xorg-video-nouveau - distro free builtin\n"
    "\n"
    "== /sys/devices/pci0000:00/0000:00:1c.2/0000:03:00.0 ==\n"
    "modalias : pci:v000014E4d000043B1sv\n"
    "vendor   : Broadcom Inc. and subsidiaries\n"
    "manual_install: True\n"
    "driver   : bcmwl-kernel-source - distro non-free\n"
    "driver   : oem-somerville-meta - third-party free\n";

const char *list =
    "nvidia-driver-570, (kernel modules provided by linux-modules-nvidia-570-generic)\n"
    "nvidia-driver-580-open, (kernel modules provided by linux-modules-nvidia-580-open-generic)\n"
    "bcmwl-kernel-source\n";
} // namespace

class TestDrivers : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void devicesAreRead()
    {
        const QList<DriverEntry> entries = DriverUpdates::parseDevices(QString::fromUtf8(devices));
        // the OEM enablement package is never offered
        QCOMPARE(entries.size(), 4);
        QCOMPARE(entries.at(0).package, QStringLiteral("nvidia-driver-570"));
        QCOMPARE(entries.at(0).device, QStringLiteral("NVIDIA Corporation · GA106 [GeForce RTX 3060]"));
        QVERIFY(!entries.at(0).recommended);
        QVERIFY(entries.at(1).recommended);
        QVERIFY(!entries.at(1).free);
        QVERIFY(entries.at(2).builtin);
        QVERIFY(entries.at(2).free);
        // a device without a model is named by its maker
        QCOMPARE(entries.at(3).device, QStringLiteral("Broadcom Inc. and subsidiaries"));
        QCOMPARE(entries.at(3).package, QStringLiteral("bcmwl-kernel-source"));
    }

    void nothingToRead()
    {
        QVERIFY(DriverUpdates::parseDevices(QString()).isEmpty());
        QVERIFY(DriverUpdates::parseDevices(QStringLiteral("driver   : nvidia-driver-580 - distro non-free\n")).isEmpty());
    }

    void modulesAreRead()
    {
        const QHash<QString, QString> modules = DriverUpdates::parseModules(QString::fromUtf8(list));
        QCOMPARE(modules.value(QStringLiteral("nvidia-driver-580-open")), QStringLiteral("linux-modules-nvidia-580-open-generic"));
        QVERIFY(!modules.contains(QStringLiteral("bcmwl-kernel-source")));
    }

    void installedAreRead()
    {
        const QStringList names = DriverUpdates::parseInstalled(
            QStringLiteral("nvidia-driver-580-open ii \nnvidia-driver-570 un \nxserver-xorg-video-nouveau ii \nbcmwl-kernel-source rc \n"));
        QCOMPARE(names, (QStringList{QStringLiteral("nvidia-driver-580-open"), QStringLiteral("xserver-xorg-video-nouveau")}));
    }

    void states()
    {
        DriverEntry nvidia;
        nvidia.package = QStringLiteral("nvidia-driver-580-open");
        // without signed modules it would need a key enrolment
        QCOMPARE(DriverUpdates::stateOf(nvidia), QStringLiteral("unsigned"));
        nvidia.modules = QStringLiteral("linux-modules-nvidia-580-open-generic");
        QCOMPARE(DriverUpdates::stateOf(nvidia), QStringLiteral("installable"));
        QCOMPARE(DriverUpdates::packagesOf(nvidia),
                 (QStringList{QStringLiteral("nvidia-driver-580-open"), QStringLiteral("linux-modules-nvidia-580-open-generic")}));
        nvidia.installed = true;
        QCOMPARE(DriverUpdates::stateOf(nvidia), QStringLiteral("installed"));

        DriverEntry broadcom;
        broadcom.package = QStringLiteral("bcmwl-kernel-source");
        QCOMPARE(DriverUpdates::stateOf(broadcom), QStringLiteral("unsigned"));

        DriverEntry nouveau;
        nouveau.package = QStringLiteral("xserver-xorg-video-nouveau");
        nouveau.builtin = true;
        QCOMPARE(DriverUpdates::stateOf(nouveau), QStringLiteral("builtin"));

        // a driver that brings no kernel module installs as it is
        DriverEntry firmware;
        firmware.package = QStringLiteral("intel-microcode");
        QCOMPARE(DriverUpdates::stateOf(firmware), QStringLiteral("installable"));
        QCOMPARE(DriverUpdates::packagesOf(firmware), QStringList{QStringLiteral("intel-microcode")});
    }
};

QTEST_GUILESS_MAIN(TestDrivers)
#include "test_drivers.moc"
