// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth & devices: groups, icons and order; then the settings
// against a stand-in for the system's Bluetooth service (python-dbusmock
// and its bluez5 template, on a bus of the test's own): the switch, the
// adapter's name, a device found nearby, pairing it, connecting,
// disconnecting and forgetting it. Skipped when dbusmock is missing.
#include "bluetooth.h"

#include <BluezQt/Device>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace {

QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg
        && (qstrcmp(context.category, "qml") == 0 || qstrncmp(context.category, "qt.qml", 6) == 0
            || message.contains(QLatin1String("qrc:")))) {
        qmlWarnings << message;
    }
    fprintf(stderr, "%s\n", qPrintable(message));
}

QString roleText(BluetoothDeviceList *list, int row, int role)
{
    return list->data(list->index(row), role).toString();
}

} // namespace

class TestBluetooth : public QObject
{
    Q_OBJECT

    QProcess m_bus;
    QProcess m_mock;
    bool m_ready = false;

    QDBusInterface *mock()
    {
        static QDBusInterface *iface = nullptr;
        if (!iface)
            iface = new QDBusInterface(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.bluez.Mock"),
                                       QDBusConnection::systemBus(), this);
        return iface;
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        // no session: the pages must not reach a real desktop
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (daemon.isEmpty() || python.isEmpty() || QProcess::execute(python, {QStringLiteral("-c"), QStringLiteral("import dbusmock")}) != 0) {
            // the pages below still load, with no Bluetooth service at all
            qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent");
            return;
        }
        // a bus of the test's own stands for the system bus
        m_bus.start(daemon, {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        QVERIFY(m_bus.waitForStarted());
        QVERIFY(m_bus.waitForReadyRead(5000));
        const QByteArray address = m_bus.readLine().trimmed();
        QVERIFY(!address.isEmpty());
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", address);
        m_mock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_mock.start(python, {QStringLiteral("-m"), QStringLiteral("dbusmock"), QStringLiteral("--system"),
                              QStringLiteral("--template"), QStringLiteral("bluez5")});
        QVERIFY(m_mock.waitForStarted());
        for (int i = 0; i < 100 && !QDBusConnection::systemBus().interface()->isServiceRegistered(QStringLiteral("org.bluez")); ++i)
            QTest::qWait(50);
        QVERIFY(QDBusConnection::systemBus().interface()->isServiceRegistered(QStringLiteral("org.bluez")));
        // the template answers ProfileManager1 calls but does not list the
        // interface, and BluezQt will not start without seeing it
        QDBusInterface bluez(QStringLiteral("org.bluez"), QStringLiteral("/org/bluez"), QStringLiteral("org.freedesktop.DBus.Mock"),
                             QDBusConnection::systemBus());
        const QDBusMessage listed = bluez.call(QStringLiteral("AddProperties"), QStringLiteral("org.bluez.ProfileManager1"),
                                               QVariantMap{{QStringLiteral("Listed"), true}});
        QVERIFY2(listed.type() != QDBusMessage::ErrorMessage, qPrintable(listed.errorMessage()));
        m_ready = true;
    }

    void cleanupTestCase()
    {
        if (m_mock.state() != QProcess::NotRunning) {
            m_mock.terminate();
            m_mock.waitForFinished(3000);
        }
        if (m_bus.state() != QProcess::NotRunning) {
            m_bus.terminate();
            m_bus.waitForFinished(3000);
        }
    }

    void groups()
    {
        QCOMPARE(BluetoothSettings::groupOf(BluezQt::Device::Mouse), QStringLiteral("input"));
        QCOMPARE(BluetoothSettings::groupOf(BluezQt::Device::Keyboard), QStringLiteral("input"));
        QCOMPARE(BluetoothSettings::groupOf(BluezQt::Device::Tablet), QStringLiteral("input"));
        QCOMPARE(BluetoothSettings::groupOf(BluezQt::Device::Headphones), QStringLiteral("audio"));
        QCOMPARE(BluetoothSettings::groupOf(BluezQt::Device::AudioVideo), QStringLiteral("audio"));
        QCOMPARE(BluetoothSettings::groupOf(BluezQt::Device::Phone), QStringLiteral("other"));
        QCOMPARE(BluetoothSettings::groupOf(BluezQt::Device::Joypad), QStringLiteral("other"));
        QCOMPARE(BluetoothSettings::iconFor(BluezQt::Device::Mouse, QString()), QStringLiteral("input-mouse"));
        QCOMPARE(BluetoothSettings::iconFor(BluezQt::Device::Mouse, QStringLiteral("own-icon")), QStringLiteral("own-icon"));
        QCOMPARE(BluetoothSettings::iconFor(BluezQt::Device::Uncategorized, QString()), QStringLiteral("preferences-system-bluetooth"));
    }

    void order()
    {
        QVERIFY(BluetoothSettings::before(true, QStringLiteral("Zed"), false, QStringLiteral("Alpha")));
        QVERIFY(BluetoothSettings::before(false, QStringLiteral("alpha"), false, QStringLiteral("Beta")));
        QVERIFY(!BluetoothSettings::before(false, QStringLiteral("Beta"), false, QStringLiteral("alpha")));
    }

    void noAdapter()
    {
        if (!m_ready)
            QSKIP("python3-dbusmock is not installed");
        BluetoothSettings bt;
        QTest::qWait(500);
        QVERIFY(!bt.available());
        QVERIFY(!bt.powered());
        QCOMPARE(bt.pairedDevices()->rowCount(), 0);
        // nothing to act on, and nothing breaks
        QVERIFY(!bt.pair(QStringLiteral("11:22:33:44:55:66")));
        bt.setPowered(true);
        bt.startScan();
        bt.stopScan();
    }

    void adapterAndDevices()
    {
        if (!m_ready)
            QSKIP("python3-dbusmock is not installed");
        QDBusReply<QString> added = mock()->call(QStringLiteral("AddAdapter"), QStringLiteral("hci0"), QStringLiteral("Raye PC"));
        QVERIFY2(added.isValid(), qPrintable(added.error().message()));

        BluetoothSettings bt;
        QTRY_VERIFY_WITH_TIMEOUT(bt.available(), 5000);
        QVERIFY(bt.powered());
        QCOMPARE(bt.adapterName(), QStringLiteral("Raye PC"));

        // a phone nearby, not paired
        QVERIFY(mock()->call(QStringLiteral("AddDevice"), QStringLiteral("hci0"), QStringLiteral("11:22:33:44:55:66"), QStringLiteral("Alex's phone"))
                    .type() != QDBusMessage::ErrorMessage);
        QTRY_COMPARE_WITH_TIMEOUT(bt.nearbyDevices()->rowCount(), 1, 5000);
        QCOMPARE(bt.pairedDevices()->rowCount(), 0);
        QCOMPARE(roleText(bt.nearbyDevices(), 0, BluetoothDeviceList::NameRole), QStringLiteral("Alex's phone"));
        QCOMPARE(roleText(bt.nearbyDevices(), 0, BluetoothDeviceList::GroupRole), QStringLiteral("other"));
        QCOMPARE(bt.nearbyDevices()->data(bt.nearbyDevices()->index(0), BluetoothDeviceList::BatteryRole).toInt(), -1);

        // pair it: it moves to this PC's devices, trusted and connected
        QSignalSpy paired(&bt, &BluetoothSettings::pairingFinished);
        QVERIFY(bt.pair(QStringLiteral("11:22:33:44:55:66")));
        QVERIFY(paired.wait(5000));
        QCOMPARE(paired.first().at(1).toBool(), true);
        QTRY_COMPARE_WITH_TIMEOUT(bt.pairedDevices()->rowCount(), 1, 5000);
        QCOMPARE(bt.nearbyDevices()->rowCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(bt.pairedDevices()->data(bt.pairedDevices()->index(0), BluetoothDeviceList::ConnectedRole).toBool(), 5000);

        QVERIFY(bt.disconnectDevice(QStringLiteral("11:22:33:44:55:66")));
        QTRY_VERIFY_WITH_TIMEOUT(!bt.pairedDevices()->data(bt.pairedDevices()->index(0), BluetoothDeviceList::ConnectedRole).toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!bt.pairedDevices()->data(bt.pairedDevices()->index(0), BluetoothDeviceList::BusyRole).toBool(), 5000);
        QVERIFY(bt.connectDevice(QStringLiteral("11:22:33:44:55:66")));
        QTRY_VERIFY_WITH_TIMEOUT(bt.pairedDevices()->data(bt.pairedDevices()->index(0), BluetoothDeviceList::ConnectedRole).toBool(), 5000);

        // the switch
        bt.setPowered(false);
        QTRY_VERIFY_WITH_TIMEOUT(!bt.powered(), 5000);
        bt.setPowered(true);
        QTRY_VERIFY_WITH_TIMEOUT(bt.powered(), 5000);

        // the search the Add a device dialog runs (the template wants a
        // filter set before it searches; the real service does not)
        QDBusInterface adapter(QStringLiteral("org.bluez"), QStringLiteral("/org/bluez/hci0"), QStringLiteral("org.bluez.Adapter1"),
                               QDBusConnection::systemBus());
        adapter.call(QStringLiteral("SetDiscoveryFilter"), QVariantMap());
        bt.startScan();
        QTRY_VERIFY_WITH_TIMEOUT(bt.scanning(), 5000);
        bt.stopScan();
        QTRY_VERIFY_WITH_TIMEOUT(!bt.scanning(), 5000);

        // forget it
        QVERIFY(bt.removeDevice(QStringLiteral("11:22:33:44:55:66")));
        QTRY_COMPARE_WITH_TIMEOUT(bt.pairedDevices()->rowCount(), 0, 5000);
    }

    // the pages of this package load as Settings shows them, with the
    // adapter of the test (or with no Bluetooth service): no QML warning,
    // and every row the search knows is on its page
    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        QTest::newRow("landing") << QStringLiteral("pages/devices/DevicesPage.qml") << QString();
        QTest::newRow("bluetooth") << QStringLiteral("pages/devices/BluetoothPage.qml") << QStringLiteral("devices/bluetooth");
    }

    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);

        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/bluetooth.json"));
        QVERIFY(fragment.open(QIODevice::ReadOnly));
        QStringList indexed;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        QVERIFY(!entries.isEmpty());
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
            if (!page.isEmpty() && id.startsWith(page + QLatin1Char('#')))
                indexed << id.section(QLatin1Char('#'), 1);
        }

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            QQuickView view(&engine, nullptr);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1120, 760);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + file));
            QCOMPARE(view.status(), QQuickView::Ready);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(800);
            if (!page.isEmpty()) {
                QVariant ids;
                QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
                const QStringList drawn = ids.toStringList();
                for (const QString &id : std::as_const(indexed))
                    QVERIFY2(drawn.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not on the page")));
                for (const QString &id : drawn)
                    QVERIFY2(indexed.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not in data/search")));
            }
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestBluetooth)
#include "test_bluetooth.moc"
