// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Language & region: a new display language reaches
// the system (the sign-in screen, the names the network service gives its
// connections). The system bus here is one of the test's own, with a
// pretend polkit and locale1 that behave as on the PC (pretend_locale1.py).
//
// Measured on the 2026-11-08 image: localed waits 25 s for polkit, then
// gives up without a word. A person who reads the prompt and types a
// password takes longer than that, so "Yes" changed nothing.
#include "region.h"

#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString localeOutput = QStringLiteral("C\nC.utf8\nPOSIX\nen_US.utf8\nfr_FR.utf8\n");

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// The bus and its two pretend services; locale1 gives up after 1 s, the
// person answers after 2 s, as on the PC (25 s against half a minute).
struct PretendSystem {
    QProcess bus;
    QProcess services;
    QString address;

    bool start(const QString &dir)
    {
        bus.start(QStringLiteral("dbus-daemon"),
                  {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!bus.waitForStarted() || !bus.waitForReadyRead(5000))
            return false;
        address = QString::fromUtf8(bus.readLine()).trimmed();
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"), address);
        services.setProcessEnvironment(env);
        services.setProcessChannelMode(QProcess::ForwardedChannels);
        services.start(QStringLiteral("python3"),
                       {QStringLiteral(SOURCE_DIR "/tests/pretend_locale1.py"), dir + QStringLiteral("/log"),
                        dir + QStringLiteral("/written"), dir + QStringLiteral("/answer"), QStringLiteral("2"), QStringLiteral("1")});
        if (!services.waitForStarted())
            return false;
        QDBusConnection c = QDBusConnection::connectToBus(address, QStringLiteral("raye-test-setup"));
        for (int i = 0; i < 100; ++i) {
            if (c.isConnected() && c.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.locale1")))
                return true;
            QTest::qWait(50);
        }
        return false;
    }

    ~PretendSystem()
    {
        services.kill();
        services.waitForFinished();
        bus.kill();
        bus.waitForFinished();
    }
};

PretendSystem *pretend = nullptr;

} // namespace

class TestRegionSystem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString file(const QString &name) const { return m_dir.filePath(name); }

    // what the person answers at the next prompt
    void willAnswer(const QString &word)
    {
        QFile::remove(file(QStringLiteral("log")));
        QFile::remove(file(QStringLiteral("written")));
        QFile f(file(QStringLiteral("answer")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(word.toUtf8());
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", file(QStringLiteral("config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", file(QStringLiteral("xdg")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("LANG", "en_US.UTF-8");
        qunsetenv("LANGUAGE");
        qunsetenv("LC_TIME");
        willAnswer(QStringLiteral("yes"));
        if (QStandardPaths::findExecutable(QStringLiteral("dbus-daemon")).isEmpty())
            QFAIL("dbus-daemon is missing (Build-Depends: dbus)");
        pretend = new PretendSystem;
        QVERIFY2(pretend->start(m_dir.path()), "the pretend system bus did not start (python3-dbus?)");
        // the program's system bus is this one, from its first use on
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", pretend->address.toUtf8());
    }

    void init()
    {
        QDir(file(QStringLiteral("config"))).removeRecursively();
        QDir().mkpath(file(QStringLiteral("config")));
        KSharedConfig::openConfig(QStringLiteral("plasma-localerc"))->reparseConfiguration();
    }

    void cleanupTestCase()
    {
        delete pretend;
        pretend = nullptr;
    }

    // One prompt, a person slower than locale1's wait, and the system
    // still takes the language.
    void aSlowYesReachesTheSystem()
    {
        willAnswer(QStringLiteral("yes"));
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(localeOutput), {QStringLiteral("fr")});
        r.setLanguage(QStringLiteral("fr"));

        QTRY_VERIFY_WITH_TIMEOUT(readAll(file(QStringLiteral("written"))).contains(QStringLiteral("LANG=fr_FR.UTF-8")),
                                 10000);
        const QString written = readAll(file(QStringLiteral("written")));
        QVERIFY(written.contains(QStringLiteral("LC_TIME=fr_FR.UTF-8")));
        // the person was asked once, not twice
        QCOMPARE(readAll(file(QStringLiteral("log"))).count(QStringLiteral("prompt")), 1);
    }

    // "No" leaves the system as it was, and asks nothing more.
    void aNoLeavesTheSystem()
    {
        willAnswer(QStringLiteral("no"));
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(localeOutput), {QStringLiteral("fr")});
        r.setLanguage(QStringLiteral("fr"));

        QTRY_VERIFY_WITH_TIMEOUT(readAll(file(QStringLiteral("log"))).contains(QStringLiteral("answered no")), 10000);
        QTest::qWait(1500);
        QVERIFY(!QFile::exists(file(QStringLiteral("written"))));
        // locale1 is not even called: it would only ask again
        QVERIFY(!readAll(file(QStringLiteral("log"))).contains(QStringLiteral("SetLocale")));
        QCOMPARE(readAll(file(QStringLiteral("log"))).count(QStringLiteral("prompt")), 1);
    }
};

QTEST_GUILESS_MAIN(TestRegionSystem)
#include "test_region_system.moc"
