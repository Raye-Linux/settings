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
                 (QStringList{QStringLiteral("nvidia-driver-580-open"), QStringLiteral("linux-modules-nvidia-580-open-generic"),
                              QStringLiteral("nvidia-settings"), QStringLiteral("nvidia-prime")}));
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

    // what ubuntu-drivers lists for an RTX 3060 on Raye 1.0 (resolute):
    // ten NVIDIA drivers, of three branches, server builds included
    void onlyTheRecommendedBranchIsOffered()
    {
        QList<DriverEntry> all;
        for (const char *p : {"nvidia-driver-580", "nvidia-driver-580-open", "nvidia-driver-580-server",
                              "nvidia-driver-580-server-open", "nvidia-driver-595", "nvidia-driver-595-open",
                              "nvidia-driver-595-server", "nvidia-driver-595-server-open", "nvidia-driver-615-open",
                              "nvidia-driver-615-server-open", "xserver-xorg-video-nouveau"}) {
            DriverEntry e;
            e.device = QStringLiteral("NVIDIA Corporation · GA106 [GeForce RTX 3060]");
            e.package = QString::fromLatin1(p);
            e.modules = QStringLiteral("linux-modules-") + e.package.mid(7) + QStringLiteral("-generic-hwe-26.04");
            e.recommended = e.package == QLatin1String("nvidia-driver-595-open");
            e.builtin = e.package.startsWith(QLatin1String("xserver-"));
            all << e;
        }
        QStringList shown;
        for (const DriverEntry &e : DriverUpdates::offered(all, {}))
            shown << e.package;
        QCOMPARE(shown, (QStringList{QStringLiteral("nvidia-driver-595"), QStringLiteral("nvidia-driver-595-open"),
                                     QStringLiteral("xserver-xorg-video-nouveau")}));
        for (const DriverEntry &e : DriverUpdates::offered(all, {}))
            QVERIFY(DriverUpdates::stateOf(e) != QLatin1String("other"));

        // one driver in: the other one says to remove it first, and the
        // installed one (here an older branch picked by hand) stays shown
        const QList<DriverEntry> after = DriverUpdates::offered(all, {QStringLiteral("nvidia-driver-580-server")});
        QStringList states;
        for (const DriverEntry &e : after)
            states << e.package + QLatin1Char('=') + DriverUpdates::stateOf(e);
        QCOMPARE(states, (QStringList{QStringLiteral("nvidia-driver-580-server=installed"),
                                      QStringLiteral("nvidia-driver-595=other"), QStringLiteral("nvidia-driver-595-open=other"),
                                      QStringLiteral("xserver-xorg-video-nouveau=builtin")}));
    }

    void notOfferedOnTheRayeKernel()
    {
        DriverEntry e;
        e.device = QStringLiteral("NVIDIA Corporation · AD104");
        e.package = QStringLiteral("nvidia-driver-595-open");
        e.modules = QStringLiteral("linux-modules-nvidia-595-open-generic-hwe-26.04");
        e.recommended = true;
        QCOMPARE(DriverUpdates::stateOf(DriverUpdates::offered({e}, {}, QStringLiteral("7.0.14-raye")).at(0)),
                 QStringLiteral("kernel"));
        QCOMPARE(DriverUpdates::stateOf(DriverUpdates::offered({e}, {}, QStringLiteral("7.0.0-44-generic")).at(0)),
                 QStringLiteral("installable"));
    }

    void withoutARecommendationTheNewestBranch()
    {
        QList<DriverEntry> all;
        for (const char *p : {"nvidia-driver-580", "nvidia-driver-595-server", "nvidia-driver-595"}) {
            DriverEntry e;
            e.device = QStringLiteral("NVIDIA Corporation · TU106");
            e.package = QString::fromLatin1(p);
            all << e;
        }
        const QList<DriverEntry> shown = DriverUpdates::offered(all, {});
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown.at(0).package, QStringLiteral("nvidia-driver-595"));
    }

    void otherDevicesKeepAllTheirDrivers()
    {
        DriverEntry wifi;
        wifi.device = QStringLiteral("Broadcom");
        wifi.package = QStringLiteral("bcmwl-kernel-source");
        DriverEntry cpu;
        cpu.device = QStringLiteral("Intel");
        cpu.package = QStringLiteral("intel-microcode");
        const QList<DriverEntry> shown = DriverUpdates::offered({wifi, cpu}, {QStringLiteral("nvidia-driver-595")});
        QCOMPARE(shown.size(), 2);
        // the NVIDIA rule is for NVIDIA drivers only
        QVERIFY(!shown.at(0).otherInstalled && !shown.at(1).otherInstalled);
        QCOMPARE(DriverUpdates::packagesOf(cpu), QStringList{QStringLiteral("intel-microcode")});
    }
};

QTEST_GUILESS_MAIN(TestDrivers)
#include "test_drivers.moc"
