// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth & devices: the Bluetooth switch, the devices paired with
// this PC (with their battery), and finding and pairing new ones.
//
// Everything goes through BluezQt, the desktop's client of the system's
// Bluetooth service (org.bluez on the system bus). Turning Bluetooth on
// lifts a radio block first if there is one, then powers the adapter;
// off powers it down, as the tray's switch does. A PIN or a code to
// confirm while pairing is asked by the desktop's Bluetooth agent
// (BlueDevil), which shows it as a notification.
#pragma once

#include <BluezQt/Types>
#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace BluezQt
{
class Manager;
class PendingCall;
}

class BluetoothDeviceList : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        NameRole,
        IconNameRole,
        // "input" (mice, keyboards & pens), "audio" or "other"
        GroupRole,
        ConnectedRole,
        PairedRole,
        // percent, -1 when the device does not say
        BatteryRole,
        // a pair, connect or disconnect is under way
        BusyRole,
    };
    Q_ENUM(Roles)

    // paired: the devices of this PC; else the ones found nearby
    explicit BluetoothDeviceList(bool paired, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDevices(const QList<BluezQt::DevicePtr> &devices, const QSet<QString> &busy);
    // how many devices of a group ("input", "audio", "other") are listed
    Q_INVOKABLE int countIn(const QString &group) const;

Q_SIGNALS:
    void countChanged();
    // after every new reading of the devices
    void devicesChanged();

private:
    bool m_paired;
    QList<BluezQt::DevicePtr> m_devices;
    QSet<QString> m_busy;
};

class BluetoothSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // there is a Bluetooth adapter and the service runs
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered NOTIFY changed)
    // the name other devices see ("Discoverable as ...")
    Q_PROPERTY(QString adapterName READ adapterName NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(BluetoothDeviceList *pairedDevices READ pairedDevices CONSTANT)
    Q_PROPERTY(BluetoothDeviceList *nearbyDevices READ nearbyDevices CONSTANT)

public:
    explicit BluetoothSettings(QObject *parent = nullptr);
    ~BluetoothSettings() override;

    bool available() const;
    bool powered() const;
    QString adapterName() const;
    bool scanning() const;
    BluetoothDeviceList *pairedDevices() const { return m_paired; }
    BluetoothDeviceList *nearbyDevices() const { return m_nearby; }

    Q_INVOKABLE void setPowered(bool on);
    // look for devices nearby until stopScan() (the Add a device dialog)
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    // pair, trust and connect a device found nearby
    Q_INVOKABLE bool pair(const QString &address);
    Q_INVOKABLE bool connectDevice(const QString &address);
    Q_INVOKABLE bool disconnectDevice(const QString &address);
    // forget a paired device
    Q_INVOKABLE bool removeDevice(const QString &address);

    // --- the parts the tests drive ---
    static QString groupOf(int deviceType);
    static QString iconFor(int deviceType, const QString &icon);
    // connected first, then by name
    static bool before(bool connectedA, const QString &nameA, bool connectedB, const QString &nameB);

Q_SIGNALS:
    void changed();
    // after pair(): ok, and what went wrong in plain words when not
    void pairingFinished(const QString &address, bool ok, const QString &message);
    void actionFailed(const QString &address, const QString &message);

private:
    void refresh();
    void watchAdapter();
    BluezQt::AdapterPtr adapter() const;
    BluezQt::DevicePtr device(const QString &address) const;
    void track(BluezQt::PendingCall *call, const QString &address, bool pairing);
    static QString plainError(int error);

    BluezQt::Manager *m_manager = nullptr;
    BluetoothDeviceList *m_paired = nullptr;
    BluetoothDeviceList *m_nearby = nullptr;
    QSet<QString> m_busy;
    bool m_wantScan = false;
    QPointer<QObject> m_adapterWatch;
};
