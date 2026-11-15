// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update > Firmware updates: the devices fwupd can update and the
// version each one runs, with the newer version when fwupd knows one.
// Reading needs no password. Installing is Raye Update's job: it
// downloads the cabinet with the other updates and installs it at the
// next start-up (see raye-update docs/state.md).
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QProcess;

class FirmwareDevices : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    // false when fwupd is not on this system
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // [{ name, vendor, version, deviceId, to, summary, size }], to empty when up to date
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY changed)

public:
    explicit FirmwareDevices(QObject *parent = nullptr);

    bool checking() const { return m_checking; }
    bool supported() const;
    QVariantList devices() const { return m_devices; }
    int updateCount() const;

    Q_INVOKABLE void refresh();

    // `fwupdmgr get-devices --json`: the devices that can be updated
    static QVariantList parseDevices(const QByteArray &json);
    // `fwupdmgr get-updates --json`: deviceId -> { to, summary, size }
    static QVariantMap parseUpdates(const QByteArray &json);
    static QVariantList merge(const QVariantList &devices, const QVariantMap &updates);

Q_SIGNALS:
    void changed();

private:
    QProcess *run(const QStringList &arguments);

    bool m_checking = false;
    QVariantList m_devices;
};
