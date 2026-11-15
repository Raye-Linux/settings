// SPDX-License-Identifier: GPL-3.0-or-later
// Wi-Fi: the networks in range, joining one with its password, and the
// networks this computer knows.
//
// Raye asks NetworkManager directly. A new network gets its password in
// its own settings, so nothing pops up when it is right; when it is wrong
// the page says so under the field. The sorting and the settings of a new
// network are in WifiList, without NetworkManager, for the tests.
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <NetworkManagerQt/GenericTypes>
#include <NetworkManagerQt/WirelessDevice>

namespace WifiList
{
struct AccessPoint {
    QString ssid;
    int strength = 0;
    // "open", "owe", "wep", "wpa-psk", "sae", "enterprise"
    QString security;
    uint frequency = 0; // MHz
    QString path;
};

struct Row {
    QString ssid;
    int strength = 0;
    QString security;
    bool band5 = false;
    bool connected = false;
    bool known = false;
    QString uuid;
    QString path; // the strongest access point of that name
};

// One row per network name: the strongest access point of the name,
// the connected network first, then known ones, then by signal, then by
// name. Hidden networks (no name) are left out.
QList<Row> merge(const QList<AccessPoint> &points, const QHash<QString, QString> &knownUuids,
                 const QString &connectedSsid);

// NetworkManager's access point flags -> one of the security names
QString securityOf(uint flags, uint wpaFlags, uint rsnFlags);
bool isSecured(const QString &security);
// Only these are joined from the list; 802.1X needs the KDE editor.
bool canJoin(const QString &security);

// Can this be the password of a network of that kind? WPA: 8 to 63
// characters or 64 hex digits; WEP: 5 or 13 characters, 10 or 26 hex.
bool passwordAcceptable(const QString &security, const QString &password);

// The settings of a new network, ready for AddAndActivateConnection.
NMVariantMapMap newConnection(const QString &ssid, const QString &security, const QString &password,
                              bool autoconnect, bool randomAddress);
}

class WifiNetworks : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // the name being joined, "" when none
    Q_PROPERTY(QString joining READ joining NOTIFY joiningChanged)
    // the page is on screen: scan now and every 15 s
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // new networks get an address of their own on each network
    Q_PROPERTY(bool randomAddresses READ randomAddresses WRITE setRandomAddresses NOTIFY randomAddressesChanged)
    // [{ label, value }] of the adapter and the network it is on
    Q_PROPERTY(QVariantList hardware READ hardware NOTIFY hardwareChanged)
    // keep the rows where they are (a row is open, the user is typing)
    Q_PROPERTY(bool holdOrder READ holdOrder WRITE setHoldOrder NOTIFY holdOrderChanged)

public:
    enum Role {
        SsidRole = Qt::UserRole + 1,
        StrengthRole,
        BarsRole,
        SecurityRole,
        SecuredRole,
        CanJoinRole,
        ConnectedRole,
        JoiningRole,
        KnownRole,
        UuidRole,
        Band5Role,
    };
    Q_ENUM(Role)

    explicit WifiNetworks(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return !m_device.isNull(); }
    bool enabled() const;
    void setEnabled(bool enabled);
    QString joining() const { return m_joining; }
    bool active() const { return m_active; }
    void setActive(bool active);
    bool randomAddresses() const;
    void setRandomAddresses(bool on);
    QVariantList hardware() const;
    bool holdOrder() const { return m_holdOrder; }
    void setHoldOrder(bool hold);

    // Does joining this network need a password from the user?
    Q_INVOKABLE bool needsPassword(const QString &ssid) const;
    // Join: a known network with what it knows, a new one with this
    // password. Returns "" when it started, or what is wrong.
    Q_INVOKABLE QString join(const QString &ssid, const QString &password, bool autoconnect);
    Q_INVOKABLE void disconnectFrom(const QString &ssid);
    Q_INVOKABLE void forget(const QString &ssid);
    Q_INVOKABLE void scan();
    Q_INVOKABLE QString uuidOf(const QString &ssid) const;

    // for the tests: the rows as they would be shown
    void setRows(const QList<WifiList::Row> &rows);

Q_SIGNALS:
    void availableChanged();
    void enabledChanged();
    void countChanged();
    void joiningChanged();
    void activeChanged();
    void randomAddressesChanged();
    void hardwareChanged();
    void holdOrderChanged();
    void joined(const QString &ssid);
    // wrongPassword: the network turned the password down
    void joinFailed(const QString &ssid, bool wrongPassword, const QString &message);

private:
    void findDevice();
    void scheduleRebuild();
    void rebuild();
    void watchJoin(const QString &ssid, const QString &activePath);
    void setJoining(const QString &ssid);
    void applyRows(QList<WifiList::Row> rows);

    NetworkManager::WirelessDevice::Ptr m_device;
    QList<WifiList::Row> m_rows;
    QString m_joining;
    // a network made by join() that has not connected yet: forgotten
    // again if its password turns out wrong, as Windows does
    QString m_newUuid;
    bool m_active = false;
    bool m_holdOrder = false;
    QTimer m_rebuild;
    QTimer m_scan;
};

class KnownNetworks : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        SsidRole,
        UuidRole,
        AutoconnectRole,
        SecurityRole,
    };
    Q_ENUM(Role)

    explicit KnownNetworks(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void forget(const QString &uuid);
    Q_INVOKABLE QString setAutoconnect(const QString &uuid, bool autoconnect);

Q_SIGNALS:
    void countChanged();

private:
    void scheduleReload();
    void reload();

    struct Entry {
        QString name;
        QString ssid;
        QString uuid;
        bool autoconnect = true;
        QString security;
    };
    QList<Entry> m_entries;
    QTimer m_reload;
};
