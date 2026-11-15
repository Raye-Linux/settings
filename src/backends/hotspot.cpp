// SPDX-License-Identifier: GPL-3.0-or-later
#include "hotspot.h"

#include <KLocalizedString>

#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/WirelessDevice>
#include <NetworkManagerQt/WirelessSetting>

#include <QDBusPendingCallWatcher>
#include <QFile>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QUuid>

namespace {

NetworkManager::WirelessDevice::Ptr accessPointDevice()
{
    const auto devices = NetworkManager::networkInterfaces();
    for (const auto &device : devices) {
        if (device->type() != NetworkManager::Device::Wifi || !device->managed())
            continue;
        auto wifi = device.objectCast<NetworkManager::WirelessDevice>();
        if (wifi && wifi->wirelessCapabilities() & NetworkManager::WirelessDevice::ApCap)
            return wifi;
    }
    return {};
}

int wifiDeviceCount()
{
    int count = 0;
    const auto devices = NetworkManager::networkInterfaces();
    for (const auto &device : devices)
        count += device->type() == NetworkManager::Device::Wifi && device->managed() ? 1 : 0;
    return count;
}

NetworkManager::Connection::Ptr hotspotConnection()
{
    const auto connections = NetworkManager::listConnections();
    for (const auto &connection : connections) {
        const auto settings = connection->settings();
        if (settings->connectionType() != NetworkManager::ConnectionSettings::Wireless
            || settings->id() != HotspotText::connectionId())
            continue;
        auto wireless = settings->setting(NetworkManager::Setting::Wireless).staticCast<NetworkManager::WirelessSetting>();
        if (wireless && wireless->mode() == NetworkManager::WirelessSetting::Ap)
            return connection;
    }
    return {};
}

} // namespace

namespace HotspotText
{

QString connectionId()
{
    return QStringLiteral("Raye hotspot");
}

QString defaultName(const QString &hostname)
{
    QString name = i18nc("@info default hotspot name, %1 is the computer name", "%1 hotspot",
                         hostname.isEmpty() ? QStringLiteral("Raye") : hostname);
    // a network name is at most 32 bytes: shorten the computer's name
    QString host = hostname;
    while (name.toUtf8().size() > 32 && !host.isEmpty()) {
        host.chop(1);
        name = i18nc("@info default hotspot name, %1 is the computer name", "%1 hotspot", host);
    }
    return name.toUtf8().size() > 32 ? QStringLiteral("Raye hotspot") : name;
}

QString newPassword()
{
    // no 0/O, 1/l/I: read out loud or off a screen without a mistake
    static const QString letters = QStringLiteral("abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789");
    QString password;
    auto *random = QRandomGenerator::system();
    for (int i = 0; i < 12; ++i)
        password += letters.at(random->bounded(letters.size()));
    return password;
}

bool nameAcceptable(const QString &name)
{
    const int size = name.toUtf8().size();
    return size >= 1 && size <= 32 && !name.contains(QLatin1Char('\n'));
}

bool passwordAcceptable(const QString &password)
{
    if (password.size() < 8 || password.size() > 63)
        return false;
    for (const QChar c : password) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e)
            return false;
    }
    return true;
}

NMVariantMapMap settings(const QString &uuid, const QString &name, const QString &password, const QString &band)
{
    NMVariantMapMap map;
    map.insert(QStringLiteral("connection"), QVariantMap{{QStringLiteral("id"), connectionId()},
                                                         {QStringLiteral("uuid"), uuid},
                                                         {QStringLiteral("type"), QStringLiteral("802-11-wireless")},
                                                         {QStringLiteral("autoconnect"), false}});
    QVariantMap wireless{{QStringLiteral("ssid"), name.toUtf8()}, {QStringLiteral("mode"), QStringLiteral("ap")}};
    if (band == QLatin1String("5"))
        wireless.insert(QStringLiteral("band"), QStringLiteral("a"));
    else if (band == QLatin1String("2.4"))
        wireless.insert(QStringLiteral("band"), QStringLiteral("bg"));
    map.insert(QStringLiteral("802-11-wireless"), wireless);
    map.insert(QStringLiteral("802-11-wireless-security"),
               QVariantMap{{QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk")},
                           {QStringLiteral("proto"), QStringList{QStringLiteral("rsn")}},
                           {QStringLiteral("pairwise"), QStringList{QStringLiteral("ccmp")}},
                           {QStringLiteral("group"), QStringList{QStringLiteral("ccmp")}},
                           {QStringLiteral("psk"), password}});
    // shared: NetworkManager hands out addresses and routes to the internet
    map.insert(QStringLiteral("ipv4"), QVariantMap{{QStringLiteral("method"), QStringLiteral("shared")}});
    map.insert(QStringLiteral("ipv6"), QVariantMap{{QStringLiteral("method"), QStringLiteral("ignore")}});
    return map;
}

int leases(const QByteArray &file)
{
    int count = 0;
    const auto lines = file.split('\n');
    for (const QByteArray &line : lines)
        count += line.trimmed().isEmpty() ? 0 : 1;
    return count;
}

} // namespace HotspotText

Hotspot::Hotspot(QObject *parent)
    : QObject(parent)
{
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(50);
    connect(&m_refresh, &QTimer::timeout, this, &Hotspot::refresh);
    m_clientsTimer.setInterval(5000);
    connect(&m_clientsTimer, &QTimer::timeout, this, &Hotspot::refresh);

    auto *notifier = NetworkManager::notifier();
    connect(notifier, &NetworkManager::Notifier::activeConnectionsChanged, this, &Hotspot::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::primaryConnectionChanged, this, &Hotspot::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::deviceAdded, this, &Hotspot::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::deviceRemoved, this, &Hotspot::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::wirelessEnabledChanged, this, &Hotspot::scheduleRefresh);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionAdded, this,
            &Hotspot::scheduleRefresh);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionRemoved, this,
            &Hotspot::scheduleRefresh);

    m_name = HotspotText::defaultName(QSysInfo::machineHostName());
    m_password = HotspotText::newPassword();
    refresh();
}

void Hotspot::scheduleRefresh()
{
    m_refresh.start();
}

bool Hotspot::available() const
{
    return !accessPointDevice().isNull() && NetworkManager::isWirelessEnabled();
}

bool Hotspot::band5Available() const
{
    auto device = accessPointDevice();
    return device && device->wirelessCapabilities() & NetworkManager::WirelessDevice::Freq5Ghz;
}

void Hotspot::refresh()
{
    auto connection = hotspotConnection();
    const QString uuid = connection ? connection->uuid() : QString();
    if (connection && uuid != m_uuid) {
        // read what was saved, the password with it
        const auto settings = connection->settings();
        auto wireless = settings->setting(NetworkManager::Setting::Wireless).staticCast<NetworkManager::WirelessSetting>();
        m_name = QString::fromUtf8(wireless->ssid());
        m_band = wireless->band() == NetworkManager::WirelessSetting::A    ? QStringLiteral("5")
            : wireless->band() == NetworkManager::WirelessSetting::Bg ? QStringLiteral("2.4")
                                                                      : QStringLiteral("any");
        auto secrets = connection->secrets(QStringLiteral("802-11-wireless-security"));
        secrets.waitForFinished();
        if (!secrets.isError()) {
            const QString psk = secrets.value().value(QStringLiteral("802-11-wireless-security")).value(QStringLiteral("psk")).toString();
            if (!psk.isEmpty())
                m_password = psk;
        }
    }
    m_uuid = uuid;

    m_active = false;
    m_busy = false;
    QString hotspotDevice;
    const auto actives = NetworkManager::activeConnections();
    for (const auto &active : actives) {
        if (m_uuid.isEmpty() || active->uuid() != m_uuid)
            continue;
        m_active = active->state() == NetworkManager::ActiveConnection::Activated;
        m_busy = active->state() == NetworkManager::ActiveConnection::Activating
            || active->state() == NetworkManager::ActiveConnection::Deactivating;
        if (!active->devices().isEmpty())
            hotspotDevice = active->devices().first();
    }

    // what would be shared: the primary connection, if it is not us
    m_source.clear();
    m_ownWifi = false;
    auto primary = NetworkManager::primaryConnection();
    if (primary && primary->isValid() && primary->uuid() != m_uuid) {
        m_source = primary->id();
        auto device = accessPointDevice();
        const bool sameCard = device && primary->devices().contains(device->uni());
        m_ownWifi = sameCard && wifiDeviceCount() < 2;
    }

    m_clients = 0;
    if (m_active) {
        QString iface;
        if (auto device = NetworkManager::findNetworkInterface(hotspotDevice))
            iface = device->interfaceName();
        QFile file(QStringLiteral("/var/lib/NetworkManager/dnsmasq-%1.leases").arg(iface));
        if (!iface.isEmpty() && !iface.contains(QLatin1Char('/')) && file.open(QIODevice::ReadOnly))
            m_clients = HotspotText::leases(file.read(64 * 1024));
        if (!m_clientsTimer.isActive())
            m_clientsTimer.start();
    } else {
        m_clientsTimer.stop();
    }
    Q_EMIT changed();
}

QString Hotspot::store(const QString &name, const QString &password, const QString &band)
{
    if (!HotspotText::nameAcceptable(name))
        return i18nc("@info", "The network name must be 1 to 32 characters.");
    if (!HotspotText::passwordAcceptable(password))
        return i18nc("@info", "The password must be 8 to 63 characters, letters, digits and symbols without accents.");
    const QString chosenBand = band == QLatin1String("5") || band == QLatin1String("2.4") ? band : QStringLiteral("any");
    auto connection = hotspotConnection();
    const QString uuid = connection ? connection->uuid() : QUuid::createUuid().toString(QUuid::WithoutBraces);
    const NMVariantMapMap map = HotspotText::settings(uuid, name, password, chosenBand);
    if (connection) {
        auto reply = connection->update(map);
        reply.waitForFinished();
        if (reply.isError())
            return i18nc("@info", "Raye couldn't save the hotspot. %1", reply.error().message());
    } else {
        auto reply = NetworkManager::addConnection(map);
        reply.waitForFinished();
        if (reply.isError())
            return i18nc("@info", "Raye couldn't save the hotspot. %1", reply.error().message());
    }
    m_uuid = uuid;
    m_name = name;
    m_password = password;
    m_band = chosenBand;
    Q_EMIT changed();
    return {};
}

QString Hotspot::setActive(bool on)
{
    if (on == m_active && !m_busy)
        return {};
    if (!on) {
        const auto actives = NetworkManager::activeConnections();
        for (const auto &active : actives) {
            if (active->uuid() == m_uuid)
                NetworkManager::deactivateConnection(active->path());
        }
        return {};
    }
    auto device = accessPointDevice();
    if (!device)
        return i18nc("@info", "This PC has no Wi-Fi adapter that can share a connection.");
    if (!NetworkManager::isWirelessEnabled())
        return i18nc("@info", "Turn on Wi-Fi first.");
    if (const QString problem = store(m_name, m_password, m_band); !problem.isEmpty())
        return problem;
    auto connection = hotspotConnection();
    if (!connection)
        return i18nc("@info", "Raye couldn't save the hotspot.");
    m_busy = true;
    Q_EMIT changed();
    auto reply = NetworkManager::activateConnection(connection->path(), device->uni(), QStringLiteral("/"));
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError()) {
            m_busy = false;
            Q_EMIT changed();
            Q_EMIT failed(i18nc("@info", "The hotspot couldn't start. %1", call->error().message()));
        }
    });
    return {};
}

QString Hotspot::save(const QString &name, const QString &password, const QString &band)
{
    const QString problem = store(name.trimmed(), password, band);
    if (!problem.isEmpty())
        return problem;
    // a running hotspot takes the new name and password at once
    if (m_active) {
        if (auto connection = hotspotConnection()) {
            if (auto device = accessPointDevice())
                NetworkManager::activateConnection(connection->path(), device->uni(), QStringLiteral("/"));
        }
    }
    return {};
}
