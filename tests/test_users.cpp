// SPDX-License-Identifier: GPL-3.0-or-later
// Your info and Other users against a fake AccountsService on a private
// bus. What matters: you are never in the list of others, a system account
// never is either, a new account gets a real password hash and never the
// password, and a refusal of the password prompt is said in words.
#include "users.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusServer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <crypt.h>

class FakeUser : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Accounts.User")
    Q_PROPERTY(qulonglong Uid MEMBER uid)
    Q_PROPERTY(QString UserName MEMBER userName)
    Q_PROPERTY(QString RealName MEMBER realName)
    Q_PROPERTY(QString IconFile MEMBER iconFile)
    Q_PROPERTY(int AccountType MEMBER accountType)
    Q_PROPERTY(bool SystemAccount MEMBER systemAccount)
public:
    qulonglong uid = 0;
    QString userName;
    QString realName;
    QString iconFile;
    int accountType = 0;
    bool systemAccount = false;
    QString password;
    bool sawInteractive = false;
public Q_SLOTS:
    void SetRealName(const QString &name) { realName = name; }
    void SetIconFile(const QString &file) { iconFile = file; }
    void SetAccountType(int type)
    {
        sawInteractive = message().isInteractiveAuthorizationAllowed();
        accountType = type;
    }
    void SetPassword(const QString &hash, const QString &)
    {
        password = hash;
    }
};

class FakeAccounts : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Accounts")
public:
    QHash<QString, FakeUser *> users;
    QDBusConnection *connection = nullptr;
    bool refuse = false;
    qlonglong deleted = -1;
    bool deletedFiles = false;

    FakeUser *add(qulonglong uid, const QString &login, bool system = false)
    {
        auto *user = new FakeUser;
        user->setParent(this);
        user->uid = uid;
        user->userName = login;
        user->systemAccount = system;
        const QString path = QStringLiteral("/org/freedesktop/Accounts/User%1").arg(uid);
        users.insert(path, user);
        connection->registerObject(path, user, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        return user;
    }
public Q_SLOTS:
    QList<QDBusObjectPath> ListCachedUsers()
    {
        QList<QDBusObjectPath> paths;
        const auto keys = users.keys();
        for (const QString &key : keys)
            paths << QDBusObjectPath(key);
        return paths;
    }
    QDBusObjectPath FindUserById(qlonglong uid)
    {
        return QDBusObjectPath(QStringLiteral("/org/freedesktop/Accounts/User%1").arg(uid));
    }
    QDBusObjectPath CreateUser(const QString &name, const QString &realName, int type)
    {
        if (refuse) {
            sendErrorReply(QStringLiteral("org.freedesktop.Accounts.Error.PermissionDenied"), QStringLiteral("Not authorized"));
            return {};
        }
        FakeUser *user = add(2000 + users.size(), name);
        user->realName = realName;
        user->accountType = type;
        return FindUserById(user->uid);
    }
    void DeleteUser(qlonglong uid, bool files)
    {
        deleted = uid;
        deletedFiles = files;
    }
Q_SIGNALS:
    void UserAdded(const QDBusObjectPath &user);
    void UserDeleted(const QDBusObjectPath &user);
};

class FakeBus : public QObject
{
    Q_OBJECT
public:
    QString address;
    FakeAccounts *accounts = nullptr;
public Q_SLOTS:
    void start()
    {
        m_server = new QDBusServer(this);
        address = m_server->address();
        connect(m_server, &QDBusServer::newConnection, this, [this](const QDBusConnection &connection) {
            m_connection = connection;
            accounts = new FakeAccounts;
            accounts->setParent(this);
            accounts->connection = &m_connection;
            m_connection.registerObject(QStringLiteral("/org/freedesktop/Accounts"), accounts,
                                        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
            accounts->add(1000, QStringLiteral("me"));
            accounts->add(1001, QStringLiteral("sam"))->realName = QStringLiteral("Sam Roy");
            accounts->add(1002, QStringLiteral("kim"))->accountType = 1;
            accounts->add(999, QStringLiteral("service"), true);
        });
    }

private:
    QDBusServer *m_server = nullptr;
    QDBusConnection m_connection{QString()};
};

class TestUsers : public QObject
{
    Q_OBJECT
    QThread m_thread;
    int m_count = 0;

    FakeBus *newBus(QDBusConnection &out)
    {
        auto *bus = new FakeBus;
        bus->moveToThread(&m_thread);
        QMetaObject::invokeMethod(bus, &FakeBus::start, Qt::BlockingQueuedConnection);
        out = QDBusConnection::connectToPeer(bus->address, QStringLiteral("users%1").arg(++m_count));
        QTest::qWaitFor([bus] { return bus->accounts != nullptr; }, 2000);
        return bus;
    }

private Q_SLOTS:
    void initTestCase() { m_thread.start(); }
    void cleanupTestCase()
    {
        m_thread.quit();
        m_thread.wait();
    }

    void othersLeavesOutMeAndTheSystem()
    {
        QDBusConnection connection{QString()};
        newBus(connection);
        AccountUsers users(connection, 1000);
        QVERIFY(users.available());
        QStringList logins;
        for (const QVariant &other : users.others())
            logins << other.toMap().value(QStringLiteral("userName")).toString();
        logins.sort();
        QCOMPARE(logins, QStringList({QStringLiteral("kim"), QStringLiteral("sam")}));
    }

    void newAccountGetsAHashNeverThePassword()
    {
        QDBusConnection connection{QString()};
        FakeBus *bus = newBus(connection);
        AccountUsers users(connection, 1000);
        QSignalSpy done(&users, &AccountUsers::done);
        users.addUser(QStringLiteral("Élise Roy"), QStringLiteral("elise"), QStringLiteral("hunter22"), true);
        QVERIFY(users.busy());
        QVERIFY(done.wait(5000));
        QVERIFY(!users.busy());
        FakeUser *made = nullptr;
        for (FakeUser *user : bus->accounts->users)
            if (user->userName == QLatin1String("elise"))
                made = user;
        QVERIFY(made);
        QCOMPARE(made->realName, QStringLiteral("Élise Roy"));
        QCOMPARE(made->accountType, 1);
        QVERIFY(!made->password.isEmpty());
        QVERIFY(!made->password.contains(QLatin1String("hunter22")));
        // the hash really is of this password
        crypt_data data = {};
        QCOMPARE(QString::fromLatin1(crypt_r("hunter22", made->password.toLatin1().constData(), &data)), made->password);
    }

    void refusalIsSaidInWords()
    {
        QDBusConnection connection{QString()};
        FakeBus *bus = newBus(connection);
        bus->accounts->refuse = true;
        AccountUsers users(connection, 1000);
        QSignalSpy changed(&users, &AccountUsers::changed);
        users.addUser(QStringLiteral("Nobody"), QStringLiteral("nobodyelse"), QStringLiteral("pw"), false);
        QTRY_VERIFY_WITH_TIMEOUT(!users.busy(), 5000);
        QVERIFY(users.error().contains(QLatin1String("root access")));
    }

    void changesAskForThePasswordPrompt()
    {
        QDBusConnection connection{QString()};
        FakeBus *bus = newBus(connection);
        AccountUsers users(connection, 1000);
        QSignalSpy done(&users, &AccountUsers::done);
        users.setAdministrator(1001, true);
        QVERIFY(done.wait(5000));
        FakeUser *sam = bus->accounts->users.value(QStringLiteral("/org/freedesktop/Accounts/User1001"));
        QCOMPARE(sam->accountType, 1);
        // without the flag AccountsService refuses instead of asking
        QVERIFY(sam->sawInteractive);
    }

    void removeSaysWhatToDoWithTheFiles()
    {
        QDBusConnection connection{QString()};
        FakeBus *bus = newBus(connection);
        AccountUsers users(connection, 1000);
        QSignalSpy done(&users, &AccountUsers::done);
        users.removeUser(1001, true);
        QVERIFY(done.wait(5000));
        QCOMPARE(bus->accounts->deleted, 1001);
        QVERIFY(bus->accounts->deletedFiles);
        // you never remove yourself from here
        bus->accounts->deleted = -1;
        users.removeUser(1000, false);
        QTest::qWait(100);
        QCOMPARE(bus->accounts->deleted, -1);
    }

    void nameAndPictureAreMine()
    {
        QDBusConnection connection{QString()};
        FakeBus *bus = newBus(connection);
        AccountUsers users(connection, 1000);
        QSignalSpy done(&users, &AccountUsers::done);
        users.setMyName(QStringLiteral("  New Name "));
        QVERIFY(done.wait(5000));
        FakeUser *me = bus->accounts->users.value(QStringLiteral("/org/freedesktop/Accounts/User1000"));
        QCOMPARE(me->realName, QStringLiteral("New Name"));
    }

    void pictureIsCroppedSquareAndSmall()
    {
        QTemporaryDir dir;
        QImage wide(800, 400, QImage::Format_RGB32);
        wide.fill(Qt::red);
        const QString file = dir.filePath(QStringLiteral("wide.jpg"));
        QVERIFY(wide.save(file));
        const QString out = AccountUsers::preparePicture(file, dir.filePath(QStringLiteral("out")));
        QVERIFY(!out.isEmpty());
        const QImage result(out);
        QCOMPARE(result.size(), QSize(256, 256));

        QFile notImage(dir.filePath(QStringLiteral("text.png")));
        QVERIFY(notImage.open(QIODevice::WriteOnly));
        notImage.write("not a picture");
        notImage.close();
        QVERIFY(AccountUsers::preparePicture(notImage.fileName(), dir.filePath(QStringLiteral("out"))).isEmpty());
    }

    void loginNames()
    {
        QCOMPARE(AccountUsers::suggestLogin(QStringLiteral("Alex Martin")), QStringLiteral("alex"));
        QCOMPARE(AccountUsers::suggestLogin(QStringLiteral("Élise")), QStringLiteral("elise"));
        QCOMPARE(AccountUsers::suggestLogin(QStringLiteral("  3 Jean-Luc ")), QString());
        QCOMPARE(AccountUsers::suggestLogin(QStringLiteral("Jean-Luc Roy")), QStringLiteral("jean-luc"));

        QDBusConnection connection{QString()};
        newBus(connection);
        AccountUsers users(connection, 1000);
        QVERIFY(users.loginProblem(QStringLiteral("newperson")).isEmpty());
        QVERIFY(!users.loginProblem(QString()).isEmpty());
        QVERIFY(!users.loginProblem(QStringLiteral("Has Space")).isEmpty());
        QVERIFY(!users.loginProblem(QStringLiteral("9start")).isEmpty());
        QVERIFY(!users.loginProblem(QStringLiteral("root")).isEmpty());
        QVERIFY(!users.loginProblem(QStringLiteral("sam")).isEmpty()); // taken
    }

    void passwords()
    {
        QVERIFY(AccountUsers::passwordProblem(QStringLiteral("a"), QStringLiteral("a")).isEmpty());
        QVERIFY(!AccountUsers::passwordProblem(QString(), QString()).isEmpty());
        QVERIFY(!AccountUsers::passwordProblem(QStringLiteral("a"), QStringLiteral("b")).isEmpty());
    }

    void noAccountsServiceSaysSo()
    {
        QDBusServer other;
        AccountUsers users(QDBusConnection::connectToPeer(other.address(), QStringLiteral("usersnone")), 1000);
        QVERIFY(!users.available());
        QVERIFY(users.others().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUsers)
#include "test_users.moc"
