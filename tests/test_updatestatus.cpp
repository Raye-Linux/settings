// SPDX-License-Identifier: GPL-3.0-or-later
// "Restart now" on the home page and on Raye Update: downloaded updates
// are armed with raye-update first (a fake here, that writes down how it
// was called and exits with the code the test picks), then the restart
// is asked for. The restart itself is a stand-in: nothing here restarts.
#include "updatestatus.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

class TestUpdateStatus : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_restarts = 0;
    bool m_restartOk = true;

    QString path(const QString &name) const { return m_dir.filePath(name); }

    void writeState(const QByteArray &state)
    {
        QFile f(path(QStringLiteral("state.json")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{\"version\": 2, \"state\": \"" + state + "\", \"phase\": \"" + state + "\", \"updates\": 3}");
    }

    // raye-update as the test wants it: exits with `code`
    void writeTool(int code)
    {
        QFile f(path(QStringLiteral("raye-update")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\necho \"$@\" >> '" + path(QStringLiteral("calls")).toUtf8() + "'\nexit "
                + QByteArray::number(code) + "\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    }

    QByteArray calls() const
    {
        QFile f(path(QStringLiteral("calls")));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    std::unique_ptr<UpdateStatus> make()
    {
        auto u = std::make_unique<UpdateStatus>(path(QStringLiteral("state.json")));
        u->setToolPath(path(QStringLiteral("raye-update")));
        u->setRebootRequiredPath(path(QStringLiteral("reboot-required")));
        u->setRestartRequest([this](std::function<void(bool)> done) {
            ++m_restarts;
            done(m_restartOk);
        });
        return u;
    }

private Q_SLOTS:
    void init()
    {
        QFile::remove(path(QStringLiteral("calls")));
        QFile::remove(path(QStringLiteral("reboot-required")));
        QFile::remove(path(QStringLiteral("raye-update")));
        m_restarts = 0;
        m_restartOk = true;
    }

    void plainRestartWhenNothingIsDownloaded()
    {
        writeState("idle");
        writeTool(0);
        auto u = make();
        QVERIFY(!u->restartNeeded());
        u->restartAndInstall();
        QCOMPARE(m_restarts, 1);
        QVERIFY(calls().isEmpty()); // nothing to arm
    }

    void rebootRequiredAsksForARestart()
    {
        writeState("idle");
        QFile f(path(QStringLiteral("reboot-required")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        auto u = make();
        QVERIFY(u->restartNeeded());
        u->restartAndInstall();
        QCOMPARE(m_restarts, 1);
    }

    // a package can ask for a restart while the window is open
    void rebootRequiredSeenWhenItAppears()
    {
        writeState("idle");
        QDir(m_dir.path()).mkpath(QStringLiteral("run"));
        auto u = make();
        u->setRebootRequiredPath(path(QStringLiteral("run/reboot-required")));
        QVERIFY(!u->restartNeeded());
        QSignalSpy changed(u.get(), &UpdateStatus::changed);
        QFile f(path(QStringLiteral("run/reboot-required")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QTRY_VERIFY(u->restartNeeded());
        QVERIFY(changed.count() > 0);
        QFile::remove(path(QStringLiteral("run/reboot-required")));
        QTRY_VERIFY(!u->restartNeeded());
    }

    void writeRaw(const QByteArray &json)
    {
        QFile f(path(QStringLiteral("state.json")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(json);
    }

    // downloaded, and all of it installs without a restart
    void readySetWithoutRestartNeedsNone()
    {
        writeRaw(R"({"version": 2, "phase": "ready", "state": "idle", "needsRestart": false,
                     "pending": [{"name": "firefox", "restart": false}]})");
        auto u = make();
        QVERIFY(!u->restartNeeded());
        QCOMPARE(u->pending().first().toMap().value(QStringLiteral("restart")).toBool(), false);
    }

    void installingNowFollowsThePhase()
    {
        writeRaw(R"({"version": 2, "phase": "installing", "armed": null, "installPercent": 40})");
        auto u = make();
        QVERIFY(u->installingNow());
        QCOMPARE(u->installPercent(), 40);
        // the install at start-up is not one in use
        writeRaw(R"({"version": 2, "phase": "installing", "armed": {"then": "restart", "at": 0}})");
        u->reload();
        QVERIFY(!u->installingNow());
    }

    // both go through raye-update (pkexec): a standard user may take as
    // long as needed to type an administrator's password; systemctl's own
    // prompt gave up after 25 s
    void checkAndInstallGoThroughTheTool()
    {
        QFile::remove(path(QStringLiteral("calls")));
        writeTool(0);
        writeState("ready");
        auto u = make();
        u->checkNow();
        QTRY_COMPARE(calls(), QByteArray("check-now\n"));
        QFile::remove(path(QStringLiteral("calls")));
        u->installNow();
        QTRY_COMPARE(calls(), QByteArray("install-now\n"));
    }

    void rayeKernelIsNotASecondKernel()
    {
        QCOMPARE(UpdateStatus::kindOf(QStringLiteral("raye-kernel")), QStringLiteral("system"));
        QCOMPARE(UpdateStatus::kindOf(QStringLiteral("linux-image-7.0.0-12-generic")), QStringLiteral("kernel"));
    }

    void armsThenRestarts_data()
    {
        QTest::addColumn<int>("code");
        QTest::newRow("armed") << 0;
        QTest::newRow("nothing downloaded after all") << 3;
        QTest::newRow("another tool armed") << 4;
    }

    void armsThenRestarts()
    {
        QFETCH(int, code);
        writeState("ready");
        writeTool(code);
        auto u = make();
        QVERIFY(u->restartNeeded());
        QSignalSpy failed(u.get(), &UpdateStatus::restartFailed);
        u->restartAndInstall();
        QVERIFY(QTest::qWaitFor([&] { return m_restarts > 0; }, 5000));
        QCOMPARE(calls(), QByteArray("arm --then restart\n"));
        QCOMPARE(failed.count(), 0);
        QVERIFY(u->restarting()); // until the session goes away
    }

    void failureIsSaidAndNothingRestarts_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("reason");
        QTest::newRow("password not given") << 126 << QStringLiteral("cancelled");
        QTest::newRow("not allowed") << 127 << QStringLiteral("cancelled");
        QTest::newRow("arm failed") << 1 << QStringLiteral("failed");
    }

    void failureIsSaidAndNothingRestarts()
    {
        QFETCH(int, code);
        QFETCH(QString, reason);
        writeState("ready");
        writeTool(code);
        auto u = make();
        QSignalSpy failed(u.get(), &UpdateStatus::restartFailed);
        u->restartAndInstall();
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().first().toString(), reason);
        QCOMPARE(m_restarts, 0);
        QVERIFY(!u->restarting());
    }

    void missingToolIsAFailure()
    {
        writeState("ready");
        auto u = make();
        QSignalSpy failed(u.get(), &UpdateStatus::restartFailed);
        u->restartAndInstall();
        QVERIFY(failed.count() == 1 || failed.wait(5000));
        QCOMPARE(failed.first().first().toString(), QStringLiteral("failed"));
        QCOMPARE(m_restarts, 0);
    }

    void refusedRestartIsSaid()
    {
        writeState("idle");
        m_restartOk = false;
        auto u = make();
        QSignalSpy failed(u.get(), &UpdateStatus::restartFailed);
        u->restartAndInstall();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("restart"));
        QVERIFY(!u->restarting());
        // and it can be tried again
        m_restartOk = true;
        u->restartAndInstall();
        QCOMPARE(m_restarts, 2);
    }

    void secondClickIsIgnored()
    {
        writeState("ready");
        writeTool(0);
        auto u = make();
        u->restartAndInstall();
        u->restartAndInstall();
        QVERIFY(QTest::qWaitFor([&] { return m_restarts > 0; }, 5000));
        QTest::qWait(200);
        QCOMPARE(m_restarts, 1);
        QCOMPARE(calls(), QByteArray("arm --then restart\n"));
    }

    void readsTheWholeStateFile()
    {
        QFile f(path(QStringLiteral("state.json")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"version": 2, "phase": "downloading", "state": "downloading", "updates": 2,
            "downloadedBytes": 1048576, "totalBytes": 4194304,
            "pending": [{"name": "firefox", "from": "140.0", "to": "141.0", "origin": "x", "kind": "app"}],
            "firmware": [{"device": "UEFI dbx", "from": "1", "to": "2", "summary": "s", "size": 5}],
            "lastInstall": {"time": 1793376000, "count": 14, "firmware": 0, "ok": true},
            "error": {"code": "no-space", "detail": "tail"},
            "armed": {"then": "shutdown", "at": 0},
            "activeHours": {"start": 9, "end": 19}, "autoRestart": true})");
        f.close();
        auto u = make();
        QCOMPARE(u->phase(), QStringLiteral("downloading"));
        QCOMPARE(u->downloadedBytes(), 1048576);
        QCOMPARE(u->totalBytes(), 4194304);
        QCOMPARE(u->pending().size(), 1);
        QCOMPARE(u->pending().first().toMap().value(QStringLiteral("to")).toString(), QStringLiteral("141.0"));
        QCOMPARE(u->firmware().first().toMap().value(QStringLiteral("device")).toString(), QStringLiteral("UEFI dbx"));
        QCOMPARE(u->lastInstall().value(QStringLiteral("count")).toInt(), 14);
        QCOMPARE(u->errorCode(), QStringLiteral("no-space"));
        QCOMPARE(u->armedThen(), QStringLiteral("shutdown"));
        QCOMPARE(u->activeHoursStart(), 9);
        QCOMPARE(u->activeHoursEnd(), 19);
        QVERIFY(u->autoRestart());
    }

    // what the last check could not read: the file says it, and a file
    // from before those keys means everything was read
    void readsWhatTheCheckCouldNotRead()
    {
        QFile f(path(QStringLiteral("state.json")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"version": 2, "phase": "idle", "state": "idle", "checkedEverything": false,
            "brokenSources": [{"uri": "mirror+file:/etc/apt/mirrors/raye.list", "problem": "missing"}]})");
        f.close();
        auto u = make();
        QVERIFY(!u->property("checkedEverything").toBool());
        const QVariantList broken = u->property("brokenSources").toList();
        QCOMPARE(broken.size(), 1);
        QCOMPARE(broken.first().toMap().value(QStringLiteral("problem")).toString(), QStringLiteral("missing"));

        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"version": 2, "phase": "idle", "state": "idle"})");
        f.close();
        u->reload();
        QVERIFY(u->property("checkedEverything").toBool());
        QVERIFY(u->property("brokenSources").toList().isEmpty());
    }

    void readsHowFarItGot()
    {
        writeRaw(R"({"version": 2, "phase": "checking", "checkPercent": 40})");
        auto u = make();
        QCOMPARE(u->checkPercent(), 40);
        writeRaw(R"({"version": 2, "phase": "downloading", "downloadedBytes": 41103225, "totalBytes": 91340500,
                     "bytesPerSecond": 4194304, "secondsLeft": 12})");
        u->reload();
        QCOMPARE(u->checkPercent(), 0);
        QCOMPARE(u->bytesPerSecond(), 4194304);
        QCOMPARE(u->secondsLeft(), 12);
        // nonsense from a broken writer does not reach the page
        writeRaw(R"({"version": 2, "phase": "checking", "checkPercent": 140, "bytesPerSecond": -5, "secondsLeft": -1})");
        u->reload();
        QCOMPARE(u->checkPercent(), 100);
        QCOMPARE(u->bytesPerSecond(), 0);
        QCOMPARE(u->secondsLeft(), 0);
    }

    void readsWhatTheLastInstallDid()
    {
        writeRaw(R"({"version": 2, "phase": "ready", "needsRestart": true,
                     "pending": [{"name": "libc6", "restart": true}],
                     "lastInstall": {"time": 1793376000, "count": 2, "firmware": 0, "ok": true,
                                     "packages": ["firefox", "raye-settings"]}})");
        auto u = make();
        QCOMPARE(u->installedPackages(), QStringList({QStringLiteral("firefox"), QStringLiteral("raye-settings")}));
        // per-row state rides along in pending
        writeRaw(R"({"version": 2, "phase": "installing", "pending": [{"name": "firefox", "restart": false, "state": "installing"}]})");
        u->reload();
        QCOMPARE(u->pending().first().toMap().value(QStringLiteral("state")).toString(), QStringLiteral("installing"));
        QVERIFY(u->installedPackages().isEmpty());
    }

    // files written before these keys: nothing to say, nothing breaks
    void oldFileHasNoProgress()
    {
        writeRaw(R"({"version": 2, "phase": "downloading", "downloadedBytes": 5, "totalBytes": 10,
                     "lastInstall": {"time": 1, "count": 3, "firmware": 0, "ok": true}})");
        auto u = make();
        QCOMPARE(u->checkPercent(), 0);
        QCOMPARE(u->bytesPerSecond(), 0);
        QCOMPARE(u->secondsLeft(), 0);
        QVERIFY(u->installedPackages().isEmpty());
        writeState("idle");
        u->reload();
        QCOMPARE(u->checkPercent(), 0);
        QVERIFY(u->installedPackages().isEmpty());
    }

    void versionOneFileStillReads()
    {
        writeState("idle");
        auto u = make();
        QCOMPARE(u->phase(), QStringLiteral("idle"));
        QCOMPARE(u->activeHoursStart(), 8);
        QCOMPARE(u->activeHoursEnd(), 17);
        QVERIFY(u->armedThen().isEmpty());
        QVERIFY(u->pending().isEmpty());
    }

    void historyHasTheUpdatesOnly()
    {
        const QByteArray log = R"(Start-Date: 2026-10-30  09:12:01
Commandline: apt-get install -y discord
Install: discord:amd64 (0.0.90)
End-Date: 2026-10-30  09:12:20

Start-Date: 2026-10-31  07:00:03
Commandline: apt-get -q -y --no-download upgrade --with-new-pkgs
Install: linux-image-7.0.0-43-generic:amd64 (7.0.0-43.43, automatic)
Upgrade: firefox:amd64 (140.0, 141.0), linux-firmware:amd64 (20260901, 20261001), linux-image-generic:amd64 (7.0.0.42, 7.0.0.43)
End-Date: 2026-10-31  07:01:40

Start-Date: 2026-11-01  07:00:00
Commandline: /usr/bin/unattended-upgrade
Upgrade: openssl:amd64 (3.5.1-1, 3.5.1-2)
End-Date: 2026-11-01  07:00:09
)";
        const QVariantList h = UpdateStatus::parseAptHistory(log);
        QCOMPARE(h.size(), 2);
        // newest first
        const QVariantList last = h.at(0).toMap().value(QStringLiteral("packages")).toList();
        QCOMPARE(last.size(), 1);
        QCOMPARE(last.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("openssl"));
        const QVariantMap first = h.at(1).toMap();
        QCOMPARE(first.value(QStringLiteral("time")).toLongLong(),
                 QDateTime(QDate(2026, 10, 31), QTime(7, 0, 3)).toSecsSinceEpoch());
        const QVariantList pk = first.value(QStringLiteral("packages")).toList();
        QCOMPARE(pk.size(), 3);
        QCOMPARE(pk.at(0).toMap().value(QStringLiteral("from")).toString(), QStringLiteral("140.0"));
        QCOMPARE(pk.at(0).toMap().value(QStringLiteral("to")).toString(), QStringLiteral("141.0"));
        QCOMPARE(pk.at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("system"));
        QCOMPARE(pk.at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("driver"));
        QCOMPARE(pk.at(2).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("kernel"));
        QCOMPARE(UpdateStatus::parseAptHistory(log, 1).size(), 1);
        QVERIFY(UpdateStatus::parseAptHistory(QByteArray()).isEmpty());
    }

    void historyAddsTheFirmwareOfTheLastInstall()
    {
        QFile log(path(QStringLiteral("history.log")));
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Truncate));
        log.write("Start-Date: 2026-10-31  07:00:03\nUpgrade: firefox:amd64 (140.0, 141.0)\nEnd-Date: 2026-10-31  07:01:40\n");
        log.close();
        const qint64 later = QDateTime(QDate(2026, 11, 1), QTime(7, 0)).toSecsSinceEpoch();
        QFile f(path(QStringLiteral("state.json")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{\"version\": 2, \"phase\": \"idle\", \"lastInstall\": {\"time\": " + QByteArray::number(later)
                + ", \"count\": 0, \"firmware\": 1, \"ok\": true}}");
        f.close();
        auto u = make();
        u->setHistoryPath(path(QStringLiteral("history.log")));
        QCOMPARE(u->history().size(), 2);
        QCOMPARE(u->history().first().toMap().value(QStringLiteral("firmware")).toInt(), 1);
        QCOMPARE(u->history().at(1).toMap().value(QStringLiteral("packages")).toList().size(), 1);
    }

    void historyIsReadOnlyWhenAskedFor()
    {
        writeState("idle");
        auto u = make();
        u->setHistoryPath(path(QStringLiteral("later.log")));
        // written after the object was made: nothing was read at start
        QFile log(path(QStringLiteral("later.log")));
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Truncate));
        log.write("Start-Date: 2026-10-31  07:00:03\nUpgrade: firefox:amd64 (140.0, 141.0)\n");
        log.close();
        QCOMPARE(u->history().size(), 1);
    }

    void activeHoursAreChecked()
    {
        QVERIFY(UpdateStatus::validActiveHours(8, 17));
        QVERIFY(UpdateStatus::validActiveHours(22, 6));   // across midnight
        QVERIFY(UpdateStatus::validActiveHours(0, 18));
        QVERIFY(!UpdateStatus::validActiveHours(0, 19)); // 19 hours
        QVERIFY(!UpdateStatus::validActiveHours(8, 8));
        QVERIFY(!UpdateStatus::validActiveHours(-1, 5));
        QVERIFY(!UpdateStatus::validActiveHours(8, 24));
    }

    void optionsGoThroughRayeUpdate()
    {
        writeState("ready");
        writeTool(0);
        auto u = make();
        u->pause(2);
        QTRY_COMPARE(calls(), QByteArray("pause 2\n"));
        u->pause(9);                     // refused here, never reaches raye-update
        u->resume();
        QTRY_COMPARE(calls(), QByteArray("pause 2\nresume\n"));
        u->setActiveHours(9, 19);
        QTRY_VERIFY(calls().endsWith("active-hours 9 19\n"));
        u->setActiveHours(0, 20);
        u->setAutoRestart(true);
        QTRY_VERIFY(calls().endsWith("auto-restart on\n"));
        u->cancelInstall();
        QTRY_VERIFY(calls().endsWith("disarm\n"));
        QVERIFY(!calls().contains("active-hours 0 20"));
    }
};

QTEST_GUILESS_MAIN(TestUpdateStatus)
#include "test_updatestatus.moc"
