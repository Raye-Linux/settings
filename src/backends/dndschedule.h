// SPDX-License-Identifier: GPL-3.0-or-later
// System > Notifications > Turn on do not disturb automatically >
// During these times. The hours are kept in raye-settingsrc
// [DoNotDisturbSchedule] (Enabled, Start, End, Days); the user timer
// raye-dnd-schedule.timer starts data/libexec/dnd-schedule at the start
// time, which writes the end into plasmanotifyrc [DoNotDisturb] Until.
// The start time reaches the timer through a drop-in in
// ~/.config/systemd/user/raye-dnd-schedule.timer.d.
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

class DndSchedule : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // "HH:MM", 24 hours
    Q_PROPERTY(QString start READ start WRITE setStart NOTIFY changed)
    Q_PROPERTY(QString end READ end WRITE setEnd NOTIFY changed)
    // "everyday", "weekdays" or "weekends"
    Q_PROPERTY(QString days READ days WRITE setDays NOTIFY changed)

public:
    explicit DndSchedule(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString start() const { return m_start; }
    void setStart(const QString &time);
    QString end() const { return m_end; }
    void setEnd(const QString &time);
    QString days() const { return m_days; }
    void setDays(const QString &days);

    // the time as the user reads it ("10:00 PM" or "22:00")
    Q_INVOKABLE QString displayTime(const QString &time) const;
    // every 30 minutes of a day, "00:00" to "23:30"
    Q_INVOKABLE static QStringList times();

    static bool isTime(const QString &time);
    // the timer's OnCalendar for a start time on those days
    static QString calendar(const QString &start, const QString &days);

    // For the tests: their own files and programs.
    void setConfigFile(const QString &path);
    void setUnitDir(const QString &path) { m_unitDir = path; }
    void setSystemctl(const QString &path) { m_systemctl = path; }

Q_SIGNALS:
    void changed();

private:
    void read();
    void write();
    void writeDropIn();
    // the timer follows: written again, reloaded, on or off, and run once
    // so a window that has already begun starts now
    void apply();
    void systemctl(const QStringList &args, std::function<void()> done = {});

    bool m_enabled = false;
    QString m_start = QStringLiteral("22:00");
    QString m_end = QStringLiteral("07:00");
    QString m_days = QStringLiteral("everyday");
    QString m_configFile;
    QString m_unitDir;
    QString m_systemctl = QStringLiteral("systemctl");
};
