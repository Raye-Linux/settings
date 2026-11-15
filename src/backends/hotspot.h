// SPDX-License-Identifier: GPL-3.0-or-later
// Mobile hotspot: share this computer's internet over Wi-Fi.
//
// The hotspot is one saved NetworkManager connection named "Raye
// hotspot" (Wi-Fi in access point mode, IPv4 shared). It is made the
// first time it is turned on or edited.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <NetworkManagerQt/GenericTypes>

namespace HotspotText
{
// What the connection is called in NetworkManager
QString connectionId();
// "<computer name> hotspot", cut to the 32 bytes a network name allows
QString defaultName(const QString &hostname);
// 12 letters and digits that are not easy to mix up
QString newPassword();
bool nameAcceptable(const QString &name);
bool passwordAcceptable(const QString &password);
// band: "any", "2.4" or "5"
NMVariantMapMap settings(const QString &uuid, const QString &name, const QString &password,
                         const QString &band);
// how many devices a dnsmasq lease file lists
int leases(const QByteArray &file);
}

class Hotspot : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // there is a Wi-Fi adapter that can be an access point
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString password READ password NOTIFY changed)
    Q_PROPERTY(QString band READ band NOTIFY changed)
    Q_PROPERTY(bool band5Available READ band5Available NOTIFY changed)
    // the connection that is shared, "" when there is none
    Q_PROPERTY(QString source READ source NOTIFY changed)
    // turning the hotspot on would drop the Wi-Fi it would share
    Q_PROPERTY(bool sharesItsOwnWifi READ sharesItsOwnWifi NOTIFY changed)
    Q_PROPERTY(int clients READ clients NOTIFY changed)

public:
    explicit Hotspot(QObject *parent = nullptr);

    bool available() const;
    bool active() const { return m_active; }
    bool busy() const { return m_busy; }
    QString name() const { return m_name; }
    QString password() const { return m_password; }
    QString band() const { return m_band; }
    bool band5Available() const;
    QString source() const { return m_source; }
    bool sharesItsOwnWifi() const { return m_ownWifi; }
    int clients() const { return m_clients; }

    // "" when it started, or what went wrong
    Q_INVOKABLE QString setActive(bool on);
    // Save a new name, password and band; "" or what is wrong.
    Q_INVOKABLE QString save(const QString &name, const QString &password, const QString &band);

Q_SIGNALS:
    void changed();
    void failed(const QString &message);

private:
    void scheduleRefresh();
    void refresh();
    QString store(const QString &name, const QString &password, const QString &band);

    QTimer m_refresh;
    QTimer m_clientsTimer;
    QString m_uuid;
    bool m_active = false;
    bool m_busy = false;
    QString m_name;
    QString m_password;
    QString m_band = QStringLiteral("any");
    QString m_source;
    bool m_ownWifi = false;
    int m_clients = 0;
};
