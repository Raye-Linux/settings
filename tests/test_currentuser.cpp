// SPDX-License-Identifier: GPL-3.0-or-later
// The frame's own facts: who is signed in (AccountsService, here a fake
// one on a private bus), what the launch screen says about the computer,
// the cards the user hid, and how a settings module reaches KDE's own
// window.
#include "currentuser.h"
#include "modulehost.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServer>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <unistd.h>

// org.freedesktop.Accounts.User, the properties Settings reads
class FakeUser : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Accounts.User")
    Q_PROPERTY(QString UserName MEMBER userName)
    Q_PROPERTY(QString RealName MEMBER realName)
    Q_PROPERTY(QString IconFile MEMBER iconFile)
    Q_PROPERTY(int AccountType MEMBER accountType)
public:
    QString userName = QStringLiteral("alex");
    QString realName = QStringLiteral("Alex Martin");
    QString iconFile;
    int accountType = 1;
Q_SIGNALS:
    void Changed();
};

class FakeAccounts : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Accounts")
public:
    qint64 knownUid = 1000;
public Q_SLOTS:
    QDBusObjectPath FindUserById(qint64 uid)
    {
        lastUid = uid;
        return QDBusObjectPath(QStringLiteral("/org/freedesktop/Accounts/User%1").arg(uid));
    }
public:
    qint64 lastUid = -1;
};

// A private bus in a thread of its own, so the blocking calls of
// CurrentUser are answered while the test waits.
class FakeBus : public QObject
{
    Q_OBJECT
public:
    explicit FakeBus(bool withAccounts)
        : m_withAccounts(withAccounts)
    {
    }
    QString address;
    FakeUser *user = nullptr;
public Q_SLOTS:
    void start()
    {
        m_server = new QDBusServer(this);
        address = m_server->address();
        connect(m_server, &QDBusServer::newConnection, this, [this](const QDBusConnection &connection) {
            m_connection = connection;
            if (!m_withAccounts)
                return;
            auto *accounts = new FakeAccounts;
            accounts->setParent(this);
            user = new FakeUser;
            user->setParent(this);
            const auto how = QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots
                           | QDBusConnection::ExportAllSignals;
            m_connection.registerObject(QStringLiteral("/org/freedesktop/Accounts"), accounts, how);
            m_connection.registerObject(QStringLiteral("/org/freedesktop/Accounts/User1000"), user, how);
        });
    }
    void rename(const QString &name)
    {
        user->realName = name;
        Q_EMIT user->Changed();
    }

private:
    bool m_withAccounts;
    QDBusServer *m_server = nullptr;
    QDBusConnection m_connection{QString()};
};

class TestCurrentUser : public QObject
{
    Q_OBJECT

    QThread m_thread;

    QDBusConnection connectTo(FakeBus *bus, const QString &name)
    {
        bus->moveToThread(&m_thread);
        QMetaObject::invokeMethod(bus, &FakeBus::start, Qt::BlockingQueuedConnection);
        QDBusConnection connection = QDBusConnection::connectToPeer(bus->address, name);
        return connection;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain("raye-settings");
        m_thread.start();
    }

    void cleanupTestCase()
    {
        m_thread.quit();
        m_thread.wait();
    }

    void properties()
    {
        const auto info = CurrentUser::fromProperties({{QStringLiteral("UserName"), QStringLiteral("sam")},
                                                       {QStringLiteral("RealName"), QStringLiteral("  Sam Roy ")},
                                                       {QStringLiteral("IconFile"), QStringLiteral("/x/sam")},
                                                       {QStringLiteral("AccountType"), 0}});
        QCOMPARE(info.userName, QStringLiteral("sam"));
        QCOMPARE(info.realName, QStringLiteral("Sam Roy"));
        QCOMPARE(info.iconFile, QStringLiteral("/x/sam"));
        QVERIFY(!info.administrator);
        QVERIFY(CurrentUser::fromProperties({{QStringLiteral("AccountType"), 1}}).administrator);
    }

    void passwd()
    {
        const auto info = CurrentUser::fromPasswd(QStringLiteral("alex"), QStringLiteral("Alex Martin,,,"));
        QCOMPARE(info.userName, QStringLiteral("alex"));
        QCOMPARE(info.realName, QStringLiteral("Alex Martin"));
        QCOMPARE(CurrentUser::fromPasswd(QStringLiteral("bo"), QString()).realName, QString());
    }

    void accountsService()
    {
        QTemporaryDir dir;
        auto *bus = new FakeBus(true);
        QDBusConnection connection = connectTo(bus, QStringLiteral("accounts"));
        QVERIFY(connection.isConnected());
        QTRY_VERIFY(bus->user);

        CurrentUser user(connection, 1000);
        QCOMPARE(user.userName(), QStringLiteral("alex"));
        QCOMPARE(user.displayName(), QStringLiteral("Alex Martin"));
        QVERIFY(user.administrator());
        QCOMPARE(user.accountLabel(), QStringLiteral("Sudo user"));
        // no picture chosen: no URL, the page draws initials
        QVERIFY(user.picture().isEmpty());

        // a real picture, and a name change announced by the service
        const QString icon = dir.filePath(QStringLiteral("alex"));
        QFile file(icon);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("\x89PNG not really");
        file.close();
        bus->user->iconFile = icon;
        bus->user->accountType = 0;
        QSignalSpy changed(&user, &CurrentUser::changed);
        QMetaObject::invokeMethod(bus, "rename", Qt::BlockingQueuedConnection, Q_ARG(QString, QStringLiteral("Alex M.")));
        // peer buses carry no sender name: when the signal does not come
        // through, a reload must still see the change
        if (!changed.wait(1000))
            user.reload();
        QCOMPARE(user.displayName(), QStringLiteral("Alex M."));
        QCOMPARE(user.picture(), QUrl::fromLocalFile(icon));
        QCOMPARE(user.accountLabel(), QStringLiteral("Standard user"));

        // the same file again holds a new picture: the URL changes so the
        // image is read again
        changed.clear();
        user.reload();
        QCOMPARE(changed.count(), 1);
        QVERIFY(user.picture().toLocalFile() == icon);
        QVERIFY(user.picture() != QUrl::fromLocalFile(icon));
        connection.disconnectFromPeer(QStringLiteral("accounts"));
        bus->deleteLater();
    }

    void withoutAccountsService()
    {
        auto *bus = new FakeBus(false);
        QDBusConnection connection = connectTo(bus, QStringLiteral("empty"));
        CurrentUser user(connection, ::getuid());
        // the passwd entry of whoever runs the test
        QVERIFY(!user.userName().isEmpty());
        QVERIFY(!user.displayName().isEmpty());
        QVERIFY(!user.administrator());
        connection.disconnectFromPeer(QStringLiteral("empty"));
        bus->deleteLater();
    }

    void parsing()
    {
        QCOMPARE(FrameSettings::processorName(QStringLiteral("processor\t: 0\nmodel name\t: Intel(R) Core(TM) i7-1165G7 @ 2.80GHz\n")),
                 QStringLiteral("Intel Core i7-1165G7 @ 2.80GHz"));
        QCOMPARE(FrameSettings::processorName(QString()), QString());
        QCOMPARE(FrameSettings::memoryBytes(QStringLiteral("MemTotal:       16261820 kB\nMemFree: 1 kB\n")),
                 qint64(16261820) * 1024);
        QCOMPARE(FrameSettings::chassisOf(QStringLiteral("10")), QStringLiteral("laptop"));
        QCOMPARE(FrameSettings::chassisOf(QStringLiteral("3")), QStringLiteral("desktop"));
        QCOMPARE(FrameSettings::chassisOf(QStringLiteral("13")), QStringLiteral("all-in-one"));
        QCOMPARE(FrameSettings::chassisOf(QStringLiteral("30")), QStringLiteral("tablet"));
        QCOMPARE(FrameSettings::chassisOf(QString()), QStringLiteral("desktop"));
        const qint64 gib = qint64(1) << 30;
        QCOMPARE(FrameSettings::sizeText(16 * gib), QStringLiteral("16 GB"));
        QCOMPARE(FrameSettings::sizeText(gib * 3 / 2), QStringLiteral("1.5 GB"));
        QCOMPARE(FrameSettings::sizeText(476 * gib), QStringLiteral("476 GB"));
        QCOMPARE(FrameSettings::sizeText(2048 * gib), QStringLiteral("2.0 TB"));
    }

    void device()
    {
        QTemporaryDir root;
        auto put = [&](const QString &path, const QByteArray &text) {
            QDir().mkpath(QFileInfo(root.filePath(path)).absolutePath());
            QFile f(root.filePath(path));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        put(QStringLiteral("sys/class/dmi/id/sys_vendor"), "LENOVO\n");
        put(QStringLiteral("sys/class/dmi/id/product_name"), "ThinkPad X1 Carbon Gen 9\n");
        put(QStringLiteral("sys/class/dmi/id/chassis_type"), "10\n");
        put(QStringLiteral("proc/cpuinfo"), "model name\t: AMD Ryzen 7 5800U with Radeon Graphics\n");
        put(QStringLiteral("proc/meminfo"), "MemTotal:       15700000 kB\n");
        FrameSettings frame(root.path());
        const QVariantMap device = frame.device();
        QCOMPARE(device.value(QStringLiteral("model")).toString(), QStringLiteral("LENOVO ThinkPad X1 Carbon Gen 9"));
        QCOMPARE(device.value(QStringLiteral("chassis")).toString(), QStringLiteral("laptop"));
        QCOMPARE(device.value(QStringLiteral("processor")).toString(), QStringLiteral("AMD Ryzen 7 5800U with Radeon Graphics"));
        QCOMPARE(device.value(QStringLiteral("memory")).toString(), QStringLiteral("15 GB"));
        QVERIFY(device.value(QStringLiteral("storageTotal")).toLongLong() > 0);
        QVERIFY(device.value(QStringLiteral("storageText")).toString().contains(QStringLiteral(" free of ")));

        // a board nobody filled in says nothing rather than nonsense
        QTemporaryDir blank;
        QDir().mkpath(blank.filePath(QStringLiteral("sys/class/dmi/id")));
        QFile p(blank.filePath(QStringLiteral("sys/class/dmi/id/product_name")));
        QVERIFY(p.open(QIODevice::WriteOnly));
        p.write("To Be Filled By O.E.M.\n");
        p.close();
        FrameSettings empty(blank.path());
        QCOMPARE(empty.device().value(QStringLiteral("model")).toString(), QString());
        QCOMPARE(empty.device().value(QStringLiteral("chassis")).toString(), QStringLiteral("desktop"));
    }

    void hiddenCards()
    {
        KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"))->deleteGroup(QStringLiteral("Home"));
        FrameSettings frame;
        QVERIFY(frame.hiddenCards().isEmpty());
        QSignalSpy spy(&frame, &FrameSettings::hiddenCardsChanged);
        frame.hideCard(QStringLiteral("your-device"));
        frame.hideCard(QStringLiteral("your-device"));
        frame.hideCard(QStringLiteral("bluetooth-devices"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(frame.hiddenCards(), (QStringList{QStringLiteral("your-device"), QStringLiteral("bluetooth-devices")}));
        // written where the spec says: raye-settingsrc [Home] Hidden=
        KSharedConfig::Ptr fresh = KSharedConfig::openConfig(
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/raye-settingsrc"),
            KConfig::SimpleConfig);
        QCOMPARE(fresh->group(QStringLiteral("Home")).readEntry(QStringLiteral("Hidden"), QStringList()).size(), 2);
        frame.showAllCards();
        QVERIFY(frame.hiddenCards().isEmpty());
    }

    void looks()
    {
        QTemporaryDir root;
        for (const char *id : {"org.raye.night.desktop", "org.raye.day.desktop"})
            QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("usr/share/plasma/look-and-feel/%1/contents/previews").arg(QLatin1String(id)))));
        QFile preview(root.filePath(QStringLiteral("usr/share/plasma/look-and-feel/org.raye.day.desktop/contents/previews/preview.png")));
        QVERIFY(preview.open(QIODevice::WriteOnly));
        preview.close();
        KSharedConfig::Ptr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        globals->group(QStringLiteral("KDE")).writeEntry(QStringLiteral("LookAndFeelPackage"), QStringLiteral("org.raye.day.desktop"));
        globals->sync();
        FrameSettings frame(root.path());
        const QVariantList looks = frame.looks();
        QCOMPARE(looks.size(), 2);
        QCOMPARE(looks.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Raye Night"));
        QVERIFY(!looks.at(0).toMap().value(QStringLiteral("current")).toBool());
        QVERIFY(looks.at(1).toMap().value(QStringLiteral("current")).toBool());
        QVERIFY(looks.at(0).toMap().value(QStringLiteral("preview")).toUrl().isEmpty());
        QVERIFY(!looks.at(1).toMap().value(QStringLiteral("preview")).toUrl().isEmpty());
        // only the looks of the list can be applied
        QVERIFY(!frame.applyLook(QStringLiteral("org.kde.breeze.desktop")));
        QVERIFY(!frame.applyLook(QStringLiteral("; rm -rf /")));
    }

    void components()
    {
        FrameSettings frame;
        QVERIFY(frame.hasComponent(QStringLiteral("Main.qml")));
        QVERIFY(frame.hasComponent(QStringLiteral("frame/HomeView.qml")));
        QVERIFY(!frame.hasComponent(QStringLiteral("pages/none/NothingPage.qml")));
        QVERIFY(!frame.hasComponent(QStringLiteral("../Main.qml")));
        QVERIFY(!frame.hasComponent(QString()));
    }

    void kcmshell()
    {
        QTemporaryDir root;
        QCOMPARE(ModuleHost::kcmshellProgram(root.path()), QStringLiteral("kcmshell6"));
        QDir().mkpath(root.filePath(QStringLiteral("usr/bin")));
        QFile real(root.filePath(QStringLiteral("usr/bin/kcmshell6.kde")));
        QVERIFY(real.open(QIODevice::WriteOnly));
        real.write("#!/bin/sh\n");
        real.close();
        // not executable yet: not a program
        QCOMPARE(ModuleHost::kcmshellProgram(root.path()), QStringLiteral("kcmshell6"));
        real.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QCOMPARE(ModuleHost::kcmshellProgram(root.path()), root.filePath(QStringLiteral("usr/bin/kcmshell6.kde")));
    }

    void moduleIds()
    {
        QVERIFY(ModuleHost::isModuleId(QStringLiteral("kcm_kscreen")));
        QVERIFY(ModuleHost::isModuleId(QStringLiteral("kcm_mobile_power")));
        QVERIFY(ModuleHost::isModuleId(QStringLiteral("org.kde.foo-bar")));
        QVERIFY(!ModuleHost::isModuleId(QString()));
        QVERIFY(!ModuleHost::isModuleId(QStringLiteral("--list")));
        QVERIFY(!ModuleHost::isModuleId(QStringLiteral("kcm x")));
        QVERIFY(!ModuleHost::isModuleId(QStringLiteral("../kcm")));
        QVERIFY(!ModuleHost::isModuleId(QStringLiteral("kcm;id")));
    }
};

QTEST_GUILESS_MAIN(TestCurrentUser)
#include "test_currentuser.moc"
