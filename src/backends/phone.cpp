// SPDX-License-Identifier: GPL-3.0-or-later
#include "phone.h"

#include <QDBusMessage>
#include <QDBusReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {
const QString service = QStringLiteral("org.kde.kdeconnect");
const QString daemonPath = QStringLiteral("/modules/kdeconnect");
const QString daemonInterface = QStringLiteral("org.kde.kdeconnect.daemon");
const QString deviceInterface = QStringLiteral("org.kde.kdeconnect.device");
const QString shareInterface = QStringLiteral("org.kde.kdeconnect.device.share");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
constexpr int callTimeout = 3000;
// how long a freshly started service gets before the page says it failed
constexpr int startWait = 20000;
}

PhoneLink::PhoneLink(QObject *parent)
    : PhoneLink(QDBusConnection::sessionBus(), QStandardPaths::findExecutable(QStringLiteral("kdeconnectd")), parent)
{
}

PhoneLink::PhoneLink(const QDBusConnection &bus, QObject *parent)
    : PhoneLink(bus, QString(), parent)
{
}

PhoneLink::PhoneLink(const QDBusConnection &bus, const QString &daemon, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_daemon(daemon)
{
    m_bus.connect(service, daemonPath, daemonInterface, QStringLiteral("deviceListChanged"), this, SLOT(listChanged()));
    m_bus.connect(service, daemonPath, daemonInterface, QStringLiteral("deviceVisibilityChanged"), this, SLOT(listChanged()));
    m_bus.connect(service, daemonPath, daemonInterface, QStringLiteral("pairingRequestsChanged"), this, SLOT(listChanged()));
    refresh();
}

void PhoneLink::listChanged()
{
    refresh();
}

void PhoneLink::refresh()
{
    m_devices.clear();
    const QDBusReply<QString> name = m_bus.call(
        QDBusMessage::createMethodCall(service, daemonPath, daemonInterface, QStringLiteral("announcedName")), QDBus::Block, callTimeout);
    m_available = name.isValid();
    m_announcedName = name.value();
    if (m_available) {
        m_starting = false;
    } else if (!m_daemon.isEmpty() && !m_startAsked) {
        // Raye starts the service at sign-in only once a phone is linked,
        // and it has no D-Bus activation: start it here, once.
        m_startAsked = true;
        m_starting = QProcess::startDetached(m_daemon, {});
        if (m_starting) {
            QTimer::singleShot(startWait, this, [this] {
                m_starting = false;
                refresh();
            });
        }
    }
    if (m_available) {
        const QDBusReply<QStringList> ids = m_bus.call(
            QDBusMessage::createMethodCall(service, daemonPath, daemonInterface, QStringLiteral("devices")), QDBus::Block, callTimeout);
        const QStringList all = ids.isValid() ? ids.value() : QStringList();
        for (const QString &id : all) {
            const QString path = daemonPath + QStringLiteral("/devices/") + id;
            QDBusMessage ask = QDBusMessage::createMethodCall(service, path, propertiesInterface, QStringLiteral("GetAll"));
            ask.setArguments({deviceInterface});
            const QDBusReply<QVariantMap> properties = m_bus.call(ask, QDBus::Block, callTimeout);
            if (!properties.isValid())
                continue;
            const QVariantMap p = properties.value();
            m_devices << QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), p.value(QStringLiteral("name"))},
                {QStringLiteral("type"), p.value(QStringLiteral("type"))},
                {QStringLiteral("paired"), p.value(QStringLiteral("isPaired"))},
                {QStringLiteral("reachable"), p.value(QStringLiteral("isReachable"))},
                {QStringLiteral("requested"), p.value(QStringLiteral("isPairRequested"))},
                {QStringLiteral("requestedByPhone"), p.value(QStringLiteral("isPairRequestedByPeer"))},
            };
            if (!m_watched.contains(id)) {
                m_watched << id;
                m_bus.connect(service, path, deviceInterface, QStringLiteral("pairStateChanged"), this, SLOT(listChanged()));
                m_bus.connect(service, path, deviceInterface, QStringLiteral("reachableChanged"), this, SLOT(listChanged()));
            }
        }
    }
    Q_EMIT changed();
}

void PhoneLink::deviceCall(const QString &id, const QString &method)
{
    m_bus.call(QDBusMessage::createMethodCall(service, daemonPath + QStringLiteral("/devices/") + id, deviceInterface, method),
               QDBus::Block, callTimeout);
    refresh();
}

void PhoneLink::pair(const QString &id)
{
    deviceCall(id, QStringLiteral("requestPairing"));
}

void PhoneLink::acceptPairing(const QString &id)
{
    deviceCall(id, QStringLiteral("acceptPairing"));
}

void PhoneLink::cancelPairing(const QString &id)
{
    deviceCall(id, QStringLiteral("cancelPairing"));
}

void PhoneLink::unpair(const QString &id)
{
    deviceCall(id, QStringLiteral("unpair"));
}

void PhoneLink::sendFile(const QString &id, const QUrl &file)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        service, daemonPath + QStringLiteral("/devices/") + id + QStringLiteral("/share"), shareInterface, QStringLiteral("shareUrl"));
    message.setArguments({file.toString()});
    m_bus.call(message, QDBus::Block, callTimeout);
}
