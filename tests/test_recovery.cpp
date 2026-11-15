// SPDX-License-Identifier: GPL-3.0-or-later
// System › Recovery: the backend (what the helpers print, turned into
// what the page shows) and the root helpers themselves, run with fake
// snapper, apt-get, grub-editenv, btrfs... first on PATH. Nothing here
// touches the computer: every program that could is a fake that writes
// down how it was called.
#include "recovery.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace {

const QString kLibexec = QStringLiteral(SOURCE_DIR "/data/libexec");

void writeFile(const QString &path, const QByteArray &content, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    file.write(content);
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct Result {
    int code = -1;
    QString out;
    QString err;
};

// A helper as pkexec would start it, but as the test user, with fakes
// first on PATH.
Result runHelper(const QString &name, const QStringList &args, const QString &fakes,
                 const QStringList &env = {})
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), fakes + QStringLiteral(":/usr/sbin:/usr/bin:/sbin:/bin"));
    environment.remove(QStringLiteral("PKEXEC_UID"));
    for (const QString &pair : env) {
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        environment.insert(pair.left(eq), pair.mid(eq + 1));
    }
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("sh"), QStringList{kLibexec + QLatin1Char('/') + name} + args);
    process.waitForFinished(30000);
    return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
            QString::fromUtf8(process.readAllStandardOutput()), QString::fromUtf8(process.readAllStandardError())};
}

const QByteArray kSnapperJson = "{\"root\": [\n"
"  {\"number\": 0, \"type\": \"single\", \"date\": \"\", \"description\": \"current\", \"cleanup\": \"\"},\n"
"  {\"number\": 4, \"type\": \"single\", \"date\": \"2026-10-20 09:30:00\", \"description\": \"Before the big change\", \"cleanup\": \"\"},\n"
"  {\"number\": 5, \"type\": \"pre\", \"date\": \"2026-10-30 14:05:11\", \"description\": \"apt\", \"cleanup\": \"number\"},\n"
"  {\"number\": 6, \"type\": \"post\", \"pre-number\": 5, \"date\": \"2026-10-30 14:06:40\", \"description\": \"apt\", \"cleanup\": \"number\"},\n"
"  {\"number\": 7, \"type\": \"pre\", \"date\": \"2026-10-31 08:00:00\", \"description\": \"apt\", \"cleanup\": \"number\"},\n"
"  {\"number\": 8, \"type\": \"single\", \"date\": \"2026-10-31 09:00:00\", \"description\": \"timeline\", \"cleanup\": \"timeline\"},\n"
"  {\"number\": 9, \"type\": \"single\", \"date\": \"not a date\", \"description\": \"broken\", \"cleanup\": \"\"}\n"
"]}";

} // namespace

class TestRecovery : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_fakes;   // fake programs, first on PATH
    QString m_helpers; // fake helpers for the backend
    QString m_calls;   // where the fakes write how they were called
    QByteArray m_path;

    // A backend wired to the fakes, as the page would have it.
    void wire(Recovery &recovery)
    {
        recovery.setHelperDir(m_helpers);
        recovery.setBootWindowsProgram(m_fakes + QStringLiteral("/raye-boot-windows"));
        recovery.setRoot(m_dir.filePath(QStringLiteral("root")));
        recovery.setAskLogind(false);
    }
    QString calls() const { return QString::fromUtf8(readFile(m_calls)); }
    void clearCalls() { QFile::remove(m_calls); }
    // a fake that writes its name and arguments down, then exits with `code`
    void fake(const QString &dir, const QString &name, int code = 0, const QByteArray &out = {})
    {
        QByteArray script = "#!/bin/sh\necho \"" + name.toUtf8() + " $*\" >> '" + m_calls.toUtf8() + "'\n";
        if (!out.isEmpty())
            script += "cat <<'EOF'\n" + out + "\nEOF\n";
        script += "exit " + QByteArray::number(code) + "\n";
        writeFile(dir + QLatin1Char('/') + name, script, true);
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_fakes = m_dir.filePath(QStringLiteral("bin"));
        m_helpers = m_dir.filePath(QStringLiteral("helpers"));
        m_calls = m_dir.filePath(QStringLiteral("calls"));
        QDir().mkpath(m_fakes);
        QDir().mkpath(m_helpers);
        // pkexec runs what it is given (and says it did)
        writeFile(m_fakes + QStringLiteral("/pkexec"),
                  "#!/bin/sh\necho \"pkexec $*\" >> '" + m_calls.toUtf8() + "'\n"
                  "[ -n \"$FAKE_PKEXEC_EXIT\" ] && exit \"$FAKE_PKEXEC_EXIT\"\nexec \"$@\"\n",
                  true);
        fake(m_fakes, QStringLiteral("systemctl"));
        m_path = qgetenv("PATH");
        // the backend finds pkexec, systemctl and raye-restart-to-windows on PATH
        qputenv("PATH", (m_fakes + QStringLiteral(":/usr/bin:/bin")).toUtf8());
    }

    void cleanupTestCase() { qputenv("PATH", m_path); }

    void init()
    {
        clearCalls();
        qunsetenv("FAKE_PKEXEC_EXIT");
    }

    // --- parsing --------------------------------------------------------

    void repairLines()
    {
        auto line = Recovery::parseRepairLine(QStringLiteral("stage download\n"));
        QCOMPARE(line.key, QStringLiteral("stage"));
        QCOMPARE(line.value, QStringLiteral("download"));
        line = Recovery::parseRepairLine(QStringLiteral("done"));
        QCOMPARE(line.key, QStringLiteral("done"));
        QVERIFY(line.value.isEmpty());
        line = Recovery::parseRepairLine(QStringLiteral("  package   libfoo1 "));
        QCOMPARE(line.value, QStringLiteral("libfoo1"));
    }

    void repairStates()
    {
        Recovery recovery;
        QCOMPARE(recovery.repairState(), QStringLiteral("idle"));
        QCOMPARE(recovery.repairFound(), -1);
        recovery.handleRepairLine(QStringLiteral("stage check"));
        QCOMPARE(recovery.repairState(), QStringLiteral("check"));
        recovery.handleRepairLine(QStringLiteral("stage nonsense"));
        QCOMPARE(recovery.repairState(), QStringLiteral("check"));
        recovery.handleRepairLine(QStringLiteral("progress 40"));
        QCOMPARE(recovery.repairProgress(), 40);
        // never backwards, never beyond 100
        recovery.handleRepairLine(QStringLiteral("progress 30"));
        QCOMPARE(recovery.repairProgress(), 40);
        recovery.handleRepairLine(QStringLiteral("progress 400"));
        QCOMPARE(recovery.repairProgress(), 100);
        recovery.handleRepairLine(QStringLiteral("progress lots"));
        QCOMPARE(recovery.repairProgress(), 100);
        recovery.handleRepairLine(QStringLiteral("found 3"));
        QCOMPARE(recovery.repairFound(), 3);
        recovery.handleRepairLine(QStringLiteral("done 3"));
        QCOMPARE(recovery.repairState(), QStringLiteral("done"));

        Recovery other;
        other.handleRepairLine(QStringLiteral("error network"));
        QCOMPARE(other.repairState(), QStringLiteral("error"));
        QCOMPARE(other.repairError(), QStringLiteral("network"));
        // a word the page does not know is said as a plain failure
        other.handleRepairLine(QStringLiteral("error <b>boom</b>"));
        QCOMPARE(other.repairError(), QStringLiteral("failed"));
    }

    void snapshotsInWords()
    {
        const QDateTime now(QDate(2026, 10, 31), QTime(18, 0));
        const QVariantList list = Recovery::parseSnapshots(kSnapperJson, now, QLocale(QLocale::English, QLocale::UnitedKingdom));
        // 0 (today's system), the post snapshot and the broken date are left out
        QCOMPARE(list.size(), 4);
        const QList<int> numbers{8, 7, 5, 4};
        for (int i = 0; i < numbers.size(); ++i)
            QCOMPARE(list.at(i).toMap().value(QStringLiteral("number")).toInt(), numbers.at(i));
        const QVariantMap apt = list.at(1).toMap();
        QCOMPARE(apt.value(QStringLiteral("title")).toString(), QStringLiteral("Before installing updates or apps"));
        QCOMPARE(apt.value(QStringLiteral("when")).toString(), QStringLiteral("today, 08:00"));
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Automatic restore point"));
        QCOMPARE(list.at(2).toMap().value(QStringLiteral("when")).toString(), QStringLiteral("yesterday, 14:05"));
        // a name someone gave stays as it is
        QCOMPARE(list.at(3).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Before the big change"));
        QCOMPARE(list.at(3).toMap().value(QStringLiteral("when")).toString(), QStringLiteral("20 October, 09:30"));

        // what made a point names it, not its English description
        const QByteArray made = "{\"root\": ["
            "{\"number\": 11, \"type\": \"pre\", \"date\": \"2026-10-31 07:00:00\", \"description\": \"Before updates of 2026-10-31\", \"userdata\": {\"important\": \"yes\", \"raye\": \"update\"}},"
            "{\"number\": 12, \"type\": \"post\", \"date\": \"2026-10-31 07:03:00\", \"description\": \"After updates\", \"userdata\": {\"raye\": \"update\"}},"
            "{\"number\": 13, \"type\": \"single\", \"date\": \"2026-10-31 12:00:00\", \"description\": \"Daily restore point\", \"userdata\": {\"raye\": \"daily\"}},"
            "{\"number\": 14, \"type\": \"single\", \"date\": \"2026-10-31 13:00:00\", \"description\": \"Before the new driver\", \"userdata\": {\"raye\": \"manual\"}}]}";
        const QVariantList named = Recovery::parseSnapshots(made, now, QLocale(QLocale::English, QLocale::UnitedKingdom));
        QCOMPARE(named.size(), 3);
        QCOMPARE(named.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Before the new driver"));
        QCOMPARE(named.at(1).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Daily restore point"));
        QCOMPARE(named.at(2).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Before installing updates"));

        QVERIFY(Recovery::parseSnapshots("not json", now, QLocale::c()).isEmpty());
        QVERIFY(Recovery::parseSnapshots("{}", now, QLocale::c()).isEmpty());
        // an array, as older snapper prints it
        QCOMPARE(Recovery::parseSnapshots("[{\"number\": 3, \"type\": \"pre\", \"date\": \"2026-10-31 10:00:00\", \"description\": \"apt\"}]",
                                          now, QLocale::c()).size(), 1);
    }

    void whenFollowsTheLanguage()
    {
        const QDateTime now(QDate(2026, 10, 31), QTime(18, 0));
        const QDateTime earlier(QDate(2026, 10, 28), QTime(14, 5));
        const QString us = Recovery::whenInWords(earlier, now, QLocale(QLocale::English, QLocale::UnitedStates));
        QVERIFY2(us.startsWith(QLatin1String("October 28, ")), qPrintable(us));
        const QString fr = Recovery::whenInWords(earlier, now, QLocale(QLocale::French, QLocale::France));
        QVERIFY2(fr.startsWith(QLatin1String("28 octobre")), qPrintable(fr));
        QVERIFY2(fr.contains(QLatin1String("14:05")), qPrintable(fr));
        // another year says which one
        const QString old = Recovery::whenInWords(QDateTime(QDate(2025, 12, 24), QTime(9, 0)), now,
                                                  QLocale(QLocale::English, QLocale::UnitedKingdom));
        QVERIFY2(old.startsWith(QLatin1String("24 December 2025")), qPrintable(old));
    }

    void goBackOnlyOnASubvolume_data()
    {
        QTest::addColumn<QByteArray>("mountinfo");
        QTest::addColumn<bool>("snapper");
        QTest::addColumn<bool>("expected");
        const QByteArray atRoot = "29 1 0:26 /@ / rw,relatime shared:1 - btrfs /dev/vda2 rw,subvol=/@\n";
        QTest::newRow("raye install") << QByteArray("22 29 0:21 / /proc rw - proc proc rw\n" + atRoot) << true << true;
        QTest::newRow("no snapper") << atRoot << false << false;
        QTest::newRow("ext4") << QByteArray("29 1 8:2 / / rw - ext4 /dev/sda2 rw\n") << true << false;
        QTest::newRow("whole volume") << QByteArray("29 1 0:26 / / rw - btrfs /dev/vda2 rw\n") << true << false;
        QTest::newRow("nested") << QByteArray("29 1 0:26 /@/sys / rw - btrfs /dev/vda2 rw\n") << true << false;
        QTest::newRow("home only") << QByteArray("30 1 0:26 /@home /home rw - btrfs /dev/vda2 rw\n") << true << false;
    }

    void goBackOnlyOnASubvolume()
    {
        QFETCH(QByteArray, mountinfo);
        QFETCH(bool, snapper);
        QFETCH(bool, expected);
        QTemporaryDir root;
        writeFile(root.filePath(QStringLiteral("proc/self/mountinfo")), mountinfo);
        if (snapper)
            writeFile(root.filePath(QStringLiteral("etc/snapper/configs/root")), "SUBVOLUME=\"/\"\n");
        QCOMPARE(Recovery::goBackSupported(root.path()), expected);
    }

    void resetStatus()
    {
        auto status = Recovery::parseResetStatus(QStringLiteral("pending none\nundo 20261031142200 21\n"));
        QCOMPARE(status.pending, QStringLiteral("none"));
        QCOMPARE(status.daysLeft, 21);
        QCOMPARE(status.when, QDateTime(QDate(2026, 10, 31), QTime(14, 22)));
        status = Recovery::parseResetStatus(QStringLiteral("pending reset-keep-wallpaper\n"));
        QCOMPARE(status.pending, QStringLiteral("reset-keep-wallpaper"));
        QCOMPARE(status.daysLeft, 0);
        status = Recovery::parseResetStatus(QStringLiteral("pending something-else\nundo garbage 5\n"));
        QCOMPARE(status.pending, QStringLiteral("none"));
        QCOMPARE(status.daysLeft, 0);
    }

    // --- the backend with fake helpers --------------------------------------

    void refreshFindsWhatIsThere()
    {
        writeFile(m_dir.filePath(QStringLiteral("root/sys/firmware/efi/runtime")), "");
        fake(m_fakes, QStringLiteral("raye-boot-windows"), 0, "0003");
        fake(m_fakes, QStringLiteral("raye-restart-to-windows"));
        fake(m_helpers, QStringLiteral("bootmenu"), 0, "recordfail");
        fake(m_helpers, QStringLiteral("reset-settings"), 0, "pending undo\nundo 20261031142200 12");
        Recovery recovery;
        wire(recovery);
        recovery.refresh();
        QTRY_VERIFY(recovery.windowsAvailable());
        QTRY_VERIFY(recovery.bootMenuAvailable());
        QVERIFY(recovery.firmwareAvailable());
        QTRY_COMPARE(recovery.resetPending(), QStringLiteral("undo"));
        QCOMPARE(recovery.undoDaysLeft(), 12);
        QVERIFY(recovery.undoAvailable());
        QVERIFY(!recovery.undoDate().isEmpty());
        QVERIFY(!recovery.goBackAvailable());
        QVERIFY(calls().contains(QLatin1String("raye-boot-windows --check")));
        QVERIFY(calls().contains(QLatin1String("bootmenu --check")));
        // looking needs no password
        QVERIFY(!calls().contains(QLatin1String("pkexec")));
    }

    void noWindowsNoCard()
    {
        fake(m_fakes, QStringLiteral("raye-boot-windows"), 1);
        fake(m_fakes, QStringLiteral("raye-restart-to-windows"));
        fake(m_helpers, QStringLiteral("bootmenu"), 3);
        fake(m_helpers, QStringLiteral("reset-settings"), 0, "pending none");
        QFile::remove(m_dir.filePath(QStringLiteral("root/sys/firmware/efi/runtime")));
        QDir(m_dir.filePath(QStringLiteral("root/sys/firmware/efi"))).removeRecursively();
        Recovery recovery;
        wire(recovery);
        recovery.refresh();
        QTRY_VERIFY(calls().contains(QLatin1String("raye-boot-windows --check")));
        QTRY_VERIFY(calls().contains(QLatin1String("bootmenu --check")));
        QTest::qWait(50);
        QVERIFY(!recovery.windowsAvailable());
        QVERIFY(!recovery.bootMenuAvailable());
        QVERIFY(!recovery.firmwareAvailable());
        QVERIFY(!recovery.undoAvailable());
    }

    void restartToWindows()
    {
        fake(m_fakes, QStringLiteral("raye-boot-windows"), 0, "0003");
        fake(m_fakes, QStringLiteral("raye-restart-to-windows"));
        fake(m_helpers, QStringLiteral("bootmenu"), 3);
        fake(m_helpers, QStringLiteral("reset-settings"), 0, "pending none");
        Recovery recovery;
        wire(recovery);
        recovery.refresh();
        QTRY_VERIFY(recovery.windowsAvailable());
        QSignalSpy leaving(&recovery, &Recovery::leaving);
        recovery.restartToWindows();
        QTRY_COMPARE(leaving.size(), 1);
        QVERIFY(calls().contains(QLatin1String("raye-restart-to-windows")));
    }

    void repairRunsThroughPkexec()
    {
        fake(m_helpers, QStringLiteral("repair"), 0,
             "stage update\nprogress 2\nstage check\nprogress 10\npackage bash\npackage libfoo1\npackage coreutils\n"
             "found 3\nprogress 20\nstage download\nprogress 45\nstage install\nprogress 80\nprogress 100\ndone 3");
        Recovery recovery;
        wire(recovery);
        recovery.startRepair();
        QTRY_COMPARE(recovery.repairState(), QStringLiteral("done"));
        QCOMPARE(recovery.repairFound(), 3);
        QCOMPARE(recovery.repairProgress(), 100);
        QVERIFY(calls().contains(QStringLiteral("pkexec %1/repair").arg(m_helpers)));
        recovery.resetRepair();
        QCOMPARE(recovery.repairState(), QStringLiteral("idle"));
    }

    void repairPasswordAnswers()
    {
        fake(m_helpers, QStringLiteral("repair"), 0, "done 0");
        Recovery recovery;
        wire(recovery);
        // the password window closed: back to how it was, no error
        qputenv("FAKE_PKEXEC_EXIT", "126");
        recovery.startRepair();
        QTRY_COMPARE(recovery.repairState(), QStringLiteral("idle"));
        QCOMPARE(recovery.repairError(), QString());
        // the password was refused
        qputenv("FAKE_PKEXEC_EXIT", "127");
        recovery.startRepair();
        QTRY_COMPARE(recovery.repairState(), QStringLiteral("error"));
        QCOMPARE(recovery.repairError(), QStringLiteral("denied"));
        // the helper stopped without a word
        qunsetenv("FAKE_PKEXEC_EXIT");
        fake(m_helpers, QStringLiteral("repair"), 1, "stage update");
        recovery.startRepair();
        QTRY_COMPARE(recovery.repairState(), QStringLiteral("error"));
        QCOMPARE(recovery.repairError(), QStringLiteral("failed"));
    }

    void snapshotsAndGoingBack()
    {
        fake(m_helpers, QStringLiteral("snapshot-restore"), 0, kSnapperJson);
        Recovery recovery;
        wire(recovery);
        recovery.loadSnapshots();
        QTRY_COMPARE(recovery.snapshotsState(), QStringLiteral("ready"));
        QCOMPARE(recovery.snapshots().size(), 4);
        QVERIFY(calls().contains(QStringLiteral("pkexec %1/snapshot-restore list").arg(m_helpers)));

        clearCalls();
        fake(m_helpers, QStringLiteral("snapshot-restore"), 0, "done");
        QSignalSpy leaving(&recovery, &Recovery::leaving);
        recovery.goBack(5);
        QTRY_COMPARE(leaving.size(), 1);
        QVERIFY(calls().contains(QStringLiteral("snapshot-restore go-back 5")));
        QVERIFY(calls().contains(QLatin1String("systemctl reboot")));

        // a restore point made by hand, then the list again
        clearCalls();
        fake(m_helpers, QStringLiteral("snapshot-restore"), 0, "done 15");
        Recovery maker;
        wire(maker);
        maker.createRestorePoint(QStringLiteral("  Before   the new driver "));
        QTRY_COMPARE(maker.createState(), QStringLiteral("done"));
        QVERIFY(calls().contains(QStringLiteral("snapshot-restore create Before the new driver")));
        QTRY_VERIFY(calls().contains(QStringLiteral("snapshot-restore list")));
        clearCalls();
        maker.resetCreate();
        maker.createRestorePoint(QStringLiteral("   "));
        QTest::qWait(50);
        QVERIFY(calls().isEmpty());

        // a failure restarts nothing
        clearCalls();
        fake(m_helpers, QStringLiteral("snapshot-restore"), 1, "error failed");
        Recovery other;
        wire(other);
        other.goBack(5);
        QTRY_COMPARE(other.goBackState(), QStringLiteral("error"));
        QVERIFY(!calls().contains(QLatin1String("systemctl")));
        other.resetGoBack();
        QCOMPARE(other.goBackState(), QStringLiteral("idle"));
        // nonsense is not sent to a root helper
        clearCalls();
        other.goBack(0);
        other.goBack(-3);
        QTest::qWait(50);
        QVERIFY(calls().isEmpty());
    }

    void nothingRestartsWhileRepairing()
    {
        writeFile(m_helpers + QStringLiteral("/repair"), "#!/bin/sh\necho 'stage install'\nsleep 2\necho 'done 1'\n", true);
        fake(m_helpers, QStringLiteral("snapshot-restore"), 0, "done");
        fake(m_helpers, QStringLiteral("bootmenu"), 0, "recordfail");
        Recovery recovery;
        wire(recovery);
        recovery.startRepair();
        QTRY_COMPARE(recovery.repairState(), QStringLiteral("install"));
        recovery.goBack(5);
        recovery.restartToBootMenu();
        recovery.restartAfterRepair();
        QTest::qWait(200);
        QVERIFY2(!calls().contains(QLatin1String("snapshot-restore")), qPrintable(calls()));
        QVERIFY(!calls().contains(QLatin1String("bootmenu")));
        QVERIFY(!calls().contains(QLatin1String("systemctl")));
        QTRY_COMPARE_WITH_TIMEOUT(recovery.repairState(), QStringLiteral("done"), 10000);
        // once it is over, the restart it asks for goes ahead
        QSignalSpy leaving(&recovery, &Recovery::leaving);
        // "done" can come a moment before the helper has exited: ask again
        // until it is gone; once restarting, the next asks do nothing
        QTRY_VERIFY_WITH_TIMEOUT((recovery.restartAfterRepair(), leaving.size() >= 1), 10000);
        QTest::qWait(100);
        QCOMPARE(leaving.size(), 1);
    }

    void snapshotsCancelled()
    {
        fake(m_helpers, QStringLiteral("snapshot-restore"), 0, kSnapperJson);
        qputenv("FAKE_PKEXEC_EXIT", "126");
        Recovery recovery;
        wire(recovery);
        recovery.loadSnapshots();
        QTRY_COMPARE(recovery.snapshotsState(), QStringLiteral("cancelled"));
        QVERIFY(recovery.snapshots().isEmpty());
    }

    void resetPlansThenSignsOut()
    {
        fake(m_helpers, QStringLiteral("reset-settings"), 0, "pending reset-keep-wallpaper");
        Recovery recovery;
        wire(recovery);
        int logouts = 0;
        recovery.setLogout([&logouts] { ++logouts; return true; });
        QSignalSpy leaving(&recovery, &Recovery::leaving);
        recovery.resetSettings(true);
        QTRY_COMPARE(leaving.size(), 1);
        QCOMPARE(logouts, 1);
        QVERIFY(calls().contains(QLatin1String("reset-settings schedule --keep-wallpaper")));
        // no pkexec: it is the user's own settings
        QVERIFY(!calls().contains(QLatin1String("pkexec")));
        QTRY_COMPARE(recovery.resetPending(), QStringLiteral("reset-keep-wallpaper"));
    }

    void resetWhenSignOutFails()
    {
        fake(m_helpers, QStringLiteral("reset-settings"), 0, "pending reset");
        Recovery recovery;
        wire(recovery);
        recovery.setLogout([] { return false; });
        QSignalSpy failed(&recovery, &Recovery::failed);
        recovery.resetSettings(false);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("logout-failed"));
        QVERIFY2(calls().contains(QLatin1String("reset-settings schedule\n")), qPrintable(calls()));
        QVERIFY(!recovery.busy());
    }

    void undoAndCancel()
    {
        fake(m_helpers, QStringLiteral("reset-settings"), 0, "pending none\nundo 20261031142200 3");
        Recovery recovery;
        wire(recovery);
        int logouts = 0;
        recovery.setLogout([&logouts] { ++logouts; return true; });
        // nothing to undo yet: nothing happens
        recovery.undoReset();
        QTest::qWait(50);
        QCOMPARE(logouts, 0);
        recovery.refresh();
        QTRY_VERIFY(recovery.undoAvailable());
        recovery.undoReset();
        QTRY_COMPARE(logouts, 1);
        QVERIFY(calls().contains(QLatin1String("reset-settings schedule-undo")));
        recovery.cancelReset();
        QTRY_VERIFY(calls().contains(QLatin1String("reset-settings cancel")));

        // the bar's "Sign out now"
        Recovery bar;
        wire(bar);
        bar.setLogout([&logouts] { ++logouts; return true; });
        QSignalSpy leaving(&bar, &Recovery::leaving);
        bar.signOut();
        QCOMPARE(logouts, 2);
        QCOMPARE(leaving.size(), 1);
        Recovery stuck;
        wire(stuck);
        stuck.setLogout([] { return false; });
        QSignalSpy failed(&stuck, &Recovery::failed);
        stuck.signOut();
        QCOMPARE(failed.size(), 1);
    }

    void advancedStartup()
    {
        fake(m_helpers, QStringLiteral("bootmenu"), 0, "recordfail");
        fake(m_helpers, QStringLiteral("reset-settings"), 0, "pending none");
        writeFile(m_dir.filePath(QStringLiteral("root/sys/firmware/efi/runtime")), "");
        Recovery recovery;
        wire(recovery);
        recovery.refresh();
        QTRY_VERIFY(recovery.firmwareAvailable());
        QSignalSpy leaving(&recovery, &Recovery::leaving);
        recovery.restartToFirmware();
        QTRY_COMPARE(leaving.size(), 1);
        QVERIFY(calls().contains(QLatin1String("systemctl reboot --firmware-setup")));

        clearCalls();
        Recovery menu;
        wire(menu);
        QSignalSpy menuLeaving(&menu, &Recovery::leaving);
        menu.restartToBootMenu();
        QTRY_COMPARE(menuLeaving.size(), 1);
        QVERIFY(calls().contains(QStringLiteral("pkexec %1/bootmenu").arg(m_helpers)));
        QVERIFY(calls().contains(QLatin1String("systemctl reboot")));

        // a boot menu that cannot show: say so, do not restart
        clearCalls();
        fake(m_helpers, QStringLiteral("bootmenu"), 3);
        Recovery none;
        wire(none);
        QSignalSpy failed(&none, &Recovery::failed);
        none.restartToBootMenu();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("boot-menu-unavailable"));
        QVERIFY(!calls().contains(QLatin1String("systemctl")));
        QVERIFY(!none.busy());
    }

    // --- the root helpers, with fakes -----------------------------------------

    void bootmenuHelper()
    {
        const QString grub = m_dir.filePath(QStringLiteral("grub"));
        const QString bin = m_dir.filePath(QStringLiteral("bootbin"));
        fake(bin, QStringLiteral("grub-editenv"));
        const QStringList env{QStringLiteral("RAYE_GRUB_DIR=") + grub};

        // Raye's entries read menu_show_once
        writeFile(grub + QStringLiteral("/grub.cfg"), "if [ \"${menu_show_once}\" ]; then\n  set timeout_style=menu\nfi\n");
        Result r = runHelper(QStringLiteral("bootmenu"), {QStringLiteral("--check")}, bin, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out.trimmed(), QStringLiteral("menu_show_once"));
        QVERIFY(calls().isEmpty()); // --check changes nothing
        writeFile(grub + QStringLiteral("/grubenv"), "# GRUB Environment Block\n");
        r = runHelper(QStringLiteral("bootmenu"), {}, bin, env);
        QCOMPARE(r.code, 0);
        QVERIFY2(calls().contains(QStringLiteral("grub-editenv %1/grubenv set menu_show_once=1").arg(grub)), qPrintable(calls()));

        // the stock menu shows after a failed start, if it waits
        clearCalls();
        writeFile(grub + QStringLiteral("/grub.cfg"),
                  "if [ \"${recordfail}\" = 1 ] ; then\n  set timeout=5\nelse\n  set timeout=0\nfi\n");
        QFile::remove(grub + QStringLiteral("/grubenv"));
        r = runHelper(QStringLiteral("bootmenu"), {}, bin, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out.trimmed(), QStringLiteral("recordfail"));
        QVERIFY(calls().contains(QStringLiteral("grub-editenv %1/grubenv create").arg(grub)));
        QVERIFY(calls().contains(QStringLiteral("grub-editenv %1/grubenv set recordfail=1").arg(grub)));

        // it does not wait: nothing can be done from here
        clearCalls();
        writeFile(grub + QStringLiteral("/grub.cfg"), "if [ \"${recordfail}\" = 1 ] ; then\n  set timeout=0\nfi\n");
        r = runHelper(QStringLiteral("bootmenu"), {}, bin, env);
        QCOMPARE(r.code, 3);
        QVERIFY(calls().isEmpty());
        writeFile(grub + QStringLiteral("/grub.cfg"), "menuentry 'Raye' {\n}\n");
        QCOMPARE(runHelper(QStringLiteral("bootmenu"), {}, bin, env).code, 3);

        // nothing else is accepted, and under pkexec the test folder is ignored
        QCOMPARE(runHelper(QStringLiteral("bootmenu"), {QStringLiteral("--set=x")}, bin, env).code, 2);
        writeFile(grub + QStringLiteral("/grub.cfg"), "menu_show_once\n");
        r = runHelper(QStringLiteral("bootmenu"), {QStringLiteral("--check")}, bin,
                      env + QStringList{QStringLiteral("PKEXEC_UID=1000")});
        if (!QFileInfo::exists(QStringLiteral("/boot/grub/grub.cfg")))
            QCOMPARE(r.code, 3);
        QVERIFY(!calls().contains(grub));
    }

    void repairHelper()
    {
        const QString bin = m_dir.filePath(QStringLiteral("repairbin"));
        const QString log = m_dir.filePath(QStringLiteral("repair.log"));
        const QStringList env{QStringLiteral("RAYE_REPAIR_LOG=") + log};
        // debsums: one changed file of bash, one missing file of libfoo1, one
        // of a package installed by hand, one diverted file
        writeFile(bin + QStringLiteral("/debsums"),
                  "#!/bin/sh\necho \"debsums $*\" >> '" + m_calls.toUtf8() + "'\n"
                  "echo /usr/bin/bash\necho /opt/local/thing\necho /usr/sbin/diverted\n"
                  "echo 'debsums: missing file /usr/lib/libfoo.so.1 (from libfoo1:amd64 package)' >&2\nexit 2\n",
                  true);
        writeFile(bin + QStringLiteral("/dpkg-query"), "#!/bin/sh\n"
"case \"$1\" in\n"
"-S)\n"
"    case \"$2\" in\n"
"    /usr/bin/bash) echo \"bash: /usr/bin/bash\" ;;\n"
"    /opt/local/thing) echo \"handmade: /opt/local/thing\" ;;\n"
"    /usr/sbin/diverted) echo \"diversion by raye-system from: /usr/sbin/diverted\"; echo \"raye-system, coreutils: /usr/sbin/diverted\" ;;\n"
"    esac ;;\n"
"-W)\n"
"    name=$4\n"
"    case \"$name\" in ghost) echo \"not-installed\"; exit 0 ;; esac\n"
"    case \"$3\" in\n"
"    *binary:Package*) echo \"installed $name:amd64=1.0-1\" ;;\n"
"    *) echo installed ;;\n"
"    esac ;;\n"
"esac\n", true);
        writeFile(bin + QStringLiteral("/apt-cache"), "#!/bin/sh\n"
"echo \"$2:\"\n"
"echo \"  Installed: 1.0-1\"\n"
"echo \"  Version table:\"\n"
"echo \" *** 1.0-1 500\"\n"
"case \"$2\" in\n"
"handmade) ;;\n"
"*) echo \"        500 http://archive.ubuntu.com/ubuntu resolute/main amd64 Packages\" ;;\n"
"esac\n"
"echo \"        100 /var/lib/dpkg/status\"\n", true);
        writeFile(bin + QStringLiteral("/apt-get"),
                  "#!/bin/sh\necho \"apt-get $*\" >> '" + m_calls.toUtf8() + "'\n"
                  "case \"$*\" in *install*)\n"
                  "  echo 'dlstatus:1:0:Retrieving' >&3\n  echo 'dlstatus:2:50:Retrieving' >&3\n"
                  "  echo 'pmstatus:bash:30:Preparing bash' >&3\n  echo 'pmstatus:libfoo1:amd64:90:Setting up' >&3\n"
                  "  echo 'Setting up bash'\n"
                  "  [ -n \"$FAKE_APT_FAIL\" ] && { echo \"$FAKE_APT_FAIL\" >&2; exit 100; }\n;; esac\nexit 0\n",
                  true);

        // --check: what would be reinstalled, nothing downloaded or installed
        Result r = runHelper(QStringLiteral("repair"), {QStringLiteral("--check")}, bin, env);
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains(QLatin1String("package bash\n")), qPrintable(r.out));
        QVERIFY(r.out.contains(QLatin1String("package libfoo1\n")));
        QVERIFY(r.out.contains(QLatin1String("package coreutils\n")));
        QVERIFY(r.out.contains(QLatin1String("package raye-system\n")));
        QVERIFY2(!r.out.contains(QLatin1String("handmade")), qPrintable(r.out));
        QVERIFY(!r.out.contains(QLatin1String("diversion")));
        QVERIFY(r.out.contains(QLatin1String("found 4\n")));
        QVERIFY(r.out.endsWith(QLatin1String("done 0\n")));
        QVERIFY(!calls().contains(QLatin1String("apt-get")));
        QVERIFY(readFile(log).contains("handmade"));

        // the repair: the exact versions, never removing anything
        clearCalls();
        r = runHelper(QStringLiteral("repair"), {}, bin, env);
        QCOMPARE(r.code, 0);
        const QString c = calls();
        QVERIFY(c.contains(QLatin1String("apt-get -q update")));
        QVERIFY2(c.contains(QLatin1String("install --reinstall")), qPrintable(c));
        QVERIFY(c.contains(QLatin1String("--no-remove")));
        QVERIFY(c.contains(QLatin1String("bash:amd64=1.0-1")));
        QVERIFY(c.contains(QLatin1String("libfoo1:amd64=1.0-1")));
        QVERIFY(!c.contains(QLatin1String("handmade")));
        QVERIFY(r.out.contains(QLatin1String("stage update\n")));
        QVERIFY(r.out.contains(QLatin1String("stage download\n")));
        QVERIFY(r.out.contains(QLatin1String("progress 40\n"))); // 20 + 50 % of 40
        QVERIFY(r.out.contains(QLatin1String("stage install\n")));
        QVERIFY(r.out.contains(QLatin1String("progress 72\n"))); // 60 + 30 % of 40
        QVERIFY(r.out.contains(QLatin1String("progress 96\n"))); // the architecture is not read as the percentage
        QVERIFY(r.out.endsWith(QLatin1String("progress 100\ndone 4\n")));
        // apt's own words go to the log, not to the page
        QVERIFY(!r.out.contains(QLatin1String("Setting up")));
        QVERIFY(readFile(log).contains("Setting up bash"));

        // the errors the page knows
        r = runHelper(QStringLiteral("repair"), {}, bin,
                      env + QStringList{QStringLiteral("FAKE_APT_FAIL=E: Could not get lock /var/lib/dpkg/lock-frontend")});
        QCOMPARE(r.code, 1);
        QVERIFY(r.out.endsWith(QLatin1String("error locked\n")));
        r = runHelper(QStringLiteral("repair"), {}, bin,
                      env + QStringList{QStringLiteral("FAKE_APT_FAIL=E: Failed to fetch http://archive")});
        QVERIFY(r.out.endsWith(QLatin1String("error network\n")));
        r = runHelper(QStringLiteral("repair"), {}, bin, env + QStringList{QStringLiteral("FAKE_APT_FAIL=E: oops")});
        QVERIFY(r.out.endsWith(QLatin1String("error apt\n")));

        // nothing damaged: nothing installed
        clearCalls();
        writeFile(bin + QStringLiteral("/debsums"), "#!/bin/sh\nexit 0\n", true);
        r = runHelper(QStringLiteral("repair"), {}, bin, env);
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains(QLatin1String("found 0\n")));
        QVERIFY(!calls().contains(QLatin1String("--reinstall")));

        QCOMPARE(runHelper(QStringLiteral("repair"), {QStringLiteral("--all")}, bin, env).code, 2);
    }

    void snapshotRestoreHelper()
    {
        const QString bin = m_dir.filePath(QStringLiteral("snapbin"));
        const QString root = m_dir.filePath(QStringLiteral("snaproot"));
        const QString top = m_dir.filePath(QStringLiteral("snaptop"));
        const QString list = m_dir.filePath(QStringLiteral("subvolumes"));
        const QStringList env{QStringLiteral("RAYE_GO_BACK_ROOT=") + root, QStringLiteral("RAYE_GO_BACK_TOP=") + top,
                              QStringLiteral("FAKE_LIST=") + list};
        writeFile(bin + QStringLiteral("/findmnt"), "#!/bin/sh\n"
"case \"$2\" in\n"
"FSTYPE) echo btrfs ;;\n"
"FSROOT) echo \"${FAKE_FSROOT:-/@}\" ;;\n"
"SOURCE) echo \"/dev/vda2[/@]\" ;;\n"
"esac\n", true);
        writeFile(bin + QStringLiteral("/mountpoint"), "#!/bin/sh\nexit 0\n", true);
        fake(bin, QStringLiteral("mount"));
        fake(bin, QStringLiteral("umount"));
        writeFile(bin + QStringLiteral("/snapper"), "#!/bin/sh\necho \"snapper $*\" >> '" + m_calls.toUtf8() + "'\n"
                  "case \"$*\" in *' create '*) echo 12 ;; *) echo '{\"root\": []}' ;; esac\n", true);
        writeFile(bin + QStringLiteral("/btrfs"),
                  "#!/bin/sh\necho \"btrfs $*\" >> '" + m_calls.toUtf8() + "'\n"
                  "case \"$2\" in\n"
                  "snapshot) cp -a \"$3\" \"$4\" ;;\n"
                  "list) cat \"$FAKE_LIST\" ;;\n"
                  "delete) rm -rf -- \"${3:?}\" ;;\n"
                  "esac\n",
                  true);
        writeFile(root + QStringLiteral("/etc/fstab"), "UUID=1 / btrfs subvol=/@,defaults 0 1\n");
        writeFile(root + QStringLiteral("/etc/snapper/configs/root"), "SUBVOLUME=\"/\"\n");
        writeFile(root + QStringLiteral("/proc/cmdline"), "BOOT_IMAGE=/vmlinuz root=UUID=1 rootflags=subvol=@ quiet\n");
        writeFile(root + QStringLiteral("/boot/vmlinuz-6.20.0-1-raye"), "kernel");
        writeFile(root + QStringLiteral("/usr/lib/modules/6.20.0-1-raye/modules.dep"), "new modules");
        // the volume: today's system, its snapshots, a nested subvolume, an older swap
        writeFile(top + QStringLiteral("/@/etc/version"), "today");
        writeFile(top + QStringLiteral("/@/.snapshots/5/snapshot/etc/version"), "before");
        writeFile(top + QStringLiteral("/@/.snapshots/5/info.xml"), "<snapshot/>");
        QDir().mkpath(top + QStringLiteral("/@/.snapshots/5/snapshot/.snapshots"));
        QDir().mkpath(top + QStringLiteral("/@/.snapshots/5/snapshot/var/lib/machines"));
        writeFile(top + QStringLiteral("/@/var/lib/machines/box/file"), "container");
        writeFile(top + QStringLiteral("/@.raye-before-20200101-000000/etc/version"), "ancient");
        writeFile(list, "ID 260 gen 5 top level 256 path @/.snapshots\n"
                        "ID 261 gen 5 top level 260 path @/.snapshots/5/snapshot\n"
                        "ID 262 gen 5 top level 256 path @/var/lib/machines\n");

        Result r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("--check")}, bin, env);
        QCOMPARE(r.code, 0);
        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("list")}, bin, env);
        QCOMPARE(r.code, 0);
        QVERIFY(calls().contains(QLatin1String("snapper --no-dbus -c root --jsonout list")));

        // a restore point made by hand: one plain name, kept like the update ones
        clearCalls();
        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("create"), QStringLiteral("Before the new driver")}, bin, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out.trimmed(), QStringLiteral("done 12"));
        QVERIFY2(calls().contains(QLatin1String("-d Before the new driver -u important=yes,raye=manual")), qPrintable(calls()));
        clearCalls();
        for (const QStringList &bad : {QStringList{QStringLiteral("create")}, QStringList{QStringLiteral("create"), QString()},
                                       QStringList{QStringLiteral("create"), QString(61, QLatin1Char('x'))},
                                       QStringList{QStringLiteral("create"), QStringLiteral("a\tb")},
                                       QStringList{QStringLiteral("create"), QStringLiteral("a"), QStringLiteral("b")}}) {
            QCOMPARE(runHelper(QStringLiteral("snapshot-restore"), bad, bin, env).code, 2);
        }
        QVERIFY(calls().isEmpty());

        // arguments that are not one snapshot number never reach the volume
        clearCalls();
        for (const QStringList &bad : {QStringList{QStringLiteral("go-back")}, QStringList{QStringLiteral("go-back"), QStringLiteral("0")},
                                       QStringList{QStringLiteral("go-back"), QStringLiteral("5;reboot")},
                                       QStringList{QStringLiteral("go-back"), QStringLiteral("../5")},
                                       QStringList{QStringLiteral("go-back"), QStringLiteral("5"), QStringLiteral("6")},
                                       QStringList{QStringLiteral("rollback"), QStringLiteral("5")}}) {
            QCOMPARE(runHelper(QStringLiteral("snapshot-restore"), bad, bin, env).code, 2);
        }
        QVERIFY(calls().isEmpty());

        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("go-back"), QStringLiteral("9")}, bin, env);
        QCOMPARE(r.code, 1);
        QCOMPARE(r.out.trimmed(), QStringLiteral("error no-snapshot"));
        QCOMPARE(readFile(top + QStringLiteral("/@/etc/version")), QByteArray("today"));

        // Raye Update's "before" point: taken with the install already armed
        const QString snap5 = top + QStringLiteral("/@/.snapshots/5/snapshot");
        QVERIFY(QFile::link(QStringLiteral("/var/lib/raye-update/offline"), snap5 + QStringLiteral("/system-update")));
        writeFile(snap5 + QStringLiteral("/var/lib/raye-update/offline"), "5df3332586f4\n");
        writeFile(snap5 + QStringLiteral("/var/lib/raye-update/state.json"),
                  R"({"phase": "installing", "readyId": "5df3332586f4", "armed": {"then": "restart", "at": 0},)"
                  R"( "installAttempts": 1, "autoRestart": true})");

        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("go-back"), QStringLiteral("5")}, bin, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out.trimmed(), QStringLiteral("done"));
        // the update it was taken before does not install again at the next start
        QVERIFY(!QFileInfo(top + QStringLiteral("/@/system-update")).isSymLink());
        QVERIFY(!QFileInfo::exists(top + QStringLiteral("/@/var/lib/raye-update/offline")));
        const QJsonObject state =
            QJsonDocument::fromJson(readFile(top + QStringLiteral("/@/var/lib/raye-update/state.json"))).object();
        QCOMPARE(state.value(QStringLiteral("phase")).toString(), QStringLiteral("idle"));
        QVERIFY(state.value(QStringLiteral("armed")).isNull());
        QCOMPARE(state.value(QStringLiteral("readyId")).toString(), QString());
        QCOMPARE(state.value(QStringLiteral("autoRestart")).toBool(), true);
        // @ is now the snapshot, writable copy
        QCOMPARE(readFile(top + QStringLiteral("/@/etc/version")), QByteArray("before"));
        // the snapshots and the containers moved with it
        QCOMPARE(readFile(top + QStringLiteral("/@/.snapshots/5/info.xml")), QByteArray("<snapshot/>"));
        QCOMPARE(readFile(top + QStringLiteral("/@/var/lib/machines/box/file")), QByteArray("container"));
        // the kernel in /boot keeps its modules
        QCOMPARE(readFile(top + QStringLiteral("/@/usr/lib/modules/6.20.0-1-raye/modules.dep")), QByteArray("new modules"));
        // today's system is kept next to it, the older one is gone
        const QStringList before = QDir(top).entryList({QStringLiteral("@.raye-before-*")}, QDir::Dirs);
        QCOMPARE(before.size(), 1);
        QCOMPARE(readFile(top + QLatin1Char('/') + before.first() + QStringLiteral("/etc/version")), QByteArray("today"));
        QVERIFY(!QFileInfo::exists(top + QStringLiteral("/@.raye-new")));

        // an offline update armed by another tool is left to it
        QVERIFY(QFile::remove(snap5 + QStringLiteral("/system-update")));
        QVERIFY(QFile::link(QStringLiteral("/var/lib/PackageKit/prepared-update"), snap5 + QStringLiteral("/system-update")));
        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("go-back"), QStringLiteral("5")}, bin, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(QFileInfo(top + QStringLiteral("/@/system-update")).symLinkTarget(),
                 QStringLiteral("/var/lib/PackageKit/prepared-update"));

        // a system mounted by number cannot go back this way
        writeFile(root + QStringLiteral("/etc/fstab"), "UUID=1 / btrfs subvolid=256,defaults 0 1\n");
        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("--check")}, bin, env);
        QCOMPARE(r.code, 3);
        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("go-back"), QStringLiteral("5")}, bin, env);
        QCOMPARE(r.code, 3);
        QCOMPARE(r.out.trimmed(), QStringLiteral("error not-supported"));
        writeFile(root + QStringLiteral("/etc/fstab"), "UUID=1 / btrfs subvol=/@,defaults 0 1\n");
        // nor a system that is the whole volume
        r = runHelper(QStringLiteral("snapshot-restore"), {QStringLiteral("--check")}, bin,
                      env + QStringList{QStringLiteral("FAKE_FSROOT=/")});
        QCOMPARE(r.code, 3);
    }

    void resetHelperRefusesRootUnlessTold()
    {
        // the full behaviour is in tests/test_reset_settings.sh
        if (geteuid() != 0)
            QSKIP("not running as root");
        Result r = runHelper(QStringLiteral("reset-settings"), {QStringLiteral("status")}, m_fakes);
        QCOMPARE(r.code, 2);
    }
};

QTEST_GUILESS_MAIN(TestRecovery)
#include "test_recovery.moc"
