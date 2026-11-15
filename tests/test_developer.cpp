// SPDX-License-Identifier: GPL-3.0-or-later
// System > Advanced > For developers. The portal program, the root
// helper and systemctl are stand-ins that log what they are asked.
#include "developer.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
void write(const QString &path, const QByteArray &text, bool executable = false)
{
    QVERIFY(QFileInfo(path).dir().mkpath(QStringLiteral(".")));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
    file.close();
    if (executable)
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
}

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
} // namespace

class TestDeveloper : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_portalLog;
    QString m_helperLog;
    QString m_stdin;

    void fakes(DeveloperSettings &dev, bool firewall = false)
    {
        const QString dir = m_tmp.path();
        m_portalLog = dir + QStringLiteral("/portal.log");
        m_helperLog = dir + QStringLiteral("/helper.log");
        m_stdin = dir + QStringLiteral("/stdin");
        QFile::remove(m_portalLog);
        QFile::remove(m_helperLog);
        QFile::remove(m_stdin);
        const QByteArray status = firewall ? "true" : "false";
        write(dir + QStringLiteral("/portal"),
              "#!/bin/sh\necho \"$*\" >> " + m_portalLog.toUtf8() + "\n"
              "case $1 in\n"
              "status) echo '{\"enabled\": true, \"addresses\": [\"https://192.168.50.5:50443\"], \"fingerprint\": \"AB:CD\","
              " \"user\": \"alex\", \"paired\": 2, \"allow_screenshot\": false, \"discovery\": true, \"firewall\": " + status + ","
              " \"log\": [{\"time\": 1, \"event\": \"sign-in\", \"address\": \"192.168.50.9\", \"detail\": \"alex\"}]}' ;;\n"
              "set-login) cat > " + m_stdin.toUtf8() + " ;;\n"
              "pair) echo 123456 ;;\n"
              "esac\n",
              true);
        write(dir + QStringLiteral("/helper"), "#!/bin/sh\necho \"$*\" >> " + m_helperLog.toUtf8() + "\n", true);
        write(dir + QStringLiteral("/systemctl"), "#!/bin/sh\nexit 3\n", true);
        dev.setPortalCommand({dir + QStringLiteral("/portal")});
        dev.setHelperCommand({dir + QStringLiteral("/helper")});
        dev.setSystemctlCommand({dir + QStringLiteral("/systemctl")});
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + QStringLiteral("/config")).toUtf8());
    }

    void developerModeWritesItsFiles()
    {
        DeveloperSettings dev;
        fakes(dev);
        dev.setDeveloperMode(true);
        const QString env = read(dev.environmentFile());
        QVERIFY(env.contains(QStringLiteral("QT_LOGGING_RULES=raye.*.debug=true")));
        QVERIFY(read(m_tmp.path() + QStringLiteral("/config/raye-developerrc")).contains(QStringLiteral("DeveloperMode=true")));
        DeveloperSettings again;
        QVERIFY(again.developerMode());
        dev.setDeveloperMode(false);
        QVERIFY(!QFile::exists(dev.environmentFile()));
        DeveloperSettings off;
        QVERIFY(!off.developerMode());
    }

    void statusIsRead()
    {
        DeveloperSettings dev;
        const QByteArray status = "{\"enabled\": true, \"addresses\": [\"https://10.0.0.2:50443\"], \"fingerprint\": \"AA:BB\","
                                  " \"user\": \"sam\", \"paired\": 1, \"allow_screenshot\": true, \"discovery\": false,"
                                  " \"log\": [{\"event\": \"sign-in\"}]}";
        QVERIFY(dev.readStatus(status));
        QVERIFY(dev.portalEnabled());
        QCOMPARE(dev.portalAddresses(), QStringList{QStringLiteral("https://10.0.0.2:50443")});
        QCOMPARE(dev.portalFingerprint(), QStringLiteral("AA:BB"));
        QCOMPARE(dev.portalUser(), QStringLiteral("sam"));
        QCOMPARE(dev.pairedDevices(), 1);
        QVERIFY(dev.allowScreenshot());
        QVERIFY(!dev.discovery());
        QCOMPARE(dev.portalLog().size(), 1);
        QVERIFY(!dev.readStatus("not json"));
    }

    void portalNeedsDeveloperMode()
    {
        DeveloperSettings dev;
        fakes(dev);
        dev.setDeveloperMode(false);
        dev.setPortalEnabled(true);
        QTest::qWait(300);
        QVERIFY(!read(m_portalLog).contains(QStringLiteral("enable")));
    }

    void turnOnAndOffWithTheFirewall()
    {
        DeveloperSettings dev;
        fakes(dev, true);
        dev.refresh();
        QTRY_VERIFY(read(m_portalLog).contains(QStringLiteral("status")));
        QTRY_COMPARE(dev.portalUser(), QStringLiteral("alex"));
        dev.setDeveloperMode(true);
        dev.setPortalEnabled(true);
        QVERIFY(dev.portalBusy());
        QTRY_VERIFY(!dev.portalBusy());
        QVERIFY(read(m_portalLog).contains(QStringLiteral("enable\n")));
        QCOMPARE(read(m_helperLog), QStringLiteral("portal-firewall allow\n"));
        // turning Developer Mode off turns the portal off too
        dev.setDeveloperMode(false);
        QTRY_VERIFY(read(m_portalLog).contains(QStringLiteral("disable\n")));
        QTRY_VERIFY(read(m_helperLog).contains(QStringLiteral("portal-firewall deny\n")));
    }

    void passwordGoesOnStdin()
    {
        DeveloperSettings dev;
        fakes(dev);
        QSignalSpy saved(&dev, &DeveloperSettings::loginSaved);
        dev.setPortalLogin(QStringLiteral("alex"), QStringLiteral("short"));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.at(0).at(0).toBool(), false);
        dev.setPortalLogin(QStringLiteral(" alex "), QStringLiteral("a long secret"));
        QTRY_COMPARE(saved.count(), 2);
        QCOMPARE(saved.at(1).at(0).toBool(), true);
        QCOMPARE(read(m_stdin), QStringLiteral("a long secret\n"));
        QVERIFY(read(m_portalLog).contains(QStringLiteral("set-login alex\n")));
        QVERIFY(!read(m_portalLog).contains(QStringLiteral("secret")));
    }

    void pairCode()
    {
        DeveloperSettings dev;
        fakes(dev);
        dev.newPairCode();
        QTRY_COMPARE(dev.pairCode(), QStringLiteral("123456"));
    }

    void sshKeysOnlyByDefault()
    {
        DeveloperSettings dev;
        fakes(dev);
        const QString keys = m_tmp.path() + QStringLiteral("/01-raye-keys-only.conf");
        write(m_tmp.path() + QStringLiteral("/sshd"), "#!/bin/sh\n", true);
        dev.setSshPaths(m_tmp.path() + QStringLiteral("/sshd"), keys);
        QVERIFY(dev.sshInstalled());
        dev.setSshEnabled(true);
        QTRY_VERIFY(!dev.sshBusy());
        QCOMPARE(read(m_helperLog), QStringLiteral("ssh on keys\n"));
        // passwords asked for once: kept the next time it starts
        dev.setSshKeysOnly(false);
        QTRY_VERIFY(!dev.sshBusy());
        dev.setSshEnabled(true);
        QTRY_VERIFY(!dev.sshBusy());
        QVERIFY(read(m_helperLog).endsWith(QStringLiteral("ssh keys-only off\nssh on passwords\n")));
        dev.setSshEnabled(false);
        QTRY_VERIFY(!dev.sshBusy());
        QVERIFY(read(m_helperLog).endsWith(QStringLiteral("ssh off\n")));
        dev.setSshPaths(m_tmp.path() + QStringLiteral("/missing"), keys);
        QVERIFY(!dev.sshInstalled());
    }

    void sshKeysOnlyReadsWhatSshdReads()
    {
        DeveloperSettings dev;
        fakes(dev);
        const QString etc = m_tmp.path() + QStringLiteral("/sshkeys/ssh");
        const QString ours = etc + QStringLiteral("/sshd_config.d/01-raye-keys-only.conf");
        write(etc + QStringLiteral("/sshd_config"), "Include /etc/ssh/sshd_config.d/*.conf\nPasswordAuthentication yes\n");
        dev.setSshPaths(m_tmp.path() + QStringLiteral("/sshd"), ours);
        QVERIFY(!dev.sshKeysOnly());
        // set by hand in another drop-in: sshd refuses passwords already
        write(etc + QStringLiteral("/sshd_config.d/01-raye-test.conf"), "# test\n  passwordauthentication   no\n");
        QVERIFY(dev.sshKeysOnly());
        QVERIFY(dev.sshKeysOnlyFixed());
        // the first value wins, and 00 is read before 01
        write(etc + QStringLiteral("/sshd_config.d/00-cloud.conf"), "PasswordAuthentication yes\n");
        QVERIFY(!dev.sshKeysOnly());
        QFile::remove(etc + QStringLiteral("/sshd_config.d/00-cloud.conf"));
        QFile::remove(etc + QStringLiteral("/sshd_config.d/01-raye-test.conf"));
        write(ours, "PasswordAuthentication no\n");
        QVERIFY(dev.sshKeysOnly());
        QVERIFY(!dev.sshKeysOnlyFixed());
    }

    void discoveryNeedsAvahi()
    {
        DeveloperSettings dev;
        fakes(dev);
        dev.setAvahiPath(m_tmp.path() + QStringLiteral("/no-avahi"));
        QVERIFY(!dev.discoveryAvailable());
        write(m_tmp.path() + QStringLiteral("/avahi"), "#!/bin/sh\n", true);
        dev.setAvahiPath(m_tmp.path() + QStringLiteral("/avahi"));
        QVERIFY(dev.discoveryAvailable());
        QVERIFY(dev.readStatus("{\"enabled\": false}"));
        QVERIFY(!dev.discovery());
    }
};

QTEST_GUILESS_MAIN(TestDeveloper)
#include "test_developer.moc"
