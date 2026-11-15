// SPDX-License-Identifier: GPL-3.0-or-later
// System > Troubleshoot: the runner with fake helpers (what it shows for
// each line, the password asked once for the fixes that need it, a
// refused password, a helper that is gone or hangs), then the real
// helper of data/libexec with fake system commands on PATH.
#include "troubleshoot.h"

#include <KLocalizedString>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString realHelper = QStringLiteral(SOURCE_DIR "/data/libexec/troubleshoot");

void writeScript(const QString &path, const QByteArray &body)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("#!/bin/sh\n" + body);
    f.close();
    QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// wait until the runner stops in one of the resting states
bool settle(Troubleshooter &t, int ms = 15000)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        if (!t.busy())
            return true;
        QTest::qWait(20);
    }
    return false;
}

QString statusOf(const Troubleshooter &t, const QString &id)
{
    const auto steps = t.steps();
    for (const QVariant &v : steps) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id)
            return m.value(QStringLiteral("status")).toString();
    }
    return QString();
}

QString detailOf(const Troubleshooter &t, const QString &id)
{
    const auto steps = t.steps();
    for (const QVariant &v : steps) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id)
            return m.value(QStringLiteral("detail")).toString();
    }
    return QString();
}

// The real helper run with fake commands: PATH starts with `bin`, the
// helper is told it is under test, its / is `root` and its uid `uid`.
struct HelperRun {
    QString out;
    int code = -1;
};

HelperRun runHelper(const QString &bin, const QString &home, const QString &root, const QStringList &args,
                    const QString &uid = QStringLiteral("1000"), const QStringList &extraEnv = {})
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), bin + QStringLiteral(":/usr/bin:/bin:/usr/sbin:/sbin"));
    env.insert(QStringLiteral("HOME"), home);
    env.remove(QStringLiteral("XDG_DATA_HOME"));
    env.insert(QStringLiteral("RAYE_TROUBLESHOOT_TEST"), QStringLiteral("1"));
    env.insert(QStringLiteral("RAYE_TROUBLESHOOT_UID"), uid);
    env.insert(QStringLiteral("RAYE_TROUBLESHOOT_ROOT"), root);
    env.insert(QStringLiteral("FAKE_LOG"), home + QStringLiteral("/calls.log"));
    for (const QString &kv : extraEnv)
        env.insert(kv.section(QLatin1Char('='), 0, 0), kv.section(QLatin1Char('='), 1));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("sh"), QStringList{realHelper} + args);
    p.waitForFinished(30000);
    return {QString::fromUtf8(p.readAllStandardOutput()), p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1};
}

} // namespace

class TestTroubleshoot : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString path(const QString &name) const { return m_dir.filePath(name); }

    // A fake helper: "check" prints what $dir/check-<n> holds, the n-th
    // time it is run (the last file once they run out); "fix" prints
    // "fixed" for every remedy, or what $dir/fix-out holds.
    void fakeHelper(const QStringList &checkRuns, const QByteArray &fixOut = QByteArray())
    {
        for (int i = 0; i < checkRuns.size(); ++i) {
            QFile f(path(QStringLiteral("check-%1").arg(i + 1)));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(checkRuns.at(i).toUtf8());
        }
        QFile::remove(path(QStringLiteral("runs")));
        QFile::remove(path(QStringLiteral("fix-out")));
        QFile::remove(path(QStringLiteral("helper.log")));
        if (!fixOut.isNull()) {
            QFile f(path(QStringLiteral("fix-out")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(fixOut);
        }
        const QByteArray dir = m_dir.path().toUtf8();
        writeScript(path(QStringLiteral("helper")),
                    "d='" + dir + "'\n"
                    "echo \"$(id -u) $*\" >> \"$d/helper.log\"\n"
                    "case \"$1\" in\n"
                    "check)\n"
                    "  n=$(cat \"$d/runs\" 2>/dev/null || echo 0); n=$((n + 1)); echo $n > \"$d/runs\"\n"
                    "  f=\"$d/check-$n\"; [ -f \"$f\" ] || f=$(ls \"$d\"/check-* | sort | tail -n 1)\n"
                    "  cat \"$f\" ;;\n"
                    "fix)\n"
                    "  shift\n"
                    "  if [ -f \"$d/fix-out\" ]; then cat \"$d/fix-out\"; else for r in \"$@\"; do echo \"fixed $r\"; done; fi ;;\n"
                    "esac\n");
    }

    // pkexec that records how it was called, then runs the rest (or
    // answers like a dismissed password dialog)
    void fakePkexec(int exitCode = 0)
    {
        const QByteArray dir = m_dir.path().toUtf8();
        QFile::remove(path(QStringLiteral("pkexec.log")));
        writeScript(path(QStringLiteral("pkexec")),
                    "echo \"$*\" >> '" + dir + "/pkexec.log'\n"
                    + (exitCode ? "exit " + QByteArray::number(exitCode) + "\n" : QByteArray("exec \"$@\"\n")));
    }

    void prepare(Troubleshooter &t, const QString &name = QStringLiteral("audio"))
    {
        t.setHelper(path(QStringLiteral("helper")));
        t.setPkexec(path(QStringLiteral("pkexec")));
        t.setName(name);
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        QVERIFY(QFile::exists(realHelper));
    }

    void parsesLines()
    {
        using L = Troubleshooter::Line;
        L l = Troubleshooter::parseLine(QStringLiteral("begin audio-service"));
        QCOMPARE(l.kind, L::Begin);
        QCOMPARE(l.id, QStringLiteral("audio-service"));

        l = Troubleshooter::parseLine(QStringLiteral("check audio-volume problem unmute user"));
        QCOMPARE(l.kind, L::Check);
        QCOMPARE(l.result, QStringLiteral("problem"));
        QCOMPARE(l.remedy, QStringLiteral("unmute"));
        QCOMPARE(l.level, Troubleshooter::Level::User);

        l = Troubleshooter::parseLine(QStringLiteral("check network-service problem restart-network admin\r"));
        QCOMPARE(l.level, Troubleshooter::Level::Admin);
        l = Troubleshooter::parseLine(QStringLiteral("check audio-output problem - none"));
        QCOMPARE(l.level, Troubleshooter::Level::None);
        QCOMPARE(l.remedy, QStringLiteral("-"));
        QCOMPARE(Troubleshooter::parseLine(QStringLiteral("check a ok")).result, QStringLiteral("ok"));
        QCOMPARE(Troubleshooter::parseLine(QStringLiteral("check a unknown")).result, QStringLiteral("unknown"));
        QCOMPARE(Troubleshooter::parseLine(QStringLiteral("fixed unmute")).kind, L::Fixed);
        QCOMPARE(Troubleshooter::parseLine(QStringLiteral("failed unmute")).kind, L::Failed);

        // anything else is noise from a tool, never a step
        for (const char *bad : {"", "begin", "begin Audio", "begin a b", "check a", "check a maybe",
                                "check a problem unmute", "check a problem unmute root",
                                "check a ok extra", "check ../x ok", "check a;rm ok", "fixed",
                                "fixed un mute", "Volume: 0.40 [MUTED]", "check a problem $(x) user"}) {
            QCOMPARE(Troubleshooter::parseLine(QString::fromLatin1(bad)).kind, L::Invalid);
        }
    }

    // every troubleshooter has a name, and only those are accepted
    void names()
    {
        QCOMPARE(Troubleshooter::names(), (QStringList{QStringLiteral("audio"), QStringLiteral("network"),
                                                        QStringLiteral("printer"), QStringLiteral("update"),
                                                        QStringLiteral("bluetooth"), QStringLiteral("display")}));
        Troubleshooter t;
        for (const QString &n : Troubleshooter::names()) {
            t.setName(n);
            QCOMPARE(t.name(), n);
            QVERIFY(!t.title().isEmpty());
        }
        t.setName(QStringLiteral("audio"));
        t.setName(QStringLiteral("../../bin/sh"));
        QCOMPARE(t.name(), QStringLiteral("audio"));
    }

    void nothingWrong()
    {
        fakeHelper({QStringLiteral("begin audio-service\ncheck audio-service ok\n"
                                   "begin audio-output\ncheck audio-output ok\n")});
        fakePkexec();
        Troubleshooter t;
        prepare(t);
        QSignalSpy steps(&t, &Troubleshooter::stepsChanged);
        t.run();
        QCOMPARE(t.state(), QStringLiteral("checking"));
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("done"));
        QCOMPARE(t.resultStatus(), QStringLiteral("ok"));
        QVERIFY(!t.resultTitle().isEmpty());
        QCOMPARE(t.steps().size(), 2);
        QCOMPARE(statusOf(t, QStringLiteral("audio-service")), QStringLiteral("ok"));
        QVERIFY(!detailOf(t, QStringLiteral("audio-service")).isEmpty());
        QVERIFY(steps.count() >= 2);
        QVERIFY(!QFile::exists(path(QStringLiteral("pkexec.log"))));
    }

    // a problem with a fix: the page asks, the fix runs as the user,
    // the check runs again and the step ends "fixed"
    void fixAsUser()
    {
        fakeHelper({QStringLiteral("begin audio-service\ncheck audio-service ok\n"
                                   "begin audio-volume\ncheck audio-volume problem unmute user\n"),
                    QStringLiteral("begin audio-service\ncheck audio-service ok\n"
                                   "begin audio-volume\ncheck audio-volume ok\n")});
        fakePkexec();
        Troubleshooter t;
        prepare(t);
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("found"));
        QCOMPARE(statusOf(t, QStringLiteral("audio-volume")), QStringLiteral("problem"));
        QCOMPARE(t.remedies().size(), 1);
        QCOMPARE(t.remedies().first().toMap().value(QStringLiteral("remedy")).toString(), QStringLiteral("unmute"));
        QVERIFY(!t.remedies().first().toMap().value(QStringLiteral("text")).toString().isEmpty());
        QVERIFY(!t.needsAdmin());

        t.fix();
        QVERIFY(t.busy());
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("done"));
        QCOMPARE(statusOf(t, QStringLiteral("audio-volume")), QStringLiteral("fixed"));
        QCOMPARE(statusOf(t, QStringLiteral("audio-service")), QStringLiteral("ok"));
        QCOMPARE(t.resultStatus(), QStringLiteral("fixed"));
        QVERIFY(!QFile::exists(path(QStringLiteral("pkexec.log"))));
        const QStringList calls = readAll(path(QStringLiteral("helper.log"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(calls.size(), 3);
        QVERIFY(calls.at(1).endsWith(QStringLiteral(" fix unmute")));
    }

    // user fixes as the user, the admin ones in one pkexec call that
    // names the helper by its full path
    void fixWithAdmin()
    {
        fakeHelper({QStringLiteral("begin network-service\ncheck network-service problem restart-network admin\n"
                                   "begin network-enabled\ncheck network-enabled problem enable-networking user\n"
                                   "begin network-wifi\ncheck network-wifi problem wifi-on user\n"),
                    QStringLiteral("begin network-service\ncheck network-service ok\n"
                                   "begin network-enabled\ncheck network-enabled ok\n"
                                   "begin network-wifi\ncheck network-wifi problem wifi-on user\n")});
        fakePkexec();
        Troubleshooter t;
        prepare(t, QStringLiteral("network"));
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("found"));
        QVERIFY(t.needsAdmin());
        QCOMPARE(t.remedies().size(), 3);
        t.fix();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("done"));

        const QStringList pk = readAll(path(QStringLiteral("pkexec.log"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(pk.size(), 1);
        QCOMPARE(pk.first(), path(QStringLiteral("helper")) + QStringLiteral(" fix restart-network"));
        const QString log = readAll(path(QStringLiteral("helper.log")));
        QVERIFY(log.contains(QStringLiteral(" fix enable-networking wifi-on\n")));

        QCOMPARE(statusOf(t, QStringLiteral("network-service")), QStringLiteral("fixed"));
        QCOMPARE(statusOf(t, QStringLiteral("network-enabled")), QStringLiteral("fixed"));
        QCOMPARE(statusOf(t, QStringLiteral("network-wifi")), QStringLiteral("notFixed"));
        QCOMPARE(t.resultStatus(), QStringLiteral("partly"));
        QVERIFY(!detailOf(t, QStringLiteral("network-wifi")).isEmpty());
    }

    // the password dialog was closed: nothing ran as root, and the
    // result says why
    void adminRefused()
    {
        fakeHelper({QStringLiteral("begin bluetooth-service\ncheck bluetooth-service problem restart-bluetooth admin\n")});
        fakePkexec(126);
        Troubleshooter t;
        prepare(t, QStringLiteral("bluetooth"));
        t.run();
        QVERIFY(settle(t));
        t.fix();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("done"));
        QCOMPARE(statusOf(t, QStringLiteral("bluetooth-service")), QStringLiteral("notFixed"));
        QCOMPARE(t.resultStatus(), QStringLiteral("notFixed"));
        QVERIFY(t.resultText().contains(QStringLiteral("root access")));
        QVERIFY(!readAll(path(QStringLiteral("helper.log"))).contains(QStringLiteral("fix")));
    }

    // a fix that says it failed, and a problem with only advice
    void failedFixAndAdvice()
    {
        fakeHelper({QStringLiteral("begin audio-output\ncheck audio-output problem - none\n"
                                   "begin audio-service\ncheck audio-service problem restart-audio user\n")},
                   QByteArray("failed restart-audio\n"));
        fakePkexec();
        Troubleshooter t;
        prepare(t);
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("found"));
        QCOMPARE(t.remedies().size(), 1); // advice is not something fix() does
        t.fix();
        QVERIFY(settle(t));
        QCOMPARE(statusOf(t, QStringLiteral("audio-service")), QStringLiteral("notFixed"));
        QCOMPARE(statusOf(t, QStringLiteral("audio-output")), QStringLiteral("notFixed"));
        QVERIFY(!detailOf(t, QStringLiteral("audio-output")).isEmpty());
        QCOMPARE(t.resultStatus(), QStringLiteral("notFixed"));
    }

    // only advice: nothing to ask, the result comes at once
    void onlyAdvice()
    {
        fakeHelper({QStringLiteral("begin printer-found\ncheck printer-found problem add-printer none\n")});
        Troubleshooter t;
        prepare(t, QStringLiteral("printer"));
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("done"));
        QCOMPARE(statusOf(t, QStringLiteral("printer-found")), QStringLiteral("notFixed"));
        QCOMPARE(detailOf(t, QStringLiteral("printer-found")), Troubleshooter::adviceText(QStringLiteral("printer-found"), QStringLiteral("add-printer")));
        QCOMPARE(t.resultStatus(), QStringLiteral("notFixed"));
    }

    // checks the page does not know, tool noise and odd ids stay out
    void unknownLinesIgnored()
    {
        fakeHelper({QStringLiteral("Volume: 0.40\nbegin made-up-check\ncheck made-up-check problem made-up user\n"
                                   "begin audio-service\nwarning: something\ncheck audio-service ok\n"
                                   "check audio-volume problem rm-rf user\n")});
        Troubleshooter t;
        prepare(t);
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(t.steps().size(), 1);
        QCOMPARE(t.state(), QStringLiteral("done"));
    }

    // a check that started and never answered counts as "unknown"
    void unfinishedCheck()
    {
        fakeHelper({QStringLiteral("begin audio-service\ncheck audio-service ok\nbegin audio-volume\n")});
        Troubleshooter t;
        prepare(t);
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(statusOf(t, QStringLiteral("audio-volume")), QStringLiteral("unknown"));
        QCOMPARE(t.state(), QStringLiteral("done"));
    }

    void helperMissing()
    {
        Troubleshooter t;
        t.setHelper(path(QStringLiteral("no-such-helper")));
        t.setName(QStringLiteral("audio"));
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("failed"));
        QVERIFY(!t.resultTitle().isEmpty());
    }

    void helperHangs()
    {
        writeScript(path(QStringLiteral("slow")), "echo 'begin audio-service'\nexec sleep 30\n");
        Troubleshooter t;
        t.setHelper(path(QStringLiteral("slow")));
        t.setName(QStringLiteral("audio"));
        t.setTimeouts(500, 500);
        t.run();
        QVERIFY(settle(t, 5000));
        QCOMPARE(t.state(), QStringLiteral("failed"));
    }

    void cancel()
    {
        writeScript(path(QStringLiteral("slow")), "echo 'begin audio-service'\nexec sleep 30\n");
        Troubleshooter t;
        t.setHelper(path(QStringLiteral("slow")));
        t.setName(QStringLiteral("audio"));
        t.run();
        QTRY_COMPARE(t.steps().size(), 1);
        t.cancel();
        QCOMPARE(t.state(), QStringLiteral("idle"));
        QVERIFY(t.steps().isEmpty());
        // and it can run again
        fakeHelper({QStringLiteral("begin audio-service\ncheck audio-service ok\n")});
        prepare(t);
        t.run();
        QVERIFY(settle(t));
        QCOMPARE(t.state(), QStringLiteral("done"));
    }

    // fix() does nothing unless there is something to fix
    void fixOnlyWhenFound()
    {
        Troubleshooter t;
        t.fix();
        QCOMPARE(t.state(), QStringLiteral("idle"));
    }

    // ---- the real helper, with fake commands -----------------------

    void helperRefusesOddInput()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        HelperRun r = runHelper(home.path(), home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("nosuch")});
        QCOMPARE(r.code, 2);
        QVERIFY(r.out.isEmpty());
        r = runHelper(home.path(), home.path(), home.path(), {QStringLiteral("rm"), QStringLiteral("-rf")});
        QCOMPARE(r.code, 2);
        r = runHelper(home.path(), home.path(), home.path(),
                      {QStringLiteral("fix"), QStringLiteral("unmute; touch ") + home.filePath(QStringLiteral("owned"))});
        QCOMPARE(r.out, QStringLiteral("failed unmute; touch %1\n").arg(home.filePath(QStringLiteral("owned"))));
        QVERIFY(!QFile::exists(home.filePath(QStringLiteral("owned"))));
    }

    // a fix runs only as who it is meant for: admin ones as root, the
    // others as the person (systemctl --user as root would be wrong)
    void helperChecksWhoRunsAFix()
    {
        QTemporaryDir home;
        const QString bin = home.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        writeScript(bin + QStringLiteral("/systemctl"), "echo \"systemctl $*\" >> \"$FAKE_LOG\"\n");
        writeScript(bin + QStringLiteral("/wpctl"), "echo \"wpctl $*\" >> \"$FAKE_LOG\"\necho 'Volume: 0.40'\n");
        writeScript(bin + QStringLiteral("/nm-online"), "exit 0\n");
        HelperRun r = runHelper(bin, home.path(), home.path(), {QStringLiteral("fix"), QStringLiteral("restart-network")});
        QCOMPARE(r.out, QStringLiteral("failed restart-network\n"));
        QVERIFY(readAll(home.filePath(QStringLiteral("calls.log"))).isEmpty());
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("fix"), QStringLiteral("unmute")}, QStringLiteral("0"));
        QCOMPARE(r.out, QStringLiteral("failed unmute\n"));
        QVERIFY(readAll(home.filePath(QStringLiteral("calls.log"))).isEmpty());

        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("fix"), QStringLiteral("restart-network")}, QStringLiteral("0"));
        QCOMPARE(r.out, QStringLiteral("fixed restart-network\n"));
        QVERIFY(readAll(home.filePath(QStringLiteral("calls.log"))).contains(QStringLiteral("systemctl restart NetworkManager.service")));
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("fix"), QStringLiteral("unmute")});
        QCOMPARE(r.out, QStringLiteral("fixed unmute\n"));
        QVERIFY(readAll(home.filePath(QStringLiteral("calls.log"))).contains(QStringLiteral("wpctl set-mute @DEFAULT_AUDIO_SINK@ 0")));
    }

    void helperAudio()
    {
        QTemporaryDir home;
        const QString bin = home.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        // services running, a real output, but muted
        writeScript(bin + QStringLiteral("/systemctl"), "exit 0\n");
        writeScript(bin + QStringLiteral("/wpctl"),
                    "case \"$1\" in\n"
                    "inspect) echo '  node.name = \"alsa_output.pci\"' ;;\n"
                    "get-volume) echo 'Volume: 0.40 [MUTED]' ;;\n"
                    "esac\n");
        HelperRun r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("audio")});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, QStringLiteral("begin audio-service\ncheck audio-service ok\n"
                                       "begin audio-output\ncheck audio-output ok\n"
                                       "begin audio-volume\ncheck audio-volume problem unmute user\n"));

        // services down, nothing to play on
        writeScript(bin + QStringLiteral("/systemctl"), "exit 3\n");
        writeScript(bin + QStringLiteral("/wpctl"),
                    "case \"$1\" in\n"
                    "inspect) echo '  node.name = \"auto_null\"' ;;\n"
                    "get-volume) echo 'Volume: 0.00' ;;\n"
                    "esac\n");
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("audio")});
        QVERIFY(r.out.contains(QStringLiteral("check audio-service problem restart-audio user\n")));
        QVERIFY(r.out.contains(QStringLiteral("check audio-output problem plug-in-audio none\n")));
        QVERIFY(r.out.contains(QStringLiteral("check audio-volume problem unmute user\n")));
    }

    void helperNetwork()
    {
        QTemporaryDir home;
        const QString bin = home.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        writeScript(bin + QStringLiteral("/systemctl"), "exit 0\n");
        writeScript(bin + QStringLiteral("/nmcli"),
                    "case \"$*\" in\n"
                    "networking) echo enabled ;;\n"
                    "'-t -f TYPE device') printf 'ethernet\\nwifi\\nloopback\\n' ;;\n"
                    "'radio wifi') echo disabled ;;\n"
                    "'networking connectivity check') echo portal ;;\n"
                    "esac\n");
        HelperRun r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("network")});
        QCOMPARE(r.out, QStringLiteral("begin network-service\ncheck network-service ok\n"
                                       "begin network-enabled\ncheck network-enabled ok\n"
                                       "begin network-wifi\ncheck network-wifi problem wifi-on user\n"
                                       "begin network-connected\ncheck network-connected problem sign-in none\n"));
        // no Wi-Fi card: no Wi-Fi step; no connection at all
        writeScript(bin + QStringLiteral("/nmcli"),
                    "case \"$*\" in\n"
                    "networking) echo disabled ;;\n"
                    "'-t -f TYPE device') printf 'ethernet\\n' ;;\n"
                    "'networking connectivity check') echo none ;;\n"
                    "esac\n");
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("network")});
        QVERIFY(!r.out.contains(QStringLiteral("network-wifi")));
        QVERIFY(r.out.contains(QStringLiteral("check network-enabled problem enable-networking user\n")));
        QVERIFY(r.out.contains(QStringLiteral("check network-connected problem restart-network admin\n")));
    }

    void helperBluetooth()
    {
        QTemporaryDir home;
        const QString bin = home.filePath(QStringLiteral("bin"));
        const QString root = home.filePath(QStringLiteral("root"));
        QDir().mkpath(bin);
        QDir().mkpath(root + QStringLiteral("/sys/class/bluetooth/hci0"));
        writeScript(bin + QStringLiteral("/systemctl"), "exit 0\n");
        writeScript(bin + QStringLiteral("/rfkill"),
                    "printf '1: hci0: Bluetooth\\n\\tSoft blocked: yes\\n\\tHard blocked: no\\n'\n");
        writeScript(bin + QStringLiteral("/bluetoothctl"), "printf 'Controller 00:11 (public)\\n\\tPowered: no\\n'\n");
        HelperRun r = runHelper(bin, home.path(), root, {QStringLiteral("check"), QStringLiteral("bluetooth")});
        QCOMPARE(r.out, QStringLiteral("begin bluetooth-service\ncheck bluetooth-service ok\n"
                                       "begin bluetooth-adapter\ncheck bluetooth-adapter ok\n"
                                       "begin bluetooth-blocked\ncheck bluetooth-blocked problem unblock-bluetooth user\n"
                                       "begin bluetooth-powered\ncheck bluetooth-powered problem power-bluetooth user\n"));
        // switched off by the hardware key, and no adapter at all
        writeScript(bin + QStringLiteral("/rfkill"),
                    "printf '1: hci0: Bluetooth\\n\\tSoft blocked: no\\n\\tHard blocked: yes\\n'\n");
        QDir(root + QStringLiteral("/sys/class/bluetooth/hci0")).removeRecursively();
        r = runHelper(bin, home.path(), root, {QStringLiteral("check"), QStringLiteral("bluetooth")});
        QVERIFY(r.out.contains(QStringLiteral("check bluetooth-adapter problem no-adapter none\n")));
        QVERIFY(r.out.contains(QStringLiteral("check bluetooth-blocked problem hardware-switch none\n")));
        QVERIFY(!r.out.contains(QStringLiteral("bluetooth-powered")));
    }

    void helperPrinter()
    {
        QTemporaryDir home;
        const QString bin = home.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        writeScript(bin + QStringLiteral("/systemctl"), "exit 0\n");
        writeScript(bin + QStringLiteral("/lpstat"),
                    "case \"$1\" in\n"
                    "-e) printf 'Office_Laser\\nHome\\n' ;;\n"
                    "-p) printf 'printer Office_Laser disabled since Fri 31 Oct -\\n\\treason\\n"
                    "printer Home is idle.  enabled since Fri\\nprinter bad$(name) disabled since x\\n' ;;\n"
                    "esac\n");
        writeScript(bin + QStringLiteral("/cupsenable"), "echo \"cupsenable $*\" >> \"$FAKE_LOG\"\n");
        writeScript(bin + QStringLiteral("/cupsaccept"), "echo \"cupsaccept $*\" >> \"$FAKE_LOG\"\n");
        HelperRun r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("printer")});
        QCOMPARE(r.out, QStringLiteral("begin printer-service\ncheck printer-service ok\n"
                                       "begin printer-found\ncheck printer-found ok\n"
                                       "begin printer-paused\ncheck printer-paused problem resume-printers admin\n"));
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("fix"), QStringLiteral("resume-printers")}, QStringLiteral("0"));
        QCOMPARE(r.out, QStringLiteral("fixed resume-printers\n"));
        // only the printer with a plain name, nothing that looks like a command
        QCOMPARE(readAll(home.filePath(QStringLiteral("calls.log"))),
                 QStringLiteral("cupsenable Office_Laser\ncupsaccept Office_Laser\n"));

        writeScript(bin + QStringLiteral("/lpstat"), "exit 0\n");
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("printer")});
        QVERIFY(r.out.contains(QStringLiteral("check printer-found problem add-printer none\n")));
        QVERIFY(!r.out.contains(QStringLiteral("printer-paused")));
    }

    void helperDisplay()
    {
        QTemporaryDir home;
        const QString bin = home.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        writeScript(bin + QStringLiteral("/kscreen-doctor"),
                    "echo '{\"outputs\": [{\"name\": \"eDP-1\", \"connected\": true, \"enabled\": true},"
                    " {\"name\": \"HDMI-1\", \"connected\": true, \"enabled\": false},"
                    " {\"name\": \"DP-2\", \"connected\": false, \"enabled\": false}]}'\n");
        writeScript(bin + QStringLiteral("/busctl"), "echo \"busctl $*\" >> \"$FAKE_LOG\"\n");
        HelperRun r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("display")});
        QCOMPARE(r.out, QStringLiteral("begin display-screens\ncheck display-screens problem reset-display user\n"));

        // the saved layouts are moved aside, never deleted
        const QString saved = home.filePath(QStringLiteral(".local/share/kscreen"));
        QDir().mkpath(saved + QStringLiteral("/outputs"));
        QFile f(saved + QStringLiteral("/abc123"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("fix"), QStringLiteral("reset-display")});
        QCOMPARE(r.out, QStringLiteral("fixed reset-display\n"));
        QVERIFY(!QFile::exists(saved));
        QVERIFY(QFile::exists(home.filePath(QStringLiteral(".local/share/kscreen.before-reset/abc123"))));
        QVERIFY(readAll(home.filePath(QStringLiteral("calls.log"))).contains(QStringLiteral("loadModule s kscreen")));

        writeScript(bin + QStringLiteral("/kscreen-doctor"),
                    "echo '{\"outputs\": [{\"name\": \"eDP-1\", \"connected\": true, \"enabled\": true}]}'\n");
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("display")});
        QCOMPARE(r.out, QStringLiteral("begin display-screens\ncheck display-screens ok\n"));
        writeScript(bin + QStringLiteral("/kscreen-doctor"), "exit 1\n");
        r = runHelper(bin, home.path(), home.path(), {QStringLiteral("check"), QStringLiteral("display")});
        QCOMPARE(r.out, QStringLiteral("begin display-screens\ncheck display-screens unknown\n"));
    }

    void helperUpdate()
    {
        QTemporaryDir home;
        const QString bin = home.filePath(QStringLiteral("bin"));
        const QString root = home.filePath(QStringLiteral("root"));
        QDir().mkpath(bin);
        QDir().mkpath(root + QStringLiteral("/var/lib/raye-update"));
        QDir().mkpath(root + QStringLiteral("/var/lib/dpkg/updates"));
        writeScript(bin + QStringLiteral("/systemctl"), "echo \"systemctl $*\" >> \"$FAKE_LOG\"\nexit 0\n");
        writeScript(bin + QStringLiteral("/df"),
                    "echo 'Filesystem 1024-blocks Used Available Capacity Mounted on'\n"
                    "echo \"/dev/x 100000000 10000000 ${FAKE_AVAIL:-50000000} 10% /\"\n");
        writeScript(bin + QStringLiteral("/dpkg"), "echo \"dpkg $*\" >> \"$FAKE_LOG\"\n");
        {
            QFile s(root + QStringLiteral("/var/lib/raye-update/state.json"));
            QVERIFY(s.open(QIODevice::WriteOnly));
            s.write("{\n  \"version\": 2,\n  \"phase\": \"idle\"\n}\n");
        }
        HelperRun r = runHelper(bin, home.path(), root, {QStringLiteral("check"), QStringLiteral("update")});
        QCOMPARE(r.out, QStringLiteral("begin update-service\ncheck update-service ok\n"
                                       "begin update-state\ncheck update-state ok\n"
                                       "begin update-packages\ncheck update-packages ok\n"
                                       "begin update-space\ncheck update-space ok\n"));

        {
            QFile s(root + QStringLiteral("/var/lib/raye-update/state.json"));
            QVERIFY(s.open(QIODevice::WriteOnly | QIODevice::Truncate));
            s.write("{\"version\": 2, \"phase\": \"error\", \"error\": {\"code\": \"apt\"}}");
            QFile u(root + QStringLiteral("/var/lib/dpkg/updates/0001"));
            QVERIFY(u.open(QIODevice::WriteOnly));
        }
        r = runHelper(bin, home.path(), root, {QStringLiteral("check"), QStringLiteral("update")},
                      QStringLiteral("1000"), {QStringLiteral("FAKE_AVAIL=500000")});
        QVERIFY(r.out.contains(QStringLiteral("check update-state problem repair-updates admin\n")));
        QVERIFY(r.out.contains(QStringLiteral("check update-packages problem repair-updates admin\n")));
        QVERIFY(r.out.contains(QStringLiteral("check update-space problem free-space none\n")));

        QFile::remove(home.filePath(QStringLiteral("calls.log")));
        r = runHelper(bin, home.path(), root, {QStringLiteral("fix"), QStringLiteral("repair-updates")}, QStringLiteral("0"));
        QCOMPARE(r.out, QStringLiteral("fixed repair-updates\n"));
        const QString calls = readAll(home.filePath(QStringLiteral("calls.log")));
        QVERIFY(calls.contains(QStringLiteral("dpkg --configure -a")));
        QVERIFY(calls.contains(QStringLiteral("systemctl enable --now raye-update.timer")));
        QVERIFY(calls.contains(QStringLiteral("systemctl start --no-block raye-update.service")));
    }

    // every check and remedy the helper can print has its words in
    // the page, and every remedy is one the helper can run
    void wordsForEverything()
    {
        const QString script = readAll(realHelper);
        static const QRegularExpression said(QStringLiteral("\\b(?:result|problem) ([a-z0-9-]+)(?: ([a-z0-9-]+) (user|admin|none))?"));
        static const QRegularExpression began(QStringLiteral("\\bbegin ([a-z0-9-]+)"));
        QSet<QString> checks, remedies;
        for (auto it = began.globalMatch(script); it.hasNext();)
            checks.insert(it.next().captured(1));
        for (auto it = said.globalMatch(script); it.hasNext();) {
            const auto m = it.next();
            if (!m.captured(2).isEmpty() && m.captured(2) != QLatin1String("-"))
                remedies.insert(m.captured(2) + QLatin1Char(' ') + m.captured(3));
        }
        QVERIFY(checks.size() >= 15);
        for (const QString &c : std::as_const(checks)) {
            QVERIFY2(Troubleshooter::knowsCheck(c), qPrintable(c));
            for (const char *status : {"running", "ok", "problem", "fixed", "notFixed", "unknown"})
                QVERIFY2(!Troubleshooter::checkDetail(c, QString::fromLatin1(status)).isEmpty(), qPrintable(c));
        }
        QVERIFY(remedies.size() >= 15);
        for (const QString &r : std::as_const(remedies)) {
            const QString remedy = r.section(QLatin1Char(' '), 0, 0);
            if (r.endsWith(QLatin1String(" none"))) {
                QVERIFY2(!Troubleshooter::adviceText(QString(), remedy).isEmpty(), qPrintable(remedy));
            } else {
                QVERIFY2(Troubleshooter::knowsRemedy(remedy), qPrintable(remedy));
                QVERIFY2(script.contains(QLatin1String("    ") + remedy + QLatin1Char(')')), qPrintable(remedy));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestTroubleshoot)
#include "test_troubleshoot.moc"
