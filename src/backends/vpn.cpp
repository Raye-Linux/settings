// SPDX-License-Identifier: GPL-3.0-or-later
#include "vpn.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/VpnSetting>

#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

#include <algorithm>

namespace {

constexpr qint64 largestFile = 64 * 1024;
const QString pluginDir = QStringLiteral("/usr/lib/NetworkManager/VPN");

KConfigGroup rayeGroup()
{
    return KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"))->group(QStringLiteral("Network"));
}

bool isKey(const QString &text)
{
    // 32 bytes of base64: 43 characters and one '='
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z0-9+/]{43}=$"));
    return shape.match(text).hasMatch() && QByteArray::fromBase64(text.toLatin1()).size() == 32;
}

struct Cidr {
    QHostAddress address;
    int prefix = -1;
};

bool readCidr(const QString &text, Cidr *out, bool needsPrefix)
{
    const QString address = text.section(QLatin1Char('/'), 0, 0).trimmed();
    QHostAddress ip;
    if (!ip.setAddress(address) || address.contains(QLatin1Char('%')))
        return false;
    const bool v6 = ip.protocol() == QAbstractSocket::IPv6Protocol;
    if (!v6 && address.count(QLatin1Char('.')) != 3)
        return false;
    int prefix = v6 ? 128 : 32;
    if (text.contains(QLatin1Char('/'))) {
        bool ok = false;
        prefix = text.section(QLatin1Char('/'), 1).trimmed().toInt(&ok);
        if (!ok || prefix < 0 || prefix > (v6 ? 128 : 32))
            return false;
    } else if (needsPrefix) {
        return false;
    }
    out->address = ip;
    out->prefix = prefix;
    return true;
}

QStringList listOf(const QString &value)
{
    QStringList list;
    const auto parts = value.split(QLatin1Char(','));
    for (const QString &part : parts) {
        if (!part.trimmed().isEmpty())
            list.append(part.trimmed());
    }
    return list;
}

bool isEndpoint(const QString &text)
{
    static const QRegularExpression v6(QStringLiteral("^\\[([0-9A-Fa-f:.]+)\\]:(\\d{1,5})$"));
    static const QRegularExpression host(QStringLiteral("^([A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?):(\\d{1,5})$"));
    QString port;
    if (const auto m = v6.match(text); m.hasMatch()) {
        if (QHostAddress(m.captured(1)).protocol() != QAbstractSocket::IPv6Protocol)
            return false;
        port = m.captured(2);
    } else if (const auto m = host.match(text); m.hasMatch()) {
        port = m.captured(3);
    } else {
        return false;
    }
    const int number = port.toInt();
    return number >= 1 && number <= 65535;
}

bool isDomain(const QString &text)
{
    static const QRegularExpression domain(QStringLiteral("^[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+$"));
    return domain.match(text).hasMatch();
}

void registerTypes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    qDBusRegisterMetaType<NMVariantMapList>();
    qDBusRegisterMetaType<NMVariantMapMap>();
}

// the import-capable VPN plugins installed: short name -> service
QMap<QString, QString> installedPlugins()
{
    QMap<QString, QString> plugins;
    const auto files = QDir(pluginDir).entryInfoList({QStringLiteral("*.name")}, QDir::Files);
    for (const QFileInfo &file : files) {
        QSettings name(file.filePath(), QSettings::IniFormat);
        name.beginGroup(QStringLiteral("VPN Connection"));
        const QString service = name.value(QStringLiteral("service")).toString();
        const QString shortName = service.section(QLatin1Char('.'), -1);
        if (shortName == QLatin1String("openvpn") || shortName == QLatin1String("vpnc"))
            plugins.insert(shortName, service);
    }
    return plugins;
}

void openKde(const QString &uuid)
{
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

} // namespace

namespace VpnText
{

QString readWireGuard(const QByteArray &file, const QString &name, NMVariantMapMap *settings)
{
    if (file.size() > largestFile)
        return QStringLiteral("too-big");
    const QString text = QString::fromUtf8(file);
    if (text.trimmed().isEmpty())
        return QStringLiteral("empty");

    struct Peer {
        QString publicKey, presharedKey, endpoint;
        QStringList allowed;
        int keepalive = -1;
    };
    bool interfaceSeen = false;
    QString privateKey, addresses, dns, listenPort, mtu;
    QList<Peer> peers;
    QString sectionName;
    const auto lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.section(QLatin1Char('#'), 0, 0).trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            sectionName = line.mid(1, line.size() - 2).trimmed().toLower();
            if (sectionName == QLatin1String("interface"))
                interfaceSeen = true;
            else if (sectionName == QLatin1String("peer"))
                peers.append(Peer());
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed().toLower();
        const QString value = line.mid(equals + 1).trimmed();
        if (sectionName == QLatin1String("interface")) {
            if (key == QLatin1String("privatekey"))
                privateKey = value;
            else if (key == QLatin1String("address"))
                addresses += (addresses.isEmpty() ? QString() : QStringLiteral(",")) + value;
            else if (key == QLatin1String("dns"))
                dns += (dns.isEmpty() ? QString() : QStringLiteral(",")) + value;
            else if (key == QLatin1String("listenport"))
                listenPort = value;
            else if (key == QLatin1String("mtu"))
                mtu = value;
            // PreUp, PostUp and the like are commands for wg-quick: never run
        } else if (sectionName == QLatin1String("peer") && !peers.isEmpty()) {
            Peer &peer = peers.last();
            if (key == QLatin1String("publickey"))
                peer.publicKey = value;
            else if (key == QLatin1String("presharedkey"))
                peer.presharedKey = value;
            else if (key == QLatin1String("endpoint"))
                peer.endpoint = value;
            else if (key == QLatin1String("allowedips"))
                peer.allowed += listOf(value);
            else if (key == QLatin1String("persistentkeepalive"))
                peer.keepalive = value.toLower() == QLatin1String("off") ? 0 : value.toInt();
        }
    }

    if (!interfaceSeen)
        return QStringLiteral("no-interface");
    if (!isKey(privateKey))
        return QStringLiteral("private-key");
    QList<Cidr> v4, v6;
    const QStringList addressList = listOf(addresses);
    if (addressList.isEmpty())
        return QStringLiteral("address");
    for (const QString &entry : addressList) {
        Cidr cidr;
        if (!readCidr(entry, &cidr, false))
            return QStringLiteral("address");
        (cidr.address.protocol() == QAbstractSocket::IPv6Protocol ? v6 : v4).append(cidr);
    }
    QList<uint> dns4;
    QList<QByteArray> dns6;
    QStringList searches;
    for (const QString &entry : listOf(dns)) {
        QHostAddress ip;
        if (ip.setAddress(entry) && ip.protocol() == QAbstractSocket::IPv4Protocol && entry.count(QLatin1Char('.')) == 3) {
            dns4.append(qToBigEndian(ip.toIPv4Address()));
        } else if (ip.setAddress(entry) && ip.protocol() == QAbstractSocket::IPv6Protocol) {
            const Q_IPV6ADDR raw = ip.toIPv6Address();
            dns6.append(QByteArray(reinterpret_cast<const char *>(raw.c), 16));
        } else if (isDomain(entry)) {
            searches.append(entry);
        } else {
            return QStringLiteral("dns");
        }
    }
    int port = 0;
    if (!listenPort.isEmpty()) {
        bool ok = false;
        port = listenPort.toInt(&ok);
        if (!ok || port < 1 || port > 65535)
            return QStringLiteral("port");
    }
    if (peers.isEmpty())
        return QStringLiteral("peer");

    NMVariantMapList peerList;
    for (const Peer &peer : std::as_const(peers)) {
        if (!isKey(peer.publicKey))
            return QStringLiteral("public-key");
        if (!peer.presharedKey.isEmpty() && !isKey(peer.presharedKey))
            return QStringLiteral("public-key");
        if (!isEndpoint(peer.endpoint))
            return QStringLiteral("endpoint");
        if (peer.allowed.isEmpty())
            return QStringLiteral("allowed-ips");
        for (const QString &allowed : peer.allowed) {
            Cidr cidr;
            if (!readCidr(allowed, &cidr, false))
                return QStringLiteral("allowed-ips");
        }
        QVariantMap entry{{QStringLiteral("public-key"), peer.publicKey},
                          {QStringLiteral("endpoint"), peer.endpoint},
                          {QStringLiteral("allowed-ips"), peer.allowed}};
        if (!peer.presharedKey.isEmpty()) {
            entry.insert(QStringLiteral("preshared-key"), peer.presharedKey);
            entry.insert(QStringLiteral("preshared-key-flags"), 0u);
        }
        if (peer.keepalive > 0 && peer.keepalive <= 65535)
            entry.insert(QStringLiteral("persistent-keepalive"), uint(peer.keepalive));
        peerList.append(entry);
    }

    registerTypes();
    const QString id = name.trimmed().isEmpty() ? QStringLiteral("WireGuard") : name.trimmed();
    NMVariantMapMap result;
    result.insert(QStringLiteral("connection"),
                  QVariantMap{{QStringLiteral("id"), id},
                              {QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                              {QStringLiteral("type"), QStringLiteral("wireguard")},
                              {QStringLiteral("interface-name"), interfaceName(id)},
                              {QStringLiteral("autoconnect"), false}});
    QVariantMap wireguard{{QStringLiteral("private-key"), privateKey},
                          {QStringLiteral("private-key-flags"), 0u},
                          {QStringLiteral("peers"), QVariant::fromValue(peerList)}};
    if (port > 0)
        wireguard.insert(QStringLiteral("listen-port"), uint(port));
    if (const int value = mtu.toInt(); value >= 576 && value <= 9000)
        wireguard.insert(QStringLiteral("mtu"), uint(value));
    result.insert(QStringLiteral("wireguard"), wireguard);

    const auto family = [&](const QList<Cidr> &list, bool isV6) {
        QVariantMap section;
        if (list.isEmpty()) {
            section.insert(QStringLiteral("method"), QStringLiteral("disabled"));
            return section;
        }
        NMVariantMapList data;
        for (const Cidr &cidr : list)
            data.append(QVariantMap{{QStringLiteral("address"), cidr.address.toString()},
                                    {QStringLiteral("prefix"), uint(cidr.prefix)}});
        section.insert(QStringLiteral("method"), QStringLiteral("manual"));
        section.insert(QStringLiteral("address-data"), QVariant::fromValue(data));
        if (isV6 && !dns6.isEmpty())
            section.insert(QStringLiteral("dns"), QVariant::fromValue(dns6));
        if (!isV6 && !dns4.isEmpty())
            section.insert(QStringLiteral("dns"), QVariant::fromValue(dns4));
        if (!searches.isEmpty())
            section.insert(QStringLiteral("dns-search"), searches);
        // the VPN's DNS servers answer before the network's
        if (!dns4.isEmpty() || !dns6.isEmpty())
            section.insert(QStringLiteral("dns-priority"), -50);
        return section;
    };
    result.insert(QStringLiteral("ipv4"), family(v4, false));
    result.insert(QStringLiteral("ipv6"), family(v6, true));
    *settings = result;
    return {};
}

QString errorText(const QString &problem)
{
    if (problem == QLatin1String("empty"))
        return i18nc("@info", "This file is empty.");
    if (problem == QLatin1String("too-big"))
        return i18nc("@info", "This file is too big to be a VPN configuration.");
    if (problem == QLatin1String("no-interface"))
        return i18nc("@info", "This isn't a WireGuard configuration file. It has no [Interface] section.");
    if (problem == QLatin1String("private-key"))
        return i18nc("@info", "The private key in this file isn't valid.");
    if (problem == QLatin1String("address"))
        return i18nc("@info", "The address in this file isn't valid.");
    if (problem == QLatin1String("peer"))
        return i18nc("@info", "This file has no server ([Peer] section).");
    if (problem == QLatin1String("public-key"))
        return i18nc("@info", "A server key in this file isn't valid.");
    if (problem == QLatin1String("endpoint"))
        return i18nc("@info", "The server address (Endpoint) in this file isn't valid.");
    if (problem == QLatin1String("allowed-ips"))
        return i18nc("@info", "The AllowedIPs line in this file isn't valid.");
    if (problem == QLatin1String("dns"))
        return i18nc("@info", "The DNS line in this file isn't valid.");
    if (problem == QLatin1String("port"))
        return i18nc("@info", "The ListenPort in this file isn't valid.");
    return i18nc("@info", "This file can't be used.");
}

QString interfaceName(const QString &name)
{
    QString result;
    for (const QChar c : name) {
        if (c.isSpace())
            result += QLatin1Char('-');
        else if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
                 || (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_') || c == QLatin1Char('-'))
            result += c;
    }
    while (result.startsWith(QLatin1Char('-')))
        result.remove(0, 1);
    result = result.left(15);
    while (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result.isEmpty() ? QStringLiteral("wg-raye") : result;
}

QString uuidFromImport(const QString &output)
{
    static const QRegularExpression uuid(
        QStringLiteral("\\(([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\\)"));
    const auto match = uuid.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString kindName(const QString &serviceType)
{
    const QString shortName = serviceType.section(QLatin1Char('.'), -1);
    if (shortName == QLatin1String("openvpn"))
        return QStringLiteral("OpenVPN");
    if (shortName == QLatin1String("vpnc"))
        return i18nc("@info VPN kind", "Cisco compatible");
    if (shortName == QLatin1String("openconnect"))
        return QStringLiteral("OpenConnect");
    if (shortName == QLatin1String("l2tp"))
        return QStringLiteral("L2TP");
    if (shortName == QLatin1String("pptp"))
        return QStringLiteral("PPTP");
    if (shortName == QLatin1String("strongswan"))
        return QStringLiteral("IKEv2");
    if (shortName == QLatin1String("wireguard"))
        return QStringLiteral("WireGuard");
    return shortName.isEmpty() ? i18nc("@info VPN kind", "VPN") : shortName;
}

} // namespace VpnText

VpnConnections::VpnConnections(QObject *parent)
    : QAbstractListModel(parent)
{
    registerTypes();
    m_reload.setSingleShot(true);
    m_reload.setInterval(50);
    connect(&m_reload, &QTimer::timeout, this, &VpnConnections::reload);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionAdded, this,
            &VpnConnections::scheduleReload);
    connect(NetworkManager::settingsNotifier(), &NetworkManager::SettingsNotifier::connectionRemoved, this,
            &VpnConnections::scheduleReload);
    connect(NetworkManager::notifier(), &NetworkManager::Notifier::activeConnectionsChanged, this,
            &VpnConnections::scheduleReload);
    reload();
}

void VpnConnections::scheduleReload()
{
    m_reload.start();
}

void VpnConnections::reload()
{
    QHash<QString, QString> states;
    const auto actives = NetworkManager::activeConnections();
    for (const auto &active : actives) {
        if (!active->property("rayeWatched").toBool()) {
            active->setProperty("rayeWatched", true);
            connect(active.data(), &NetworkManager::ActiveConnection::stateChanged, this, &VpnConnections::scheduleReload);
        }
        const auto state = active->state();
        states.insert(active->uuid(), state == NetworkManager::ActiveConnection::Activated ? QStringLiteral("connected")
                                          : state == NetworkManager::ActiveConnection::Activating
                                          ? QStringLiteral("connecting")
                                          : QStringLiteral("off"));
    }
    QList<Entry> entries;
    const auto connections = NetworkManager::listConnections();
    for (const auto &connection : connections) {
        const auto settings = connection->settings();
        QString kind;
        if (settings->connectionType() == NetworkManager::ConnectionSettings::WireGuard) {
            kind = QStringLiteral("WireGuard");
        } else if (settings->connectionType() == NetworkManager::ConnectionSettings::Vpn) {
            auto vpn = settings->setting(NetworkManager::Setting::Vpn).staticCast<NetworkManager::VpnSetting>();
            kind = VpnText::kindName(vpn ? vpn->serviceType() : QString());
        } else {
            continue;
        }
        entries.append({settings->id(), settings->uuid(), kind, states.value(settings->uuid(), QStringLiteral("off"))});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    bool same = entries.size() == m_entries.size();
    for (int i = 0; same && i < entries.size(); ++i)
        same = entries[i].uuid == m_entries[i].uuid;
    if (same) {
        m_entries = entries;
        if (!m_entries.isEmpty())
            Q_EMIT dataChanged(index(0, 0), index(m_entries.size() - 1, 0));
        return;
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

int VpnConnections::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant VpnConnections::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case UuidRole:
        return entry.uuid;
    case KindRole:
        return entry.kind;
    case StateRole:
        return entry.state;
    }
    return {};
}

QHash<int, QByteArray> VpnConnections::roleNames() const
{
    return {{NameRole, "name"}, {UuidRole, "uuid"}, {KindRole, "kind"}, {StateRole, "connectionState"}};
}

bool VpnConnections::overMetered() const
{
    return rayeGroup().readEntry("VpnOverMetered", true);
}

void VpnConnections::setOverMetered(bool allow)
{
    if (allow == overMetered())
        return;
    KConfigGroup group = rayeGroup();
    group.writeEntry("VpnOverMetered", allow);
    group.sync();
    Q_EMIT overMeteredChanged();
}

QVariantList VpnConnections::providers() const
{
    QVariantList list{QVariantMap{{QStringLiteral("id"), QStringLiteral("wireguard")},
                                  {QStringLiteral("name"), i18nc("@item VPN provider", "WireGuard (.conf file)")}}};
    const auto plugins = installedPlugins();
    if (plugins.contains(QStringLiteral("openvpn")))
        list.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("openvpn")},
                                {QStringLiteral("name"), i18nc("@item VPN provider", "OpenVPN (.ovpn file)")}});
    if (plugins.contains(QStringLiteral("vpnc")))
        list.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("vpnc")},
                                {QStringLiteral("name"), i18nc("@item VPN provider", "Cisco compatible (.pcf file)")}});
    return list;
}

QString VpnConnections::connectTo(const QString &uuid)
{
    auto connection = NetworkManager::findConnectionByUuid(uuid);
    if (!connection)
        return i18nc("@info", "This VPN is no longer saved on this PC.");
    const auto metered = NetworkManager::metered();
    if (!overMetered() && (metered == NetworkManager::Device::Yes || metered == NetworkManager::Device::GuessYes))
        return i18nc("@info", "Your network is metered. Turn on \"Allow VPN over metered networks\" to connect.");
    const QString name = connection->name();
    auto reply = NetworkManager::activateConnection(connection->path(), QStringLiteral("/"), QStringLiteral("/"));
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError())
            Q_EMIT connectFailed(name, i18nc("@info", "Can't connect to %1.", name));
    });
    return {};
}

void VpnConnections::disconnectFrom(const QString &uuid)
{
    const auto actives = NetworkManager::activeConnections();
    for (const auto &active : actives) {
        if (active->uuid() == uuid)
            NetworkManager::deactivateConnection(active->path());
    }
}

void VpnConnections::remove(const QString &uuid)
{
    if (auto connection = NetworkManager::findConnectionByUuid(uuid))
        connection->remove();
}

void VpnConnections::editInKde(const QString &uuid)
{
    openKde(uuid);
}

void VpnConnections::importFile(const QString &provider, const QString &file, const QString &name)
{
    if (m_import || m_importName == QLatin1String("pending"))
        return;
    const QUrl url = QUrl::fromUserInput(file, QDir::currentPath(), QUrl::AssumeLocalFile);
    if (!url.isLocalFile()) {
        Q_EMIT importFinished(false, i18nc("@info", "Choose a file on this PC."));
        return;
    }
    const QFileInfo info(url.toLocalFile());
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        Q_EMIT importFinished(false, i18nc("@info", "Raye can't read this file."));
        return;
    }
    if (info.size() > largestFile) {
        Q_EMIT importFinished(false, VpnText::errorText(QStringLiteral("too-big")));
        return;
    }
    const QString cleanName = name.trimmed().left(64);

    if (provider == QLatin1String("wireguard")) {
        QFile f(info.filePath());
        if (!f.open(QIODevice::ReadOnly)) {
            Q_EMIT importFinished(false, i18nc("@info", "Raye can't read this file."));
            return;
        }
        NMVariantMapMap settings;
        const QString problem = VpnText::readWireGuard(f.read(largestFile + 1),
                                                       cleanName.isEmpty() ? info.completeBaseName() : cleanName, &settings);
        if (!problem.isEmpty()) {
            Q_EMIT importFinished(false, VpnText::errorText(problem));
            return;
        }
        m_importName = QStringLiteral("pending");
        Q_EMIT importingChanged();
        auto reply = NetworkManager::addConnection(settings);
        auto *watcher = new QDBusPendingCallWatcher(reply, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            m_importName.clear();
            if (call->isError())
                finishImport(false, i18nc("@info", "Raye couldn't save this VPN. %1", call->error().message()));
            else
                finishImport(true, QString());
        });
        return;
    }

    if (!installedPlugins().contains(provider)) {
        Q_EMIT importFinished(false, i18nc("@info", "This kind of VPN isn't installed."));
        return;
    }
    // The plugin names the connection after the file: hand it a copy
    // named the way the user asked.
    m_importDir = std::make_unique<QTemporaryDir>();
    QTemporaryDir *folder = m_importDir.get();
    const QString base = cleanName.isEmpty() ? info.completeBaseName() : cleanName;
    QString safe = base;
    safe.replace(QRegularExpression(QStringLiteral("[/\\\\\\x00-\\x1f]")), QStringLiteral("-"));
    if (safe.startsWith(QLatin1Char('.')) || safe.startsWith(QLatin1Char('-')))
        safe.prepend(QStringLiteral("VPN "));
    const QString copy = folder->filePath(safe + QLatin1Char('.') + info.suffix());
    if (!folder->isValid() || !QFile::copy(info.filePath(), copy)) {
        m_importDir.reset();
        Q_EMIT importFinished(false, i18nc("@info", "Raye can't read this file."));
        return;
    }
    m_import = new QProcess(this);
    m_import->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_import, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(m_import->readAll());
        m_importDir.reset();
        m_import->deleteLater();
        m_import = nullptr;
        const QString uuid = VpnText::uuidFromImport(output);
        if (status == QProcess::NormalExit && code == 0 && !uuid.isEmpty())
            finishImport(true, QString());
        else
            finishImport(false, i18nc("@info", "This file couldn't be added. Check that it comes from your VPN provider."));
    });
    connect(m_import, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_importDir.reset();
        m_import->deleteLater();
        m_import = nullptr;
        finishImport(false, i18nc("@info", "This file couldn't be added."));
    });
    m_import->start(QStringLiteral("nmcli"), {QStringLiteral("connection"), QStringLiteral("import"), QStringLiteral("type"),
                                              provider, QStringLiteral("file"), copy});
    Q_EMIT importingChanged();
}

void VpnConnections::finishImport(bool ok, const QString &message)
{
    Q_EMIT importingChanged();
    Q_EMIT importFinished(ok, message);
    scheduleReload();
}
