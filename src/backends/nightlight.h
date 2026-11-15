// SPDX-License-Identifier: GPL-3.0-or-later
// Night light: warmer colours in the evening. KWin does it; its
// settings are kwinrc [NightColor] (Active, Mode, NightTemperature). In
// Plasma 6.6 "when" comes from the day and night schedule shared with
// the automatic light/dark theme (knighttimerc [General]: Source,
// SunsetStart, SunriseStart), so the schedule here is that one.
#pragma once

#include <QObject>
#include <QTime>
#include <QtQml/qqmlregistration.h>

class NightLightSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // night light can turn on (by hand or on its schedule)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // it follows the schedule rather than staying on
    Q_PROPERTY(bool scheduled READ scheduled WRITE setScheduled NOTIFY changed)
    // "sunset" (sunset to sunrise) or "times" (set hours)
    Q_PROPERTY(QString schedule READ schedule WRITE setSchedule NOTIFY changed)
    // "21:00", "07:00"
    Q_PROPERTY(QString turnOn READ turnOn WRITE setTurnOn NOTIFY changed)
    Q_PROPERTY(QString turnOff READ turnOff WRITE setTurnOff NOTIFY changed)
    // 0 (no change) to 100 (warmest)
    Q_PROPERTY(int strength READ strength WRITE setStrength NOTIFY changed)

public:
    explicit NightLightSettings(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool on);
    bool scheduled() const;
    void setScheduled(bool on);
    QString schedule() const;
    void setSchedule(const QString &schedule);
    QString turnOn() const;
    void setTurnOn(const QString &time);
    QString turnOff() const;
    void setTurnOff(const QString &time);
    int strength() const;
    void setStrength(int strength);

    // Shows a strength while the slider moves, without saving it.
    Q_INVOKABLE void preview(int strength);
    Q_INVOKABLE void stopPreview();

    // strength <-> colour temperature in kelvin (6500 K is daylight)
    static int temperatureOf(int strength);
    static int strengthOf(int temperature);
    // "21:00" -> 21:00; anything else -> invalid
    static QTime parseTime(const QString &text);

    static constexpr int defaultTemperature = 4500;

Q_SIGNALS:
    void changed();

private:
    void writeNightLight(const QString &key, const QVariant &value);
    void writeSchedule(const QString &key, const QVariant &value);
};
