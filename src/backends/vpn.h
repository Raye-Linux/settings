// SPDX-License-Identifier: GPL-3.0-or-later
// VPN: the saved VPN connections, connecting to one, and adding one
// from the file the VPN provider gives (WireGuard .conf, OpenVPN .ovpn).
//
// A WireGuard file is read here and saved as NetworkManager settings;
// the other kinds are read by their NetworkManager plugin (nmcli import).
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <NetworkManagerQt/GenericTypes>

#include <memory>

class QProcess;

namespace VpnText
{
// A WireGuard file, as wg-quick reads it. On success `settings` holds a
// whole connection named `name`; on failure the answer names the
// problem: "empty", "too-big", "no-interface", "private-key", "address",
// "peer", "public-key", "endpoint", "allowed-ips", "dns", "port".
QString readWireGuard(const QByteArray &file, const QString &name, NMVariantMapMap *settings);
// The sentence for one of those names.
QString errorText(const QString &problem);
// NetworkManager wants an interface name of at most 15 safe characters
QString interfaceName(const QString &name);
// "Connection 'x' (uuid) successfully added." -> the uuid
QString uuidFromImport(const QString &output);
// "vpn.service-type" -> "OpenVPN", "WireGuard", ...
QString kindName(const QString &serviceType);
}

class VpnConnections : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // connect even when the network is metered
    Q_PROPERTY(bool overMetered READ overMetered WRITE setOverMetered NOTIFY overMeteredChanged)
    // [{ id, name }] of the kinds of VPN a file can be added for
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        UuidRole,
        KindRole,
        // "connected", "connecting", "off"
        StateRole,
    };
    Q_ENUM(Role)

    explicit VpnConnections(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool overMetered() const;
    void setOverMetered(bool allow);
    QVariantList providers() const;
    bool importing() const { return m_import != nullptr || !m_importName.isEmpty(); }

    // "" when it started, or why it did not
    Q_INVOKABLE QString connectTo(const QString &uuid);
    Q_INVOKABLE void disconnectFrom(const QString &uuid);
    Q_INVOKABLE void remove(const QString &uuid);
    Q_INVOKABLE void editInKde(const QString &uuid);
    // Add a VPN from a file (a path or a file:// URL). The answer comes
    // with importFinished.
    Q_INVOKABLE void importFile(const QString &provider, const QString &file, const QString &name);

Q_SIGNALS:
    void countChanged();
    void overMeteredChanged();
    void importingChanged();
    void importFinished(bool ok, const QString &message);
    void connectFailed(const QString &name, const QString &message);

private:
    void scheduleReload();
    void reload();
    void finishImport(bool ok, const QString &message);

    struct Entry {
        QString name;
        QString uuid;
        QString kind;
        QString state;
    };
    QList<Entry> m_entries;
    QTimer m_reload;
    QProcess *m_import = nullptr;
    // the renamed copy the plugin reads, gone with the import
    std::unique_ptr<QTemporaryDir> m_importDir;
    QString m_importName;
};
