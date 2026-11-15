// SPDX-License-Identifier: GPL-3.0-or-later
#include "bluetooth.h"

#include <BluezQt/Adapter>
#include <BluezQt/Battery>
#include <BluezQt/Device>
#include <BluezQt/InitManagerJob>
#include <BluezQt/Manager>
#include <BluezQt/PendingCall>
#include <KLocalizedString>
#include <QCollator>
#include <QTimer>

#include <algorithm>

// --- the lists ---

BluetoothDeviceList::BluetoothDeviceList(bool paired, QObject *parent)
    : QAbstractListModel(parent)
    , m_paired(paired)
{
}

int BluetoothDeviceList::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant BluetoothDeviceList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return {};
    const BluezQt::DevicePtr &d = m_devices.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return d->name();
    case AddressRole:
        return d->address();
    case IconNameRole:
        return BluetoothSettings::iconFor(d->type(), d->icon());
    case GroupRole:
        return BluetoothSettings::groupOf(d->type());
    case ConnectedRole:
        return d->isConnected();
    case PairedRole:
        return d->isPaired();
    case BatteryRole:
        return d->battery() ? d->battery()->percentage() : -1;
    case BusyRole:
        return m_busy.contains(d->address());
    }
    return {};
}

QHash<int, QByteArray> BluetoothDeviceList::roleNames() const
{
    return {{AddressRole, "address"},     {NameRole, "name"},     {IconNameRole, "iconName"}, {GroupRole, "group"},
            {ConnectedRole, "connected"}, {PairedRole, "paired"}, {BatteryRole, "battery"},   {BusyRole, "busy"}};
}

void BluetoothDeviceList::setDevices(const QList<BluezQt::DevicePtr> &devices, const QSet<QString> &busy)
{
    QList<BluezQt::DevicePtr> list;
    for (const BluezQt::DevicePtr &d : devices) {
        if (d->isPaired() != m_paired)
            continue;
        // found nearby, but nothing to call it by: not offered (Windows does the same)
        if (!m_paired && d->remoteName().isEmpty())
            continue;
        list << d;
    }
    std::sort(list.begin(), list.end(), [](const BluezQt::DevicePtr &a, const BluezQt::DevicePtr &b) {
        return BluetoothSettings::before(a->isConnected(), a->name(), b->isConnected(), b->name());
    });
    const int before = m_devices.size();
    beginResetModel();
    m_devices = list;
    m_busy = busy;
    endResetModel();
    if (before != m_devices.size())
        Q_EMIT countChanged();
    Q_EMIT devicesChanged();
}

int BluetoothDeviceList::countIn(const QString &group) const
{
    return int(std::count_if(m_devices.cbegin(), m_devices.cend(), [&group](const BluezQt::DevicePtr &d) {
        return BluetoothSettings::groupOf(d->type()) == group;
    }));
}

// --- the settings ---

BluetoothSettings::BluetoothSettings(QObject *parent)
    : QObject(parent)
    , m_manager(new BluezQt::Manager(this))
    , m_paired(new BluetoothDeviceList(true, this))
    , m_nearby(new BluetoothDeviceList(false, this))
{
    auto *coalesce = new QTimer(this);
    coalesce->setSingleShot(true);
    coalesce->setInterval(50);
    connect(coalesce, &QTimer::timeout, this, &BluetoothSettings::refresh);
    auto later = [coalesce] { coalesce->start(); };

    connect(m_manager, &BluezQt::Manager::operationalChanged, this, later);
    connect(m_manager, &BluezQt::Manager::bluetoothBlockedChanged, this, later);
    connect(m_manager, &BluezQt::Manager::usableAdapterChanged, this, [this, later] {
        watchAdapter();
        later();
    });
    connect(m_manager, &BluezQt::Manager::adapterChanged, this, later);
    connect(m_manager, &BluezQt::Manager::deviceAdded, this, later);
    connect(m_manager, &BluezQt::Manager::deviceRemoved, this, later);
    connect(m_manager, &BluezQt::Manager::deviceChanged, this, later);

    BluezQt::InitManagerJob *job = m_manager->init();
    connect(job, &BluezQt::InitManagerJob::result, this, [this] {
        watchAdapter();
        refresh();
    });
    job->start();
}

BluetoothSettings::~BluetoothSettings()
{
    // the search is the dialog's: it ends with the page
    if (m_wantScan)
        stopScan();
}

QString BluetoothSettings::groupOf(int deviceType)
{
    switch (deviceType) {
    case BluezQt::Device::Keyboard:
    case BluezQt::Device::Mouse:
    case BluezQt::Device::Tablet:
    case BluezQt::Device::Peripheral:
        return QStringLiteral("input");
    case BluezQt::Device::Headset:
    case BluezQt::Device::Headphones:
    case BluezQt::Device::AudioVideo:
        return QStringLiteral("audio");
    }
    return QStringLiteral("other");
}

QString BluetoothSettings::iconFor(int deviceType, const QString &icon)
{
    if (!icon.isEmpty())
        return icon;
    switch (deviceType) {
    case BluezQt::Device::Keyboard:
        return QStringLiteral("input-keyboard");
    case BluezQt::Device::Mouse:
        return QStringLiteral("input-mouse");
    case BluezQt::Device::Tablet:
        return QStringLiteral("input-tablet");
    case BluezQt::Device::Headset:
    case BluezQt::Device::Headphones:
        return QStringLiteral("audio-headphones");
    case BluezQt::Device::AudioVideo:
        return QStringLiteral("audio-speakers");
    case BluezQt::Device::Phone:
        return QStringLiteral("smartphone");
    case BluezQt::Device::Computer:
        return QStringLiteral("computer");
    case BluezQt::Device::Joypad:
        return QStringLiteral("input-gamepad");
    case BluezQt::Device::Printer:
        return QStringLiteral("printer");
    case BluezQt::Device::Camera:
        return QStringLiteral("camera-photo");
    }
    return QStringLiteral("preferences-system-bluetooth");
}

bool BluetoothSettings::before(bool connectedA, const QString &nameA, bool connectedB, const QString &nameB)
{
    if (connectedA != connectedB)
        return connectedA;
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(nameA, nameB) < 0;
}

BluezQt::AdapterPtr BluetoothSettings::adapter() const
{
    BluezQt::AdapterPtr a = m_manager->usableAdapter();
    if (!a && !m_manager->adapters().isEmpty())
        a = m_manager->adapters().first();
    return a;
}

BluezQt::DevicePtr BluetoothSettings::device(const QString &address) const
{
    return m_manager->deviceForAddress(address);
}

bool BluetoothSettings::available() const
{
    return m_manager->isInitialized() && m_manager->isOperational() && adapter();
}

bool BluetoothSettings::powered() const
{
    const BluezQt::AdapterPtr a = adapter();
    return a && a->isPowered() && !m_manager->isBluetoothBlocked();
}

QString BluetoothSettings::adapterName() const
{
    const BluezQt::AdapterPtr a = adapter();
    return a ? a->name() : QString();
}

bool BluetoothSettings::scanning() const
{
    const BluezQt::AdapterPtr a = adapter();
    return a && a->isDiscovering();
}

void BluetoothSettings::watchAdapter()
{
    // when the adapter comes back on, a search the dialog asked for goes on
    delete m_adapterWatch;
    const BluezQt::AdapterPtr a = adapter();
    if (!a)
        return;
    auto *watch = new QObject(this);
    m_adapterWatch = watch;
    connect(a.data(), &BluezQt::Adapter::poweredChanged, watch, [this](bool on) {
        if (on && m_wantScan)
            startScan();
        Q_EMIT changed();
    });
    connect(a.data(), &BluezQt::Adapter::discoveringChanged, watch, [this] { Q_EMIT changed(); });
    connect(a.data(), &BluezQt::Adapter::nameChanged, watch, [this] { Q_EMIT changed(); });
}

void BluetoothSettings::refresh()
{
    const QList<BluezQt::DevicePtr> devices = m_manager->isInitialized() ? m_manager->devices() : QList<BluezQt::DevicePtr>();
    m_paired->setDevices(devices, m_busy);
    m_nearby->setDevices(devices, m_busy);
    // a battery tells its level on its own object
    for (const BluezQt::DevicePtr &d : devices) {
        if (d->battery())
            connect(d->battery().data(), &BluezQt::Battery::percentageChanged, this, &BluetoothSettings::refresh, Qt::UniqueConnection);
    }
    Q_EMIT changed();
}

void BluetoothSettings::setPowered(bool on)
{
    if (on && m_manager->isBluetoothBlocked())
        m_manager->setBluetoothBlocked(false);
    const BluezQt::AdapterPtr a = adapter();
    if (!a)
        return;
    BluezQt::PendingCall *call = a->setPowered(on);
    connect(call, &BluezQt::PendingCall::finished, this, [this](BluezQt::PendingCall *c) {
        if (c->error() != BluezQt::PendingCall::NoError)
            Q_EMIT actionFailed(QString(), plainError(c->error()));
        refresh();
    });
}

void BluetoothSettings::startScan()
{
    m_wantScan = true;
    const BluezQt::AdapterPtr a = adapter();
    if (!a || !a->isPowered() || a->isDiscovering())
        return;
    a->startDiscovery();
}

void BluetoothSettings::stopScan()
{
    m_wantScan = false;
    const BluezQt::AdapterPtr a = adapter();
    if (a && a->isDiscovering())
        a->stopDiscovery();
}

QString BluetoothSettings::plainError(int error)
{
    switch (error) {
    case BluezQt::PendingCall::AuthenticationCanceled:
    case BluezQt::PendingCall::Canceled:
        return i18n("Pairing was canceled.");
    case BluezQt::PendingCall::AuthenticationFailed:
    case BluezQt::PendingCall::AuthenticationRejected:
        return i18n("The code did not match. Try again.");
    case BluezQt::PendingCall::AuthenticationTimeout:
    case BluezQt::PendingCall::ConnectionAttemptFailed:
    case BluezQt::PendingCall::ConnectFailed:
        return i18n("The device did not answer. Make sure it is on and close to this PC.");
    case BluezQt::PendingCall::NotReady:
        return i18n("Bluetooth is not ready. Turn it on and try again.");
    }
    return i18n("That did not work. Try again.");
}

void BluetoothSettings::track(BluezQt::PendingCall *call, const QString &address, bool pairing)
{
    m_busy.insert(address);
    refresh();
    connect(call, &BluezQt::PendingCall::finished, this, [this, address, pairing](BluezQt::PendingCall *c) {
        const bool ok = c->error() == BluezQt::PendingCall::NoError;
        m_busy.remove(address);
        if (pairing) {
            if (ok) {
                // as Windows does: once paired it is trusted and in use
                if (BluezQt::DevicePtr d = device(address)) {
                    d->setTrusted(true);
                    if (!d->isConnected())
                        d->connectToDevice();
                }
            }
            Q_EMIT pairingFinished(address, ok, ok ? QString() : plainError(c->error()));
        } else if (!ok) {
            Q_EMIT actionFailed(address, plainError(c->error()));
        }
        refresh();
    });
}

bool BluetoothSettings::pair(const QString &address)
{
    BluezQt::DevicePtr d = device(address);
    if (!d || m_busy.contains(address))
        return false;
    // the search slows pairing down: it has done its job
    stopScan();
    track(d->pair(), address, true);
    return true;
}

bool BluetoothSettings::connectDevice(const QString &address)
{
    BluezQt::DevicePtr d = device(address);
    if (!d || m_busy.contains(address))
        return false;
    track(d->connectToDevice(), address, false);
    return true;
}

bool BluetoothSettings::disconnectDevice(const QString &address)
{
    BluezQt::DevicePtr d = device(address);
    if (!d || m_busy.contains(address))
        return false;
    track(d->disconnectFromDevice(), address, false);
    return true;
}

bool BluetoothSettings::removeDevice(const QString &address)
{
    BluezQt::DevicePtr d = device(address);
    if (!d || !d->adapter())
        return false;
    track(d->adapter()->removeDevice(d), address, false);
    return true;
}
