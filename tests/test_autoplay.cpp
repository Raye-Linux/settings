// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > AutoPlay: the choices land in rayeshellrc the way the
// taskbar's devices applet reads them.
#include "autoplay.h"

#include <KConfig>
#include <KConfigGroup>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestAutoPlay : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaults()
    {
        QTemporaryDir dir;
        AutoPlaySettings s(dir.filePath(QStringLiteral("rayeshellrc")));
        QVERIFY(s.enabled());
        QCOMPARE(s.removableDrive(), QStringLiteral("ask"));
        QCOMPARE(s.memoryCard(), QStringLiteral("ask"));
    }

    void choicesAreWritten()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("rayeshellrc"));
        AutoPlaySettings s(file);
        QSignalSpy changed(&s, &AutoPlaySettings::changed);
        s.setRemovableDrive(QStringLiteral("open"));
        // a card follows the drive until it gets a choice of its own
        QCOMPARE(s.memoryCard(), QStringLiteral("open"));
        s.setMemoryCard(QStringLiteral("nothing"));
        s.setEnabled(false);
        s.setRemovableDrive(QStringLiteral("open"));
        QCOMPARE(changed.count(), 3);

        KConfig shell(file, KConfig::SimpleConfig);
        const KConfigGroup group = shell.group(QStringLiteral("AutoPlay"));
        QCOMPARE(group.readEntry("RemovableDrive", QString()), QStringLiteral("open"));
        QCOMPARE(group.readEntry("MemoryCard", QString()), QStringLiteral("nothing"));
        QCOMPARE(group.readEntry("Enabled", true), false);

        s.setEnabled(true);
        QVERIFY(!KConfig(file, KConfig::SimpleConfig).group(QStringLiteral("AutoPlay")).hasKey("Enabled"));
    }

    void oddValuesReadAsAsk()
    {
        QCOMPARE(AutoPlaySettings::normalized(QStringLiteral("format-c")), QStringLiteral("ask"));
        QCOMPARE(AutoPlaySettings::normalized(QString()), QStringLiteral("ask"));
        QCOMPARE(AutoPlaySettings::normalized(QStringLiteral("nothing")), QStringLiteral("nothing"));
    }
};

QTEST_GUILESS_MAIN(TestAutoPlay)
#include "test_autoplay.moc"
