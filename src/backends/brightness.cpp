// SPDX-License-Identifier: GPL-3.0-or-later
#include "brightness.h"

#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QDBusVariant>
#include <QHash>
#include <KLocalizedString>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace
{
const QString service = QStringLiteral("org.kde.ScreenBrightness");
const QString rootPath = QStringLiteral("/org/kde/ScreenBrightness");
const QString rootInterface = QStringLiteral("org.kde.ScreenBrightness");
const QString displayInterface = QStringLiteral("org.kde.ScreenBrightness.Display");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
// SuppressIndicator: the slider is the indicator, no on-screen display
constexpr uint suppressIndicator = 0x1;
constexpr int callTimeout = 2000;

QVariantMap allProperties(const QDBusConnection &bus, const QString &path, const QString &interface)
{
    QDBusInterface props(service, path, properties, bus);
    props.setTimeout(callTimeout);
    const QDBusReply<QVariantMap> reply = props.call(QStringLiteral("GetAll"), interface);
    return reply.isValid() ? reply.value() : QVariantMap();
}
} // namespace

ScreenBrightness::ScreenBrightness(QObject *parent)
    : ScreenBrightness(QDBusConnection::sessionBus(), parent)
{
}

ScreenBrightness::ScreenBrightness(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    m_bus.connect(service, rootPath, rootInterface, QStringLiteral("BrightnessChanged"), this,
                  SLOT(brightnessChanged(QString, int)));
    m_bus.connect(service, rootPath, rootInterface, QStringLiteral("BrightnessRangeChanged"), this,
                  SLOT(rangeChanged(QString, int, int)));
    m_bus.connect(service, rootPath, rootInterface, QStringLiteral("DisplayAdded"), this, SLOT(displaysChanged()));
    m_bus.connect(service, rootPath, rootInterface, QStringLiteral("DisplayRemoved"), this, SLOT(displaysChanged()));
    reload();
}

bool ScreenBrightness::isDisplayName(const QString &name)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_]{1,128}$"));
    return valid.match(name).hasMatch();
}

int ScreenBrightness::percentOf(int brightness, int maximum)
{
    if (maximum <= 0)
        return 0;
    return std::clamp(int(std::lround(100.0 * brightness / maximum)), 0, 100);
}

int ScreenBrightness::brightnessOf(int percent, int maximum, bool builtIn)
{
    if (maximum <= 0)
        return 0;
    const int value = int(std::lround(double(std::clamp(percent, 0, 100)) * maximum / 100.0));
    return builtIn ? std::max(1, value) : value;
}

void ScreenBrightness::reload()
{
    QList<Display> found;
    const QVariantMap root = allProperties(m_bus, rootPath, rootInterface);
    const QStringList names = root.value(QStringLiteral("DisplaysDBusNames")).toStringList();
    for (const QString &name : names) {
        if (!isDisplayName(name))
            continue;
        const QVariantMap p = allProperties(m_bus, rootPath + QLatin1Char('/') + name, displayInterface);
        if (p.isEmpty())
            continue;
        Display d;
        d.name = name;
        d.label = p.value(QStringLiteral("Label")).toString();
        d.builtIn = p.value(QStringLiteral("IsInternal")).toBool();
        d.brightness = p.value(QStringLiteral("Brightness")).toInt();
        d.maximum = p.value(QStringLiteral("MaxBrightness")).toInt();
        if (d.maximum > 0)
            found.append(d);
    }
    std::stable_sort(found.begin(), found.end(), [](const Display &a, const Display &b) { return a.builtIn && !b.builtIn; });
    m_displays = found;
    Q_EMIT changed();
}

QStringList ScreenBrightness::titles(const QStringList &labels, const QVariantList &screens)
{
    QStringList out;
    QHash<QString, int> seen;
    for (int i = 0; i < labels.size(); ++i) {
        const QString &label = labels.at(i);
        if (labels.count(label) < 2) {
            out.append(label);
            continue;
        }
        // the k-th display with this label goes with the k-th screen of that name
        const int k = seen[label]++;
        int number = i + 1;
        int match = 0;
        for (const QVariant &v : screens) {
            const QVariantMap s = v.toMap();
            if (s.value(QStringLiteral("name")).toString() != label)
                continue;
            if (match++ == k) {
                number = s.value(QStringLiteral("number")).toInt();
                break;
            }
        }
        out.append(i18nc("@label %1 is a screen model, %2 its number", "%1 (Display %2)", label, number));
    }
    return out;
}

QVariantList ScreenBrightness::displays() const
{
    QVariantList list;
    for (const Display &d : m_displays) {
        list.append(QVariantMap{{QStringLiteral("name"), d.name},
                                {QStringLiteral("label"), d.label},
                                {QStringLiteral("builtIn"), d.builtIn},
                                {QStringLiteral("percent"), percentOf(d.brightness, d.maximum)}});
    }
    return list;
}

void ScreenBrightness::setBrightness(const QString &name, int percent)
{
    auto it = std::find_if(m_displays.begin(), m_displays.end(), [&name](const Display &d) { return d.name == name; });
    if (it == m_displays.end())
        return;
    const int value = brightnessOf(percent, it->maximum, it->builtIn);
    QDBusMessage call = QDBusMessage::createMethodCall(service, rootPath + QLatin1Char('/') + name, displayInterface,
                                                       QStringLiteral("SetBrightness"));
    call << value << suppressIndicator;
    m_bus.asyncCall(call, callTimeout);
    it->brightness = value;
    Q_EMIT changed();
}

void ScreenBrightness::brightnessChanged(const QString &name, int brightness)
{
    for (Display &d : m_displays) {
        if (d.name == name && d.brightness != brightness) {
            d.brightness = brightness;
            Q_EMIT changed();
        }
    }
}

void ScreenBrightness::rangeChanged(const QString &name, int maximum, int brightness)
{
    for (Display &d : m_displays) {
        if (d.name == name) {
            d.maximum = maximum;
            d.brightness = brightness;
            Q_EMIT changed();
        }
    }
}

void ScreenBrightness::displaysChanged()
{
    reload();
}
