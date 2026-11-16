// SPDX-License-Identifier: GPL-3.0-or-later
// What the Raye Update page shows for each state of the state file, the
// download progress, and the history sorted by kind
// (src/qml/pages/update/updatetext.js).
#include <QFile>
#include <QJSEngine>
#include <QTest>

class TestUpdatePage : public QObject
{
    Q_OBJECT

private:
    QJSEngine m_js;

    QJSValue call(const QString &function, const QString &arguments)
    {
        const QJSValue v = m_js.evaluate(function + QLatin1Char('(') + arguments + QLatin1Char(')'));
        if (v.isError())
            qWarning("%s", qPrintable(v.toString()));
        return v;
    }

    QString state(const QString &u, const QString &now = QStringLiteral("1000000"))
    {
        return call(QStringLiteral("pageState"), u + QLatin1Char(',') + now).toString();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QFile f(QStringLiteral(SOURCE_DIR "/src/qml/pages/update/updatetext.js"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        // ".pragma library" is for the QML engine only
        const QString code = QString::fromUtf8(f.readAll()).replace(QStringLiteral(".pragma library"), QString());
        const QJSValue r = m_js.evaluate(code);
        QVERIFY2(!r.isError(), qPrintable(r.toString()));
    }

    // A RotationAnimator leaves the icon at the angle it stopped at: an
    // icon that both turned and showed the result came out with a tilted
    // check mark after every check. The turning icon only ever shows the
    // spinner; the status sits in an icon that never turns.
    void onlyTheSpinnerTurns_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("page") << QStringLiteral("src/qml/pages/update/UpdatePage.qml");
        QTest::newRow("card") << QStringLiteral("src/qml/frame/UpdateCard.qml");
    }

    void onlyTheSpinnerTurns()
    {
        QFETCH(QString, file);
        QFile f(QStringLiteral(SOURCE_DIR "/") + file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList icons = QString::fromUtf8(f.readAll()).split(QStringLiteral("Kirigami.Icon {"));
        int turning = 0;
        for (const QString &icon : icons.mid(1)) {
            if (!icon.contains(QStringLiteral("RotationAnimator")))
                continue;
            ++turning;
            QVERIFY2(icon.contains(QStringLiteral("source: \"org.raye.settings-status-updating\"")), qPrintable(icon.left(400)));
            QVERIFY2(!icon.contains(QStringLiteral("status-ok")) && !icon.contains(QStringLiteral("status-warning")),
                     qPrintable(icon.left(400)));
        }
        QCOMPARE(turning, 1);
    }

    void states()
    {
        QCOMPARE(state(QStringLiteral("null")), QStringLiteral("never"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 0, checked: 0}")), QStringLiteral("never"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 0, checked: 5}")), QStringLiteral("uptodate"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 3, checked: 5}")), QStringLiteral("available"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 0, firmware: [{}], checked: 5}")), QStringLiteral("available"));
        QCOMPARE(state(QStringLiteral("{phase: 'downloading', count: 3}")), QStringLiteral("downloading"));
        QCOMPARE(state(QStringLiteral("{phase: 'checking'}")), QStringLiteral("checking"));
        QCOMPARE(state(QStringLiteral("{phase: 'waiting', count: 2}")), QStringLiteral("waiting"));
        QCOMPARE(state(QStringLiteral("{phase: 'error'}")), QStringLiteral("error"));
        QCOMPARE(state(QStringLiteral("{phase: 'ready', restartNeeded: true}")), QStringLiteral("restart"));
        // a kernel from unattended-upgrades asks for a restart too
        QCOMPARE(state(QStringLiteral("{phase: 'idle', restartNeeded: true}")), QStringLiteral("restart"));
        QCOMPARE(state(QStringLiteral("{phase: 'armed', state: 'ready', restartNeeded: true}")), QStringLiteral("armed"));
        // a pause wins while it lasts, and ends on its own
        QCOMPARE(state(QStringLiteral("{phase: 'paused', pausedUntil: 2000}"), QStringLiteral("1000000")), QStringLiteral("paused"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', pausedUntil: 900, checked: 5}"), QStringLiteral("1000000")),
                 QStringLiteral("uptodate"));
        // the list counts, not a stale count next to it
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 3, pending: [], firmware: [], checked: 5}")), QStringLiteral("uptodate"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 0, pending: [{}], firmware: [], checked: 5}")), QStringLiteral("available"));
        // a version 1 file has no phase
        QCOMPARE(state(QStringLiteral("{state: 'downloading'}")), QStringLiteral("downloading"));
    }

    // Downloaded updates that need no restart are installed, not restarted for
    void installsWithoutARestart()
    {
        // right after the download they install on their own: no "Install now"
        const QString live = QStringLiteral("{phase: 'ready', state: 'idle', restartNeeded: false, pending: [{name: 'firefox', restart: false}], firmware: []}");
        QCOMPARE(state(live), QStringLiteral("installing"));
        // waiting for the charger (or a metered network, a game): the button
        QCOMPARE(state(QStringLiteral("{phase: 'ready', waitReason: 'battery', pending: [{name: 'firefox', restart: false}]}")), QStringLiteral("install"));
        QCOMPARE(state(QStringLiteral("{phase: 'installing', installingNow: true, pending: [{restart: false}]}")), QStringLiteral("installing"));
        // updates that install without a restart come first: no "Restart
        // now" while some of them are still to install
        QCOMPARE(state(QStringLiteral("{phase: 'ready', restartNeeded: true, pending: [{restart: false}, {restart: true}]}")), QStringLiteral("installing"));
        // once only the restart ones are left, the restart
        QCOMPARE(state(QStringLiteral("{phase: 'ready', restartNeeded: true, pending: [{restart: true}]}")), QStringLiteral("restart"));
        // "Restart now" waits a moment after installing, never on its own
        QVERIFY(call(QStringLiteral("holdsRestart"), QStringLiteral("'installing', 'restart'")).toBool());
        QVERIFY(call(QStringLiteral("holdsRestart"), QStringLiteral("'downloading', 'restart'")).toBool());
        QVERIFY(!call(QStringLiteral("holdsRestart"), QStringLiteral("'uptodate', 'restart'")).toBool());
        QVERIFY(!call(QStringLiteral("holdsRestart"), QStringLiteral("'installing', 'uptodate'")).toBool());
        // a file from before 0.3: everything waited for the restart
        QVERIFY(call(QStringLiteral("needsRestart"), QStringLiteral("{name: 'x'}")).toBool());
        QVERIFY(call(QStringLiteral("needsRestart"), QStringLiteral("{restart: true}")).toBool());
        QVERIFY(!call(QStringLiteral("needsRestart"), QStringLiteral("{restart: false}")).toBool());
    }

    void firmwareAlreadyCurrent_data()
    {
        QTest::addColumn<QString>("have");
        QTest::addColumn<QString>("offered");
        QTest::addColumn<bool>("current");
        QTest::newRow("dbx just written") << QStringLiteral("20260707") << QStringLiteral("20260707") << true;
        QTest::newRow("newer than offered") << QStringLiteral("1.58.0") << QStringLiteral("1.56.0") << true;
        QTest::newRow("behind") << QStringLiteral("1.56.0") << QStringLiteral("1.58.0") << false;
        QTest::newRow("no version read") << QString() << QStringLiteral("20260707") << false;
        QTest::newRow("different shape") << QStringLiteral("A12") << QStringLiteral("1.2") << false;
    }

    void firmwareAlreadyCurrent()
    {
        QFETCH(QString, have);
        QFETCH(QString, offered);
        QFETCH(bool, current);
        QCOMPARE(call(QStringLiteral("firmwareCurrent"), QStringLiteral("'%1', '%2'").arg(have, offered)).toBool(), current);
    }

    // every update that waits for a restart says so in the list, by name
    void restartIsWrittenOnTheRowsThatNeedIt()
    {
        QFile f(QStringLiteral(SOURCE_DIR "/src/qml/pages/update/UpdatePage.qml"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString page = QString::fromUtf8(f.readAll());
        QVERIFY(page.contains(QStringLiteral("U.needsRestart(modelData)")));
        QVERIFY(page.contains(QStringLiteral("\"@info tag on an update\", \"Restart required\"")));
        QVERIFY(page.contains(QStringLiteral("installNow()")));
    }

    // Under 40 % battery "Install now" stays, and the page says why
    void installNowOnALowBatterySaysWhy()
    {
        QCOMPARE(call(QStringLiteral("installWait"), QStringLiteral("{phase: 'ready', reason: 'battery'}")).toString(), QStringLiteral("battery"));
        QCOMPARE(call(QStringLiteral("installWait"), QStringLiteral("{phase: 'ready', reason: ''}")).toString(), QString());
        QCOMPARE(call(QStringLiteral("installWait"), QStringLiteral("null")).toString(), QString());
        for (const char *file : {"src/qml/pages/update/UpdatePage.qml", "src/qml/frame/UpdateCard.qml"}) {
            QFile f(QStringLiteral(SOURCE_DIR "/") + QLatin1String(file));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(f.readAll());
            QVERIFY2(text.contains(QStringLiteral("Plug in your PC to install these updates.")), file);
            QVERIFY2(text.contains(QStringLiteral("installWait")), file);
        }
    }

    // Nothing to install and a repository unread looks exactly like a PC
    // that is up to date (update/docs/state.md): the file says which it is.
    void uptodateOnlyWhenEverythingWasRead()
    {
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 0, checked: 5, checkedEverything: true}")), QStringLiteral("uptodate"));
        // a file from before the key: only the all-clear case wrote those
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 0, checked: 5}")), QStringLiteral("uptodate"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 0, checked: 5, checkedEverything: false}")), QStringLiteral("incomplete"));
        // the updates that were found are real: they stay what the page shows
        QCOMPARE(state(QStringLiteral("{phase: 'idle', count: 2, checked: 5, checkedEverything: false}")), QStringLiteral("available"));
        QCOMPARE(state(QStringLiteral("{phase: 'ready', state: 'ready', restartNeeded: true, checkedEverything: false}")), QStringLiteral("restart"));
        QCOMPARE(state(QStringLiteral("{phase: 'error', checked: 5, checkedEverything: false}")), QStringLiteral("error"));
    }

    void theSourceThatWasNotRead_data()
    {
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("notice");
        const QString raye = QStringLiteral("{phase: 'idle', checkedEverything: false, brokenSources: [{uri: '%1', problem: 'missing'}]}");
        QTest::newRow("all read") << QStringLiteral("{phase: 'idle', checkedEverything: true, brokenSources: []}") << QString();
        QTest::newRow("old file") << QStringLiteral("{phase: 'idle'}") << QString();
        QTest::newRow("Raye mirror list") << raye.arg(QStringLiteral("mirror+file:/etc/apt/mirrors/raye.list")) << QStringLiteral("raye");
        QTest::newRow("Raye host") << raye.arg(QStringLiteral("https://dl.rayelinux.com/apt/")) << QStringLiteral("raye");
        QTest::newRow("Raye second host") << raye.arg(QStringLiteral("https://raye-linux.github.io/packages/")) << QStringLiteral("raye");
        QTest::newRow("another source") << QStringLiteral("{phase: 'idle', checkedEverything: false, brokenSources: [{uri: 'https://example.org/x', problem: 'offline'}]}") << QStringLiteral("offline");
        QTest::newRow("a name that only looks like Raye") << QStringLiteral("{phase: 'idle', checkedEverything: false, brokenSources: [{uri: 'https://rayepics.example/x', problem: 'invalid'}]}") << QStringLiteral("invalid");
        QTest::newRow("a word we do not know") << QStringLiteral("{phase: 'idle', checkedEverything: false, brokenSources: [{uri: 'https://example.org/x', problem: 'strange'}]}") << QStringLiteral("unknown");
        QTest::newRow("the flag alone") << QStringLiteral("{phase: 'idle', checkedEverything: false, brokenSources: []}") << QStringLiteral("unknown");
        QTest::newRow("Raye second of two") << QStringLiteral("{phase: 'idle', brokenSources: [{uri: 'https://example.org/x', problem: 'offline'}, {uri: 'mirror+file:/etc/apt/mirrors/raye.list', problem: 'missing'}]}") << QStringLiteral("raye");
        // a check is running: what it found last time is about to be replaced
        QTest::newRow("while checking") << QString(raye.arg(QStringLiteral("https://dl.rayelinux.com/apt/"))).replace(QStringLiteral("'idle'"), QStringLiteral("'checking'")) << QString();
        // the whole check failed: the error says it, in its own words
        QTest::newRow("error") << QString(raye.arg(QStringLiteral("https://dl.rayelinux.com/apt/"))).replace(QStringLiteral("'idle'"), QStringLiteral("'error'")) << QString();
    }

    void theSourceThatWasNotRead()
    {
        QFETCH(QString, status);
        QFETCH(QString, notice);
        QCOMPARE(call(QStringLiteral("sourceNotice"), status).toString(), notice);
    }

    void firmwareAction()
    {
        const auto action = [this](const QString &u) {
            return call(QStringLiteral("firmwareAction"), u + QStringLiteral(", 'dbx', 1000000")).toString();
        };
        QCOMPARE(action(QStringLiteral("null")), QStringLiteral("download"));
        QCOMPARE(action(QStringLiteral("{phase: 'idle', checked: 5, firmware: []}")), QStringLiteral("download"));
        QCOMPARE(action(QStringLiteral("{phase: 'downloading', firmware: [{deviceId: 'dbx'}]}")), QStringLiteral("working"));
        QCOMPARE(action(QStringLiteral("{phase: 'ready', restartNeeded: true, firmware: [{deviceId: 'dbx'}]}")), QStringLiteral("restart"));
        QCOMPARE(action(QStringLiteral("{phase: 'armed', firmware: [{deviceId: 'dbx'}]}")), QStringLiteral("armed"));
        // the set waiting for a restart does not hold this device yet
        QCOMPARE(action(QStringLiteral("{phase: 'ready', restartNeeded: true, firmware: [{deviceId: 'other'}]}")), QStringLiteral("download"));
    }

    void progress()
    {
        QCOMPARE(call(QStringLiteral("downloadProgress"), QStringLiteral("{downloadedBytes: 25, totalBytes: 100}")).toNumber(), 0.25);
        QCOMPARE(call(QStringLiteral("downloadProgress"), QStringLiteral("{downloadedBytes: 250, totalBytes: 100}")).toNumber(), 1.0);
        QCOMPARE(call(QStringLiteral("downloadProgress"), QStringLiteral("{downloadedBytes: 0, totalBytes: 0}")).toNumber(), -1.0);
        QCOMPARE(call(QStringLiteral("downloadProgress"), QStringLiteral("null")).toNumber(), -1.0);
    }

    void historyByKind()
    {
        const QJSValue g = call(QStringLiteral("historyGroups"), QStringLiteral(
            "[{time: 300, firmware: 1, packages: []},"
            " {time: 200, packages: [{name: 'firefox', to: '141', kind: 'system'}, {name: 'linux-image-generic', to: '7.0.43', kind: 'kernel'}]},"
            " {time: 100, packages: [{name: 'openssl', to: '3.5', kind: 'system'}, {name: 'nvidia-driver-580', to: '580.1', kind: 'driver'}]}]"));
        QCOMPARE(g.property(QStringLiteral("length")).toInt(), 4);
        QCOMPARE(g.property(0).property(QStringLiteral("kind")).toString(), QStringLiteral("kernel"));
        QCOMPARE(g.property(1).property(QStringLiteral("kind")).toString(), QStringLiteral("driver"));
        QCOMPARE(g.property(2).property(QStringLiteral("kind")).toString(), QStringLiteral("firmware"));
        QCOMPARE(g.property(2).property(QStringLiteral("rows")).property(0).property(QStringLiteral("devices")).toInt(), 1);
        const QJSValue system = g.property(3).property(QStringLiteral("rows"));
        QCOMPARE(system.property(QStringLiteral("length")).toInt(), 2);
        // newest first
        QCOMPARE(system.property(0).property(QStringLiteral("name")).toString(), QStringLiteral("firefox"));
        QCOMPARE(system.property(1).property(QStringLiteral("version")).toString(), QStringLiteral("3.5"));
        QCOMPARE(call(QStringLiteral("historyGroups"), QStringLiteral("[]")).property(QStringLiteral("length")).toInt(), 0);
    }

    void activeHours()
    {
        QVERIFY(call(QStringLiteral("validActiveHours"), QStringLiteral("8, 17")).toBool());
        QVERIFY(call(QStringLiteral("validActiveHours"), QStringLiteral("22, 6")).toBool());
        QVERIFY(!call(QStringLiteral("validActiveHours"), QStringLiteral("0, 19")).toBool());
        QVERIFY(!call(QStringLiteral("validActiveHours"), QStringLiteral("9, 9")).toBool());
    }
};

QTEST_GUILESS_MAIN(TestUpdatePage)
#include "test_updatepage.moc"
