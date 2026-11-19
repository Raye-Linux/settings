// SPDX-License-Identifier: GPL-3.0-or-later
// Do not disturb during these times: what reaches raye-settingsrc, the
// timer's drop-in and systemctl (a stand-in that writes a log).
#include "dndschedule.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{
QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}
}

class TestDndSchedule : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void calendar()
    {
        QCOMPARE(DndSchedule::calendar(QStringLiteral("22:00"), QStringLiteral("everyday")), QStringLiteral("*-*-* 22:00:00"));
        QCOMPARE(DndSchedule::calendar(QStringLiteral("09:30"), QStringLiteral("weekdays")), QStringLiteral("Mon..Fri *-*-* 09:30:00"));
        QCOMPARE(DndSchedule::calendar(QStringLiteral("07:00"), QStringLiteral("weekends")), QStringLiteral("Sat,Sun *-*-* 07:00:00"));
        QVERIFY(DndSchedule::calendar(QStringLiteral("24:00"), QStringLiteral("everyday")).isEmpty());
        QVERIFY(DndSchedule::calendar(QStringLiteral("07:00"), QStringLiteral("sometimes")).isEmpty());
        QVERIFY(!DndSchedule::isTime(QStringLiteral("7:00")));
        QVERIFY(!DndSchedule::isTime(QStringLiteral("07:00; touch /tmp/x")));
        QCOMPARE(DndSchedule::times().size(), 48);
        QCOMPARE(DndSchedule::times().first(), QStringLiteral("00:00"));
        QCOMPARE(DndSchedule::times().last(), QStringLiteral("23:30"));
    }

    void schedule()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("systemctl.log"));
        const QString fake = dir.filePath(QStringLiteral("systemctl"));
        QFile script(fake);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\necho \"$*\" >> " + QFile::encodeName(log) + "\n");
        script.close();
        script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QString config = dir.filePath(QStringLiteral("raye-settingsrc"));
        const QString dropIn = dir.filePath(QStringLiteral("units/raye-dnd-schedule.timer.d/schedule.conf"));

        DndSchedule s;
        s.setSystemctl(fake);
        s.setUnitDir(dir.filePath(QStringLiteral("units")));
        s.setConfigFile(config);
        QVERIFY(!s.enabled());
        QCOMPARE(s.start(), QStringLiteral("22:00"));
        QCOMPARE(s.end(), QStringLiteral("07:00"));
        QCOMPARE(s.days(), QStringLiteral("everyday"));

        s.setEnabled(true);
        QVERIFY(readAll(config).contains(QStringLiteral("Enabled=true")));
        QVERIFY(readAll(dropIn).contains(QStringLiteral("OnCalendar=\nOnCalendar=*-*-* 22:00:00")));
        QTRY_VERIFY_WITH_TIMEOUT(readAll(log).contains(QStringLiteral("--user start raye-dnd-schedule.service")), 5000);
        const QString first = readAll(log);
        QVERIFY(first.indexOf(QStringLiteral("daemon-reload")) < first.indexOf(QStringLiteral("enable --now raye-dnd-schedule.timer")));

        s.setStart(QStringLiteral("21:30"));
        s.setDays(QStringLiteral("weekdays"));
        s.setEnd(QStringLiteral("21:30")); // the same as the start: refused
        s.setStart(QStringLiteral("9:00")); // refused
        s.setDays(QStringLiteral("holidays")); // refused
        QCOMPARE(s.start(), QStringLiteral("21:30"));
        QCOMPARE(s.end(), QStringLiteral("07:00"));
        QCOMPARE(s.days(), QStringLiteral("weekdays"));
        QVERIFY(readAll(dropIn).contains(QStringLiteral("OnCalendar=Mon..Fri *-*-* 21:30:00")));

        s.setEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(readAll(log).contains(QStringLiteral("--user disable --now raye-dnd-schedule.timer")), 5000);

        DndSchedule again;
        again.setConfigFile(config);
        QVERIFY(!again.enabled());
        QCOMPARE(again.start(), QStringLiteral("21:30"));
        QCOMPARE(again.days(), QStringLiteral("weekdays"));
    }
};

QTEST_MAIN(TestDndSchedule)
#include "test_dndschedule.moc"
