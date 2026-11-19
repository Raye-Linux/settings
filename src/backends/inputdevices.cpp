// SPDX-License-Identifier: GPL-3.0-or-later
#include "inputdevices.h"

#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>

namespace {
const QString service = QStringLiteral("org.kde.KWin");
const QString managerPath = QStringLiteral("/org/kde/KWin/InputDevice");
const QString managerInterface = QStringLiteral("org.kde.KWin.InputDeviceManager");
const QString deviceInterface = QStringLiteral("org.kde.KWin.InputDevice");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
constexpr int callTimeout = 2000;
} // namespace

InputDeviceSettings::InputDeviceSettings(QObject *parent)
    : InputDeviceSettings(QDBusConnection::sessionBus(), parent)
{
}

InputDeviceSettings::InputDeviceSettings(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    m_bus.connect(service, managerPath, managerInterface, QStringLiteral("deviceAdded"), this,
                  SLOT(devicesChanged()));
    m_bus.connect(service, managerPath, managerInterface, QStringLiteral("deviceRemoved"), this,
                  SLOT(devicesChanged()));
}

void InputDeviceSettings::setKind(const QString &kind)
{
    if (kind == m_kind)
        return;
    m_kind = kind;
    reload();
}

QString InputDeviceSettings::kindOf(const QVariantMap &properties)
{
    const auto is = [&properties](const char *name) {
        return properties.value(QString::fromLatin1(name)).toBool();
    };
    if (is("tabletTool") || is("tabletPad"))
        return QStringLiteral("pen");
    if (is("touchpad"))
        return QStringLiteral("touchpad");
    if (is("pointer"))
        return QStringLiteral("mouse");
    if (is("touch"))
        return QStringLiteral("touchscreen");
    return {};
}

void InputDeviceSettings::devicesChanged()
{
    reload();
}

void InputDeviceSettings::reload()
{
    m_devices.clear();
    QDBusMessage ask = QDBusMessage::createMethodCall(service, managerPath, propertiesInterface, QStringLiteral("Get"));
    ask.setArguments({managerInterface, QStringLiteral("devicesSysNames")});
    const QDBusReply<QDBusVariant> names = m_bus.call(ask, QDBus::Block, callTimeout);
    m_available = names.isValid();
    if (m_available) {
        const QStringList sysNames = names.value().variant().toStringList();
        for (const QString &sysName : sysNames) {
            const QString path = managerPath + QLatin1Char('/') + sysName;
            QDBusMessage all = QDBusMessage::createMethodCall(service, path, propertiesInterface, QStringLiteral("GetAll"));
            all.setArguments({deviceInterface});
            const QDBusReply<QVariantMap> properties = m_bus.call(all, QDBus::Block, callTimeout);
            if (properties.isValid() && kindOf(properties.value()) == m_kind)
                m_devices.append({path, properties.value()});
        }
    }
    Q_EMIT changed();
}

QStringList InputDeviceSettings::deviceNames() const
{
    QStringList names;
    for (const Device &device : m_devices) {
        const QString name = device.properties.value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    return names;
}

bool InputDeviceSettings::supports(const QString &capability) const
{
    for (const Device &device : m_devices) {
        const auto found = device.properties.constFind(capability);
        if (found == device.properties.constEnd())
            continue;
        if (!capability.startsWith(QLatin1String("supports")) || found->toBool())
            return true;
    }
    return false;
}

QVariant InputDeviceSettings::value(const QString &property, const QVariant &fallback) const
{
    // the device a value is read from must be able to change it: a
    // tablet that ignores pointer speed does not speak for the mouse
    QString capability = QStringLiteral("supports") + property;
    capability[8] = capability[8].toUpper();
    for (const Device &device : m_devices) {
        if (device.properties.contains(capability) && !device.properties.value(capability).toBool())
            continue;
        const auto found = device.properties.constFind(property);
        if (found != device.properties.constEnd())
            return *found;
    }
    return fallback;
}

bool InputDeviceSettings::setValue(const QString &property, const QVariant &value)
{
    QString capability = QStringLiteral("supports") + property;
    capability[8] = capability[8].toUpper();
    bool wrote = false;
    for (Device &device : m_devices) {
        if (device.properties.contains(capability) && !device.properties.value(capability).toBool())
            continue;
        if (!device.properties.contains(property))
            continue;
        QVariant typed = value;
        typed.convert(device.properties.value(property).metaType());
        QDBusMessage set = QDBusMessage::createMethodCall(service, device.path, propertiesInterface, QStringLiteral("Set"));
        set.setArguments({deviceInterface, property, QVariant::fromValue(QDBusVariant(typed))});
        const QDBusMessage reply = m_bus.call(set, QDBus::Block, callTimeout);
        if (reply.type() == QDBusMessage::ReplyMessage) {
            device.properties.insert(property, typed);
            wrote = true;
        }
    }
    if (wrote)
        Q_EMIT changed();
    return wrote;
}
