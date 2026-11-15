// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet: what the computer is connected to, its adapters,
// airplane mode, and the IP and DNS of one connection.
//
// NetworkManager does the work; this reads it for the pages and turns
// what they ask for into connection settings. The checks and the text
// that do not need NetworkManager are in NetworkText, so the tests can
// try them without a network.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <NetworkManagerQt/Device>

class QProcess;

namespace BluezQt
{
class Manager;
}

namespace NetworkText
{
// 0 to 4 bars, as the Wi-Fi glyph draws them (strength is 0-100)
int signalBars(int strength);

bool isIpv4(const QString &text);
bool isIpv6(const QString &text);
// "24" or "255.255.255.0" -> 24; -1 when it is neither
int ipv4Prefix(const QString &text);
QString ipv4Mask(int prefix);
// both addresses in the same network of this prefix
bool sameIpv4Network(const QString &a, const QString &b, int prefix);

// What the IP or DNS dialog filled in:
//   { ipv4: { method: "auto"|"manual", address, prefix (or a mask), gateway },
//     ipv6: { method: "auto"|"manual"|"off", address, prefix, gateway },
//     dnsAuto: bool, dns4: [preferred, alternate], dns6: [...] }
// The answer names the first field that is wrong, "" when all is fine:
// "ipv4-address", "ipv4-prefix", "ipv4-gateway", "ipv6-address",
// "ipv6-prefix", "ipv6-gateway", "dns4", "dns6".
QString checkIpConfig(const QVariantMap &config);
// The sentence for one of those names.
QString errorText(const QString &field);

// 1536 -> "1.5 KB", as Windows writes sizes
QString byteSize(quint64 bytes);

// "Connected, secured", "Connected", "No internet", "Not connected"
QString statusText(bool connected, bool secured, bool internet);
// The line under Wi-Fi: "Off", "Connected, Home", "Not in use, this PC
// is connected by cable", "Not connected". The name comes escaped,
// ready for styled text.
QString wifiSummary(bool wifiEnabled, const QString &primaryType, const QString &primaryName);
// The sentences of the network pages that would talk about Wi-Fi or
// Bluetooth: on a PC without them they say nothing of either.
// Keys: offline, noEthernet, proxy, proxyIntro, resetSummary,
// resetWarning, resetDone, airplaneOn, airplaneOff.
QVariantMap wording(bool hasWifi, bool hasBluetooth);
}

// Is there a Wi-Fi card, as NetworkManager sees it? The window holds
// the router's "wifi" requirement to this, so a USB key plugged in with
// Settings open brings the Wi-Fi pages in.
class WifiHardware : public QObject
{
    Q_OBJECT

public:
    explicit WifiHardware(QObject *parent = nullptr);

    // false while NetworkManager is not there to ask
    bool known() const { return m_known; }
    bool present() const { return m_present; }

Q_SIGNALS:
    void changed(bool present);

private:
    void check();

    QTimer m_check;
    bool m_known = false;
    bool m_present = false;
};

class NetworkStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // the connection the computer reaches the internet through
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    // "wifi", "ethernet", "vpn", "other" or "none"
    Q_PROPERTY(QString primaryType READ primaryType NOTIFY changed)
    Q_PROPERTY(QString primaryName READ primaryName NOTIFY changed)
    Q_PROPERTY(QString primaryUuid READ primaryUuid NOTIFY changed)
    // the page "Properties" opens: network/wifi or network/ethernet
    Q_PROPERTY(QString primaryPage READ primaryPage NOTIFY changed)
    Q_PROPERTY(bool secured READ secured NOTIFY changed)
    // 0-100 for Wi-Fi, -1 otherwise
    Q_PROPERTY(int signalStrength READ signalStrength NOTIFY changed)
    Q_PROPERTY(bool internet READ internet NOTIFY changed)
    // NetworkManager found a sign-in page between us and the internet
    Q_PROPERTY(bool portal READ portal NOTIFY changed)
    // the page that leads to the sign-in page of such a network
    Q_PROPERTY(QString portalUrl READ portalUrl NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    // the line under Wi-Fi on the cards (escaped, for styled text)
    Q_PROPERTY(QString wifiSummary READ wifiSummary NOTIFY changed)
    // a Wi-Fi card is there but the cable carries the connection
    Q_PROPERTY(bool onCable READ onCable NOTIFY changed)
    // NetworkText::wording() for this PC
    Q_PROPERTY(QVariantMap wording READ wording NOTIFY changed)
    Q_PROPERTY(QString ipv4Address READ ipv4Address NOTIFY changed)
    Q_PROPERTY(QString ipv6Address READ ipv6Address NOTIFY changed)
    Q_PROPERTY(bool metered READ metered NOTIFY changed)
    // bytes moved by the primary adapter since it connected
    Q_PROPERTY(QString dataUsed READ dataUsed NOTIFY dataUsedChanged)
    Q_PROPERTY(bool available READ available NOTIFY changed)

    Q_PROPERTY(bool hasWifi READ hasWifi NOTIFY changed)
    Q_PROPERTY(bool hasEthernet READ hasEthernet NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled WRITE setWifiEnabled NOTIFY changed)
    // the switch on the side of a laptop, or the BIOS
    Q_PROPERTY(bool wifiHardwareEnabled READ wifiHardwareEnabled NOTIFY changed)
    Q_PROPERTY(bool hasBluetooth READ hasBluetooth NOTIFY changed)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled WRITE setBluetoothEnabled NOTIFY changed)
    Q_PROPERTY(bool airplaneMode READ airplaneMode WRITE setAirplaneMode NOTIFY changed)

    // [{ uni, interface, uuid, name, connected, cable, speed, mac, driver,
    //    ipv4, ipv6, gateway, dns, metered, autoMetered, ipAuto, dnsAuto,
    //    statusText }] for the Ethernet page
    Q_PROPERTY(QVariantList ethernet READ ethernet NOTIFY changed)
    // [{ uni, interface, kind, description, statusText, enabled, mac, speed }]
    Q_PROPERTY(QVariantList adapters READ adapters NOTIFY changed)

    // the network reset is running
    Q_PROPERTY(bool resetting READ resetting NOTIFY resettingChanged)

public:
    explicit NetworkStatus(QObject *parent = nullptr);
    ~NetworkStatus() override;

    bool connected() const { return m_connected; }
    QString primaryType() const { return m_primaryType; }
    QString primaryName() const { return m_primaryName; }
    QString primaryUuid() const { return m_primaryUuid; }
    QString primaryPage() const;
    bool secured() const { return m_secured; }
    int signalStrength() const { return m_signal; }
    bool internet() const { return m_internet; }
    bool portal() const { return m_portal; }
    QString portalUrl() const;
    QString statusText() const;
    QString wifiSummary() const;
    bool onCable() const { return m_hasWifi && m_primaryType == QLatin1String("ethernet"); }
    QVariantMap wording() const { return NetworkText::wording(m_hasWifi, hasBluetooth()); }
    QString ipv4Address() const { return m_ipv4; }
    QString ipv6Address() const { return m_ipv6; }
    bool metered() const { return m_metered; }
    QString dataUsed() const;
    bool available() const { return m_available; }

    bool hasWifi() const { return m_hasWifi; }
    bool hasEthernet() const { return m_hasEthernet; }
    bool wifiEnabled() const;
    void setWifiEnabled(bool enabled);
    bool wifiHardwareEnabled() const;
    bool hasBluetooth() const;
    bool bluetoothEnabled() const;
    void setBluetoothEnabled(bool enabled);
    bool airplaneMode() const;
    void setAirplaneMode(bool on);

    QVariantList ethernet() const { return m_ethernet; }
    QVariantList adapters() const { return m_adapters; }
    bool resetting() const { return m_reset != nullptr; }

    // The IP and DNS of a saved connection, in the shape checkIpConfig
    // reads. Empty when there is no such connection.
    Q_INVOKABLE QVariantMap ipConfig(const QString &uuid) const;
    // Check, save, and apply to the adapter at once. Returns "" or the
    // sentence that says what is wrong.
    Q_INVOKABLE QString setIpConfig(const QString &uuid, const QVariantMap &config);
    Q_INVOKABLE QString setDnsConfig(const QString &uuid, const QVariantMap &config);
    Q_INVOKABLE bool isMetered(const QString &uuid) const;
    Q_INVOKABLE QString setMetered(const QString &uuid, bool metered);

    // Turn an adapter off (disconnected, no more connecting by itself)
    // or back on.
    Q_INVOKABLE void setAdapterEnabled(const QString &uni, bool enabled);

    // KDE's network connections window, for what Raye does not draw
    // (802.1X, bonds, VLANs, odd VPNs).
    Q_INVOKABLE void openMoreOptions(const QString &uuid = QString());
    Q_INVOKABLE void copyText(const QString &text);

    // Forget every Wi-Fi network and VPN and restart networking, with
    // the password of an administrator.
    Q_INVOKABLE void resetNetwork();

Q_SIGNALS:
    void changed();
    void dataUsedChanged();
    void resettingChanged();
    // ok: done; otherwise the reset was cancelled or failed
    void resetFinished(bool ok);

private:
    void scheduleRefresh();
    void refresh();
    void watchDevices();
    void watchStatistics();
    QVariantMap ethernetEntry(const NetworkManager::Device::Ptr &device) const;
    QString update(const QString &uuid, const QVariantMap &config, bool ip, bool dns);

    QTimer m_refresh;
    BluezQt::Manager *m_bluetooth = nullptr;
    QProcess *m_reset = nullptr;
    QString m_statisticsDevice;
    // we turned the adapter's counters on, so we turn them off again
    bool m_ownsRefreshRate = false;

    bool m_available = false;
    bool m_connected = false;
    QString m_primaryType = QStringLiteral("none");
    QString m_primaryName;
    QString m_primaryUuid;
    bool m_secured = false;
    int m_signal = -1;
    bool m_internet = false;
    bool m_portal = false;
    QString m_ipv4;
    QString m_ipv6;
    bool m_metered = false;
    quint64 m_bytes = 0;
    bool m_hasWifi = false;
    bool m_hasEthernet = false;
    QVariantList m_ethernet;
    QVariantList m_adapters;
};
