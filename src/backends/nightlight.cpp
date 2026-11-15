// SPDX-License-Identifier: GPL-3.0-or-later
#include "nightlight.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int dayTemperature = 6500;
constexpr int warmestTemperature = 1800;

KConfigGroup nightLightGroup()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();
    return config->group(QStringLiteral("NightColor"));
}

KConfigGroup scheduleGroup()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("knighttimerc"));
    config->reparseConfiguration();
    return config->group(QStringLiteral("General"));
}

QDBusMessage nightLightCall(const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/NightLight"),
                                          QStringLiteral("org.kde.KWin.NightLight"), method);
}
} // namespace

NightLightSettings::NightLightSettings(QObject *parent)
    : QObject(parent)
{
}

int NightLightSettings::temperatureOf(int strength)
{
    const int s = std::clamp(strength, 0, 100);
    return int(std::lround(dayTemperature - s * (dayTemperature - warmestTemperature) / 100.0));
}

int NightLightSettings::strengthOf(int temperature)
{
    const int t = std::clamp(temperature, warmestTemperature, dayTemperature);
    return int(std::lround((dayTemperature - t) * 100.0 / (dayTemperature - warmestTemperature)));
}

QTime NightLightSettings::parseTime(const QString &text)
{
    return QTime::fromString(text.trimmed(), QStringLiteral("H:mm"));
}

void NightLightSettings::writeNightLight(const QString &key, const QVariant &value)
{
    KConfigGroup group = nightLightGroup();
    // KWin watches its file: Notify makes it apply the change at once
    group.writeEntry(key, value, KConfig::Notify);
    group.sync();
    Q_EMIT changed();
}

void NightLightSettings::writeSchedule(const QString &key, const QVariant &value)
{
    KConfigGroup group = scheduleGroup();
    group.writeEntry(key, value, KConfig::Notify);
    group.sync();
    Q_EMIT changed();
}

bool NightLightSettings::enabled() const
{
    return nightLightGroup().readEntry(QStringLiteral("Active"), false);
}

void NightLightSettings::setEnabled(bool on)
{
    if (on != enabled())
        writeNightLight(QStringLiteral("Active"), on);
}

bool NightLightSettings::scheduled() const
{
    // DarkLight is KWin's default mode: on at night, off in the day
    return nightLightGroup().readEntry(QStringLiteral("Mode"), QStringLiteral("DarkLight")) == QLatin1String("DarkLight");
}

void NightLightSettings::setScheduled(bool on)
{
    if (on != scheduled())
        writeNightLight(QStringLiteral("Mode"), on ? QStringLiteral("DarkLight") : QStringLiteral("Constant"));
}

QString NightLightSettings::schedule() const
{
    return scheduleGroup().readEntry(QStringLiteral("Source"), QStringLiteral("Automatic")) == QLatin1String("Times")
        ? QStringLiteral("times")
        : QStringLiteral("sunset");
}

void NightLightSettings::setSchedule(const QString &schedule)
{
    if (schedule != QLatin1String("times") && schedule != QLatin1String("sunset"))
        return;
    if (schedule == this->schedule())
        return;
    if (schedule == QLatin1String("times")) {
        // set hours start from what the user sees in the boxes
        KConfigGroup group = scheduleGroup();
        if (!group.hasKey(QStringLiteral("SunsetStart")))
            group.writeEntry(QStringLiteral("SunsetStart"), QTime(21, 0), KConfig::Notify);
        if (!group.hasKey(QStringLiteral("SunriseStart")))
            group.writeEntry(QStringLiteral("SunriseStart"), QTime(7, 0), KConfig::Notify);
    }
    writeSchedule(QStringLiteral("Source"), schedule == QLatin1String("times") ? QStringLiteral("Times") : QStringLiteral("Automatic"));
}

QString NightLightSettings::turnOn() const
{
    return scheduleGroup().readEntry(QStringLiteral("SunsetStart"), QTime(21, 0)).toString(QStringLiteral("HH:mm"));
}

void NightLightSettings::setTurnOn(const QString &time)
{
    const QTime t = parseTime(time);
    if (t.isValid() && t.toString(QStringLiteral("HH:mm")) != turnOn())
        writeSchedule(QStringLiteral("SunsetStart"), t);
}

QString NightLightSettings::turnOff() const
{
    return scheduleGroup().readEntry(QStringLiteral("SunriseStart"), QTime(7, 0)).toString(QStringLiteral("HH:mm"));
}

void NightLightSettings::setTurnOff(const QString &time)
{
    const QTime t = parseTime(time);
    if (t.isValid() && t.toString(QStringLiteral("HH:mm")) != turnOff())
        writeSchedule(QStringLiteral("SunriseStart"), t);
}

int NightLightSettings::strength() const
{
    return strengthOf(nightLightGroup().readEntry(QStringLiteral("NightTemperature"), defaultTemperature));
}

void NightLightSettings::setStrength(int strength)
{
    if (strength != this->strength())
        writeNightLight(QStringLiteral("NightTemperature"), temperatureOf(strength));
}

void NightLightSettings::preview(int strength)
{
    QDBusMessage call = nightLightCall(QStringLiteral("preview"));
    call << uint(temperatureOf(strength));
    QDBusConnection::sessionBus().asyncCall(call);
}

void NightLightSettings::stopPreview()
{
    QDBusConnection::sessionBus().asyncCall(nightLightCall(QStringLiteral("stopPreview")));
}
