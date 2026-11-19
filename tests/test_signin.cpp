// SPDX-License-Identifier: GPL-3.0-or-later
// Sign-in options. The password goes to a stand-in for passwd on a real
// terminal, which checks the answers it is given. Nothing here can touch a
// real password. Automatic sign-in is read from configuration files the
// way SDDM reads them; the helper that writes them has test_autologin.sh.
#include "signin.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
void write(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

// Speaks the way the real one does in the C locale, reading with the
// terminal's echo on: it only checks what reaches it.
const char *fakePasswd = R"(#!/bin/sh
printf 'Changing password for me.\n'
printf 'Current password: '
read current
if [ "$current" != "right-one" ]; then
    printf 'passwd: Authentication token manipulation error\npasswd: password unchanged\n'
    exit 10
fi
printf 'New password: '
read new
case $new in
short) printf 'BAD PASSWORD: The password is shorter than 8 characters\nNew password: '; read new; printf 'passwd: Have exhausted maximum number of retries for service\n'; exit 10 ;;
esac
printf 'Retype new password: '
read again
[ "$new" = "$again" ] || { printf 'Sorry, passwords do not match.\npasswd: password unchanged\n'; exit 10; }
printf '%s' "$new" > "$FAKE_RESULT"
printf 'passwd: password updated successfully\n'
)";
}

class TestSignIn : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString fake()
    {
        const QString path = m_dir.filePath(QStringLiteral("fake-passwd"));
        write(path, fakePasswd);
        QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
    std::unique_ptr<SignInSettings> settings(const QStringList &configs = {})
    {
        return std::make_unique<SignInSettings>(configs, QStringList{QStringLiteral("/bin/true")}, fake(),
                                                m_dir.filePath(QStringLiteral("powerrc")), QStringLiteral("me"));
    }

private Q_SLOTS:
    void autoLoginIsReadTheWaySddmReadsIt()
    {
        QTemporaryDir dir;
        const QString conf = dir.filePath(QStringLiteral("conf.d"));
        QVERIFY(QDir().mkpath(conf));
        QCOMPARE(SignInSettings::autoLoginUserIn({conf}), QString());

        write(conf + QStringLiteral("/a.conf"), "[Autologin]\nUser=first\n[Other]\nUser=notthisone\n");
        QCOMPARE(SignInSettings::autoLoginUserIn({conf}), QStringLiteral("first"));
        // later names win
        write(conf + QStringLiteral("/b.conf"), "[Autologin]\nUser=second\n");
        QCOMPARE(SignInSettings::autoLoginUserIn({conf}), QStringLiteral("second"));
        // and the main file comes after the folder
        const QString main = dir.filePath(QStringLiteral("sddm.conf"));
        write(main, "[Autologin]\nUser=\n");
        QCOMPARE(SignInSettings::autoLoginUserIn({conf, main}), QString());
        // a longer key is not User
        write(main, "[Autologin]\nUserName=someoneelse\n");
        QCOMPARE(SignInSettings::autoLoginUserIn({conf, main}), QStringLiteral("second"));
    }

    void autoLoginIsOnOnlyForMe()
    {
        QTemporaryDir dir;
        const QString main = dir.filePath(QStringLiteral("sddm.conf"));
        write(main, "[Autologin]\nUser=me\n");
        QVERIFY(settings({main})->autoLogin());
        write(main, "[Autologin]\nUser=someoneelse\n");
        auto other = settings({main});
        QVERIFY(!other->autoLogin());
        QCOMPARE(other->autoLoginUser(), QStringLiteral("someoneelse"));
    }

    void helperIsRunAndStateRereadAfter()
    {
        QTemporaryDir dir;
        const QString main = dir.filePath(QStringLiteral("sddm.conf"));
        const QString script = dir.filePath(QStringLiteral("helper"));
        // the argument must reach it; writes what SDDM would then read
        write(script, QStringLiteral("#!/bin/sh\n[ \"$1\" = on ] && printf '[Autologin]\\nUser=me\\n' > '%1'\nexit 0\n").arg(main).toUtf8());
        QFile(script).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        SignInSettings signin({main}, {script}, fake(), m_dir.filePath(QStringLiteral("powerrc")), QStringLiteral("me"));
        QVERIFY(!signin.autoLogin());
        signin.setAutoLogin(true);
        QVERIFY(signin.autoLoginBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!signin.autoLoginBusy(), 3000);
        QVERIFY(signin.autoLogin());
        QVERIFY(!signin.autoLoginFailed());
    }

    void aHelperThatChangesNothingIsAFailure()
    {
        // it ends well, yet SDDM would still read the other way
        QTemporaryDir dir;
        const QString main = dir.filePath(QStringLiteral("sddm.conf"));
        const QString script = dir.filePath(QStringLiteral("helper"));
        write(script, "#!/bin/sh\nexit 0\n");
        QFile(script).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        SignInSettings signin({main}, {script}, fake(), m_dir.filePath(QStringLiteral("powerrc")), QStringLiteral("me"));
        signin.setAutoLogin(true);
        QTRY_VERIFY_WITH_TIMEOUT(!signin.autoLoginBusy(), 3000);
        QVERIFY(!signin.autoLogin());
        QVERIFY(signin.autoLoginFailed());

        write(main, "[Autologin]\nUser=me\n");
        signin.refresh();
        signin.setAutoLogin(false);
        QTRY_VERIFY_WITH_TIMEOUT(!signin.autoLoginBusy(), 3000);
        QVERIFY(signin.autoLogin());
        QVERIFY(signin.autoLoginFailed());
    }

    void closingThePasswordPromptIsNotAFailure()
    {
        QTemporaryDir dir;
        const QString script = dir.filePath(QStringLiteral("helper"));
        write(script, "#!/bin/sh\nexit 126\n");
        QFile(script).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        SignInSettings signin({}, {script}, fake(), m_dir.filePath(QStringLiteral("powerrc")), QStringLiteral("me"));
        signin.setAutoLogin(true);
        QTRY_VERIFY_WITH_TIMEOUT(!signin.autoLoginBusy(), 3000);
        QVERIFY(!signin.autoLoginFailed());

        write(script, "#!/bin/sh\nexit 1\n");
        signin.setAutoLogin(true);
        QTRY_VERIFY_WITH_TIMEOUT(!signin.autoLoginBusy(), 3000);
        QVERIFY(signin.autoLoginFailed());
    }

    void passwordReachesPasswdOnATerminal()
    {
        const QString result = m_dir.filePath(QStringLiteral("result"));
        qputenv("FAKE_RESULT", result.toLocal8Bit());
        auto signin = settings();
        QSignalSpy changed(signin.get(), &SignInSettings::passwordChanged);
        signin->changePassword(QStringLiteral("right-one"), QStringLiteral("brand-new-phrase"));
        QVERIFY(signin->passwordBusy());
        QVERIFY(changed.wait(5000));
        QVERIFY(!signin->passwordBusy());
        QCOMPARE(signin->passwordError(), QString());
        QFile written(result);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("brand-new-phrase"));
    }

    void wrongCurrentPasswordIsSaid()
    {
        qputenv("FAKE_RESULT", m_dir.filePath(QStringLiteral("no")).toLocal8Bit());
        auto signin = settings();
        signin->changePassword(QStringLiteral("wrong"), QStringLiteral("brand-new-phrase"));
        QTRY_VERIFY_WITH_TIMEOUT(!signin->passwordBusy(), 5000);
        QVERIFY(signin->passwordError().contains(QLatin1String("current password")));
    }

    void rejectedNewPasswordEndsInsteadOfWaiting()
    {
        qputenv("FAKE_RESULT", m_dir.filePath(QStringLiteral("no")).toLocal8Bit());
        auto signin = settings();
        signin->changePassword(QStringLiteral("right-one"), QStringLiteral("short"));
        QTRY_VERIFY_WITH_TIMEOUT(!signin->passwordBusy(), 5000);
        QVERIFY(!signin->passwordError().isEmpty());
    }

    void anUnknownQuestionEndsInsteadOfHanging()
    {
        QTemporaryDir dir;
        const QString script = dir.filePath(QStringLiteral("stuck"));
        write(script, "#!/bin/sh\nprintf 'Something new: '\nread answer\n");
        QFile(script).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        SignInSettings signin({}, {}, script, dir.filePath(QStringLiteral("powerrc")), QStringLiteral("me"));
        signin.passwordTimeout = 200;
        signin.changePassword(QStringLiteral("a"), QStringLiteral("b"));
        QVERIFY(signin.passwordBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!signin.passwordBusy(), 3000);
        QVERIFY(!signin.passwordError().isEmpty());
    }

    void whatPasswdSaysBecomesOurWords_data()
    {
        QTest::addColumn<QString>("output");
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("expected");
        QTest::newRow("good") << "passwd: password updated successfully\n" << 0 << "";
        QTest::newRow("current") << "passwd: Authentication token manipulation error\n" << 10 << "current password";
        QTest::newRow("too short") << "BAD PASSWORD: The password is shorter than 8 characters\n" << 10 << "too short";
        QTest::newRow("pam_unix short") << "You must choose a longer password.\n" << 10 << "too short";
        QTest::newRow("dictionary") << "BAD PASSWORD: it is based on a dictionary word\n" << 10 << "too easy";
        QTest::newRow("same") << "Password unchanged\n" << 10 << "different";
        QTest::newRow("anything else") << "something odd\n" << 1 << "could not be changed";
        // exit 0 without the word: not a success to trust
        QTest::newRow("silent") << "" << 0 << "could not be changed";
    }
    void whatPasswdSaysBecomesOurWords()
    {
        QFETCH(QString, output);
        QFETCH(int, code);
        QFETCH(QString, expected);
        const QString said = SignInSettings::describePasswdOutput(output, code);
        if (expected.isEmpty())
            QCOMPARE(said, QString());
        else
            QVERIFY2(said.contains(expected), qPrintable(said));
    }

    void lockWhenAwayWritesEveryPowerProfile()
    {
        QTemporaryDir dir;
        const QString rc = dir.filePath(QStringLiteral("powerrc"));
        SignInSettings signin({}, {}, fake(), rc, QStringLiteral("me"));
        QVERIFY(signin.lockWhenAway());
        signin.setLockWhenAway(false);
        QVERIFY(!signin.lockWhenAway());
        KConfig reread(rc, KConfig::SimpleConfig);
        for (const char *profile : {"AC", "Battery", "LowBattery"})
            QCOMPARE(reread.group(QString::fromLatin1(profile)).group(QStringLiteral("Display")).readEntry("LockBeforeTurnOffDisplay", true), false);
        signin.setLockWhenAway(true);
        QVERIFY(signin.lockWhenAway());
    }

    void restartAppsIsTheSessionLoginMode()
    {
        QTemporaryDir dir;
        const QString rc = dir.filePath(QStringLiteral("ksmserverrc"));
        write(rc, "[General]\nloginMode=emptySession\nexcludeApps=foo\n");
        auto signin = settings();
        signin->setSessionFile(rc);
        QVERIFY(!signin->restartApps());
        signin->setRestartApps(true);
        QVERIFY(signin->restartApps());
        KConfig reread(rc, KConfig::SimpleConfig);
        QCOMPARE(reread.group(QStringLiteral("General")).readEntry("loginMode"), QStringLiteral("restorePreviousLogout"));
        QCOMPARE(reread.group(QStringLiteral("General")).readEntry("excludeApps"), QStringLiteral("foo"));
        signin->setRestartApps(false);
        KConfig again(rc, KConfig::SimpleConfig);
        QCOMPARE(again.group(QStringLiteral("General")).readEntry("loginMode"), QStringLiteral("emptySession"));
        // a session saved by hand is also restored
        write(rc, "[General]\nloginMode=restoreSavedSession\n");
        signin->setSessionFile(rc);
        QVERIFY(signin->restartApps());
    }
};

QTEST_GUILESS_MAIN(TestSignIn)
#include "test_signin.moc"
