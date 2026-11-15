// SPDX-License-Identifier: GPL-3.0-or-later
#include "systeminfo.h"

#include <QTest>

class TestSystemInfo : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void secureBoot()
    {
        QCOMPARE(SystemInfo::parseSecureBoot(QByteArray("\x06\x00\x00\x00\x01", 5)), 1);
        QCOMPARE(SystemInfo::parseSecureBoot(QByteArray("\x06\x00\x00\x00\x00", 5)), 0);
        QCOMPARE(SystemInfo::parseSecureBoot(QByteArray("\x06\x00", 2)), -1);
    }

    void crypttab()
    {
        QVERIFY(!SystemInfo::crypttabHasEntries(QStringLiteral("# <target name> <source device>\n\n")));
        QVERIFY(SystemInfo::crypttabHasEntries(QStringLiteral("# header\nluks-123 UUID=abc none luks,discard\n")));
        QVERIFY(!SystemInfo::crypttabHasEntries(QString()));
    }

    void gpus()
    {
        const QString lspci = QStringLiteral(
            "00:02.0 \"VGA compatible controller\" \"Intel Corporation\" \"TigerLake-LP GT2 [Iris Xe Graphics]\" -r01 \"Dell\" \"Device 0a1f\"\n"
            "00:14.3 \"Network controller\" \"Intel Corporation\" \"Wi-Fi 6 AX201\" -r20 \"Intel Corporation\" \"Wi-Fi 6 AX201\"\n"
            "01:00.0 \"3D controller\" \"NVIDIA Corporation\" \"GA107M [GeForce RTX 3050 Mobile]\" -ra1 \"Dell\" \"Device 0a1f\"\n");
        QCOMPARE(SystemInfo::parseGpus(lspci),
                 (QStringList{QStringLiteral("Intel Corporation TigerLake-LP GT2 [Iris Xe Graphics]"),
                              QStringLiteral("NVIDIA Corporation GA107M [GeForce RTX 3050 Mobile]")}));
    }

    // what the Raye kernel card says, from uname -r and the helper
    void kernelState()
    {
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-raye"), QStringLiteral("enrolled\n")), QStringLiteral("running"));
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QStringLiteral("enrolled\n")), QStringLiteral("enrolled"));
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QStringLiteral("pending")), QStringLiteral("pending"));
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QStringLiteral("not-enrolled")), QStringLiteral("off"));
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QStringLiteral("no-secure-boot")), QStringLiteral("off"));
        // installed, waiting for its one trial boot; or that boot failed
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QStringLiteral("trial\n")), QStringLiteral("trial"));
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QStringLiteral("failed")), QStringLiteral("failed"));
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-raye"), QStringLiteral("trial")), QStringLiteral("running"));
        // the helper is not installed, or says something unexpected
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QString()), QStringLiteral("unavailable"));
        QCOMPARE(SystemInfo::kernelState(QStringLiteral("7.0.0-42-generic"), QStringLiteral("garbage")), QStringLiteral("unavailable"));
    }

    // what makes Device security say "Action recommended"
    void actionsNeeded()
    {
        QCOMPARE(SystemInfo::actionsNeeded(1, QStringLiteral("ok"), false, 1), QStringList());
        QCOMPARE(SystemInfo::actionsNeeded(0, QStringLiteral("ok"), false, 1), QStringList{QStringLiteral("secure-boot")});
        // a PC without UEFI: nothing to switch on
        QCOMPARE(SystemInfo::actionsNeeded(-1, QStringLiteral("none"), false, 1), QStringList());
        QCOMPARE(SystemInfo::actionsNeeded(1, QStringLiteral("stale"), false, 1), QStringList{QStringLiteral("tpm")});
        // nobody could tell: not a reason to worry the user
        QCOMPARE(SystemInfo::actionsNeeded(1, QStringLiteral("unknown"), false, 1), QStringList());
        QCOMPARE(SystemInfo::actionsNeeded(1, QStringLiteral("ok"), true, 1), QStringList{QStringLiteral("recovery-key")});
        QCOMPARE(SystemInfo::actionsNeeded(1, QStringLiteral("ok"), false, 0), QStringList{QStringLiteral("firewall")});
        // Raye installs the firewall: missing, it is something to fix
        QCOMPARE(SystemInfo::actionsNeeded(1, QStringLiteral("ok"), false, -1), QStringList{QStringLiteral("firewall")});
        QCOMPARE(SystemInfo::actionsNeeded(0, QStringLiteral("stale"), true, 0),
                 (QStringList{QStringLiteral("secure-boot"), QStringLiteral("tpm"), QStringLiteral("recovery-key"),
                              QStringLiteral("firewall")}));
    }
};

QTEST_GUILESS_MAIN(TestSystemInfo)
#include "test_systeminfo.moc"
