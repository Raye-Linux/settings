// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsdebug.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

void write(const QString &path, const QByteArray &text)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestWindowsDebug : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void offByDefault()
    {
        QTemporaryDir dir;
        WindowsDebug debug(dir.filePath(QStringLiteral("etc.conf")), dir.filePath(QStringLiteral("raye/windows.conf")));
        QVERIFY(!debug.on());
    }

    void lastLineWins()
    {
        QCOMPARE(WindowsDebug::valueIn(QStringLiteral("debug=on\nengine=raye\n debug = off \n")), QStringLiteral("off"));
        QCOMPARE(WindowsDebug::valueIn(QStringLiteral("debug=maybe\n")), QString());
        QCOMPARE(WindowsDebug::valueIn(QStringLiteral("# debug=on\n")), QString());
    }

    void personBeatsSystem()
    {
        QTemporaryDir dir;
        const QString etc = dir.filePath(QStringLiteral("etc.conf"));
        const QString mine = dir.filePath(QStringLiteral("raye/windows.conf"));
        write(etc, "debug=on\n");
        WindowsDebug debug(etc, mine);
        QVERIFY(debug.on());

        QSignalSpy changed(&debug, &WindowsDebug::changed);
        debug.setOn(false);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!debug.on());
        QCOMPARE(read(mine), QByteArray("debug=off\n"));
        QCOMPARE(read(etc), QByteArray("debug=on\n"));
    }

    void keepsTheOtherLines()
    {
        QTemporaryDir dir;
        const QString mine = dir.filePath(QStringLiteral("windows.conf"));
        write(mine, "engine=system\ndebug=off\n# mine\n\n");
        WindowsDebug debug(dir.filePath(QStringLiteral("none")), mine);
        debug.setOn(true);
        QVERIFY(debug.on());
        QCOMPARE(read(mine), QByteArray("engine=system\n# mine\ndebug=on\n"));
        debug.setOn(true);
        QCOMPARE(read(mine), QByteArray("engine=system\n# mine\ndebug=on\n"));
    }

    void switchIsOnThePage()
    {
        QFile qml(QStringLiteral(SOURCE_DIR "/src/qml/pages/system/AdvancedDeveloperSection.qml"));
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QByteArray text = qml.readAll();
        QVERIFY(text.contains("WindowsDebug {"));
        QVERIFY(text.contains("searchId: \"windows-debug\""));
    }
};

QTEST_GUILESS_MAIN(TestWindowsDebug)
#include "test_windowsdebug.moc"
