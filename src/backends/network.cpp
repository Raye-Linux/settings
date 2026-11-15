// SPDX-License-Identifier: GPL-3.0-or-later
#include "network.h"

#include <BluezQt/Adapter>
#include <BluezQt/InitManagerJob>
#include <BluezQt/Manager>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <NetworkManagerQt/AccessPoint>
#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/DeviceStatistics>
#include <NetworkManagerQt/IpAddress>
#include <NetworkManagerQt/Ipv4Setting>
#include <NetworkManagerQt/Ipv6Setting>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/WiredDevice>
#include <NetworkManagerQt/WirelessDevice>
#include <NetworkManagerQt/WirelessSetting>

#include <QClipboard>
#include <QGuiApplication>
#include <QHostAddress>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

namespace {

// where Raye keeps what it must remember between two runs
KConfigGroup rayeGroup()
{
    return KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"))->group(QStringLiteral("Network"));
}

QStringList strings(const QVariant &value)
{
    if (value.canConvert<QStringList>())
        return value.toStringList();
    return {value.toString()};
}

bool isStrictIpv4(const QString &text)
{
    static const QRegularExpression shape(QStringLiteral("^\\d{1,3}(\\.\\d{1,3}){3}$"));
    if (!shape.match(text).hasMatch())
        return false;
    const auto parts = text.split(QLatin1Char('.'));
    for (const QString &part : parts) {
        if (part.toInt() > 255 || (part.size() > 1 && part.startsWith(QLatin1Char('0'))))
            return false;
    }
    return true;
}

QString checkFamily(const QVariantMap &family, bool v6)
{
    const QString name = v6 ? QStringLiteral("ipv6") : QStringLiteral("ipv4");
    const QString method = family.value(QStringLiteral("method")).toString();
    if (method == QLatin1String("auto") || (v6 && method == QLatin1String("off")))
        return {};
    if (method != QLatin1String("manual"))
        return name + QStringLiteral("-address");
    const QString address = family.value(QStringLiteral("address")).toString().trimmed();
    if (!(v6 ? NetworkText::isIpv6(address) : NetworkText::isIpv4(address)))
        return name + QStringLiteral("-address");
    int prefix = -1;
    const QString prefixText = family.value(QStringLiteral("prefix")).toString().trimmed();
    if (v6) {
        bool ok = false;
        prefix = prefixText.toInt(&ok);
        if (!ok || prefix < 1 || prefix > 128)
            return name + QStringLiteral("-prefix");
    } else {
        prefix = NetworkText::ipv4Prefix(prefixText);
        if (prefix < 0)
            return name + QStringLiteral("-prefix");
    }
    const QString gateway = family.value(QStringLiteral("gateway")).toString().trimmed();
    if (!gateway.isEmpty()) {
        if (v6 ? !NetworkText::isIpv6(gateway) : !NetworkText::isIpv4(gateway))
            return name + QStringLiteral("-gateway");
        if (!v6 && (gateway == address || !NetworkText::sameIpv4Network(address, gateway, prefix)))
            return name + QStringLiteral("-gateway");
    }
    return {};
}

QString deviceKind(NetworkManager::Device::Type type)
{
    switch (type) {
    case NetworkManager::Device::Wifi:
        return QStringLiteral("wifi");
    case NetworkManager::Device::Ethernet:
        return QStringLiteral("ethernet");
    case NetworkManager::Device::Modem:
        return QStringLiteral("modem");
    default:
        return {};
    }
}

QString speedText(int kbits)
{
    if (kbits <= 0)
        return {};
    if (kbits >= 1000000) {
        const double gbits = kbits / 1000000.0;
        return i18nc("@info link speed", "%1 Gbps", QLocale().toString(gbits, 'g', 3));
    }
    return i18nc("@info link speed", "%1 Mbps", kbits / 1000);
}

bool activated(const NetworkManager::Device::Ptr &device)
{
    return device->state() == NetworkManager::Device::Activated;
}

QString firstAddress(const NetworkManager::IpConfig &config, bool v6)
{
    const auto addresses = config.addresses();
    for (const auto &address : addresses) {
        const QHostAddress ip = address.ip();
        if (v6 && ip.isLinkLocal())
            continue;
        return ip.toString();
    }
    return {};
}

QStringList nameservers(const NetworkManager::IpConfig &config)
{
    QStringList list;
    const auto servers = config.nameservers();
    for (const auto &server : servers)
        list.append(server.toString());
    return list;
}

// the settings of a saved connection with the secrets it keeps, so
// saving them back does not lose the Wi-Fi password
NMVariantMapMap withSecrets(const NetworkManager::Connection::Ptr &connection,
                            const NetworkManager::ConnectionSettings::Ptr &settings)
{
    NMVariantMapMap map = settings->toMap();
    for (const char *name : {"802-11-wireless-security", "802-1x"}) {
        const QString section = QString::fromLatin1(name);
        if (!map.contains(section))
            continue;
        auto reply = connection->secrets(section);
        reply.waitForFinished();
        if (reply.isError())
            continue;
        const NMVariantMapMap secrets = reply.value();
        for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
            QVariantMap merged = map.value(it.key());
            for (auto value = it.value().cbegin(); value != it.value().cend(); ++value)
                merged.insert(value.key(), value.value());
            map.insert(it.key(), merged);
        }
    }
    return map;
}

NetworkManager::Device::Ptr deviceOf(const NetworkManager::ActiveConnection::Ptr &active)
{
    if (!active)
        return {};
    const QStringList devices = active->devices();
    return devices.isEmpty() ? NetworkManager::Device::Ptr() : NetworkManager::findNetworkInterface(devices.first());
}

} // namespace

namespace NetworkText
{

int signalBars(int strength)
{
    if (strength >= 75)
        return 4;
    if (strength >= 50)
        return 3;
    if (strength >= 25)
        return 2;
    if (strength > 0)
        return 1;
    return 0;
}

bool isIpv4(const QString &text)
{
    return isStrictIpv4(text);
}

bool isIpv6(const QString &text)
{
    if (!text.contains(QLatin1Char(':')) || text.contains(QLatin1Char('%')) || text.trimmed() != text)
        return false;
    QHostAddress address;
    return address.setAddress(text) && address.protocol() == QAbstractSocket::IPv6Protocol;
}

int ipv4Prefix(const QString &text)
{
    bool number = false;
    const int prefix = text.toInt(&number);
    if (number)
        return prefix >= 1 && prefix <= 32 ? prefix : -1;
    if (!isStrictIpv4(text))
        return -1;
    const quint32 mask = QHostAddress(text).toIPv4Address();
    // a mask is ones then zeros, nothing else
    const quint32 inverted = ~mask;
    if (mask == 0 || (inverted & (inverted + 1)) != 0)
        return -1;
    int ones = 0;
    for (quint32 m = mask; m; m <<= 1)
        ++ones;
    return ones;
}

QString ipv4Mask(int prefix)
{
    if (prefix < 0 || prefix > 32)
        return {};
    const quint32 mask = prefix == 0 ? 0 : 0xffffffffu << (32 - prefix);
    return QHostAddress(mask).toString();
}

bool sameIpv4Network(const QString &a, const QString &b, int prefix)
{
    if (!isStrictIpv4(a) || !isStrictIpv4(b) || prefix < 1 || prefix > 32)
        return false;
    return QHostAddress(a).isInSubnet(QHostAddress(b), prefix);
}

QString checkIpConfig(const QVariantMap &config)
{
    if (config.contains(QStringLiteral("ipv4"))) {
        const QString problem = checkFamily(config.value(QStringLiteral("ipv4")).toMap(), false);
        if (!problem.isEmpty())
            return problem;
    }
    if (config.contains(QStringLiteral("ipv6"))) {
        const QString problem = checkFamily(config.value(QStringLiteral("ipv6")).toMap(), true);
        if (!problem.isEmpty())
            return problem;
    }
    if (config.contains(QStringLiteral("dnsAuto")) && !config.value(QStringLiteral("dnsAuto")).toBool()) {
        int servers = 0;
        for (const char *family : {"dns4", "dns6"}) {
            const bool v6 = family[3] == '6';
            const QStringList list = strings(config.value(QString::fromLatin1(family)));
            for (const QString &entry : list) {
                const QString server = entry.trimmed();
                if (server.isEmpty())
                    continue;
                if (v6 ? !isIpv6(server) : !isIpv4(server))
                    return QString::fromLatin1(family);
                ++servers;
            }
        }
        if (servers == 0)
            return QStringLiteral("dns4");
    }
    return {};
}

QString errorText(const QString &field)
{
    if (field == QLatin1String("ipv4-address"))
        return i18nc("@info", "Enter an IPv4 address, like 192.168.1.20.");
    if (field == QLatin1String("ipv4-prefix"))
        return i18nc("@info", "Enter a subnet mask, like 255.255.255.0, or a prefix length from 1 to 32.");
    if (field == QLatin1String("ipv4-gateway"))
        return i18nc("@info", "The gateway must be an IPv4 address in the same network as this PC.");
    if (field == QLatin1String("ipv6-address"))
        return i18nc("@info", "Enter an IPv6 address, like 2001:db8::20.");
    if (field == QLatin1String("ipv6-prefix"))
        return i18nc("@info", "Enter a subnet prefix length from 1 to 128.");
    if (field == QLatin1String("ipv6-gateway"))
        return i18nc("@info", "The gateway must be an IPv6 address.");
    if (field == QLatin1String("dns4"))
        return i18nc("@info", "Enter a valid DNS server, like 1.1.1.1.");
    if (field == QLatin1String("dns6"))
        return i18nc("@info", "Enter an IPv6 DNS server, like 2606:4700:4700::1111.");
    return {};
}

QString byteSize(quint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    // one decimal below 10, as Windows writes "1.5 KB" and "25 MB"
    const int decimals = unit > 0 && value < 10.0 && value != double(qint64(value)) ? 1 : 0;
    return QLocale().toString(value, 'f', decimals) + QLatin1Char(' ') + QString::fromLatin1(units[unit]);
}

QString statusText(bool connected, bool secured, bool internet)
{
    if (!connected)
        return i18nc("@info network status", "Not connected");
    if (!internet)
        return i18nc("@info network status", "No internet");
    return secured ? i18nc("@info network status", "Connected, secured")
                   : i18nc("@info network status", "Connected");
}

QString wifiSummary(bool wifiEnabled, const QString &primaryType, const QString &primaryName)
{
    if (!wifiEnabled)
        return i18nc("@info Wi-Fi", "Off");
    if (primaryType == QLatin1String("wifi"))
        return i18nc("@info Wi-Fi, %1 is a network name", "Connected, %1", primaryName.toHtmlEscaped());
    if (primaryType == QLatin1String("ethernet"))
        return i18nc("@info Wi-Fi", "Not in use, this PC is connected by cable");
    return i18nc("@info Wi-Fi", "Not connected");
}

QVariantMap wording(bool hasWifi, bool hasBluetooth)
{
    QVariantMap w;
    if (hasWifi) {
        w.insert(QStringLiteral("offline"), i18nc("@info", "Connect to Wi-Fi or plug in a cable to get online"));
        w.insert(QStringLiteral("noEthernet"), i18nc("@info", "Plug in a USB network adapter, or connect to Wi-Fi."));
        w.insert(QStringLiteral("proxy"), i18nc("@info", "Proxy server for Wi-Fi and Ethernet connections"));
        w.insert(QStringLiteral("proxyIntro"), i18nc("@info", "Use a proxy server for Ethernet or Wi-Fi connections. These settings don't apply to VPN connections."));
        w.insert(QStringLiteral("resetSummary"), i18nc("@info", "Forget all Wi-Fi networks and VPNs, and restart networking"));
        w.insert(QStringLiteral("resetWarning"), i18nc("@info", "Raye will forget every Wi-Fi network and VPN saved on this PC, then restart networking. You'll need your Wi-Fi passwords to connect again."));
        w.insert(QStringLiteral("resetDone"), i18nc("@info", "Connect to your Wi-Fi network again from the Wi-Fi page."));
    } else {
        w.insert(QStringLiteral("offline"), i18nc("@info on a PC without Wi-Fi", "Plug in a network cable to get online"));
        w.insert(QStringLiteral("noEthernet"), i18nc("@info on a PC without Wi-Fi", "Plug in a network cable, or a USB network adapter."));
        w.insert(QStringLiteral("proxy"), i18nc("@info on a PC without Wi-Fi", "Proxy server for Ethernet connections"));
        w.insert(QStringLiteral("proxyIntro"), i18nc("@info on a PC without Wi-Fi", "Use a proxy server for Ethernet connections. These settings don't apply to VPN connections."));
        w.insert(QStringLiteral("resetSummary"), i18nc("@info on a PC without Wi-Fi", "Forget all VPNs, and restart networking"));
        w.insert(QStringLiteral("resetWarning"), i18nc("@info on a PC without Wi-Fi", "Raye will forget every VPN saved on this PC, then restart networking. Your cable connection comes back by itself."));
        w.insert(QStringLiteral("resetDone"), i18nc("@info on a PC without Wi-Fi", "Networking has restarted. Your cable connection is back."));
    }
    if (hasWifi && hasBluetooth) {
        w.insert(QStringLiteral("airplaneOn"), i18nc("@info", "Airplane mode is on. Wi-Fi and Bluetooth are off until you turn them back on."));
        w.insert(QStringLiteral("airplaneOff"), i18nc("@info", "Turn on airplane mode to stop all wireless communication, such as Wi-Fi and Bluetooth."));
    } else if (hasWifi) {
        w.insert(QStringLiteral("airplaneOn"), i18nc("@info", "Airplane mode is on. Wi-Fi is off until you turn it back on."));
        w.insert(QStringLiteral("airplaneOff"), i18nc("@info", "Turn on airplane mode to stop all wireless communication, such as Wi-Fi."));
    } else if (hasBluetooth) {
        w.insert(QStringLiteral("airplaneOn"), i18nc("@info", "Airplane mode is on. Bluetooth is off until you turn it back on."));
        w.insert(QStringLiteral("airplaneOff"), i18nc("@info", "Turn on airplane mode to stop all wireless communication, such as Bluetooth."));
    } else {
        w.insert(QStringLiteral("airplaneOn"), i18nc("@info", "Airplane mode is on."));
        w.insert(QStringLiteral("airplaneOff"), i18nc("@info", "Turn on airplane mode to stop all wireless communication."));
    }
    return w;
}

} // namespace NetworkText

WifiHardware::WifiHardware(QObject *parent)
    : QObject(parent)
{
    // a device's type comes in after deviceAdded, so look a moment later
    m_check.setSingleShot(true);
    m_check.setInterval(100);
    connect(&m_check, &QTimer::timeout, this, &WifiHardware::check);
    auto *notifier = NetworkManager::notifier();
    connect(notifier, &NetworkManager::Notifier::deviceAdded, &m_check, qOverload<>(&QTimer::start));
    connect(notifier, &NetworkManager::Notifier::deviceRemoved, &m_check, qOverload<>(&QTimer::start));
    connect(notifier, &NetworkManager::Notifier::serviceAppeared, &m_check, qOverload<>(&QTimer::start));
    check();
}

void WifiHardware::check()
{
    const auto devices = NetworkManager::networkInterfaces();
    if (devices.isEmpty() && NetworkManager::status() == NetworkManager::Unknown)
        return;
    bool present = false;
    for (const auto &device : devices)
        present = present || (device->type() == NetworkManager::Device::Wifi && device->managed());
    if (m_known && present == m_present)
        return;
    m_known = true;
    m_present = present;
    Q_EMIT changed(present);
}

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent)
{
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(0);
    connect(&m_refresh, &QTimer::timeout, this, &NetworkStatus::refresh);

    auto *notifier = NetworkManager::notifier();
    connect(notifier, &NetworkManager::Notifier::primaryConnectionChanged, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::activeConnectionsChanged, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::connectivityChanged, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::networkingEnabledChanged, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::serviceAppeared, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::serviceDisappeared, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::wirelessEnabledChanged, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::wirelessHardwareEnabledChanged, this, &NetworkStatus::scheduleRefresh);
    connect(notifier, &NetworkManager::Notifier::deviceAdded, this, [this] {
        watchDevices();
        scheduleRefresh();
    });
    connect(notifier, &NetworkManager::Notifier::deviceRemoved, this, &NetworkStatus::scheduleRefresh);

    m_bluetooth = new BluezQt::Manager(this);
    connect(m_bluetooth, &BluezQt::Manager::bluetoothBlockedChanged, this, &NetworkStatus::changed);
    connect(m_bluetooth, &BluezQt::Manager::adapterAdded, this, &NetworkStatus::changed);
    connect(m_bluetooth, &BluezQt::Manager::adapterRemoved, this, &NetworkStatus::changed);
    connect(m_bluetooth, &BluezQt::Manager::operationalChanged, this, &NetworkStatus::changed);
    m_bluetooth->init()->start();

    watchDevices();
    refresh();
}

NetworkStatus::~NetworkStatus()
{
    // hand the counters back to their rest rate, if we woke them
    if (m_ownsRefreshRate && !m_statisticsDevice.isEmpty()) {
        if (auto device = NetworkManager::findNetworkInterface(m_statisticsDevice))
            device->deviceStatistics()->setRefreshRateMs(0);
    }
}

void NetworkStatus::scheduleRefresh()
{
    m_refresh.start();
}

void NetworkStatus::watchDevices()
{
    const auto devices = NetworkManager::networkInterfaces();
    for (const auto &device : devices) {
        // a Qt::UniqueConnection cannot take a lambda: connect once
        if (device->property("rayeWatched").toBool())
            continue;
        device->setProperty("rayeWatched", true);
        const auto refresh = [] {
            // every NetworkStatus listens to the notifier below
            Q_EMIT NetworkManager::notifier()->activeConnectionsChanged();
        };
        connect(device.data(), &NetworkManager::Device::stateChanged, NetworkManager::notifier(), refresh);
        connect(device.data(), &NetworkManager::Device::autoconnectChanged, NetworkManager::notifier(), refresh);
        connect(device.data(), &NetworkManager::Device::ipV4ConfigChanged, NetworkManager::notifier(), refresh);
        connect(device.data(), &NetworkManager::Device::meteredChanged, NetworkManager::notifier(), refresh);
        if (auto wired = device.objectCast<NetworkManager::WiredDevice>())
            connect(wired.data(), &NetworkManager::WiredDevice::carrierChanged, NetworkManager::notifier(), refresh);
    }
}

void NetworkStatus::refresh()
{
    m_available = NetworkManager::status() != NetworkManager::Unknown || !NetworkManager::networkInterfaces().isEmpty();

    // the connection that carries the default route; NetworkManager
    // names it, else the first active one that is not a VPN
    NetworkManager::ActiveConnection::Ptr primary = NetworkManager::primaryConnection();
    if (!primary || !primary->isValid() || primary->state() != NetworkManager::ActiveConnection::Activated) {
        primary.clear();
        const auto actives = NetworkManager::activeConnections();
        for (const auto &active : actives) {
            if (active->state() != NetworkManager::ActiveConnection::Activated || active->vpn())
                continue;
            if (!primary || active->default4() || active->default6())
                primary = active;
        }
    }

    m_connected = primary != nullptr;
    m_primaryType = QStringLiteral("none");
    m_primaryName.clear();
    m_primaryUuid.clear();
    m_secured = false;
    m_signal = -1;
    m_ipv4.clear();
    m_ipv6.clear();
    m_metered = false;
    NetworkManager::Device::Ptr primaryDevice;
    if (primary) {
        m_primaryName = primary->id();
        m_primaryUuid = primary->uuid();
        primaryDevice = deviceOf(primary);
        switch (primary->type()) {
        case NetworkManager::ConnectionSettings::Wireless:
            m_primaryType = QStringLiteral("wifi");
            break;
        case NetworkManager::ConnectionSettings::Wired:
            // Windows says "Connected" for a cable, never "secured"
            m_primaryType = QStringLiteral("ethernet");
            break;
        case NetworkManager::ConnectionSettings::Vpn:
        case NetworkManager::ConnectionSettings::WireGuard:
            m_primaryType = QStringLiteral("vpn");
            m_secured = true;
            break;
        default:
            m_primaryType = QStringLiteral("other");
            break;
        }
        if (auto wifi = primaryDevice.objectCast<NetworkManager::WirelessDevice>()) {
            if (auto ap = wifi->activeAccessPoint()) {
                m_signal = ap->signalStrength();
                m_primaryName = ap->ssid().isEmpty() ? m_primaryName : ap->ssid();
                m_secured = ap->capabilities() & NetworkManager::AccessPoint::Privacy || ap->rsnFlags() || ap->wpaFlags();
            } else if (auto connection = primary->connection()) {
                auto setting = connection->settings()->setting(NetworkManager::Setting::Wireless)
                                   .staticCast<NetworkManager::WirelessSetting>();
                m_secured = setting && !setting->security().isEmpty();
            }
        }
        m_ipv4 = firstAddress(primary->ipV4Config(), false);
        m_ipv6 = firstAddress(primary->ipV6Config(), true);
        if (primaryDevice) {
            const auto metered = primaryDevice->metered();
            m_metered = metered == NetworkManager::Device::Yes || metered == NetworkManager::Device::GuessYes;
        }
    }
    const auto connectivity = NetworkManager::connectivity();
    m_portal = connectivity == NetworkManager::Portal;
    // no check configured: a working connection is taken at its word
    m_internet = m_connected
        && (connectivity == NetworkManager::Full || connectivity == NetworkManager::UnknownConnectivity);

    // statistics of the primary adapter
    const QString statisticsDevice = primaryDevice ? primaryDevice->uni() : QString();
    if (statisticsDevice != m_statisticsDevice) {
        if (m_ownsRefreshRate && !m_statisticsDevice.isEmpty()) {
            if (auto old = NetworkManager::findNetworkInterface(m_statisticsDevice))
                old->deviceStatistics()->setRefreshRateMs(0);
        }
        m_ownsRefreshRate = false;
        m_statisticsDevice = statisticsDevice;
        watchStatistics();
    }

    m_hasWifi = false;
    m_hasEthernet = false;
    m_ethernet.clear();
    m_adapters.clear();
    const auto devices = NetworkManager::networkInterfaces();
    for (const auto &device : devices) {
        const QString kind = deviceKind(device->type());
        if (kind.isEmpty() || !device->managed())
            continue;
        m_hasWifi = m_hasWifi || kind == QLatin1String("wifi");
        m_hasEthernet = m_hasEthernet || kind == QLatin1String("ethernet");
        if (kind == QLatin1String("ethernet"))
            m_ethernet.append(ethernetEntry(device));

        const bool on = device->autoconnect() || device->state() >= NetworkManager::Device::Preparing;
        QString status;
        if (!on)
            status = i18nc("@info network adapter", "Disabled");
        else if (activated(device) && device->activeConnection())
            status = i18nc("@info network adapter, %1 is a network name", "Connected to %1", device->activeConnection()->id());
        else if (device->state() > NetworkManager::Device::Disconnected && device->state() < NetworkManager::Device::Activated)
            status = i18nc("@info network adapter", "Connecting…");
        else if (device->state() == NetworkManager::Device::Unavailable && kind == QLatin1String("ethernet"))
            status = i18nc("@info network adapter", "Cable unplugged");
        else
            status = i18nc("@info network adapter", "Not connected");

        QString mac;
        int speed = 0;
        if (auto wired = device.objectCast<NetworkManager::WiredDevice>()) {
            mac = wired->permanentHardwareAddress().isEmpty() ? wired->hardwareAddress() : wired->permanentHardwareAddress();
            speed = wired->bitRate();
        } else if (auto wifi = device.objectCast<NetworkManager::WirelessDevice>()) {
            mac = wifi->permanentHardwareAddress().isEmpty() ? wifi->hardwareAddress() : wifi->permanentHardwareAddress();
            speed = wifi->bitRate();
        }
        const QString description = kind == QLatin1String("wifi") ? i18nc("@title network adapter", "Wi-Fi")
            : kind == QLatin1String("ethernet")                    ? i18nc("@title network adapter", "Ethernet")
                                                                   : i18nc("@title network adapter", "Mobile broadband");
        m_adapters.append(QVariantMap{
            {QStringLiteral("uni"), device->uni()},
            {QStringLiteral("interface"), device->interfaceName()},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("description"), description},
            {QStringLiteral("driver"), device->driver()},
            {QStringLiteral("statusText"), status},
            {QStringLiteral("enabled"), on},
            {QStringLiteral("mac"), mac.toUpper()},
            {QStringLiteral("speed"), speedText(speed)},
        });
    }
    Q_EMIT changed();
    Q_EMIT dataUsedChanged();
}

void NetworkStatus::watchStatistics()
{
    m_bytes = 0;
    if (m_statisticsDevice.isEmpty())
        return;
    auto device = NetworkManager::findNetworkInterface(m_statisticsDevice);
    if (!device)
        return;
    auto statistics = device->deviceStatistics();
    const auto update = [this, statistics] {
        m_bytes = statistics->rxBytes() + statistics->txBytes();
        Q_EMIT dataUsedChanged();
    };
    connect(statistics.data(), &NetworkManager::DeviceStatistics::rxBytesChanged, this, update);
    connect(statistics.data(), &NetworkManager::DeviceStatistics::txBytesChanged, this, update);
    if (statistics->refreshRateMs() == 0) {
        statistics->setRefreshRateMs(2000);
        m_ownsRefreshRate = true;
    }
    update();
}

QVariantMap NetworkStatus::ethernetEntry(const NetworkManager::Device::Ptr &device) const
{
    auto wired = device.objectCast<NetworkManager::WiredDevice>();
    NetworkManager::Connection::Ptr connection;
    if (auto active = device->activeConnection())
        connection = active->connection();
    if (!connection) {
        const auto available = device->availableConnections();
        if (!available.isEmpty())
            connection = available.first();
    }
    bool ipAuto = true;
    bool dnsAuto = true;
    bool metered = false;
    bool autoMetered = true;
    QString name;
    QString uuid;
    if (connection) {
        const auto settings = connection->settings();
        name = settings->id();
        uuid = settings->uuid();
        auto ipv4 = settings->setting(NetworkManager::Setting::Ipv4).staticCast<NetworkManager::Ipv4Setting>();
        if (ipv4) {
            ipAuto = ipv4->method() != NetworkManager::Ipv4Setting::Manual;
            dnsAuto = !ipv4->ignoreAutoDns() && ipv4->dns().isEmpty();
        }
        metered = settings->metered() == NetworkManager::ConnectionSettings::MeteredYes;
        autoMetered = settings->metered() == NetworkManager::ConnectionSettings::MeteredUnknown;
    }
    const NetworkManager::IpConfig v4 = device->ipV4Config();
    const bool on = activated(device);
    QString status;
    if (on)
        status = NetworkText::statusText(true, false, m_internet || !m_connected);
    else if (wired && !wired->carrier())
        status = i18nc("@info network adapter", "Cable unplugged");
    else
        status = i18nc("@info network adapter", "Not connected");
    return {
        {QStringLiteral("uni"), device->uni()},
        {QStringLiteral("interface"), device->interfaceName()},
        {QStringLiteral("uuid"), uuid},
        {QStringLiteral("name"), name.isEmpty() ? device->interfaceName() : name},
        {QStringLiteral("connected"), on},
        {QStringLiteral("cable"), wired ? wired->carrier() : false},
        {QStringLiteral("speed"), wired ? speedText(wired->bitRate()) : QString()},
        {QStringLiteral("mac"), wired ? wired->permanentHardwareAddress().toUpper() : QString()},
        {QStringLiteral("driver"), device->driver()},
        {QStringLiteral("ipv4"), on ? firstAddress(v4, false) : QString()},
        {QStringLiteral("ipv6"), on ? firstAddress(device->ipV6Config(), true) : QString()},
        {QStringLiteral("gateway"), on ? v4.gateway() : QString()},
        {QStringLiteral("dns"), on ? nameservers(v4).join(QStringLiteral(", ")) : QString()},
        {QStringLiteral("metered"), metered},
        {QStringLiteral("autoMetered"), autoMetered},
        {QStringLiteral("ipAuto"), ipAuto},
        {QStringLiteral("dnsAuto"), dnsAuto},
        {QStringLiteral("statusText"), status},
    };
}

QString NetworkStatus::primaryPage() const
{
    if (m_primaryType == QLatin1String("wifi"))
        return QStringLiteral("network/wifi");
    if (m_primaryType == QLatin1String("ethernet"))
        return QStringLiteral("network/ethernet");
    if (m_primaryType == QLatin1String("vpn"))
        return QStringLiteral("network/vpn");
    return QStringLiteral("network/advanced");
}

QString NetworkStatus::statusText() const
{
    if (m_portal)
        return i18nc("@info network status", "Action needed: sign in to the network");
    return NetworkText::statusText(m_connected, m_secured, m_internet);
}

QString NetworkStatus::wifiSummary() const
{
    return NetworkText::wifiSummary(wifiEnabled(), m_primaryType, m_primaryName);
}

QString NetworkStatus::portalUrl() const
{
    // NetworkManager's own check page: a sign-in page answers in its place
    const QUrl url(NetworkManager::connectivityCheckUri());
    if (url.isValid() && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")))
        return url.toString();
    return QStringLiteral("http://connectivity-check.ubuntu.com/");
}

QString NetworkStatus::dataUsed() const
{
    // nothing counted yet (or counters off): say nothing rather than "0 B"
    return m_statisticsDevice.isEmpty() || m_bytes == 0 ? QString() : NetworkText::byteSize(m_bytes);
}

bool NetworkStatus::wifiEnabled() const
{
    return NetworkManager::isWirelessEnabled();
}

void NetworkStatus::setWifiEnabled(bool enabled)
{
    if (enabled == wifiEnabled())
        return;
    NetworkManager::setWirelessEnabled(enabled);
}

bool NetworkStatus::wifiHardwareEnabled() const
{
    return NetworkManager::isWirelessHardwareEnabled();
}

bool NetworkStatus::hasBluetooth() const
{
    return m_bluetooth->isInitialized() && !m_bluetooth->adapters().isEmpty();
}

bool NetworkStatus::bluetoothEnabled() const
{
    return hasBluetooth() && !m_bluetooth->isBluetoothBlocked();
}

void NetworkStatus::setBluetoothEnabled(bool enabled)
{
    if (!hasBluetooth())
        return;
    m_bluetooth->setBluetoothBlocked(!enabled);
    // unblocking leaves the adapter powered off; Windows turns it on
    if (enabled) {
        const auto adapters = m_bluetooth->adapters();
        for (const auto &adapter : adapters)
            adapter->setPowered(true);
    }
    Q_EMIT changed();
}

// The same flag as the network applet of the taskbar, so both say the
// same thing: on while the flag is set and Wi-Fi stays off.
static KConfigGroup airplaneGroup()
{
    return KSharedConfig::openConfig(QStringLiteral("plasma-nm"))->group(QStringLiteral("General"));
}

bool NetworkStatus::airplaneMode() const
{
    if (!airplaneGroup().readEntry("AirplaneModeEnabled", false))
        return false;
    return !wifiEnabled() || !wifiHardwareEnabled();
}

void NetworkStatus::setAirplaneMode(bool on)
{
    if (on == airplaneMode())
        return;
    KConfigGroup raye = rayeGroup();
    KConfigGroup flag = airplaneGroup();
    if (on) {
        // remember the radios, to give them back as they were
        raye.writeEntry("WifiBeforeAirplane", wifiEnabled());
        raye.writeEntry("BluetoothBeforeAirplane", bluetoothEnabled());
        raye.sync();
        flag.writeEntry("AirplaneModeEnabled", true);
        flag.sync();
        setWifiEnabled(false);
        NetworkManager::setWwanEnabled(false);
        setBluetoothEnabled(false);
    } else {
        flag.writeEntry("AirplaneModeEnabled", false);
        flag.sync();
        setWifiEnabled(raye.readEntry("WifiBeforeAirplane", true));
        NetworkManager::setWwanEnabled(true);
        if (raye.readEntry("BluetoothBeforeAirplane", true))
            setBluetoothEnabled(true);
    }
    Q_EMIT changed();
}

QVariantMap NetworkStatus::ipConfig(const QString &uuid) const
{
    auto connection = NetworkManager::findConnectionByUuid(uuid);
    if (!connection)
        return {};
    const auto settings = connection->settings();
    QVariantMap v4{{QStringLiteral("method"), QStringLiteral("auto")}};
    QVariantMap v6{{QStringLiteral("method"), QStringLiteral("auto")}};
    QStringList dns4;
    QStringList dns6;
    bool dnsAuto = true;
    if (auto ipv4 = settings->setting(NetworkManager::Setting::Ipv4).staticCast<NetworkManager::Ipv4Setting>()) {
        if (ipv4->method() == NetworkManager::Ipv4Setting::Manual) {
            v4.insert(QStringLiteral("method"), QStringLiteral("manual"));
            const auto addresses = ipv4->addresses();
            if (!addresses.isEmpty()) {
                v4.insert(QStringLiteral("address"), addresses.first().ip().toString());
                v4.insert(QStringLiteral("prefix"), NetworkText::ipv4Mask(addresses.first().prefixLength()));
                const QString gateway = ipv4->gateway().isEmpty() ? (addresses.first().gateway().isNull() ? QString() : addresses.first().gateway().toString())
                                                                  : ipv4->gateway();
                v4.insert(QStringLiteral("gateway"), gateway);
            }
        }
        for (const auto &server : ipv4->dns())
            dns4.append(server.toString());
        dnsAuto = !ipv4->ignoreAutoDns() && dns4.isEmpty();
    }
    if (auto ipv6 = settings->setting(NetworkManager::Setting::Ipv6).staticCast<NetworkManager::Ipv6Setting>()) {
        const auto method = ipv6->method();
        if (method == NetworkManager::Ipv6Setting::Manual) {
            v6.insert(QStringLiteral("method"), QStringLiteral("manual"));
            const auto addresses = ipv6->addresses();
            if (!addresses.isEmpty()) {
                v6.insert(QStringLiteral("address"), addresses.first().ip().toString());
                v6.insert(QStringLiteral("prefix"), addresses.first().prefixLength());
                if (!addresses.first().gateway().isNull())
                    v6.insert(QStringLiteral("gateway"), addresses.first().gateway().toString());
            }
        } else if (method == NetworkManager::Ipv6Setting::Ignored || method == NetworkManager::Ipv6Setting::ConfigDisabled) {
            v6.insert(QStringLiteral("method"), QStringLiteral("off"));
        }
        for (const auto &server : ipv6->dns())
            dns6.append(server.toString());
    }
    return {{QStringLiteral("ipv4"), v4},
            {QStringLiteral("ipv6"), v6},
            {QStringLiteral("dnsAuto"), dnsAuto},
            {QStringLiteral("dns4"), dns4},
            {QStringLiteral("dns6"), dns6}};
}

QString NetworkStatus::setIpConfig(const QString &uuid, const QVariantMap &config)
{
    return update(uuid, config, true, false);
}

QString NetworkStatus::setDnsConfig(const QString &uuid, const QVariantMap &config)
{
    return update(uuid, config, false, true);
}

QString NetworkStatus::update(const QString &uuid, const QVariantMap &config, bool ip, bool dns)
{
    const QString problem = NetworkText::checkIpConfig(config);
    if (!problem.isEmpty())
        return NetworkText::errorText(problem);
    auto connection = NetworkManager::findConnectionByUuid(uuid);
    if (!connection)
        return i18nc("@info", "This network is no longer saved on this PC.");
    auto settings = connection->settings();
    auto ipv4 = settings->setting(NetworkManager::Setting::Ipv4).staticCast<NetworkManager::Ipv4Setting>();
    auto ipv6 = settings->setting(NetworkManager::Setting::Ipv6).staticCast<NetworkManager::Ipv6Setting>();
    if (!ipv4 || !ipv6)
        return i18nc("@info", "This connection has no IP settings.");

    if (ip) {
        const QVariantMap v4 = config.value(QStringLiteral("ipv4")).toMap();
        if (v4.value(QStringLiteral("method")).toString() == QLatin1String("manual")) {
            NetworkManager::IpAddress address;
            address.setIp(QHostAddress(v4.value(QStringLiteral("address")).toString().trimmed()));
            address.setPrefixLength(NetworkText::ipv4Prefix(v4.value(QStringLiteral("prefix")).toString().trimmed()));
            const QString gateway = v4.value(QStringLiteral("gateway")).toString().trimmed();
            if (!gateway.isEmpty())
                address.setGateway(QHostAddress(gateway));
            ipv4->setMethod(NetworkManager::Ipv4Setting::Manual);
            ipv4->setAddresses({address});
        } else if (!v4.isEmpty()) {
            ipv4->setMethod(NetworkManager::Ipv4Setting::Automatic);
            ipv4->setAddresses({});
        }
        const QVariantMap v6 = config.value(QStringLiteral("ipv6")).toMap();
        const QString method6 = v6.value(QStringLiteral("method")).toString();
        if (method6 == QLatin1String("manual")) {
            NetworkManager::IpAddress address;
            address.setIp(QHostAddress(v6.value(QStringLiteral("address")).toString().trimmed()));
            address.setPrefixLength(v6.value(QStringLiteral("prefix")).toInt());
            const QString gateway = v6.value(QStringLiteral("gateway")).toString().trimmed();
            if (!gateway.isEmpty())
                address.setGateway(QHostAddress(gateway));
            ipv6->setMethod(NetworkManager::Ipv6Setting::Manual);
            ipv6->setAddresses({address});
        } else if (method6 == QLatin1String("off")) {
            ipv6->setMethod(NetworkManager::Ipv6Setting::Ignored);
            ipv6->setAddresses({});
        } else if (!v6.isEmpty()) {
            ipv6->setMethod(NetworkManager::Ipv6Setting::Automatic);
            ipv6->setAddresses({});
        }
    }
    if (dns) {
        const bool automatic = config.value(QStringLiteral("dnsAuto"), true).toBool();
        QList<QHostAddress> servers4;
        QList<QHostAddress> servers6;
        if (!automatic) {
            for (const QString &s : strings(config.value(QStringLiteral("dns4"))))
                if (!s.trimmed().isEmpty())
                    servers4.append(QHostAddress(s.trimmed()));
            for (const QString &s : strings(config.value(QStringLiteral("dns6"))))
                if (!s.trimmed().isEmpty())
                    servers6.append(QHostAddress(s.trimmed()));
        }
        ipv4->setDns(servers4);
        ipv4->setIgnoreAutoDns(!automatic);
        ipv6->setDns(servers6);
        ipv6->setIgnoreAutoDns(!automatic);
    }

    auto reply = connection->update(withSecrets(connection, settings));
    reply.waitForFinished();
    if (reply.isError())
        return i18nc("@info", "Raye couldn't save these settings. %1", reply.error().message());

    // apply now, as Windows does, by bringing the connection up again
    const auto actives = NetworkManager::activeConnections();
    for (const auto &active : actives) {
        if (active->uuid() != uuid)
            continue;
        if (auto device = deviceOf(active))
            NetworkManager::activateConnection(connection->path(), device->uni(), QString());
    }
    scheduleRefresh();
    return {};
}

bool NetworkStatus::isMetered(const QString &uuid) const
{
    auto connection = NetworkManager::findConnectionByUuid(uuid);
    return connection && connection->settings()->metered() == NetworkManager::ConnectionSettings::MeteredYes;
}

QString NetworkStatus::setMetered(const QString &uuid, bool metered)
{
    auto connection = NetworkManager::findConnectionByUuid(uuid);
    if (!connection)
        return i18nc("@info", "This network is no longer saved on this PC.");
    auto settings = connection->settings();
    settings->setMetered(metered ? NetworkManager::ConnectionSettings::MeteredYes
                                 : NetworkManager::ConnectionSettings::MeteredNo);
    auto reply = connection->update(withSecrets(connection, settings));
    reply.waitForFinished();
    if (reply.isError())
        return i18nc("@info", "Raye couldn't save this setting. %1", reply.error().message());
    scheduleRefresh();
    return {};
}

void NetworkStatus::setAdapterEnabled(const QString &uni, bool enabled)
{
    auto device = NetworkManager::findNetworkInterface(uni);
    if (!device)
        return;
    device->setAutoconnect(enabled);
    if (!enabled)
        device->disconnectInterface();
    scheduleRefresh();
}

void NetworkStatus::openMoreOptions(const QString &uuid)
{
    // our kcmshell6 would send this module back to Raye Settings
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("RAYE_SETTINGS_KCMSHELL"), QStringLiteral("pass-through"));
    process.setProcessEnvironment(env);
    process.setProgram(QStringLiteral("kcmshell6"));
    QStringList arguments{QStringLiteral("kcm_networkmanagement")};
    static const QRegularExpression uuidShape(QStringLiteral("^[0-9a-fA-F-]{36}$"));
    if (uuidShape.match(uuid).hasMatch())
        arguments << QStringLiteral("--args") << QStringLiteral("Uuid=") + uuid;
    process.setArguments(arguments);
    process.startDetached();
}

void NetworkStatus::copyText(const QString &text)
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void NetworkStatus::resetNetwork()
{
    if (m_reset)
        return;
    m_reset = new QProcess(this);
    connect(m_reset, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        m_reset->deleteLater();
        m_reset = nullptr;
        Q_EMIT resettingChanged();
        Q_EMIT resetFinished(ok);
        scheduleRefresh();
    });
    connect(m_reset, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_reset->deleteLater();
        m_reset = nullptr;
        Q_EMIT resettingChanged();
        Q_EMIT resetFinished(false);
    });
    m_reset->start(QStringLiteral("pkexec"), {QStringLiteral(RAYE_LIBEXECDIR "/network-reset")});
    Q_EMIT resettingChanged();
}
