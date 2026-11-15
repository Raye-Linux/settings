// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi.h"
#include "network.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <NetworkManagerQt/AccessPoint>
#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/WirelessSecuritySetting>
#include <NetworkManagerQt/WirelessSetting>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLocale>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace {

// NetworkManager's NM80211ApSecurityFlags
constexpr uint keyMgmtPsk = 0x100;
constexpr uint keyMgmt8021x = 0x200;
constexpr uint keyMgmtSae = 0x400;
constexpr uint keyMgmtOwe = 0x800;
constexpr uint keyMgmtOweTm = 0x1000;
constexpr uint keyMgmtEapSuiteB = 0x2000;

KConfigGroup rayeGroup()
{
    return KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"))->group(QStringLiteral("Network"));
}

bool isHex(const QString &text)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]+$"));
    return hex.match(text).hasMatch();
}

bool printableAscii(const QString &text)
{
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e)
            return false;
    }
    return true;
}

// the Wi-Fi networks saved on this computer: network name -> uuid
QHash<QString, QString> knownUuids()
{
    QHash<QString, QString> known;
    const auto connections = NetworkManager::listConnections();
    for (const auto &connection : connections) {
        const auto settings = connection->settings();
        if (settings->connectionType() != NetworkManager::ConnectionSettings::Wireless)
            continue;
        auto wireless = settings->setting(NetworkManager::Setting::Wireless).staticCast<NetworkManager::WirelessSetting>();
        if (!wireless || wireless->mode() == NetworkManager::WirelessSetting::Ap)
            continue;
        const QString ssid = QString::fromUtf8(wireless->ssid());
        if (!ssid.isEmpty() && !known.contains(ssid))
            known.insert(ssid, settings->uuid());
    }
    return known;
}

QString ssidOf(const NetworkManager::Connection::Ptr &connection)
{
    if (!connection)
        return {};
    auto wireless = connection->settings()->setting(NetworkManager::Setting::Wireless).staticCast<NetworkManager::WirelessSetting>();
    return wireless ? QString::fromUtf8(wireless->ssid()) : QString();
}

QString bandName(uint frequency)
{
    if (frequency >= 5925)
        return i18nc("@info Wi-Fi band", "6 GHz");
    if (frequency >= 4900)
        return i18nc("@info Wi-Fi band", "5 GHz");
    return frequency > 0 ? i18nc("@info Wi-Fi band", "2.4 GHz") : QString();
}

int channelOf(uint frequency)
{
    if (frequency == 2484)
        return 14;
    if (frequency >= 2412 && frequency < 2484)
        return int(frequency - 2407) / 5;
    if (frequency >= 5925)
        return int(frequency - 5950) / 5;
    if (frequency >= 4900)
        return int(frequency - 5000) / 5;
    return 0;
}

QString securityName(const QString &security)
{
    if (security == QLatin1String("wpa-psk"))
        return i18nc("@info Wi-Fi security", "WPA2-Personal");
    if (security == QLatin1String("sae"))
        return i18nc("@info Wi-Fi security", "WPA3-Personal");
    if (security == QLatin1String("enterprise"))
        return i18nc("@info Wi-Fi security", "WPA2-Enterprise");
    if (security == QLatin1String("wep"))
        return i18nc("@info Wi-Fi security", "WEP");
    if (security == QLatin1String("owe"))
        return i18nc("@info Wi-Fi security", "Enhanced Open");
    return i18nc("@info Wi-Fi security", "Open");
}

} // namespace

namespace WifiList
{

QList<Row> merge(const QList<AccessPoint> &points, const QHash<QString, QString> &knownUuids,
                 const QString &connectedSsid)
{
    QHash<QString, int> index;
    QList<Row> rows;
    for (const AccessPoint &point : points) {
        if (point.ssid.isEmpty())
            continue;
        const auto found = index.constFind(point.ssid);
        if (found != index.constEnd()) {
            Row &row = rows[*found];
            if (point.strength > row.strength) {
                row.strength = point.strength;
                row.path = point.path;
                row.band5 = point.frequency >= 4900;
                row.security = point.security;
            }
            continue;
        }
        Row row;
        row.ssid = point.ssid;
        row.strength = point.strength;
        row.security = point.security;
        row.band5 = point.frequency >= 4900;
        row.path = point.path;
        row.connected = !connectedSsid.isEmpty() && point.ssid == connectedSsid;
        row.uuid = knownUuids.value(point.ssid);
        row.known = !row.uuid.isEmpty();
        index.insert(row.ssid, rows.size());
        rows.append(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.connected != b.connected)
            return a.connected;
        if (a.known != b.known)
            return a.known;
        if (a.strength != b.strength)
            return a.strength > b.strength;
        return QString::localeAwareCompare(a.ssid, b.ssid) < 0;
    });
    return rows;
}

QString securityOf(uint flags, uint wpaFlags, uint rsnFlags)
{
    const uint all = wpaFlags | rsnFlags;
    if (all & (keyMgmt8021x | keyMgmtEapSuiteB))
        return QStringLiteral("enterprise");
    // WPA2/WPA3 transition networks take a PSK too
    if (all & keyMgmtPsk)
        return QStringLiteral("wpa-psk");
    if (all & keyMgmtSae)
        return QStringLiteral("sae");
    if (all & (keyMgmtOwe | keyMgmtOweTm))
        return QStringLiteral("owe");
    if (flags & 0x1) // privacy without WPA: WEP
        return QStringLiteral("wep");
    return QStringLiteral("open");
}

bool isSecured(const QString &security)
{
    return security != QLatin1String("open") && security != QLatin1String("owe");
}

bool canJoin(const QString &security)
{
    return security != QLatin1String("enterprise");
}

bool passwordAcceptable(const QString &security, const QString &password)
{
    if (!isSecured(security))
        return true;
    if (security == QLatin1String("wpa-psk")) {
        if (password.size() == 64)
            return isHex(password);
        return password.size() >= 8 && password.size() <= 63 && printableAscii(password);
    }
    if (security == QLatin1String("sae"))
        return !password.isEmpty() && password.toUtf8().size() <= 128;
    if (security == QLatin1String("wep")) {
        if (password.size() == 10 || password.size() == 26)
            return isHex(password);
        return (password.size() == 5 || password.size() == 13) && printableAscii(password);
    }
    return false;
}

NMVariantMapMap newConnection(const QString &ssid, const QString &security, const QString &password,
                              bool autoconnect, bool randomAddress)
{
    NMVariantMapMap settings;
    settings.insert(QStringLiteral("connection"),
                    QVariantMap{{QStringLiteral("id"), ssid},
                                {QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                {QStringLiteral("type"), QStringLiteral("802-11-wireless")},
                                {QStringLiteral("autoconnect"), autoconnect}});
    QVariantMap wireless{{QStringLiteral("ssid"), ssid.toUtf8()},
                         {QStringLiteral("mode"), QStringLiteral("infrastructure")}};
    // "stable": an address of its own on each network, the same each time
    if (randomAddress)
        wireless.insert(QStringLiteral("assigned-mac-address"), QStringLiteral("stable"));
    settings.insert(QStringLiteral("802-11-wireless"), wireless);
    settings.insert(QStringLiteral("ipv4"), QVariantMap{{QStringLiteral("method"), QStringLiteral("auto")}});
    settings.insert(QStringLiteral("ipv6"), QVariantMap{{QStringLiteral("method"), QStringLiteral("auto")}});

    QVariantMap secured;
    if (security == QLatin1String("wpa-psk") || security == QLatin1String("sae")) {
        secured.insert(QStringLiteral("key-mgmt"), security);
        secured.insert(QStringLiteral("psk"), password);
    } else if (security == QLatin1String("wep")) {
        secured.insert(QStringLiteral("key-mgmt"), QStringLiteral("none"));
        secured.insert(QStringLiteral("wep-key0"), password);
        // 1: the key itself, as text or hex (2 would be a passphrase)
        secured.insert(QStringLiteral("wep-key-type"), 1u);
    } else if (security == QLatin1String("owe")) {
        secured.insert(QStringLiteral("key-mgmt"), QStringLiteral("owe"));
    }
    if (!secured.isEmpty())
        settings.insert(QStringLiteral("802-11-wireless-security"), secured);
    return settings;
}

} // namespace WifiList

WifiNetworks::WifiNetworks(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(150);
    connect(&m_rebuild, &QTimer::timeout, this, &WifiNetworks::rebuild);
    m_scan.setInterval(15000);
    connect(&m_scan, &QTimer::timeout, this, &WifiNetworks::scan);

    auto *notifier = NetworkManager::notifier();
    connect(notifier, &NetworkManager::Notifier::wirelessEnabledChanged, this, [this] {
        Q_EMIT enabledChanged();
        scheduleRebuild();
    });
    connect(notifier, &NetworkManager::Notifier::deviceAdded, this, &WifiNetworks::findDevice);
    connect(notifier, &NetworkManager::Notifier::deviceRemoved, this, &WifiNetworks::findDevice);
    connect(notifier, &NetworkManager::Notifier::activeConnectionsChanged, this, &WifiNetworks::scheduleRebuild);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionAdded, this,
            &WifiNetworks::scheduleRebuild);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionRemoved, this,
            &WifiNetworks::scheduleRebuild);
    findDevice();
}

void WifiNetworks::findDevice()
{
    NetworkManager::WirelessDevice::Ptr found;
    const auto devices = NetworkManager::networkInterfaces();
    for (const auto &device : devices) {
        if (device->type() == NetworkManager::Device::Wifi && device->managed()) {
            found = device.objectCast<NetworkManager::WirelessDevice>();
            break;
        }
    }
    if (found == m_device) {
        rebuild();
        return;
    }
    if (m_device)
        m_device->disconnect(this);
    m_device = found;
    if (m_device) {
        connect(m_device.data(), &NetworkManager::WirelessDevice::accessPointAppeared, this, &WifiNetworks::scheduleRebuild);
        connect(m_device.data(), &NetworkManager::WirelessDevice::accessPointDisappeared, this, &WifiNetworks::scheduleRebuild);
        connect(m_device.data(), &NetworkManager::WirelessDevice::activeAccessPointChanged, this, &WifiNetworks::scheduleRebuild);
        connect(m_device.data(), &NetworkManager::Device::activeConnectionChanged, this, &WifiNetworks::scheduleRebuild);
        connect(m_device.data(), &NetworkManager::Device::stateChanged, this,
                [this](NetworkManager::Device::State state, NetworkManager::Device::State,
                       NetworkManager::Device::StateChangeReason reason) {
                    scheduleRebuild();
                    Q_EMIT hardwareChanged();
                    if (m_joining.isEmpty())
                        return;
                    if (state == NetworkManager::Device::Activated) {
                        const QString ssid = m_joining;
                        m_newUuid.clear();
                        setJoining(QString());
                        Q_EMIT joined(ssid);
                    } else if (state == NetworkManager::Device::Failed) {
                        const bool wrongPassword = reason == NetworkManager::Device::NoSecretsReason
                            || reason == NetworkManager::Device::AuthSupplicantDisconnectReason
                            || reason == NetworkManager::Device::AuthSupplicantTimeoutReason;
                        const QString ssid = m_joining;
                        if (!m_newUuid.isEmpty()) {
                            if (auto connection = NetworkManager::findConnectionByUuid(m_newUuid))
                                connection->remove();
                            m_newUuid.clear();
                        }
                        setJoining(QString());
                        Q_EMIT joinFailed(ssid, wrongPassword,
                                          wrongPassword ? i18nc("@info", "The password is incorrect.")
                                                        : i18nc("@info", "Can't connect to this network."));
                    }
                });
    }
    Q_EMIT availableChanged();
    Q_EMIT hardwareChanged();
    rebuild();
}

void WifiNetworks::scheduleRebuild()
{
    m_rebuild.start();
}

void WifiNetworks::rebuild()
{
    if (!m_device || !enabled()) {
        applyRows({});
        return;
    }
    QList<WifiList::AccessPoint> points;
    const auto paths = m_device->accessPoints();
    for (const QString &path : paths) {
        auto ap = m_device->findAccessPoint(path);
        if (!ap || ap->mode() == NetworkManager::AccessPoint::ApMode)
            continue;
        if (!ap->property("rayeWatched").toBool()) {
            ap->setProperty("rayeWatched", true);
            connect(ap.data(), &NetworkManager::AccessPoint::signalStrengthChanged, this, &WifiNetworks::scheduleRebuild);
        }
        points.append({ap->ssid(), ap->signalStrength(),
                       WifiList::securityOf(ap->capabilities(), ap->wpaFlags(), ap->rsnFlags()), ap->frequency(),
                       ap->uni()});
    }
    QString connected;
    QString activating;
    if (auto active = m_device->activeConnection()) {
        const QString ssid = ssidOf(active->connection());
        if (active->state() == NetworkManager::ActiveConnection::Activated)
            connected = ssid;
        else if (active->state() == NetworkManager::ActiveConnection::Activating)
            activating = ssid;
    }
    if (!activating.isEmpty() && m_joining.isEmpty())
        setJoining(activating);
    applyRows(WifiList::merge(points, knownUuids(), connected));
}

void WifiNetworks::applyRows(QList<WifiList::Row> rows)
{
    if (m_holdOrder && !m_rows.isEmpty()) {
        // the old order first, newcomers after
        QList<WifiList::Row> ordered;
        for (const auto &old : std::as_const(m_rows)) {
            for (int i = 0; i < rows.size(); ++i) {
                if (rows[i].ssid == old.ssid) {
                    ordered.append(rows.takeAt(i));
                    break;
                }
            }
        }
        ordered.append(rows);
        rows = ordered;
    }
    bool sameOrder = rows.size() == m_rows.size();
    for (int i = 0; sameOrder && i < rows.size(); ++i)
        sameOrder = rows[i].ssid == m_rows[i].ssid;
    if (sameOrder) {
        m_rows = rows;
        if (!m_rows.isEmpty())
            Q_EMIT dataChanged(index(0, 0), index(m_rows.size() - 1, 0));
        return;
    }
    beginResetModel();
    m_rows = rows;
    endResetModel();
    Q_EMIT countChanged();
}

void WifiNetworks::setRows(const QList<WifiList::Row> &rows)
{
    applyRows(rows);
}

int WifiNetworks::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant WifiNetworks::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const WifiList::Row &row = m_rows[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case SsidRole:
        return row.ssid;
    case StrengthRole:
        return row.strength;
    case BarsRole:
        return NetworkText::signalBars(row.strength);
    case SecurityRole:
        return row.security;
    case SecuredRole:
        return WifiList::isSecured(row.security);
    case CanJoinRole:
        return WifiList::canJoin(row.security);
    case ConnectedRole:
        return row.connected;
    case JoiningRole:
        return !m_joining.isEmpty() && row.ssid == m_joining;
    case KnownRole:
        return row.known;
    case UuidRole:
        return row.uuid;
    case Band5Role:
        return row.band5;
    }
    return {};
}

QHash<int, QByteArray> WifiNetworks::roleNames() const
{
    return {{SsidRole, "ssid"},       {StrengthRole, "strength"}, {BarsRole, "bars"},
            {SecurityRole, "security"}, {SecuredRole, "secured"}, {CanJoinRole, "canJoin"},
            {ConnectedRole, "connected"}, {JoiningRole, "joining"}, {KnownRole, "known"},
            {UuidRole, "uuid"},       {Band5Role, "band5"}};
}

bool WifiNetworks::enabled() const
{
    return NetworkManager::isWirelessEnabled();
}

void WifiNetworks::setEnabled(bool enabled)
{
    if (enabled != this->enabled())
        NetworkManager::setWirelessEnabled(enabled);
}

void WifiNetworks::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        scan();
        m_scan.start();
    } else {
        m_scan.stop();
    }
    Q_EMIT activeChanged();
}

void WifiNetworks::setHoldOrder(bool hold)
{
    if (hold == m_holdOrder)
        return;
    m_holdOrder = hold;
    Q_EMIT holdOrderChanged();
    if (!hold)
        scheduleRebuild();
}

bool WifiNetworks::randomAddresses() const
{
    return rayeGroup().readEntry("RandomAddresses", false);
}

void WifiNetworks::setRandomAddresses(bool on)
{
    if (on == randomAddresses())
        return;
    KConfigGroup group = rayeGroup();
    group.writeEntry("RandomAddresses", on);
    group.sync();
    // the saved networks follow too
    const auto connections = NetworkManager::listConnections();
    for (const auto &connection : connections) {
        auto settings = connection->settings();
        if (settings->connectionType() != NetworkManager::ConnectionSettings::Wireless)
            continue;
        auto wireless = settings->setting(NetworkManager::Setting::Wireless).staticCast<NetworkManager::WirelessSetting>();
        if (!wireless || wireless->mode() == NetworkManager::WirelessSetting::Ap)
            continue;
        NMVariantMapMap map = settings->toMap();
        QVariantMap section = map.value(QStringLiteral("802-11-wireless"));
        section.remove(QStringLiteral("cloned-mac-address"));
        if (on)
            section.insert(QStringLiteral("assigned-mac-address"), QStringLiteral("stable"));
        else
            section.remove(QStringLiteral("assigned-mac-address"));
        map.insert(QStringLiteral("802-11-wireless"), section);
        // keep the password: fetch it before saving
        if (map.contains(QStringLiteral("802-11-wireless-security"))) {
            auto secrets = connection->secrets(QStringLiteral("802-11-wireless-security"));
            secrets.waitForFinished();
            if (secrets.isError())
                continue;
            QVariantMap security = map.value(QStringLiteral("802-11-wireless-security"));
            const QVariantMap values = secrets.value().value(QStringLiteral("802-11-wireless-security"));
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                security.insert(it.key(), it.value());
            map.insert(QStringLiteral("802-11-wireless-security"), security);
        }
        connection->update(map);
    }
    Q_EMIT randomAddressesChanged();
}

QVariantList WifiNetworks::hardware() const
{
    if (!m_device)
        return {};
    QVariantList list;
    const auto add = [&list](const QString &label, const QString &value) {
        if (!value.isEmpty())
            list.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
    };
    auto active = m_device->activeConnection();
    auto ap = m_device->activeAccessPoint();
    if (active && active->state() == NetworkManager::ActiveConnection::Activated) {
        add(i18nc("@label", "SSID"), ap ? ap->ssid() : ssidOf(active->connection()));
        if (ap) {
            add(i18nc("@label", "Security type"), securityName(WifiList::securityOf(ap->capabilities(), ap->wpaFlags(), ap->rsnFlags())));
            add(i18nc("@label", "Network band"), bandName(ap->frequency()));
            const int channel = channelOf(ap->frequency());
            add(i18nc("@label", "Network channel"), channel > 0 ? QString::number(channel) : QString());
        }
        if (m_device->bitRate() > 0)
            add(i18nc("@label", "Link speed"), i18nc("@info link speed", "%1 Mbps", m_device->bitRate() / 1000));
        const auto v4 = active->ipV4Config();
        if (!v4.addresses().isEmpty())
            add(i18nc("@label", "IPv4 address"), v4.addresses().first().ip().toString());
        const auto v6 = active->ipV6Config().addresses();
        for (const auto &address : v6) {
            if (!address.ip().isLinkLocal()) {
                add(i18nc("@label", "IPv6 address"), address.ip().toString());
                break;
            }
        }
        QStringList dns;
        for (const auto &server : v4.nameservers())
            dns.append(server.toString());
        add(i18nc("@label", "IPv4 DNS servers"), dns.join(QStringLiteral(", ")));
    }
    add(i18nc("@label", "Driver"), m_device->driver());
    const QString mac = m_device->permanentHardwareAddress().isEmpty() ? m_device->hardwareAddress()
                                                                       : m_device->permanentHardwareAddress();
    add(i18nc("@label", "Physical address (MAC)"), mac.toUpper());
    return list;
}

bool WifiNetworks::needsPassword(const QString &ssid) const
{
    for (const auto &row : m_rows) {
        if (row.ssid == ssid)
            return WifiList::isSecured(row.security) && !row.known;
    }
    return false;
}

QString WifiNetworks::uuidOf(const QString &ssid) const
{
    for (const auto &row : m_rows) {
        if (row.ssid == ssid)
            return row.uuid;
    }
    return knownUuids().value(ssid);
}

QString WifiNetworks::join(const QString &ssid, const QString &password, bool autoconnect)
{
    if (!m_device)
        return i18nc("@info", "This PC has no Wi-Fi adapter.");
    const WifiList::Row *row = nullptr;
    for (const auto &r : std::as_const(m_rows)) {
        if (r.ssid == ssid)
            row = &r;
    }
    if (!row)
        return i18nc("@info", "This network is out of range.");
    if (!WifiList::canJoin(row->security))
        return i18nc("@info", "This network needs a sign-in that Raye can't set up here. Use More network adapter options.");

    if (row->known && password.isEmpty()) {
        auto connection = NetworkManager::findConnectionByUuid(row->uuid);
        if (!connection)
            return i18nc("@info", "This network is no longer saved on this PC.");
        // a saved profile: never forgotten if this attempt fails
        m_newUuid.clear();
        setJoining(ssid);
        NetworkManager::activateConnection(connection->path(), m_device->uni(), row->path);
        return {};
    }
    if (!WifiList::passwordAcceptable(row->security, password)) {
        return row->security == QLatin1String("wep")
            ? i18nc("@info", "Enter a key of 5 or 13 characters, or 10 or 26 hexadecimal digits.")
            : i18nc("@info", "The password must be 8 to 63 characters.");
    }
    if (row->known) {
        // a new password for a saved network: forget the old profile
        if (auto connection = NetworkManager::findConnectionByUuid(row->uuid)) {
            auto reply = connection->remove();
            reply.waitForFinished();
        }
    }
    const NMVariantMapMap settings = WifiList::newConnection(ssid, row->security, password, autoconnect, randomAddresses());
    m_newUuid = settings.value(QStringLiteral("connection")).value(QStringLiteral("uuid")).toString();
    setJoining(ssid);
    auto reply = NetworkManager::addAndActivateConnection(settings, m_device->uni(), row->path);
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, ssid](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (!call->isError())
            return;
        m_newUuid.clear();
        setJoining(QString());
        Q_EMIT joinFailed(ssid, false, i18nc("@info", "Can't connect to this network."));
    });
    return {};
}

void WifiNetworks::setJoining(const QString &ssid)
{
    if (ssid == m_joining)
        return;
    m_joining = ssid;
    Q_EMIT joiningChanged();
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0, 0), index(m_rows.size() - 1, 0), {JoiningRole});
}

void WifiNetworks::disconnectFrom(const QString &ssid)
{
    if (!m_device)
        return;
    auto active = m_device->activeConnection();
    if (active && ssidOf(active->connection()) == ssid)
        NetworkManager::deactivateConnection(active->path());
}

void WifiNetworks::forget(const QString &ssid)
{
    const QString uuid = uuidOf(ssid);
    if (uuid.isEmpty())
        return;
    if (auto connection = NetworkManager::findConnectionByUuid(uuid))
        connection->remove();
}

void WifiNetworks::scan()
{
    if (m_device && enabled())
        m_device->requestScan();
}

KnownNetworks::KnownNetworks(QObject *parent)
    : QAbstractListModel(parent)
{
    m_reload.setSingleShot(true);
    m_reload.setInterval(50);
    connect(&m_reload, &QTimer::timeout, this, &KnownNetworks::reload);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionAdded, this,
            &KnownNetworks::scheduleReload);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionRemoved, this,
            &KnownNetworks::scheduleReload);
    connect(NetworkManager::notifier(), &NetworkManager::Notifier::serviceAppeared, this, &KnownNetworks::scheduleReload);
    reload();
}

void KnownNetworks::scheduleReload()
{
    m_reload.start();
}

void KnownNetworks::reload()
{
    QList<Entry> entries;
    const auto connections = NetworkManager::listConnections();
    for (const auto &connection : connections) {
        const auto settings = connection->settings();
        if (settings->connectionType() != NetworkManager::ConnectionSettings::Wireless)
            continue;
        auto wireless = settings->setting(NetworkManager::Setting::Wireless).staticCast<NetworkManager::WirelessSetting>();
        if (!wireless || wireless->mode() == NetworkManager::WirelessSetting::Ap)
            continue;
        if (!connection->property("rayeWatched").toBool()) {
            connection->setProperty("rayeWatched", true);
            connect(connection.data(), &NetworkManager::Connection::updated, this, &KnownNetworks::scheduleReload);
        }
        QString security = QStringLiteral("open");
        if (auto secured = settings->setting(NetworkManager::Setting::WirelessSecurity)
                               .staticCast<NetworkManager::WirelessSecuritySetting>()) {
            switch (secured->keyMgmt()) {
            case NetworkManager::WirelessSecuritySetting::WpaPsk:
                security = QStringLiteral("wpa-psk");
                break;
            case NetworkManager::WirelessSecuritySetting::SAE:
                security = QStringLiteral("sae");
                break;
            case NetworkManager::WirelessSecuritySetting::WpaEap:
            case NetworkManager::WirelessSecuritySetting::WpaEapSuiteB192:
            case NetworkManager::WirelessSecuritySetting::Ieee8021x:
                security = QStringLiteral("enterprise");
                break;
            case NetworkManager::WirelessSecuritySetting::Wep:
                security = QStringLiteral("wep");
                break;
            case NetworkManager::WirelessSecuritySetting::OWE:
                security = QStringLiteral("owe");
                break;
            default:
                break;
            }
        }
        entries.append({settings->id(), QString::fromUtf8(wireless->ssid()), settings->uuid(), settings->autoconnect(), security});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

int KnownNetworks::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant KnownNetworks::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case SsidRole:
        return entry.ssid;
    case UuidRole:
        return entry.uuid;
    case AutoconnectRole:
        return entry.autoconnect;
    case SecurityRole:
        return entry.security;
    }
    return {};
}

QHash<int, QByteArray> KnownNetworks::roleNames() const
{
    return {{NameRole, "name"}, {SsidRole, "ssid"}, {UuidRole, "uuid"}, {AutoconnectRole, "autoconnect"},
            {SecurityRole, "security"}};
}

void KnownNetworks::forget(const QString &uuid)
{
    if (auto connection = NetworkManager::findConnectionByUuid(uuid))
        connection->remove();
}

QString KnownNetworks::setAutoconnect(const QString &uuid, bool autoconnect)
{
    auto connection = NetworkManager::findConnectionByUuid(uuid);
    if (!connection)
        return i18nc("@info", "This network is no longer saved on this PC.");
    auto settings = connection->settings();
    settings->setAutoconnect(autoconnect);
    NMVariantMapMap map = settings->toMap();
    if (map.contains(QStringLiteral("802-11-wireless-security"))) {
        auto secrets = connection->secrets(QStringLiteral("802-11-wireless-security"));
        secrets.waitForFinished();
        if (!secrets.isError()) {
            QVariantMap security = map.value(QStringLiteral("802-11-wireless-security"));
            const QVariantMap values = secrets.value().value(QStringLiteral("802-11-wireless-security"));
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                security.insert(it.key(), it.value());
            map.insert(QStringLiteral("802-11-wireless-security"), security);
        }
    }
    auto reply = connection->update(map);
    reply.waitForFinished();
    if (reply.isError())
        return i18nc("@info", "Raye couldn't save this setting. %1", reply.error().message());
    scheduleReload();
    return {};
}
