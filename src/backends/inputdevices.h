// SPDX-License-Identifier: GPL-3.0-or-later
// Mouse, touchpad and pen settings. KWin owns the input devices and shows
// each one on D-Bus at /org/kde/KWin/InputDevice/<sysname>. Setting one of
// its properties applies at once, and KWin itself writes kcminputrc, so a
// setting survives a restart without anything here touching that file.
//
// A page says which kind it wants. A value is read from the first device
// of that kind that supports it, and written to all of them, the way
// Windows has one pointer speed however many mice are plugged in.
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class InputDeviceSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "mouse", "touchpad" or "pen"
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY changed)
    // KWin answered: without it no page can change anything
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QStringList deviceNames READ deviceNames NOTIFY changed)

public:
    explicit InputDeviceSettings(QObject *parent = nullptr);
    explicit InputDeviceSettings(const QDBusConnection &bus, QObject *parent = nullptr);

    QString kind() const { return m_kind; }
    void setKind(const QString &kind);
    bool available() const { return m_available; }
    QStringList deviceNames() const;

    // Which kind of device these properties describe, or nothing for a
    // device no page has a use for (a keyboard, the power button).
    static QString kindOf(const QVariantMap &properties);

    // The value of a KWin property on the first device that has it.
    Q_INVOKABLE QVariant value(const QString &property, const QVariant &fallback = {}) const;
    // Does any device of this kind support it? `supportsTapToClick` and
    // the like are KWin's own names. A name KWin does not have is true
    // when the device simply has the property.
    Q_INVOKABLE bool supports(const QString &capability) const;
    // Writes to every device of the kind. False when none could take it.
    Q_INVOKABLE bool setValue(const QString &property, const QVariant &value);
    Q_INVOKABLE void reload();

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void devicesChanged();

private:
    struct Device {
        QString path;
        QVariantMap properties;
    };
    QList<Device> m_devices;
    QDBusConnection m_bus;
    QString m_kind;
    bool m_available = false;
};
