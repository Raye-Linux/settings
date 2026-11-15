// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Phone: the phones this PC can see through its phone link
// service, pairing with them, and sending them a file.
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class PhoneLink : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // the phone service is on this PC (it ships with Raye) and is being
    // started because it wasn't running
    Q_PROPERTY(bool installed READ installed CONSTANT)
    Q_PROPERTY(bool starting READ starting NOTIFY changed)
    // the name phones see for this PC
    Q_PROPERTY(QString announcedName READ announcedName NOTIFY changed)
    // { id, name, type, paired, reachable, requested, requestedByPhone }
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)

public:
    explicit PhoneLink(QObject *parent = nullptr);
    explicit PhoneLink(const QDBusConnection &bus, QObject *parent = nullptr);
    // daemon: the program that starts the service, empty when not installed
    PhoneLink(const QDBusConnection &bus, const QString &daemon, QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool installed() const { return !m_daemon.isEmpty(); }
    bool starting() const { return m_starting; }
    QString announcedName() const { return m_announcedName; }
    QVariantList devices() const { return m_devices; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void pair(const QString &id);
    Q_INVOKABLE void acceptPairing(const QString &id);
    Q_INVOKABLE void cancelPairing(const QString &id);
    Q_INVOKABLE void unpair(const QString &id);
    Q_INVOKABLE void sendFile(const QString &id, const QUrl &file);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void listChanged();

private:
    void deviceCall(const QString &id, const QString &method);

    QDBusConnection m_bus;
    bool m_available = false;
    QString m_daemon;
    bool m_startAsked = false;
    bool m_starting = false;
    QString m_announcedName;
    QVariantList m_devices;
    QStringList m_watched;
};
