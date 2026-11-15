// SPDX-License-Identifier: GPL-3.0-or-later
// "Use the security chip again": the disk password goes to the helper on
// its standard input and nowhere else (not in its arguments, not in its
// environment), with one newline after it, and the page learns how it
// went from the exit code. A fake raye-kernel-setup stands in for the
// real one and writes down what it was given.
#include "tpm.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestTpm : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString path(const QString &name) const { return m_dir.filePath(name); }

    QByteArray read(const QString &name) const
    {
        QFile f(path(name));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // A raye-kernel-setup that records its arguments, its environment and
    // its standard input, then answers with `code` (or with its stdin
    // compared to "expected": 0 when equal, 1 otherwise).
    QString fakeHelper(const QString &code)
    {
        const QString script = path(QStringLiteral("raye-kernel-setup"));
        QFile f(script);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        const QString dir = m_dir.path();
        f.write(QStringLiteral("#!/bin/sh\n"
                               "d='%1'\n"
                               "printf '%s\\n' \"$@\" > \"$d/argv\"\n"
                               "env > \"$d/env\"\n"
                               "cat > \"$d/stdin\"\n"
                               "case \"$1\" in\n"
                               "tpm-status) cat \"$d/status\" 2>/dev/null; exit 0 ;;\n"
                               "esac\n"
                               "code=%2\n"
                               "if [ \"$code\" = compare ]; then cmp -s \"$d/stdin\" \"$d/expected\" && exit 0; exit 1; fi\n"
                               "exit $code\n")
                    .arg(dir, code)
                    .toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return script;
    }

    void write(const QString &name, const QByteArray &data)
    {
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
    }

    void reset()
    {
        for (const char *name : {"argv", "env", "stdin", "expected", "status"})
            QFile::remove(path(QString::fromLatin1(name)));
    }

    // runs a rebind with the fake answering `code`; returns the result
    QString rebindWith(const QString &code, const QString &password, bool *started = nullptr)
    {
        Tpm tpm;
        const QString helper = fakeHelper(code);
        tpm.setRebindCommand({helper, QStringLiteral("rebind")});
        // the state is asked again after a rebind: not through the fake,
        // whose notes must stay those of the rebind
        tpm.setStatusCommand({path(QStringLiteral("not-there")), QStringLiteral("tpm-status")});
        tpm.setNeedsPasswordFlag(path(QStringLiteral("no-flag")));
        QSignalSpy finished(&tpm, &Tpm::rebindFinished);
        const bool ok = tpm.rebind(password);
        if (started)
            *started = ok;
        QSignalSpy refreshed(&tpm, &Tpm::refreshed);
        if (ok) {
            if (!finished.wait(10000))
                return QStringLiteral("timeout");
            // the state is asked again after every rebind
            if (refreshed.isEmpty() && !refreshed.wait(10000))
                return QStringLiteral("not refreshed");
        }
        if (finished.isEmpty())
            return QStringLiteral("no signal");
        const QString result = finished.first().first().toString();
        if (result != tpm.result())
            return QStringLiteral("result property differs");
        if (tpm.busy())
            return QStringLiteral("still busy");
        return result;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void init()
    {
        reset();
    }

    // the commands a real computer runs: the state without a password,
    // the rebind through pkexec
    void defaultCommands()
    {
        Tpm tpm;
        QCOMPARE(tpm.statusCommand(),
                 (QStringList{QStringLiteral("/usr/libexec/raye-kernel-setup"), QStringLiteral("tpm-status")}));
        QCOMPARE(tpm.rebindCommand(), (QStringList{QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-kernel-setup"),
                                                   QStringLiteral("rebind")}));
        QCOMPARE(tpm.state(), QStringLiteral("unknown"));
        QVERIFY(!tpm.busy());
        QCOMPARE(tpm.result(), QString());
    }

    void parseStatus()
    {
        QCOMPARE(Tpm::parseStatus("ok\n"), QStringLiteral("ok"));
        QCOMPARE(Tpm::parseStatus("stale"), QStringLiteral("stale"));
        QCOMPARE(Tpm::parseStatus("  none \n"), QStringLiteral("none"));
        // an older helper may say it the long way
        QCOMPARE(Tpm::parseStatus("tpm-needs-password\n"), QStringLiteral("stale"));
        // only the first line counts
        QCOMPARE(Tpm::parseStatus("ok\nsomething else\n"), QStringLiteral("ok"));
        QCOMPARE(Tpm::parseStatus(""), QStringLiteral("unknown"));
        QCOMPARE(Tpm::parseStatus("usage: raye-kernel-setup status|enroll\n"), QStringLiteral("unknown"));
        QCOMPARE(Tpm::parseStatus("OK"), QStringLiteral("unknown"));
    }

    void statusFromHelper()
    {
        Tpm tpm;
        const QString helper = fakeHelper(QStringLiteral("0"));
        tpm.setStatusCommand({helper, QStringLiteral("tpm-status")});
        tpm.setNeedsPasswordFlag(path(QStringLiteral("no-flag")));
        write(QStringLiteral("status"), "stale\n");
        QSignalSpy changed(&tpm, &Tpm::stateChanged);
        tpm.refresh();
        QVERIFY(changed.wait(10000));
        QCOMPARE(tpm.state(), QStringLiteral("stale"));
        QCOMPARE(read(QStringLiteral("argv")), QByteArray("tpm-status\n"));
        // nothing is fed to it: its stdin is empty
        QCOMPARE(read(QStringLiteral("stdin")), QByteArray());

        write(QStringLiteral("status"), "ok\n");
        tpm.refresh();
        QVERIFY(changed.wait(10000));
        QCOMPARE(tpm.state(), QStringLiteral("ok"));
    }

    // a helper that does not know tpm-status yet: the boot check's flag
    // still tells that the chip no longer opens the disk
    void statusFromFlag()
    {
        Tpm tpm;
        const QString helper = fakeHelper(QStringLiteral("0"));
        tpm.setStatusCommand({helper, QStringLiteral("tpm-status")});
        write(QStringLiteral("status"), "usage: raye-kernel-setup status|enroll\n");
        write(QStringLiteral("needs-password"), "");
        tpm.setNeedsPasswordFlag(path(QStringLiteral("needs-password")));
        QSignalSpy changed(&tpm, &Tpm::stateChanged);
        tpm.refresh();
        QVERIFY(changed.wait(10000));
        QCOMPARE(tpm.state(), QStringLiteral("stale"));
        QFile::remove(path(QStringLiteral("needs-password")));
    }

    void statusWithoutHelper()
    {
        Tpm tpm;
        tpm.setStatusCommand({path(QStringLiteral("not-there")), QStringLiteral("tpm-status")});
        tpm.setNeedsPasswordFlag(path(QStringLiteral("no-flag")));
        QSignalSpy refreshed(&tpm, &Tpm::refreshed);
        tpm.refresh();
        QVERIFY(refreshed.wait(10000));
        QCOMPARE(tpm.state(), QStringLiteral("unknown"));
    }

    // the password reaches stdin with one newline, then the pipe closes
    void rebindSendsPasswordOnStdin()
    {
        write(QStringLiteral("expected"), "correct horse battery\n");
        QCOMPARE(rebindWith(QStringLiteral("compare"), QStringLiteral("correct horse battery")), QStringLiteral("done"));
        QCOMPARE(read(QStringLiteral("stdin")), QByteArray("correct horse battery\n"));
        // the arguments are the command only
        QCOMPARE(read(QStringLiteral("argv")), QByteArray("rebind\n"));
        // and the password is not in the environment either
        const QByteArray env = read(QStringLiteral("env"));
        QVERIFY(!env.isEmpty());
        QVERIFY(!env.contains("horse"));
    }

    // spaces are part of a password: nothing is trimmed
    void rebindKeepsSpaces()
    {
        write(QStringLiteral("expected"), "  two  spaces \n");
        QCOMPARE(rebindWith(QStringLiteral("compare"), QStringLiteral("  two  spaces ")), QStringLiteral("done"));
    }

    void rebindSendsUtf8()
    {
        write(QStringLiteral("expected"), QStringLiteral("pässwörd€\n").toUtf8());
        QCOMPARE(rebindWith(QStringLiteral("compare"), QStringLiteral("pässwörd€")), QStringLiteral("done"));
    }

    void wrongPassword()
    {
        write(QStringLiteral("expected"), "right\n");
        QCOMPARE(rebindWith(QStringLiteral("compare"), QStringLiteral("wrong")), QStringLiteral("wrong-password"));
    }

    // pkexec: the password prompt was closed, or not allowed
    void cancelled()
    {
        QCOMPARE(rebindWith(QStringLiteral("126"), QStringLiteral("x")), QStringLiteral("cancelled"));
    }

    void otherFailures()
    {
        QCOMPARE(rebindWith(QStringLiteral("127"), QStringLiteral("x")), QStringLiteral("failed"));
        QCOMPARE(rebindWith(QStringLiteral("2"), QStringLiteral("x")), QStringLiteral("failed"));
    }

    void helperMissing()
    {
        Tpm tpm;
        tpm.setRebindCommand({path(QStringLiteral("not-there")), QStringLiteral("rebind")});
        tpm.setStatusCommand({path(QStringLiteral("not-there")), QStringLiteral("tpm-status")});
        tpm.setNeedsPasswordFlag(path(QStringLiteral("no-flag")));
        QSignalSpy finished(&tpm, &Tpm::rebindFinished);
        if (tpm.rebind(QStringLiteral("x")))
            QVERIFY(finished.wait(10000));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(tpm.result(), QStringLiteral("failed"));
        QVERIFY(!tpm.busy());
    }

    // A line break would end the password early in the helper (it reads
    // one line): refuse it rather than send half of it. An empty one is
    // not sent either.
    void refusesWhatCannotBeSent_data()
    {
        QTest::addColumn<QString>("password");
        QTest::newRow("empty") << QString();
        QTest::newRow("newline") << QStringLiteral("abc\ndef");
        QTest::newRow("trailing newline") << QStringLiteral("abc\n");
        QTest::newRow("carriage return") << QStringLiteral("abc\rdef");
        QTest::newRow("nul") << QString(QStringLiteral("ab") + QChar(0) + QStringLiteral("c"));
        QTest::newRow("too long") << QString(5000, QLatin1Char('a'));
    }
    void refusesWhatCannotBeSent()
    {
        QFETCH(QString, password);
        bool started = true;
        QCOMPARE(rebindWith(QStringLiteral("0"), password, &started), QStringLiteral("wrong-password"));
        QVERIFY(!started);
        // the helper never ran
        QVERIFY(!QFile::exists(path(QStringLiteral("argv"))));
    }

    void oneAtATime()
    {
        Tpm tpm;
        // a helper that waits a little before answering
        const QString script = path(QStringLiteral("slow"));
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\ncat >/dev/null\nsleep 1\nexit 0\n");
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        tpm.setRebindCommand({script});
        tpm.setStatusCommand({path(QStringLiteral("not-there"))});
        tpm.setNeedsPasswordFlag(path(QStringLiteral("no-flag")));
        QSignalSpy finished(&tpm, &Tpm::rebindFinished);
        QVERIFY(tpm.rebind(QStringLiteral("first")));
        QVERIFY(tpm.busy());
        QVERIFY(!tpm.rebind(QStringLiteral("second")));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(tpm.result(), QStringLiteral("done"));
        tpm.clearResult();
        QCOMPARE(tpm.result(), QString());
    }
};

QTEST_GUILESS_MAIN(TestTpm)
#include "test_tpm.moc"
