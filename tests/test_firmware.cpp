// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update > Firmware updates: what fwupdmgr prints as JSON, read
// into devices and their updates.
#include "firmware.h"

#include <QTest>

namespace {
const QByteArray devices = R"({
  "Devices": [
    { "Name": "System Firmware", "Vendor": "Dell Inc.", "Version": "1.27.0",
      "DeviceId": "a45df35ac0e948ee180fe216a5f703f32dda163f", "Flags": ["internal", "updatable", "require-ac"] },
    { "Name": "UEFI dbx", "Vendor": "UEFI:Linux Foundation", "Version": "371",
      "DeviceId": "362301da643102b9f38477387e2193e57abaa590", "Flags": ["internal", "updatable"] },
    { "Name": "Core i5-1145G7", "Vendor": "Intel", "Version": "0x000000b8",
      "DeviceId": "4bde70ba4e39b28f9eab1628f9dd6e6244c03027", "Flags": ["internal"] },
    { "Name": "No version", "DeviceId": "ffff", "Flags": ["updatable"] }
  ]
})";
const QByteArray updates = R"({
  "Devices": [
    { "Name": "UEFI dbx", "DeviceId": "362301da643102b9f38477387e2193e57abaa590", "Version": "371",
      "Releases": [ { "Version": "377", "Summary": "UEFI revocation database", "Size": 13501 },
                    { "Version": "375", "Summary": "older" } ] }
  ]
})";
} // namespace

class TestFirmware : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void devicesAreRead()
    {
        const QVariantList list = FirmwareDevices::parseDevices(devices);
        // only what fwupd can update, with a version to show
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("System Firmware"));
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("1.27.0"));
        QVERIFY(FirmwareDevices::parseDevices("not json").isEmpty());
    }

    void updatesAreMerged()
    {
        const QVariantMap found = FirmwareDevices::parseUpdates(updates);
        QCOMPARE(found.size(), 1);
        const QVariantList merged = FirmwareDevices::merge(FirmwareDevices::parseDevices(devices), found);
        // the device with an update comes first, with the newest release
        const QVariantMap first = merged.at(0).toMap();
        QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("UEFI dbx"));
        QCOMPARE(first.value(QStringLiteral("to")).toString(), QStringLiteral("377"));
        QCOMPARE(first.value(QStringLiteral("size")).toLongLong(), 13501);
        QVERIFY(merged.at(1).toMap().value(QStringLiteral("to")).toString().isEmpty());
        // "nothing to do" prints no JSON
        QVERIFY(FirmwareDevices::parseUpdates(QByteArray()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFirmware)
#include "test_firmware.moc"
