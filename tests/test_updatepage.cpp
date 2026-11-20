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
    // "Check for updates" pressed: "Checking" at once, before the state file
    // says anything (2026-11-20: a fresh install showed nothing at all)
    void aPressShowsCheckingAtOnce()
    {
        QCOMPARE(state(QStringLiteral("{phase: 'idle', checked: 0, checkRequested: true}")), QStringLiteral("checking"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', checked: 5, pending: [{name: 'a'}], checkRequested: true}")),
                 QStringLiteral("checking"));
        // a download that follows is shown as it is
        QCOMPARE(state(QStringLiteral("{phase: 'downloading', checkRequested: true}")), QStringLiteral("downloading"));
        // the answer came: the page follows the file again
        QCOMPARE(state(QStringLiteral("{phase: 'idle', checked: 5, checkRequested: false}")), QStringLiteral("uptodate"));
    }

    void aPressWithoutAnswerSaysSo()
    {
        QCOMPARE(state(QStringLiteral("{phase: 'idle', checked: 0, checkProblem: 'no-start'}")), QStringLiteral("error"));
        QCOMPARE(state(QStringLiteral("{phase: 'idle', checked: 5, checkProblem: 'failed'}")), QStringLiteral("error"));
        // no password given: back to what was there
        QCOMPARE(state(QStringLiteral("{phase: 'idle', checked: 0, checkProblem: 'cancelled'}")), QStringLiteral("never"));
    }

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

    // the check moves the bar once apt says how far it got
    void checkProgress()
    {
        QCOMPARE(call(QStringLiteral("checkProgress"), QStringLiteral("{phase: 'checking', checkPercent: 40}")).toNumber(), 0.4);
        QCOMPARE(call(QStringLiteral("checkProgress"), QStringLiteral("{phase: 'checking', checkPercent: 0}")).toNumber(), -1.0);
        // a file from before the key
        QCOMPARE(call(QStringLiteral("checkProgress"), QStringLiteral("{phase: 'checking'}")).toNumber(), -1.0);
        QCOMPARE(call(QStringLiteral("checkProgress"), QStringLiteral("{phase: 'idle', checkPercent: 40}")).toNumber(), -1.0);
        QCOMPARE(call(QStringLiteral("checkProgress"), QStringLiteral("null")).toNumber(), -1.0);
    }

    // 0 seconds left means it cannot be told, never "done"
    void timeLeft()
    {
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("0")).toInt(), -1);
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("undefined")).toInt(), -1);
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("-3")).toInt(), -1);
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("12")).toInt(), 0);
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("59")).toInt(), 0);
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("60")).toInt(), 1);
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("61")).toInt(), 2);
        QCOMPARE(call(QStringLiteral("minutesLeft"), QStringLiteral("3600")).toInt(), 60);
    }

    // 74a: the header says a restart is coming when any update or firmware waits for one
    void restartAfter()
    {
        QVERIFY(call(QStringLiteral("restartAfter"), QStringLiteral("{pending: [{restart: false}, {restart: true}], firmware: []}")).toBool());
        QVERIFY(call(QStringLiteral("restartAfter"), QStringLiteral("{pending: [{restart: false}], firmware: [{device: 'x'}]}")).toBool());
        QVERIFY(!call(QStringLiteral("restartAfter"), QStringLiteral("{pending: [{restart: false, state: 'installing'}], firmware: []}")).toBool());
        // an old row with no key waited for the restart
        QVERIFY(call(QStringLiteral("restartAfter"), QStringLiteral("{pending: [{name: 'x'}]}")).toBool());
        QVERIFY(!call(QStringLiteral("restartAfter"), QStringLiteral("null")).toBool());
        // the words on the page follow it
        QFile f(QStringLiteral(SOURCE_DIR "/src/qml/pages/update/UpdatePage.qml"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString page = QString::fromUtf8(f.readAll());
        QVERIFY(page.contains(QStringLiteral("\"%1% done. A restart is needed to finish.\"")));
        QVERIFY(page.contains(QStringLiteral("U.restartAfter(page.status)")));
    }

    void rowState()
    {
        for (const char *known : {"queued", "installing", "installed", "failed"})
            QCOMPARE(call(QStringLiteral("rowState"), QStringLiteral("{state: '%1'}").arg(QLatin1String(known))).toString(), QLatin1String(known));
        QCOMPARE(call(QStringLiteral("rowState"), QStringLiteral("{state: 'strange'}")).toString(), QString());
        QCOMPARE(call(QStringLiteral("rowState"), QStringLiteral("{name: 'x'}")).toString(), QString());
        QCOMPARE(call(QStringLiteral("rowState"), QStringLiteral("null")).toString(), QString());
    }

    // 74c: rows done in use stay on the page with their check mark, once
    // they left the file, as long as the page saw them install
    void installedRowsStay()
    {
        m_js.evaluate(QStringLiteral(
            "var mem = remember({}, [{name: 'firefox', restart: false, state: 'installing'},"
            "                        {name: 'raye-settings', origin: 'Raye:0.5', restart: false, state: 'queued'},"
            "                        {name: 'libc6', restart: true}], 1000)"));
        QCOMPARE(m_js.evaluate(QStringLiteral("Object.keys(mem).sort().join(',')")).toString(), QStringLiteral("firefox,raye-settings"));
        // a row seen before keeps its first time; only a new one makes a new object
        QVERIFY(m_js.evaluate(QStringLiteral("remember(mem, [{name: 'firefox', state: 'installed'}], 2000) === mem")).toBool());
        m_js.evaluate(QStringLiteral("var mem2 = remember(mem, [{name: 'firefox', state: 'installed'}, {name: 'vlc', state: 'queued'}], 2000)"));
        QCOMPARE(m_js.evaluate(QStringLiteral("mem2.firefox.since")).toInt(), 1000);
        QCOMPARE(m_js.evaluate(QStringLiteral("mem2.vlc.since")).toInt(), 2000);
        QVERIFY(m_js.evaluate(QStringLiteral("mem.vlc === undefined")).toBool());
        QCOMPARE(m_js.evaluate(QStringLiteral("remember(mem, [], 3000) === mem")).toBool(), true);
        // the run ended: firefox left pending, libc6 waits for the restart
        m_js.evaluate(QStringLiteral(
            "var rows = shownRows({pending: [{name: 'libc6', restart: true}],"
            "                      lastInstall: {time: 1500, packages: ['firefox', 'raye-settings']}}, mem2)"));
        QCOMPARE(m_js.evaluate(QStringLiteral("rows.map(r => r.name).join(',')")).toString(), QStringLiteral("firefox,raye-settings,libc6"));
        QCOMPARE(m_js.evaluate(QStringLiteral("rows[0].state")).toString(), QStringLiteral("installed"));
        QCOMPARE(m_js.evaluate(QStringLiteral("rows[1].origin")).toString(), QStringLiteral("Raye:0.5"));
        // an install from before the page saw the rows says nothing about them
        QCOMPARE(m_js.evaluate(QStringLiteral(
            "shownRows({pending: [], lastInstall: {time: 500, packages: ['firefox']}}, mem2).length")).toInt(), 0);
        // an old file without the list
        QCOMPARE(m_js.evaluate(QStringLiteral(
            "shownRows({pending: [{name: 'a'}], lastInstall: {time: 1500}}, mem2).length")).toInt(), 1);
        QCOMPARE(m_js.evaluate(QStringLiteral("shownRows(null, {}).length")).toInt(), 0);
    }

    // 74d: Raye's own packages go together, under a release number when raye-system is in the set
    void rayeGroup()
    {
        m_js.evaluate(QStringLiteral(
            "var g = groupRows([{name: 'firefox', origin: 'mozilla:mozilla'},"
            "                   {name: 'raye-system', origin: 'Raye:0.5/resolute', to: '1:0.5.17~beta1-2'},"
            "                   {name: 'raye-shell', origin: 'Raye:0.5/resolute', to: '0.5.9'},"
            "                   {name: 'libc6', origin: 'Ubuntu:26.04/resolute-security'},"
            "                   {name: 'rayetool', origin: 'Rayed:x'},"
            "                   {name: 'nothing'}])"));
        QCOMPARE(m_js.evaluate(QStringLiteral("g.raye.map(r => r.name).join(',')")).toString(), QStringLiteral("raye-system,raye-shell"));
        QCOMPARE(m_js.evaluate(QStringLiteral("g.other.map(r => r.name).join(',')")).toString(), QStringLiteral("firefox,libc6,rayetool,nothing"));
        QCOMPARE(call(QStringLiteral("rayeRelease"), QStringLiteral("g.raye")).toString(), QStringLiteral("0.5.17"));
        QCOMPARE(call(QStringLiteral("rayeRelease"), QStringLiteral("[{name: 'raye-shell', origin: 'Raye', to: '0.5.9'}]")).toString(), QString());
        QCOMPARE(call(QStringLiteral("rayeRelease"), QStringLiteral("[{name: 'raye-system', origin: 'Raye', to: '0.6'}]")).toString(), QStringLiteral("0.6"));
        QCOMPARE(call(QStringLiteral("rayeRelease"), QStringLiteral("[]")).toString(), QString());
    }

    void groupProgress()
    {
        m_js.evaluate(QStringLiteral(
            "var p = groupProgress([{state: 'installed'}, {state: 'installing'}, {state: 'queued'}, {state: 'failed'}, {name: 'x'}])"));
        QCOMPARE(m_js.evaluate(QStringLiteral("p.done")).toInt(), 2);
        QCOMPARE(m_js.evaluate(QStringLiteral("p.total")).toInt(), 4);
        QCOMPARE(m_js.evaluate(QStringLiteral("groupProgress([{name: 'x'}]).total")).toInt(), 0);
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
