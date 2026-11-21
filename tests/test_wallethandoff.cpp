// SPDX-License-Identifier: GPL-3.0-or-later
// The saved passwords across a desktop switch (wallethandoff.h): the key
// is the one pam_kwallet makes from the sign-in password, the password is
// checked by unix_chkpwd on its standard input (never on the command line),
// and the keeper gives the key out once, then is gone.
#include "wallethandoff.h"

#include <QFile>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestWalletHandoff : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keyIsPamKwalletsKey()
    {
        // PBKDF2-SHA512, 50000 rounds, 56 bytes: what pam_kwallet derives
        // (Python: hashlib.pbkdf2_hmac('sha512', pw, salt, 50000, 56))
        QByteArray salt;
        for (int i = 0; i < 56; ++i)
            salt.append(char(i));
        QCOMPARE(WalletHandoff::deriveKey("correct horse", salt).toHex(),
                 QByteArray("b43be351959d338e9501559068b5dcb4dce55b505def5fdc5f38e3c1284a4ede"
                            "226c6d0a14d25da87bf18bfc4e3b28d3e5351e3a7e86254a"));
    }

    void onlyAWalletTheSignInOpens()
    {
        QTemporaryDir dir;
        QVERIFY(!WalletHandoff::loginWalletExists(dir.path()));
        QFile kwl(dir.filePath(QStringLiteral("kdewallet.kwl")));
        QVERIFY(kwl.open(QIODevice::WriteOnly));
        kwl.close();
        // a wallet with a password of its own has no salt: not ours to open
        QVERIFY(!WalletHandoff::loginWalletExists(dir.path()));
        QFile salt(dir.filePath(QStringLiteral("kdewallet.salt")));
        QVERIFY(salt.open(QIODevice::WriteOnly));
        salt.write(QByteArray(56, 'x'));
        salt.close();
        QVERIFY(WalletHandoff::loginWalletExists(dir.path()));
        QCOMPARE(WalletHandoff::readSalt(dir.path()), QByteArray(56, 'x'));
        QVERIFY(salt.open(QIODevice::WriteOnly));
        salt.write(QByteArray(20, 'x'));
        salt.close();
        QVERIFY(WalletHandoff::readSalt(dir.path()).isEmpty());
    }

    void passwordGoesOnStandardInputOnly()
    {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("log"));
        const QString chk = dir.filePath(QStringLiteral("chkpwd"));
        QFile f(chk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // writes its arguments, then what came on stdin; "good" is right
        f.write(QStringLiteral("#!/bin/sh\necho \"args $*\" > %1\ncat > %1.in\n"
                               "[ \"$(tr -d '\\000' < %1.in)\" = good ]\n").arg(log).toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        int result = -1;
        WalletHandoff::checkPassword(QStringLiteral("me"), QStringLiteral("good"), [&](bool ok) { result = ok; }, chk);
        QTRY_COMPARE(result, 1);
        QFile args(log);
        QVERIFY(args.open(QIODevice::ReadOnly));
        const QByteArray a = args.readAll();
        QCOMPARE(a, QByteArray("args me nullok\n"));
        QVERIFY(!a.contains("good"));
        QFile in(log + QStringLiteral(".in"));
        QVERIFY(in.open(QIODevice::ReadOnly));
        QCOMPARE(in.readAll(), QByteArray("good\0", 5));
        result = -1;
        WalletHandoff::checkPassword(QStringLiteral("me"), QStringLiteral("bad"), [&](bool ok) { result = ok; }, chk);
        QTRY_COMPARE(result, 0);
        result = -1;
        WalletHandoff::checkPassword(QStringLiteral("me"), QString(), [&](bool ok) { result = ok; }, chk);
        QCOMPARE(result, 0);
        result = -1;
        WalletHandoff::checkPassword(QStringLiteral("me"), QStringLiteral("good"), [&](bool ok) { result = ok; },
                                     dir.filePath(QStringLiteral("none")));
        QTRY_COMPARE(result, 0);
    }

    void keeperGivesTheKeyOnceThenIsGone()
    {
        QTemporaryDir runtime;
        QFile::setPermissions(runtime.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("XDG_RUNTIME_DIR", runtime.path().toLocal8Bit());
        const QString binary = QStringLiteral(BUILD_DIR "/bin/raye-settings");
        if (!QFile::exists(binary))
            QSKIP("raye-settings is not built");
        QProcess keeper;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime.path());
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        keeper.setProcessEnvironment(env);
        keeper.start(binary, {QStringLiteral("--wallet-keep")});
        QVERIFY(keeper.waitForStarted());
        const QString path = WalletHandoff::socketPath();
        QVERIFY(path.startsWith(runtime.path()));
        QTRY_VERIFY(QFile::exists(path));
        QVERIFY(!(QFile::permissions(path) & (QFile::ReadOther | QFile::WriteOther | QFile::ReadGroup | QFile::WriteGroup)));
        auto ask = [&](const QByteArray &line) {
            QLocalSocket s;
            s.connectToServer(path);
            if (!s.waitForConnected(2000))
                return QByteArray("(no keeper)");
            s.write(line);
            s.waitForBytesWritten(1000);
            s.waitForReadyRead(3000);
            return s.readAll().trimmed();
        };
        const QByteArray key(56, 'k');
        QCOMPARE(ask("get\n"), QByteArray("no"));                       // nothing yet
        QCOMPARE(ask("put " + QByteArray(10, 'k').toBase64() + "\n"), QByteArray("no")); // not a key
        QCOMPARE(ask("put " + key.toBase64() + "\n"), QByteArray("ok"));
        QCOMPARE(ask("put " + QByteArray(56, 'z').toBase64() + "\n"), QByteArray("no")); // only one
        QCOMPARE(QByteArray::fromBase64(ask("get\n")), key);
        QVERIFY(keeper.waitForFinished(5000));
        QCOMPARE(ask("get\n"), QByteArray("(no keeper)"));
        QVERIFY(!QFile::exists(path));
    }

    void openerWithoutKeeperDoesNothing()
    {
        QTemporaryDir runtime;
        const QString binary = QStringLiteral(BUILD_DIR "/bin/raye-settings");
        if (!QFile::exists(binary))
            QSKIP("raye-settings is not built");
        // keep() takes its time limit from the caller; the program uses 180 s.
        // Here: the opener with no keeper says nothing and does nothing.
        QProcess opener;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime.path());
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        opener.setProcessEnvironment(env);
        opener.start(binary, {QStringLiteral("--wallet-open")});
        QVERIFY(opener.waitForFinished(5000));
        QCOMPARE(opener.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(TestWalletHandoff)
#include "test_wallethandoff.moc"
