// SPDX-License-Identifier: GPL-3.0-or-later
// Turning disk encryption on or off from Device security: the password
// goes to raye-encryption on its standard input only, the recovery key
// comes back once and can be saved off this disk, and the page reads
// the helper's state. A fake helper writes down what it was given.
#include "diskencryption.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestDiskEncryption : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    const QByteArray key = "123456-234567-345678-456789-567890-678901-789012-890123";

    QString path(const QString &name) const { return m_dir.filePath(name); }

    QByteArray read(const QString &name) const
    {
        QFile f(path(name));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    QString fakeHelper(int code)
    {
        const QString script = path(QStringLiteral("raye-encryption"));
        QFile f(script);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        f.write(QStringLiteral("#!/bin/sh\n"
                               "d='%1'\n"
                               "printf '%s\\n' \"$@\" > \"$d/argv\"\n"
                               "env > \"$d/env\"\n"
                               "cat > \"$d/stdin\"\n"
                               "[ \"$1\" = encrypt ] && [ %2 = 0 ] && echo 'recovery-key %3'\n"
                               "exit %2\n")
                    .arg(m_dir.path())
                    .arg(code)
                    .arg(QString::fromLatin1(key))
                    .toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return script;
    }

private Q_SLOTS:
    void statusFromTheHelper()
    {
        DiskEncryption d;
        QVERIFY(DiskEncryption::applyStatus(&d, R"({"phase":"running","mode":"encrypt","percent":42.5,"blocked":"","error":""})"));
        QCOMPARE(d.phase(), QStringLiteral("running"));
        QCOMPARE(d.percent(), 42.5);
        QVERIFY(d.known());
        QVERIFY(!DiskEncryption::applyStatus(&d, "not json"));
    }

    void recoveryKeyLine()
    {
        QCOMPARE(DiskEncryption::parseRecoveryKey("x\nrecovery-key " + key + "\n"), QString::fromLatin1(key));
        QVERIFY(DiskEncryption::parseRecoveryKey("recovery-key 12-34").isEmpty());
    }

    void exitCodes()
    {
        QString r;
        DiskEncryption::resultFor(0, true, &r);
        QCOMPARE(r, QStringLiteral("started"));
        DiskEncryption::resultFor(3, true, &r);
        QCOMPARE(r, QStringLiteral("wrong-password"));
        DiskEncryption::resultFor(2, true, &r);
        QCOMPARE(r, QStringLiteral("refused"));
        DiskEncryption::resultFor(126, true, &r);
        QCOMPARE(r, QStringLiteral("cancelled"));
        DiskEncryption::resultFor(0, false, &r);
        QCOMPARE(r, QStringLiteral("failed"));
    }

    void passwordOnStandardInputOnly()
    {
        DiskEncryption d;
        d.setHelper({}, {fakeHelper(0)});
        QSignalSpy done(&d, &DiskEncryption::resultChanged);
        QVERIFY(d.turnOn(QStringLiteral("s3cret pass")));
        QVERIFY(done.wait(5000));
        QTRY_COMPARE(d.result(), QStringLiteral("started"));
        QCOMPARE(read(QStringLiteral("stdin")), QByteArray("s3cret pass\n"));
        QCOMPARE(read(QStringLiteral("argv")), QByteArray("encrypt\n"));
        QVERIFY(!read(QStringLiteral("env")).contains("s3cret"));
        QCOMPARE(d.recoveryKey(), QString::fromLatin1(key));
        d.forgetRecoveryKey();
        QVERIFY(d.recoveryKey().isEmpty());
    }

    void wrongPassword()
    {
        DiskEncryption d;
        d.setHelper({}, {fakeHelper(3)});
        QVERIFY(d.turnOff(QStringLiteral("nope")));
        QTRY_COMPARE(d.result(), QStringLiteral("wrong-password"));
        QCOMPARE(read(QStringLiteral("argv")), QByteArray("decrypt\n"));
        QVERIFY(d.recoveryKey().isEmpty());
    }

    void passwordThatCannotBeSent()
    {
        DiskEncryption d;
        d.setHelper({}, {fakeHelper(0)});
        QVERIFY(!d.turnOn(QString()));
        QVERIFY(!d.turnOn(QStringLiteral("two\nlines")));
        QCOMPARE(d.result(), QStringLiteral("wrong-password"));
    }

    void savedKeyIsPrivate()
    {
        DiskEncryption d;
        d.setHelper({}, {fakeHelper(0)});
        d.turnOn(QStringLiteral("pw"));
        QTRY_VERIFY(!d.recoveryKey().isEmpty());
        // the "system disk" is /proc here, so the folder counts as another disk
        d.setSysRoot(QStringLiteral("/proc"));
        const QString file = path(QStringLiteral("key.txt"));
        QCOMPARE(d.saveRecoveryKey(QUrl::fromLocalFile(file)), QString());
        QVERIFY(read(QStringLiteral("key.txt")).contains(key));
        const auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther
                          | QFileDevice::WriteOther;
        QVERIFY(!(QFile::permissions(file) & others));
    }

    void keyNotSavedOnTheDiskItOpens()
    {
        DiskEncryption d;
        d.setHelper({}, {fakeHelper(0)});
        d.turnOn(QStringLiteral("pw"));
        QTRY_VERIFY(!d.recoveryKey().isEmpty());
        d.setSysRoot(m_dir.path());
        QCOMPARE(d.saveRecoveryKey(QUrl::fromLocalFile(path(QStringLiteral("k.txt")))), QStringLiteral("same-disk"));
        QVERIFY(!QFile::exists(path(QStringLiteral("k.txt"))));
    }

    void securityChipVersion()
    {
        DiskEncryption d;
        d.setSysRoot(m_dir.path());
        QCOMPARE(d.tpmVersion(), 0);
        QVERIFY(QDir().mkpath(path(QStringLiteral("sys/class/tpm/tpm0"))));
        QFile f(path(QStringLiteral("sys/class/tpm/tpm0/tpm_version_major")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("2\n");
        f.close();
        QCOMPARE(d.tpmVersion(), 2);
    }
};

QTEST_GUILESS_MAIN(TestDiskEncryption)
#include "test_diskencryption.moc"
