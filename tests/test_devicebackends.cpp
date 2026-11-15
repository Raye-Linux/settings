// SPDX-License-Identifier: GPL-3.0-or-later
// Printers, phones, cameras and online accounts. The print programs are
// stand-ins on the PATH that log what they are asked; what lpstat prints
// is copied from a real one. The phone service is a fake on a private bus.
#include "cameras.h"
#include "externalmodule.h"
#include "fingerprint.h"
#include "phone.h"
#include "printers.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServer>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

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
}

class FakePhone : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device")
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(QString type MEMBER type)
    Q_PROPERTY(bool isPaired MEMBER paired)
    Q_PROPERTY(bool isReachable MEMBER reachable)
    Q_PROPERTY(bool isPairRequested MEMBER requested)
    Q_PROPERTY(bool isPairRequestedByPeer MEMBER requestedByPeer)
public:
    QString name;
    QString type = QStringLiteral("smartphone");
    bool paired = false;
    bool reachable = true;
    bool requested = false;
    bool requestedByPeer = false;
public Q_SLOTS:
    void requestPairing() { requested = true; }
    void acceptPairing() { paired = true; requestedByPeer = false; }
    void cancelPairing() { requested = false; requestedByPeer = false; }
    void unpair() { paired = false; }
};

class FakeShare : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.share")
public:
    QString sent;
public Q_SLOTS:
    void shareUrl(const QString &url) { sent = url; }
};

class FakeDaemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.daemon")
public Q_SLOTS:
    QString announcedName() { return QStringLiteral("my-pc"); }
    QStringList devices() { return {QStringLiteral("abc"), QStringLiteral("def")}; }
};

// net.reactivated.Fprint, as far as adding a finger goes
class FakeReader : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.reactivated.Fprint.Device")
public:
    QStringList calls;
    QStringList saved;
public Q_SLOTS:
    QStringList ListEnrolledFingers(const QString &) { return saved; }
    void Claim(const QString &who) { calls << QStringLiteral("Claim ") + who; }
    void Release() { calls << QStringLiteral("Release"); }
    void EnrollStart(const QString &finger) { calls << QStringLiteral("EnrollStart ") + finger; }
    void EnrollStop() { calls << QStringLiteral("EnrollStop"); }
    void DeleteEnrolledFingers2() { calls << QStringLiteral("Delete"); saved.clear(); }
};

class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.reactivated.Fprint.Manager")
public Q_SLOTS:
    QDBusObjectPath GetDefaultDevice() { return QDBusObjectPath(QStringLiteral("/net/reactivated/Fprint/Device/0")); }
};

class Bus : public QObject
{
    Q_OBJECT
public:
    QString address;
    FakePhone *linked = nullptr;
    FakePhone *fresh = nullptr;
    FakeShare *share = nullptr;
    FakeReader *reader = nullptr;
public Q_SLOTS:
    void start()
    {
        m_server = new QDBusServer(this);
        address = m_server->address();
        linked = new FakePhone;
        linked->setParent(this);
        linked->name = QStringLiteral("Linked phone");
        linked->paired = true;
        fresh = new FakePhone;
        fresh->setParent(this);
        fresh->name = QStringLiteral("New phone");
        share = new FakeShare;
        share->setParent(this);
        reader = new FakeReader;
        reader->setParent(this);
        reader->saved = QStringList(QStringLiteral("right-thumb"));
        connect(m_server, &QDBusServer::newConnection, this, [this](const QDBusConnection &connection) {
            m_connection = connection;
            const auto how = QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals;
            auto *daemon = new FakeDaemon;
            daemon->setParent(this);
            auto *manager = new FakeManager;
            manager->setParent(this);
            m_connection.registerObject(QStringLiteral("/modules/kdeconnect"), daemon, how);
            m_connection.registerObject(QStringLiteral("/modules/kdeconnect/devices/abc"), linked, how);
            m_connection.registerObject(QStringLiteral("/modules/kdeconnect/devices/def"), fresh, how);
            m_connection.registerObject(QStringLiteral("/modules/kdeconnect/devices/abc/share"), share, how);
            m_connection.registerObject(QStringLiteral("/net/reactivated/Fprint/Manager"), manager, how);
            m_connection.registerObject(QStringLiteral("/net/reactivated/Fprint/Device/0"), reader, how);
        });
    }
private:
    QDBusServer *m_server = nullptr;
    QDBusConnection m_connection{QString()};
};

class TestDeviceBackends : public QObject
{
    Q_OBJECT
    QThread m_thread;
    Bus *m_bus = nullptr;
    int m_count = 0;
    QByteArray m_path;

    QDBusConnection bus() { return QDBusConnection::connectToPeer(m_bus->address, QStringLiteral("dev%1").arg(++m_count)); }
    // each reader test starts from a reader nobody has used
    Bus *ownBus(QDBusConnection &connection)
    {
        auto *own = new Bus;
        own->moveToThread(&m_thread);
        QMetaObject::invokeMethod(own, &Bus::start, Qt::BlockingQueuedConnection);
        connection = QDBusConnection::connectToPeer(own->address, QStringLiteral("own%1").arg(++m_count));
        QTest::qWait(50);
        return own;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_path = qgetenv("PATH");
        m_thread.start();
        m_bus = new Bus;
        m_bus->moveToThread(&m_thread);
        QMetaObject::invokeMethod(m_bus, &Bus::start, Qt::BlockingQueuedConnection);
    }
    void cleanupTestCase()
    {
        m_thread.quit();
        m_thread.wait();
    }
    void cleanup() { qputenv("PATH", m_path); }

    void lpstatIsRead()
    {
        const QVariantMap states = PrinterList::parseStates(QStringLiteral(
            "printer Bureau is idle.  enabled since Sat Nov  7 12:00:00 2026\n"
            "printer Labo now printing Labo-3.  enabled since Sat Nov  7 12:00:00 2026\n"
            "\tprinting page 1\n"
            "printer Vieille disabled since Sat Nov  7 12:00:00 2026 -\n"
            "\tPaused\n"));
        QCOMPARE(states.value(QStringLiteral("Bureau")).toString(), QStringLiteral("idle"));
        QCOMPARE(states.value(QStringLiteral("Labo")).toString(), QStringLiteral("printing"));
        QCOMPARE(states.value(QStringLiteral("Vieille")).toString(), QStringLiteral("stopped"));
        QCOMPARE(states.size(), 3);
        QCOMPARE(PrinterList::parseDefault(QStringLiteral("system default destination: Bureau\n")), QStringLiteral("Bureau"));
        QCOMPARE(PrinterList::parseDefault(QStringLiteral("no system default destination\n")), QString());
    }

    void printerNamesAndAddresses()
    {
        QVERIFY(PrinterList::nameProblem(QStringLiteral("Bureau-2")).isEmpty());
        QVERIFY(!PrinterList::nameProblem(QStringLiteral("has space")).isEmpty());
        QVERIFY(!PrinterList::nameProblem(QStringLiteral("a/b")).isEmpty());
        QVERIFY(!PrinterList::nameProblem(QString()).isEmpty());
        QVERIFY(PrinterList::addressProblem(QStringLiteral("ipp://printer.local/ipp/print")).isEmpty());
        QVERIFY(PrinterList::addressProblem(QStringLiteral("ipps://printer.local:631/ipp/print")).isEmpty());
        QVERIFY(!PrinterList::addressProblem(QStringLiteral("printer.local")).isEmpty());
        // no file: or other local backend through this door
        QVERIFY(!PrinterList::addressProblem(QStringLiteral("file:///etc/passwd")).isEmpty());
        QCOMPARE(PrinterList::suggestName(QStringLiteral("ipp://bureau.local/ipp/print")), QStringLiteral("bureau"));
    }

    void serviceOffIsNoticedAndCanBeTurnedOn()
    {
        QTemporaryDir bin;
        const QString marker = bin.filePath(QStringLiteral("running"));
        write(bin.filePath(QStringLiteral("lpstat")), QStringLiteral(
            "#!/bin/sh\n[ -e '%1' ] && echo 'scheduler is running' || echo 'scheduler is not running'\n").arg(marker).toUtf8(), true);
        const QString helper = bin.filePath(QStringLiteral("helper"));
        write(helper, QStringLiteral("#!/bin/sh\n[ \"$1\" = on ] && touch '%1'\n").arg(marker).toUtf8(), true);
        qputenv("PATH", QByteArray(bin.path().toLocal8Bit() + ":" + m_path));

        PrinterList printers({helper});
        QTRY_VERIFY_WITH_TIMEOUT(printers.loaded(), 3000);
        QVERIFY(!printers.serviceRunning());
        printers.turnOnService();
        QTRY_VERIFY_WITH_TIMEOUT(printers.serviceRunning(), 3000);
    }

    void printersAreListedAndActionsGoToTheRightProgram()
    {
        QTemporaryDir bin;
        const QString log = bin.filePath(QStringLiteral("log"));
        const QByteArray logger = QStringLiteral("#!/bin/sh\necho \"$(basename $0) $*\" >> '%1'\n").arg(log).toUtf8();
        write(bin.filePath(QStringLiteral("lpstat")), R"(#!/bin/sh
case $1 in
-r) echo 'scheduler is running' ;;
-e) printf 'Bureau\nFound_Over_Wifi\n' ;;
-p) echo 'printer Bureau is idle.  enabled since today' ;;
-d) echo 'system default destination: Bureau' ;;
esac
)", true);
        for (const char *program : {"lp", "lpoptions", "lpadmin"})
            write(bin.filePath(QString::fromLatin1(program)), logger, true);
        qputenv("PATH", QByteArray(bin.path().toLocal8Bit() + ":" + m_path));

        PrinterList printers({QStringLiteral("/bin/true")});
        QTRY_VERIFY_WITH_TIMEOUT(printers.loaded(), 3000);
        QCOMPARE(printers.printers().size(), 2);
        const QVariantMap bureau = printers.printers().at(0).toMap();
        QCOMPARE(bureau.value(QStringLiteral("name")).toString(), QStringLiteral("Bureau"));
        QVERIFY(bureau.value(QStringLiteral("isDefault")).toBool());
        QVERIFY(!bureau.value(QStringLiteral("network")).toBool());
        QVERIFY(printers.printers().at(1).toMap().value(QStringLiteral("network")).toBool());

        QSignalSpy sent(&printers, &PrinterList::testPageSent);
        printers.printTestPage(QStringLiteral("Bureau"));
        QVERIFY(sent.wait(3000));
        QTRY_VERIFY(!printers.busy());
        printers.setDefault(QStringLiteral("Found_Over_Wifi"));
        QTRY_VERIFY(!printers.busy());
        printers.remove(QStringLiteral("Bureau"));
        QTRY_VERIFY(!printers.busy());
        printers.add(QStringLiteral("Neuve"), QStringLiteral("ipp://neuve.local/ipp/print"));
        QTRY_VERIFY(!printers.busy());
        QFile logged(log);
        QVERIFY(logged.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(logged.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4);
        QVERIFY(lines.at(0).startsWith(QLatin1String("lp -d Bureau /usr/share/cups/data/")));
        QCOMPARE(lines.at(1), QStringLiteral("lpoptions -d Found_Over_Wifi"));
        QCOMPARE(lines.at(2), QStringLiteral("lpadmin -x Bureau"));
        QCOMPARE(lines.at(3), QStringLiteral("lpadmin -p Neuve -E -v ipp://neuve.local/ipp/print -m everywhere"));
    }

    void aBadAddressNeverReachesLpadmin()
    {
        QTemporaryDir bin;
        const QString log = bin.filePath(QStringLiteral("log"));
        write(bin.filePath(QStringLiteral("lpstat")), "#!/bin/sh\necho 'scheduler is running'\n", true);
        write(bin.filePath(QStringLiteral("lpadmin")), QStringLiteral("#!/bin/sh\ntouch '%1'\n").arg(log).toUtf8(), true);
        qputenv("PATH", QByteArray(bin.path().toLocal8Bit() + ":" + m_path));
        PrinterList printers({QStringLiteral("/bin/true")});
        printers.add(QStringLiteral("-x"), QStringLiteral("file:///etc/passwd"));
        QVERIFY(!printers.error().isEmpty());
        QTest::qWait(200);
        QVERIFY(!QFile::exists(log));
    }

    void aRefusalOfThePrintServiceIsSaid()
    {
        QTemporaryDir bin;
        write(bin.filePath(QStringLiteral("lpstat")), "#!/bin/sh\necho 'scheduler is running'\n", true);
        write(bin.filePath(QStringLiteral("lpadmin")), "#!/bin/sh\necho 'lpadmin: Forbidden'\nexit 1\n", true);
        qputenv("PATH", QByteArray(bin.path().toLocal8Bit() + ":" + m_path));
        PrinterList printers({QStringLiteral("/bin/true")});
        printers.remove(QStringLiteral("Bureau"));
        QTRY_VERIFY_WITH_TIMEOUT(!printers.error().isEmpty(), 3000);
        QVERIFY(printers.error().contains(QLatin1String("sudo user")));
    }

    void phonesAreListed()
    {
        PhoneLink link(bus());
        QVERIFY(link.available());
        QCOMPARE(link.announcedName(), QStringLiteral("my-pc"));
        QCOMPARE(link.devices().size(), 2);
        const QVariantMap first = link.devices().at(0).toMap();
        QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("Linked phone"));
        QVERIFY(first.value(QStringLiteral("paired")).toBool());
        QVERIFY(first.value(QStringLiteral("reachable")).toBool());
    }

    void pairingReachesThePhone()
    {
        PhoneLink link(bus());
        link.pair(QStringLiteral("def"));
        QVERIFY(m_bus->fresh->requested);
        QVERIFY(link.devices().at(1).toMap().value(QStringLiteral("requested")).toBool());
        link.cancelPairing(QStringLiteral("def"));
        QVERIFY(!m_bus->fresh->requested);
        link.unpair(QStringLiteral("abc"));
        QVERIFY(!m_bus->linked->paired);
        m_bus->linked->paired = true;
    }

    void aFileGoesToTheShareOfThatPhone()
    {
        PhoneLink link(bus());
        link.sendFile(QStringLiteral("abc"), QUrl::fromLocalFile(QStringLiteral("/home/me/photo.jpg")));
        QCOMPARE(m_bus->share->sent, QStringLiteral("file:///home/me/photo.jpg"));
    }

    void noPhoneServiceSaysSo()
    {
        QDBusServer other;
        PhoneLink link(QDBusConnection::connectToPeer(other.address(), QStringLiteral("nophone")));
        QVERIFY(!link.available());
        QVERIFY(link.devices().isEmpty());
    }

    void startsTheInstalledServiceWhenItIsNotRunning()
    {
        QTemporaryDir dir;
        const QString marker = dir.filePath(QStringLiteral("started"));
        const QString daemon = dir.filePath(QStringLiteral("kdeconnectd"));
        write(daemon, QStringLiteral("#!/bin/sh\necho started >> %1\n").arg(marker).toUtf8(), true);
        QDBusServer other;
        PhoneLink link(QDBusConnection::connectToPeer(other.address(), QStringLiteral("phone-off")), daemon);
        QVERIFY(link.installed());
        QVERIFY(link.starting());
        QVERIFY(!link.available());
        QTRY_VERIFY(QFile::exists(marker));
        link.refresh();
        link.refresh();
        QTest::qWait(300);
        QFile f(marker);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll().count("started"), 1);

        QDBusServer third;
        PhoneLink none(QDBusConnection::connectToPeer(third.address(), QStringLiteral("phone-none")), QString());
        QVERIFY(!none.installed());
        QVERIFY(!none.starting());
    }

    void onlyThePictureNodeIsACamera()
    {
        QTemporaryDir sys;
        write(sys.filePath(QStringLiteral("video0/name")), "Integrated Webcam\n");
        write(sys.filePath(QStringLiteral("video0/index")), "0\n");
        write(sys.filePath(QStringLiteral("video1/name")), "Integrated Webcam\n");
        write(sys.filePath(QStringLiteral("video1/index")), "1\n");
        write(sys.filePath(QStringLiteral("video2/name")), "USB Camera\n");
        write(sys.filePath(QStringLiteral("video2/index")), "0\n");
        QCOMPARE(CameraList::captureDevices(sys.path()), QStringList({QStringLiteral("Integrated Webcam"), QStringLiteral("USB Camera")}));
        QVERIFY(CameraList::captureDevices(sys.filePath(QStringLiteral("missing"))).isEmpty());
    }

    void noProviderMeansNothingToOffer()
    {
        QTemporaryDir dir;
        QCOMPARE(ExternalModule(QStringList{dir.path()}).accountProviders(), QStringList());
        QCOMPARE(ExternalModule(QStringList()).accountProviders(), QStringList());
        write(dir.filePath(QStringLiteral("nextcloud.provider")), "<provider/>\n");
        QCOMPARE(ExternalModule(QStringList{dir.path()}).accountProviders(), QStringList(QStringLiteral("nextcloud")));
    }

    void fingerprintReaderIsClaimedOnlyWhileUsed()
    {
        QDBusConnection connection{QString()};
        Bus *own = ownBus(connection);
        FingerprintSettings reader(connection, QStringLiteral("me"));
        QVERIFY(reader.available());
        QCOMPARE(reader.fingers(), QStringList(QStringLiteral("right-thumb")));
        QVERIFY(own->reader->calls.isEmpty());

        QSignalSpy enrolled(&reader, &FingerprintSettings::enrolled);
        reader.startEnroll(QStringLiteral("left-thumb"));
        QVERIFY(reader.enrolling());
        QCOMPARE(own->reader->calls, QStringList({QStringLiteral("Claim me"), QStringLiteral("EnrollStart left-thumb")}));
        own->reader->calls.clear();

        // a peer bus has no sender name, so its signals do not arrive: the slot directly
        QMetaObject::invokeMethod(&reader, "enrollStatus", Q_ARG(QString, QStringLiteral("enroll-stage-passed")), Q_ARG(bool, false));
        QTRY_COMPARE(reader.stagesDone(), 1);
        QVERIFY(reader.enrolling());
        // a peer bus has no sender name, so its signals do not arrive: the slot directly
        QMetaObject::invokeMethod(&reader, "enrollStatus", Q_ARG(QString, QStringLiteral("enroll-completed")), Q_ARG(bool, true));
        QTRY_COMPARE(enrolled.count(), 1);
        QVERIFY(!reader.enrolling());
        // the reader goes back free
        QCOMPARE(own->reader->calls, QStringList({QStringLiteral("EnrollStop"), QStringLiteral("Release")}));
        own->reader->calls.clear();
    }

    void aFailedScanSaysWhyAndEnds()
    {
        QDBusConnection connection{QString()};
        Bus *own = ownBus(connection);
        FingerprintSettings reader(connection, QStringLiteral("me"));
        QSignalSpy enrolled(&reader, &FingerprintSettings::enrolled);
        reader.startEnroll(QStringLiteral("left-thumb"));
        // a peer bus has no sender name, so its signals do not arrive: the slot directly
        QMetaObject::invokeMethod(&reader, "enrollStatus", Q_ARG(QString, QStringLiteral("enroll-retry-scan")), Q_ARG(bool, false));
        QTRY_VERIFY(reader.message().contains(QLatin1String("try again")));
        QVERIFY(reader.enrolling());
        // a peer bus has no sender name, so its signals do not arrive: the slot directly
        QMetaObject::invokeMethod(&reader, "enrollStatus", Q_ARG(QString, QStringLiteral("enroll-disconnected")), Q_ARG(bool, true));
        QTRY_VERIFY(!reader.enrolling());
        QVERIFY(reader.message().contains(QLatin1String("disconnected")));
        QCOMPARE(enrolled.count(), 0);
        own->reader->calls.clear();
    }

    void aFingerIsAcceptedOnlyWhenTheRulesSaySo()
    {
        QTemporaryDir dir;
        FingerprintSettings reader(bus(), QStringLiteral("me"));
        reader.pamFile = dir.filePath(QStringLiteral("common-auth"));
        QVERIFY(!reader.signInEnabled());
        write(reader.pamFile, "auth [success=2 default=ignore] pam_fprintd.so max-tries=1\n");
        QVERIFY(reader.signInEnabled());
    }

    void noReaderSaysSo()
    {
        QDBusServer other;
        FingerprintSettings reader(QDBusConnection::connectToPeer(other.address(), QStringLiteral("noreader")), QStringLiteral("me"));
        QVERIFY(!reader.available());
        QVERIFY(reader.fingers().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDeviceBackends)
#include "test_devicebackends.moc"
