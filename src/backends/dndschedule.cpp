// SPDX-License-Identifier: GPL-3.0-or-later
#include "dndschedule.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>

namespace
{
const QString group = QStringLiteral("DoNotDisturbSchedule");
const QString timer = QStringLiteral("raye-dnd-schedule.timer");
const QString service = QStringLiteral("raye-dnd-schedule.service");
}

DndSchedule::DndSchedule(QObject *parent)
    : QObject(parent)
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    m_configFile = config + QStringLiteral("/raye-settingsrc");
    m_unitDir = config + QStringLiteral("/systemd/user");
    read();
}

void DndSchedule::setConfigFile(const QString &path)
{
    m_configFile = path;
    read();
}

void DndSchedule::read()
{
    const KConfigGroup g = KSharedConfig::openConfig(m_configFile, KConfig::SimpleConfig)->group(group);
    m_enabled = g.readEntry("Enabled", false);
    const QString start = g.readEntry("Start", QStringLiteral("22:00"));
    const QString end = g.readEntry("End", QStringLiteral("07:00"));
    m_start = isTime(start) ? start : QStringLiteral("22:00");
    m_end = isTime(end) && end != m_start ? end : QStringLiteral("07:00");
    const QString days = g.readEntry("Days", QStringLiteral("everyday"));
    m_days = calendar(m_start, days).isEmpty() ? QStringLiteral("everyday") : days;
    Q_EMIT changed();
}

void DndSchedule::write()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(m_configFile, KConfig::SimpleConfig);
    config->reparseConfiguration();
    KConfigGroup g = config->group(group);
    g.writeEntry("Enabled", m_enabled);
    g.writeEntry("Start", m_start);
    g.writeEntry("End", m_end);
    g.writeEntry("Days", m_days);
    config->sync();
    Q_EMIT changed();
}

void DndSchedule::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    write();
    apply();
}

void DndSchedule::setStart(const QString &time)
{
    if (!isTime(time) || time == m_start || time == m_end)
        return;
    m_start = time;
    write();
    apply();
}

void DndSchedule::setEnd(const QString &time)
{
    if (!isTime(time) || time == m_end || time == m_start)
        return;
    m_end = time;
    write();
    apply();
}

void DndSchedule::setDays(const QString &days)
{
    if (days == m_days || calendar(m_start, days).isEmpty())
        return;
    m_days = days;
    write();
    apply();
}

bool DndSchedule::isTime(const QString &time)
{
    static const QRegularExpression re(QStringLiteral("^([01][0-9]|2[0-3]):[0-5][0-9]$"));
    return re.match(time).hasMatch();
}

QString DndSchedule::calendar(const QString &start, const QString &days)
{
    if (!isTime(start))
        return {};
    const QString at = QStringLiteral("*-*-* ") + start + QStringLiteral(":00");
    if (days == QLatin1String("everyday"))
        return at;
    if (days == QLatin1String("weekdays"))
        return QStringLiteral("Mon..Fri ") + at;
    if (days == QLatin1String("weekends"))
        return QStringLiteral("Sat,Sun ") + at;
    return {};
}

QString DndSchedule::displayTime(const QString &time) const
{
    const QTime t = QTime::fromString(time, QStringLiteral("HH:mm"));
    return t.isValid() ? QLocale().toString(t, QLocale::ShortFormat) : time;
}

QStringList DndSchedule::times()
{
    QStringList list;
    for (int m = 0; m < 24 * 60; m += 30)
        list << QTime(m / 60, m % 60).toString(QStringLiteral("HH:mm"));
    return list;
}

void DndSchedule::writeDropIn()
{
    const QString dir = m_unitDir + QStringLiteral("/") + timer + QStringLiteral(".d");
    QDir().mkpath(dir);
    QSaveFile file(dir + QStringLiteral("/schedule.conf"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write("# Settings > System > Notifications > During these times\n[Timer]\nOnCalendar=\nOnCalendar=");
    file.write(calendar(m_start, m_days).toLatin1() + "\n");
    file.commit();
}

void DndSchedule::apply()
{
    writeDropIn();
    systemctl({QStringLiteral("--user"), QStringLiteral("daemon-reload")}, [this] {
        if (!m_enabled) {
            systemctl({QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("--now"), timer});
            return;
        }
        systemctl({QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("--now"), timer}, [this] {
            systemctl({QStringLiteral("--user"), QStringLiteral("restart"), timer}, [this] {
                systemctl({QStringLiteral("--user"), QStringLiteral("start"), service});
            });
        });
    });
}

void DndSchedule::systemctl(const QStringList &args, std::function<void()> done)
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int, QProcess::ExitStatus) {
        process->deleteLater();
        if (done)
            done();
    });
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->deleteLater();
    });
    process->start(m_systemctl, args);
}
