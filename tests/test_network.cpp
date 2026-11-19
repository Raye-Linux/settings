// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet: the checks and settings the pages rely on, then
// the backends and pages against a pretend NetworkManager
// (python3-dbusmock) on a bus of the test's own, so the computer's real
// network is never touched.
#include "hotspot.h"
#include "layout.h"
#include "network.h"
#include "proxy.h"
#include "router.h"
#include "vpn.h"
#include "wifi.h"

#include <KLocalizedContext>
#include <NetworkManagerQt/Settings>
#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusVariant>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QHostAddress>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <unistd.h>

namespace {

const QString mockService = QStringLiteral("org.freedesktop.NetworkManager");
const QString mockPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString mockInterface = QStringLiteral("org.freedesktop.DBus.Mock");

// A bus standing in for the system bus, with dbusmock's NetworkManager
// on it. Started before anything in the program speaks D-Bus.
struct PretendNetwork {
    QProcess bus;
    QProcess mock;
    QString address;
    bool running = false;

    void start()
    {
        // without a bus of our own, nothing may reach the real one
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/raye-test-bus");
        if (QStandardPaths::findExecutable(QStringLiteral("dbus-daemon")).isEmpty())
            return;
        bus.start(QStringLiteral("dbus-daemon"),
                  {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!bus.waitForStarted() || !bus.waitForReadyRead(5000))
            return;
        address = QString::fromUtf8(bus.readLine()).trimmed();
        if (address.isEmpty())
            return;
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", address.toUtf8());

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"), address);
        mock.setProcessEnvironment(env);
        mock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        mock.start(QStringLiteral("python3"), {QStringLiteral("-m"), QStringLiteral("dbusmock"),
                                               QStringLiteral("--system"), QStringLiteral("--template"),
                                               QStringLiteral("networkmanager")});
        if (!mock.waitForStarted())
            return;
        QDBusConnection connection = QDBusConnection::connectToBus(address, QStringLiteral("raye-test-setup"));
        for (int i = 0; i < 100 && !running; ++i) {
            QTest::qWait(50);
            running = connection.isConnected() && connection.interface()->isServiceRegistered(mockService);
        }
    }

    ~PretendNetwork()
    {
        mock.kill();
        mock.waitForFinished();
        bus.kill();
        bus.waitForFinished();
    }
};

PretendNetwork *pretend = nullptr;

QDBusConnection setupBus()
{
    return QDBusConnection::connectToBus(pretend->address, QStringLiteral("raye-test-setup"));
}

QString callMock(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(mockService, mockPath, mockInterface, method);
    call.setArguments(arguments);
    const QDBusMessage reply = setupBus().call(call, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning("mock %s: %s", qPrintable(method), qPrintable(reply.errorMessage()));
        return {};
    }
    return reply.arguments().isEmpty() ? QString() : reply.arguments().constFirst().toString();
}

QVariantMap section(const NMVariantMapMap &settings, const char *name)
{
    return settings.value(QString::fromLatin1(name));
}

NMVariantMapMap wiredSettings()
{
    NMVariantMapMap settings;
    settings.insert(QStringLiteral("connection"), {{QStringLiteral("id"), QStringLiteral("Wired")},
                                                   {QStringLiteral("uuid"), QStringLiteral("8a2c7e1e-5d1f-4c55-9a35-1f8f0c3e7a11")},
                                                   {QStringLiteral("type"), QStringLiteral("802-3-ethernet")}});
    return settings;
}

} // namespace

class TestNetwork : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QLocale::setDefault(QLocale::c());
        QStandardPaths::setTestModeEnabled(true);
    }

    // --- network.h

    void bars_data()
    {
        QTest::addColumn<int>("strength");
        QTest::addColumn<int>("bars");
        QTest::newRow("none") << 0 << 0;
        QTest::newRow("weak") << 10 << 1;
        QTest::newRow("low") << 30 << 2;
        QTest::newRow("good") << 60 << 3;
        QTest::newRow("full") << 90 << 4;
        QTest::newRow("over") << 140 << 4;
        QTest::newRow("under") << -5 << 0;
    }
    void bars()
    {
        QFETCH(int, strength);
        QFETCH(int, bars);
        QCOMPARE(NetworkText::signalBars(strength), bars);
    }

    void addresses()
    {
        QVERIFY(NetworkText::isIpv4(QStringLiteral("192.168.1.20")));
        QVERIFY(!NetworkText::isIpv4(QStringLiteral("192.168.1")));
        QVERIFY(!NetworkText::isIpv4(QStringLiteral("192.168.1.256")));
        QVERIFY(!NetworkText::isIpv4(QStringLiteral("fe80::1")));
        QVERIFY(!NetworkText::isIpv4(QStringLiteral(" 10.0.0.1")));
        QVERIFY(!NetworkText::isIpv4(QString()));
        QVERIFY(NetworkText::isIpv6(QStringLiteral("2001:db8::1")));
        QVERIFY(!NetworkText::isIpv6(QStringLiteral("10.0.0.1")));
        QVERIFY(!NetworkText::isIpv6(QStringLiteral("2001:db8::zz")));

        QCOMPARE(NetworkText::ipv4Prefix(QStringLiteral("24")), 24);
        QCOMPARE(NetworkText::ipv4Prefix(QStringLiteral("255.255.255.0")), 24);
        QCOMPARE(NetworkText::ipv4Prefix(QStringLiteral("255.255.240.0")), 20);
        QCOMPARE(NetworkText::ipv4Prefix(QStringLiteral("255.0.255.0")), -1); // not a mask
        QCOMPARE(NetworkText::ipv4Prefix(QStringLiteral("33")), -1);
        QCOMPARE(NetworkText::ipv4Prefix(QStringLiteral("0")), -1);
        QCOMPARE(NetworkText::ipv4Prefix(QStringLiteral("abc")), -1);
        QCOMPARE(NetworkText::ipv4Mask(24), QStringLiteral("255.255.255.0"));
        QCOMPARE(NetworkText::ipv4Mask(20), QStringLiteral("255.255.240.0"));
        QCOMPARE(NetworkText::ipv4Mask(32), QStringLiteral("255.255.255.255"));

        QVERIFY(NetworkText::sameIpv4Network(QStringLiteral("192.168.1.20"), QStringLiteral("192.168.1.1"), 24));
        QVERIFY(!NetworkText::sameIpv4Network(QStringLiteral("192.168.1.20"), QStringLiteral("192.168.2.1"), 24));
        QVERIFY(NetworkText::sameIpv4Network(QStringLiteral("10.1.2.3"), QStringLiteral("10.9.9.9"), 8));
    }

    void ipConfig_data()
    {
        QTest::addColumn<QVariantMap>("config");
        QTest::addColumn<QString>("problem");
        const auto v4 = [](const QString &method, const QString &address, const QVariant &prefix,
                           const QString &gateway) {
            return QVariantMap{{QStringLiteral("method"), method},
                               {QStringLiteral("address"), address},
                               {QStringLiteral("prefix"), prefix},
                               {QStringLiteral("gateway"), gateway}};
        };
        QTest::newRow("automatic") << QVariantMap{{QStringLiteral("ipv4"), v4(QStringLiteral("auto"), {}, {}, {})}}
                                   << QString();
        QTest::newRow("manual") << QVariantMap{{QStringLiteral("ipv4"), v4(QStringLiteral("manual"),
                                                QStringLiteral("192.168.1.20"), 24, QStringLiteral("192.168.1.1"))}}
                                << QString();
        QTest::newRow("mask") << QVariantMap{{QStringLiteral("ipv4"), v4(QStringLiteral("manual"),
                                              QStringLiteral("192.168.1.20"), QStringLiteral("255.255.255.0"), {})}}
                              << QString();
        QTest::newRow("bad address") << QVariantMap{{QStringLiteral("ipv4"), v4(QStringLiteral("manual"),
                                                     QStringLiteral("192.168.1"), 24, {})}}
                                     << QStringLiteral("ipv4-address");
        QTest::newRow("bad prefix") << QVariantMap{{QStringLiteral("ipv4"), v4(QStringLiteral("manual"),
                                                    QStringLiteral("192.168.1.20"), QStringLiteral("255.0.255.0"), {})}}
                                    << QStringLiteral("ipv4-prefix");
        QTest::newRow("far gateway") << QVariantMap{{QStringLiteral("ipv4"), v4(QStringLiteral("manual"),
                                                     QStringLiteral("192.168.1.20"), 24, QStringLiteral("10.0.0.1"))}}
                                     << QStringLiteral("ipv4-gateway");
        QTest::newRow("bad method") << QVariantMap{{QStringLiteral("ipv4"), v4(QStringLiteral("magic"), {}, {}, {})}}
                                    << QStringLiteral("ipv4-address");
        QTest::newRow("ipv6") << QVariantMap{{QStringLiteral("ipv6"), v4(QStringLiteral("manual"),
                                              QStringLiteral("2001:db8::20"), 64, QStringLiteral("2001:db8::1"))}}
                              << QString();
        QTest::newRow("ipv6 prefix") << QVariantMap{{QStringLiteral("ipv6"), v4(QStringLiteral("manual"),
                                                     QStringLiteral("2001:db8::20"), 129, {})}}
                                     << QStringLiteral("ipv6-prefix");
        QTest::newRow("ipv6 gateway") << QVariantMap{{QStringLiteral("ipv6"), v4(QStringLiteral("manual"),
                                                      QStringLiteral("2001:db8::20"), 64, QStringLiteral("10.0.0.1"))}}
                                      << QStringLiteral("ipv6-gateway");
        QTest::newRow("dns") << QVariantMap{{QStringLiteral("dnsAuto"), false},
                                            {QStringLiteral("dns4"), QStringList{QStringLiteral("1.1.1.1"), QString()}}}
                             << QString();
        QTest::newRow("dns empty") << QVariantMap{{QStringLiteral("dnsAuto"), false},
                                                  {QStringLiteral("dns4"), QStringList{}}}
                                   << QStringLiteral("dns4");
        QTest::newRow("dns bad") << QVariantMap{{QStringLiteral("dnsAuto"), false},
                                                {QStringLiteral("dns4"), QStringList{QStringLiteral("1.1.1")}}}
                                 << QStringLiteral("dns4");
        QTest::newRow("dns6 bad") << QVariantMap{{QStringLiteral("dnsAuto"), false},
                                                 {QStringLiteral("dns4"), QStringList{QStringLiteral("9.9.9.9")}},
                                                 {QStringLiteral("dns6"), QStringList{QStringLiteral("9.9.9.9")}}}
                                  << QStringLiteral("dns6");
        QTest::newRow("dns only ipv6") << QVariantMap{{QStringLiteral("dnsAuto"), false},
                                                      {QStringLiteral("dns6"), QStringList{QStringLiteral("2606:4700::1111")}}}
                                       << QString();
    }
    void ipConfig()
    {
        QFETCH(QVariantMap, config);
        QFETCH(QString, problem);
        QCOMPARE(NetworkText::checkIpConfig(config), problem);
        if (!problem.isEmpty())
            QVERIFY(!NetworkText::errorText(problem).isEmpty());
    }

    void networkProfile_data()
    {
        QTest::addColumn<QString>("zone");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("profile");
        QTest::newRow("home wifi") << QString() << "802-11-wireless" << "secured" << "private";
        QTest::newRow("cable") << QString() << "802-3-ethernet" << QString() << "private";
        QTest::newRow("open wifi") << QString() << "802-11-wireless" << QString() << "public";
        QTest::newRow("enhanced open") << QString() << "802-11-wireless" << "owe" << "public";
        QTest::newRow("mobile") << QString() << "gsm" << QString() << "public";
        QTest::newRow("marked public") << "public" << "802-3-ethernet" << QString() << "public";
        QTest::newRow("marked private") << "home" << "802-11-wireless" << QString() << "private";
    }
    void networkProfile()
    {
        QFETCH(QString, zone);
        QFETCH(QString, type);
        QFETCH(QString, key);
        QFETCH(QString, profile);
        QCOMPARE(NetworkText::networkProfile(zone, type, key), profile);
        QCOMPARE(NetworkText::networkProfile(NetworkText::zoneOf(profile), type, key), profile);
    }

    void encryptedDns()
    {
        const QVariantList presets = NetworkText::dnsPresets();
        QCOMPARE(presets.size(), 3);
        QCOMPARE(NetworkText::dnsPresetOf({QStringLiteral("1.1.1.1"), QStringLiteral("1.0.0.1")},
                                          {QStringLiteral("2606:4700:4700::1111"), QStringLiteral("2606:4700:4700::1001")}),
                 QStringLiteral("cloudflare"));
        QCOMPARE(NetworkText::dnsPresetOf({QStringLiteral("9.9.9.9"), QString()}, {}), QString());
        QCOMPARE(NetworkText::dnsPresetOf({QStringLiteral("9.9.9.9"), QStringLiteral("149.112.112.112")}, {}),
                 QStringLiteral("quad9"));
        QCOMPARE(NetworkText::dnsPresetOf({QStringLiteral("8.8.8.8"), QStringLiteral("8.8.4.4")},
                                          {QStringLiteral("2606:4700:4700::1111")}), QString());
        QCOMPARE(NetworkText::dnsTlsName(QStringLiteral("2620:00fe::fe")), QStringLiteral("dns.quad9.net"));
        QCOMPARE(NetworkText::dnsTlsName(QStringLiteral("192.168.1.1")), QString());
        QCOMPARE(NetworkText::dnsData({QStringLiteral("8.8.8.8"), QStringLiteral("10.0.0.1"), QString()}, true),
                 QStringList({QStringLiteral("8.8.8.8#dns.google"), QStringLiteral("10.0.0.1")}));
        QCOMPARE(NetworkText::dnsData({QStringLiteral("8.8.8.8")}, false), QStringList{QStringLiteral("8.8.8.8")});
        for (const QString &e : {QStringLiteral("off"), QStringLiteral("preferred"), QStringLiteral("only")})
            QCOMPARE(NetworkText::dnsEncryption(NetworkText::dnsOverTls(e)), e);
        QCOMPARE(NetworkText::dnsOverTls(QString()), -1);
        QCOMPARE(NetworkText::dnsEncryption(-1), QStringLiteral("off"));
    }

    void text()
    {
        QCOMPARE(NetworkText::byteSize(0), QStringLiteral("0 B"));
        QCOMPARE(NetworkText::byteSize(900), QStringLiteral("900 B"));
        QCOMPARE(NetworkText::byteSize(1536), QStringLiteral("1.5 KB"));
        QCOMPARE(NetworkText::byteSize(25ull * 1024 * 1024), QStringLiteral("25 MB"));
        QCOMPARE(NetworkText::byteSize(3ull * 1024 * 1024 * 1024 + 512ull * 1024 * 1024), QStringLiteral("3.5 GB"));
        QCOMPARE(NetworkText::statusText(true, true, true), QStringLiteral("Connected, secured"));
        QCOMPARE(NetworkText::statusText(true, false, true), QStringLiteral("Connected"));
        QCOMPARE(NetworkText::statusText(true, true, false), QStringLiteral("No internet"));
        QCOMPARE(NetworkText::statusText(false, false, false), QStringLiteral("Not connected"));
    }

    void wifiSummary()
    {
        QCOMPARE(NetworkText::wifiSummary(false, QStringLiteral("ethernet"), QString()), QStringLiteral("Off"));
        QCOMPARE(NetworkText::wifiSummary(true, QStringLiteral("wifi"), QStringLiteral("<b>Cafe</b>")),
                 QStringLiteral("Connected, &lt;b&gt;Cafe&lt;/b&gt;"));
        QCOMPARE(NetworkText::wifiSummary(true, QStringLiteral("ethernet"), QStringLiteral("Wired")),
                 QStringLiteral("Not in use, this PC is connected by cable"));
        QCOMPARE(NetworkText::wifiSummary(true, QStringLiteral("none"), QString()), QStringLiteral("Not connected"));
    }

    void wording()
    {
        const QVariantMap none = NetworkText::wording(false, false);
        for (auto it = none.cbegin(); it != none.cend(); ++it) {
            QVERIFY2(!it.value().toString().contains(QLatin1String("Wi-Fi")), qPrintable(it.key()));
            QVERIFY2(!it.value().toString().contains(QLatin1String("Bluetooth")), qPrintable(it.key()));
        }
        QCOMPARE(none.value(QStringLiteral("offline")).toString(), QStringLiteral("Plug in a network cable to get online"));
        const QVariantMap both = NetworkText::wording(true, true);
        QCOMPARE(both.keys(), none.keys());
        QCOMPARE(both.value(QStringLiteral("offline")).toString(),
                 QStringLiteral("Connect to Wi-Fi or plug in a cable to get online"));
        const QString bluetoothOnly = NetworkText::wording(false, true).value(QStringLiteral("airplaneOff")).toString();
        QVERIFY(bluetoothOnly.contains(QLatin1String("Bluetooth")));
        QVERIFY(!bluetoothOnly.contains(QLatin1String("Wi-Fi")));
    }

    // --- wifi.h

    void merge()
    {
        using WifiList::AccessPoint;
        const QList<AccessPoint> points = {
            {QStringLiteral("Home"), 40, QStringLiteral("wpa-psk"), 2412, QStringLiteral("/ap/1")},
            {QStringLiteral("Home"), 80, QStringLiteral("wpa-psk"), 5180, QStringLiteral("/ap/2")},
            {QStringLiteral("Cafe"), 95, QStringLiteral("open"), 2437, QStringLiteral("/ap/3")},
            {QString(), 99, QStringLiteral("wpa-psk"), 2437, QStringLiteral("/ap/4")},
            {QStringLiteral("Neighbour"), 95, QStringLiteral("sae"), 2462, QStringLiteral("/ap/5")},
            {QStringLiteral("Office"), 20, QStringLiteral("wpa-psk"), 2412, QStringLiteral("/ap/6")},
        };
        const QHash<QString, QString> known = {{QStringLiteral("Office"), QStringLiteral("uuid-office")},
                                               {QStringLiteral("Home"), QStringLiteral("uuid-home")}};
        const auto rows = WifiList::merge(points, known, QStringLiteral("Home"));
        QCOMPARE(rows.size(), 4); // two "Home" are one, the hidden one is gone
        QCOMPARE(rows[0].ssid, QStringLiteral("Home"));
        QVERIFY(rows[0].connected);
        QCOMPARE(rows[0].strength, 80);
        QCOMPARE(rows[0].path, QStringLiteral("/ap/2"));
        QVERIFY(rows[0].band5);
        QCOMPARE(rows[0].uuid, QStringLiteral("uuid-home"));
        QCOMPARE(rows[1].ssid, QStringLiteral("Office")); // known before stronger strangers
        QVERIFY(rows[1].known);
        QCOMPARE(rows[2].ssid, QStringLiteral("Cafe")); // same signal: by name
        QCOMPARE(rows[3].ssid, QStringLiteral("Neighbour"));
        QVERIFY(!rows[3].known);
    }

    void security()
    {
        QCOMPARE(WifiList::securityOf(0, 0, 0), QStringLiteral("open"));
        QCOMPARE(WifiList::securityOf(1, 0, 0), QStringLiteral("wep"));
        QCOMPARE(WifiList::securityOf(1, 0x100, 0), QStringLiteral("wpa-psk"));
        QCOMPARE(WifiList::securityOf(1, 0, 0x100), QStringLiteral("wpa-psk"));
        QCOMPARE(WifiList::securityOf(1, 0, 0x400), QStringLiteral("sae"));
        QCOMPARE(WifiList::securityOf(1, 0, 0x500), QStringLiteral("wpa-psk")); // WPA2/WPA3 mixed: PSK works
        QCOMPARE(WifiList::securityOf(1, 0x200, 0), QStringLiteral("enterprise"));
        QCOMPARE(WifiList::securityOf(0, 0, 0x800), QStringLiteral("owe"));
        QVERIFY(!WifiList::isSecured(QStringLiteral("open")));
        QVERIFY(!WifiList::isSecured(QStringLiteral("owe")));
        QVERIFY(WifiList::isSecured(QStringLiteral("sae")));
        QVERIFY(WifiList::canJoin(QStringLiteral("wpa-psk")));
        QVERIFY(!WifiList::canJoin(QStringLiteral("enterprise")));
    }

    void passwords_data()
    {
        QTest::addColumn<QString>("security");
        QTest::addColumn<QString>("password");
        QTest::addColumn<bool>("ok");
        QTest::newRow("wpa short") << "wpa-psk" << "1234567" << false;
        QTest::newRow("wpa 8") << "wpa-psk" << "12345678" << true;
        QTest::newRow("wpa 63") << "wpa-psk" << QString(63, QLatin1Char('a')) << true;
        QTest::newRow("wpa 64 text") << "wpa-psk" << QString(64, QLatin1Char('z')) << false;
        QTest::newRow("wpa 64 hex") << "wpa-psk" << QString(64, QLatin1Char('a')) << true;
        QTest::newRow("wpa accent") << "wpa-psk" << QStringLiteral("éléphant!") << false;
        QTest::newRow("sae short") << "sae" << "abc" << true;
        QTest::newRow("sae empty") << "sae" << "" << false;
        QTest::newRow("wep 5") << "wep" << "abcde" << true;
        QTest::newRow("wep 10 hex") << "wep" << "0123456789" << true;
        QTest::newRow("wep 7") << "wep" << "abcdefg" << false;
        QTest::newRow("open") << "open" << "" << true;
    }
    void passwords()
    {
        QFETCH(QString, security);
        QFETCH(QString, password);
        QFETCH(bool, ok);
        QCOMPARE(WifiList::passwordAcceptable(security, password), ok);
    }

    void newConnection()
    {
        NMVariantMapMap s = WifiList::newConnection(QStringLiteral("Home"), QStringLiteral("wpa-psk"),
                                                   QStringLiteral("secret123"), false, true);
        QCOMPARE(section(s, "connection").value(QStringLiteral("type")).toString(), QStringLiteral("802-11-wireless"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("id")).toString(), QStringLiteral("Home"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("autoconnect")).toBool(), false);
        QVERIFY(!section(s, "connection").value(QStringLiteral("uuid")).toString().isEmpty());
        QCOMPARE(section(s, "802-11-wireless").value(QStringLiteral("ssid")).toByteArray(), QByteArray("Home"));
        QCOMPARE(section(s, "802-11-wireless").value(QStringLiteral("assigned-mac-address")).toString(),
                 QStringLiteral("stable"));
        QCOMPARE(section(s, "802-11-wireless-security").value(QStringLiteral("key-mgmt")).toString(),
                 QStringLiteral("wpa-psk"));
        QCOMPARE(section(s, "802-11-wireless-security").value(QStringLiteral("psk")).toString(),
                 QStringLiteral("secret123"));

        s = WifiList::newConnection(QStringLiteral("Cafe"), QStringLiteral("open"), QString(), true, false);
        QVERIFY(!s.contains(QStringLiteral("802-11-wireless-security")));
        // off is said out loud: the system's default is a private address
        QCOMPARE(section(s, "802-11-wireless").value(QStringLiteral("assigned-mac-address")).toString(),
                 QStringLiteral("permanent"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("autoconnect")).toBool(), true);

        s = WifiList::newConnection(QStringLiteral("New"), QStringLiteral("sae"), QStringLiteral("x"), true, false);
        QCOMPARE(section(s, "802-11-wireless-security").value(QStringLiteral("key-mgmt")).toString(), QStringLiteral("sae"));
        s = WifiList::newConnection(QStringLiteral("Old"), QStringLiteral("wep"), QStringLiteral("abcde"), true, false);
        QCOMPARE(section(s, "802-11-wireless-security").value(QStringLiteral("key-mgmt")).toString(), QStringLiteral("none"));
        QCOMPARE(section(s, "802-11-wireless-security").value(QStringLiteral("wep-key0")).toString(), QStringLiteral("abcde"));
        s = WifiList::newConnection(QStringLiteral("Open+"), QStringLiteral("owe"), QString(), true, false);
        QCOMPARE(section(s, "802-11-wireless-security").value(QStringLiteral("key-mgmt")).toString(), QStringLiteral("owe"));
    }

    // --- vpn.h

    void wireguard()
    {
        const QByteArray file = "# from the provider\n"
                                "[Interface]\n"
                                "PrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=\n"
                                "Address = 10.2.0.2/32, fd00::2/128\n"
                                "DNS = 10.2.0.1\n"
                                "ListenPort = 51820\n"
                                "\n"
                                "[Peer]\n"
                                "PublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\n"
                                "PresharedKey = /UwcSPg38hW/D9Y3tcS1FOV0K1wuURMbS0sesJEP5ak=\n"
                                "AllowedIPs = 0.0.0.0/0, ::/0\n"
                                "Endpoint = vpn.example.org:51820\n"
                                "PersistentKeepalive = 25\n";
        NMVariantMapMap s;
        QCOMPARE(VpnText::readWireGuard(file, QStringLiteral("Work VPN"), &s), QString());
        QCOMPARE(section(s, "connection").value(QStringLiteral("type")).toString(), QStringLiteral("wireguard"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("id")).toString(), QStringLiteral("Work VPN"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("interface-name")).toString(), QStringLiteral("Work-VPN"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("autoconnect")).toBool(), false);
        QCOMPARE(section(s, "wireguard").value(QStringLiteral("private-key")).toString(),
                 QStringLiteral("yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk="));
        QCOMPARE(section(s, "wireguard").value(QStringLiteral("listen-port")).toUInt(), 51820u);
        const NMVariantMapList peers = qdbus_cast<NMVariantMapList>(section(s, "wireguard").value(QStringLiteral("peers")));
        QCOMPARE(peers.size(), 1);
        QCOMPARE(peers[0].value(QStringLiteral("endpoint")).toString(), QStringLiteral("vpn.example.org:51820"));
        QCOMPARE(peers[0].value(QStringLiteral("allowed-ips")).toStringList(),
                 (QStringList{QStringLiteral("0.0.0.0/0"), QStringLiteral("::/0")}));
        QCOMPARE(peers[0].value(QStringLiteral("persistent-keepalive")).toUInt(), 25u);
        QCOMPARE(section(s, "ipv4").value(QStringLiteral("method")).toString(), QStringLiteral("manual"));
        QCOMPARE(section(s, "ipv6").value(QStringLiteral("method")).toString(), QStringLiteral("manual"));

        const auto problem = [](const QByteArray &f) {
            NMVariantMapMap ignored;
            return VpnText::readWireGuard(f, QStringLiteral("x"), &ignored);
        };
        const QByteArray head = "[Interface]\nPrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=\nAddress = 10.2.0.2/32\n";
        const QByteArray peer = "[Peer]\nPublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\nAllowedIPs = 0.0.0.0/0\nEndpoint = 1.2.3.4:51820\n";
        QCOMPARE(problem(head + peer), QString());
        QCOMPARE(problem(""), QStringLiteral("empty"));
        QCOMPARE(problem(QByteArray(70000, 'a')), QStringLiteral("too-big"));
        QCOMPARE(problem(peer), QStringLiteral("no-interface"));
        QCOMPARE(problem("[Interface]\nAddress = 10.2.0.2/32\n" + peer), QStringLiteral("private-key"));
        QCOMPARE(problem("[Interface]\nPrivateKey = short\nAddress = 10.2.0.2/32\n" + peer), QStringLiteral("private-key"));
        QCOMPARE(problem("[Interface]\nPrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=\nAddress = 10.2.0/32\n" + peer),
                 QStringLiteral("address"));
        QCOMPARE(problem(head), QStringLiteral("peer"));
        QCOMPARE(problem(head + "[Peer]\nAllowedIPs = 0.0.0.0/0\nEndpoint = 1.2.3.4:51820\n"), QStringLiteral("public-key"));
        QCOMPARE(problem(head + "[Peer]\nPublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\nAllowedIPs = 0.0.0.0/0\nEndpoint = nowhere\n"),
                 QStringLiteral("endpoint"));
        QCOMPARE(problem(head + "[Peer]\nPublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\nAllowedIPs = all\nEndpoint = 1.2.3.4:5\n"),
                 QStringLiteral("allowed-ips"));
        QCOMPARE(problem(head + "DNS = nope\n" + peer), QStringLiteral("dns"));
        QCOMPARE(problem(head + "ListenPort = 99999\n" + peer), QStringLiteral("port"));
        QVERIFY(!VpnText::errorText(QStringLiteral("peer")).isEmpty());

        QCOMPARE(VpnText::interfaceName(QStringLiteral("My very long VPN name")), QStringLiteral("My-very-long-VP"));
        QCOMPARE(VpnText::interfaceName(QStringLiteral("../$(x)")), QStringLiteral("x"));
        QCOMPARE(VpnText::interfaceName(QStringLiteral("!!!")), QStringLiteral("wg-raye"));
        QCOMPARE(VpnText::uuidFromImport(QStringLiteral("Connection 'work' (3f1b2c4d-0000-4000-8000-1234567890ab) successfully added.\n")),
                 QStringLiteral("3f1b2c4d-0000-4000-8000-1234567890ab"));
        QCOMPARE(VpnText::uuidFromImport(QStringLiteral("Error: failed")), QString());
        QCOMPARE(VpnText::kindName(QStringLiteral("org.freedesktop.NetworkManager.openvpn")), QStringLiteral("OpenVPN"));
    }

    // --- hotspot.h

    void hotspot()
    {
        QCOMPARE(HotspotText::defaultName(QStringLiteral("raye-pc")), QStringLiteral("raye-pc hotspot"));
        QVERIFY(HotspotText::defaultName(QString(40, QLatin1Char('x'))).toUtf8().size() <= 32);
        QVERIFY(HotspotText::nameAcceptable(HotspotText::defaultName(QString(40, QChar(0xe9)))));
        const QString a = HotspotText::newPassword();
        const QString b = HotspotText::newPassword();
        QCOMPARE(a.size(), 12);
        QVERIFY(a != b);
        QVERIFY(HotspotText::passwordAcceptable(a));
        QVERIFY(!HotspotText::nameAcceptable(QString()));
        QVERIFY(!HotspotText::nameAcceptable(QString(33, QLatin1Char('n'))));
        QVERIFY(HotspotText::nameAcceptable(QString(32, QLatin1Char('n'))));
        QVERIFY(!HotspotText::passwordAcceptable(QStringLiteral("1234567")));
        QVERIFY(HotspotText::passwordAcceptable(QStringLiteral("12345678")));
        QVERIFY(!HotspotText::passwordAcceptable(QStringLiteral("mot de passé")));

        NMVariantMapMap s = HotspotText::settings(QStringLiteral("u-1"), QStringLiteral("Share"),
                                                  QStringLiteral("12345678"), QStringLiteral("5"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("id")).toString(), HotspotText::connectionId());
        QCOMPARE(section(s, "connection").value(QStringLiteral("uuid")).toString(), QStringLiteral("u-1"));
        QCOMPARE(section(s, "connection").value(QStringLiteral("autoconnect")).toBool(), false);
        QCOMPARE(section(s, "802-11-wireless").value(QStringLiteral("mode")).toString(), QStringLiteral("ap"));
        QCOMPARE(section(s, "802-11-wireless").value(QStringLiteral("band")).toString(), QStringLiteral("a"));
        QCOMPARE(section(s, "802-11-wireless").value(QStringLiteral("ssid")).toByteArray(), QByteArray("Share"));
        QCOMPARE(section(s, "ipv4").value(QStringLiteral("method")).toString(), QStringLiteral("shared"));
        QCOMPARE(section(s, "802-11-wireless-security").value(QStringLiteral("key-mgmt")).toString(), QStringLiteral("wpa-psk"));
        s = HotspotText::settings(QStringLiteral("u-1"), QStringLiteral("Share"), QStringLiteral("12345678"), QStringLiteral("2.4"));
        QCOMPARE(section(s, "802-11-wireless").value(QStringLiteral("band")).toString(), QStringLiteral("bg"));
        s = HotspotText::settings(QStringLiteral("u-1"), QStringLiteral("Share"), QStringLiteral("12345678"), QStringLiteral("any"));
        QVERIFY(!section(s, "802-11-wireless").contains(QStringLiteral("band")));

        QCOMPARE(HotspotText::leases(""), 0);
        QCOMPARE(HotspotText::leases("1730000000 aa:bb:cc:dd:ee:ff 10.42.0.12 phone *\n"
                                     "1730000001 11:22:33:44:55:66 10.42.0.13 * *\n\n"), 2);
    }

    // --- proxy.h

    void proxyText()
    {
        ProxyConfig manual;
        manual.useManual = true;
        manual.host = QStringLiteral("proxy.example.org");
        manual.port = 3128;
        manual.exceptions = QStringLiteral("*.corp; intranet");
        manual.bypassLocal = true;
        QCOMPARE(ProxyText::check(manual), QString());
        const auto kio = ProxyText::toKio(manual);
        QCOMPARE(kio.value(QStringLiteral("ProxyType")), QStringLiteral("1"));
        QCOMPARE(kio.value(QStringLiteral("httpProxy")), QStringLiteral("http://proxy.example.org 3128"));
        QCOMPARE(kio.value(QStringLiteral("httpsProxy")), QStringLiteral("http://proxy.example.org 3128"));
        QVERIFY(kio.value(QStringLiteral("NoProxyFor")).contains(QStringLiteral("*.corp,intranet")));
        QVERIFY(kio.value(QStringLiteral("NoProxyFor")).contains(QStringLiteral("localhost")));
        QCOMPARE(ProxyText::fromKio(kio), manual);
        const QString env = ProxyText::environment(manual);
        QVERIFY(env.contains(QStringLiteral("http_proxy=http://proxy.example.org:3128\n")));
        QVERIFY(env.contains(QStringLiteral("HTTPS_PROXY=http://proxy.example.org:3128\n")));
        QVERIFY(env.contains(QStringLiteral("no_proxy=*.corp,intranet,localhost,127.0.0.0/8,::1\n")));

        ProxyConfig script;
        script.useScript = true;
        script.scriptUrl = QStringLiteral("http://wpad.corp/proxy.pac");
        QCOMPARE(ProxyText::toKio(script).value(QStringLiteral("ProxyType")), QStringLiteral("2"));
        QCOMPARE(ProxyText::toKio(script).value(QStringLiteral("Proxy Config Script")), script.scriptUrl);
        QCOMPARE(ProxyText::fromKio(ProxyText::toKio(script)).scriptUrl, script.scriptUrl);
        QVERIFY(ProxyText::fromKio(ProxyText::toKio(script)).useScript);
        QCOMPARE(ProxyText::environment(script), QString());

        ProxyConfig detect;
        detect.autoDetect = true;
        QCOMPARE(ProxyText::toKio(detect).value(QStringLiteral("ProxyType")), QStringLiteral("3"));
        QVERIFY(ProxyText::fromKio(ProxyText::toKio(detect)).autoDetect);
        QCOMPARE(ProxyText::toKio(ProxyConfig()).value(QStringLiteral("ProxyType")), QStringLiteral("0"));
        // KDE's own "use the environment" setting reads as off here
        QVERIFY(!ProxyText::fromKio({{QStringLiteral("ProxyType"), QStringLiteral("4")}}).useManual);
        // a port written with ':' by hand is read too
        QCOMPARE(ProxyText::fromKio({{QStringLiteral("ProxyType"), QStringLiteral("1")},
                                     {QStringLiteral("httpProxy"), QStringLiteral("http://p.example:8080")}}).port, 8080);

        ProxyConfig bad = manual;
        bad.host = QStringLiteral("proxy.example.org\nLD_PRELOAD=/tmp/x.so");
        QCOMPARE(ProxyText::check(bad), QStringLiteral("host"));
        bad.host = QStringLiteral("has space");
        QCOMPARE(ProxyText::check(bad), QStringLiteral("host"));
        bad = manual;
        bad.port = 0;
        QCOMPARE(ProxyText::check(bad), QStringLiteral("port"));
        bad.port = 70000;
        QCOMPARE(ProxyText::check(bad), QStringLiteral("port"));
        bad = manual;
        bad.exceptions = QStringLiteral("a\nb");
        QCOMPARE(ProxyText::check(bad), QStringLiteral("exceptions"));
        bad = script;
        bad.scriptUrl = QStringLiteral("javascript:alert(1)");
        QCOMPARE(ProxyText::check(bad), QStringLiteral("script"));
        bad.scriptUrl = QStringLiteral("http://pac\n.example");
        QCOMPARE(ProxyText::check(bad), QStringLiteral("script"));
        QVERIFY(!ProxyText::errorText(QStringLiteral("port")).isEmpty());
    }

    void proxyFiles()
    {
        QTemporaryDir dir;
        const QString kio = dir.filePath(QStringLiteral("kioslaverc"));
        const QString env = dir.filePath(QStringLiteral("environment.d/90-raye-proxy.conf"));
        {
            QFile f(kio);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[General]\nkeep=me\n");
        }
        ProxySettings proxy;
        proxy.setFiles(kio, env);
        QVERIFY(!proxy.useManual());
        QSignalSpy changed(&proxy, &ProxySettings::changed);
        QCOMPARE(proxy.setManual(true, QStringLiteral("10.0.0.5"), 8080, QStringLiteral("*.lan"), false), QString());
        QVERIFY(changed.count() >= 1);
        QVERIFY(proxy.useManual());
        QVERIFY(QFile::exists(env));
        QFile e(env);
        QVERIFY(e.open(QIODevice::ReadOnly));
        const QByteArray envText = e.readAll();
        QVERIFY(envText.contains("https_proxy=http://10.0.0.5:8080\n"));
        QVERIFY(envText.contains("no_proxy=*.lan\n"));

        ProxySettings again;
        again.setFiles(kio, env);
        QCOMPARE(again.config(), proxy.config());
        QFile k(kio);
        QVERIFY(k.open(QIODevice::ReadOnly));
        QVERIFY(k.readAll().contains("keep=me")); // other groups stay

        // a wrong address changes nothing
        QVERIFY(!proxy.setManual(true, QStringLiteral("bad host"), 8080, QString(), true).isEmpty());
        QCOMPARE(proxy.host(), QStringLiteral("10.0.0.5"));

        // one way at a time
        proxy.setAutoDetect(true);
        QVERIFY(proxy.autoDetect());
        QVERIFY(!proxy.useManual());
        QVERIFY(!QFile::exists(env));
        QCOMPARE(proxy.setScript(true, QStringLiteral("https://corp/proxy.pac")), QString());
        QVERIFY(proxy.useScript());
        QVERIFY(!proxy.autoDetect());
        QCOMPARE(proxy.setScript(false, proxy.scriptUrl()), QString());
        QVERIFY(!proxy.useScript());
        ProxySettings off;
        off.setFiles(kio, env);
        QVERIFY(!off.autoDetect() && !off.useScript() && !off.useManual());
        QCOMPARE(off.scriptUrl(), QStringLiteral("https://corp/proxy.pac")); // remembered for next time
    }

    // --- data/libexec/network-reset and its polkit action

    void resetHelper()
    {
        const QString helper = QStringLiteral(SOURCE_DIR "/data/libexec/network-reset");
        QFileInfo info(helper);
        QVERIFY(info.isExecutable());
        QProcess syntax;
        syntax.start(QStringLiteral("sh"), {QStringLiteral("-n"), helper});
        QVERIFY(syntax.waitForFinished());
        QCOMPARE(syntax.exitCode(), 0);
        QFile script(helper);
        QVERIFY(script.open(QIODevice::ReadOnly));
        const QByteArray text = script.readAll();
        // it never takes what the caller says, and runs with its own PATH
        QVERIFY(text.contains("PATH=/usr/sbin:/usr/bin:/sbin:/bin"));
        QVERIFY(text.contains("takes no argument"));
        QVERIFY(!text.contains("eval"));
        if (geteuid() != 0) {
            QProcess run;
            run.start(QStringLiteral("sh"), {helper});
            QVERIFY(run.waitForFinished());
            QCOMPARE(run.exitCode(), 1); // not root: nothing is done
        }

        QFile policy(QStringLiteral(SOURCE_DIR "/data/polkit/org.raye.settings.network.policy"));
        QVERIFY(policy.open(QIODevice::ReadOnly));
        const QByteArray xml = policy.readAll();
        QVERIFY(xml.contains("<action id=\"org.raye.settings.network.reset\">"));
        QVERIFY(xml.contains("/usr/libexec/raye-settings/network-reset"));
        QVERIFY(xml.contains("<allow_any>auth_admin</allow_any>"));
        QVERIFY(xml.contains("xml:lang=\"fr\""));
    }

    // --- the backends on the pretend NetworkManager

    // a PC on a cable with no Wi-Fi card: no page says Wi-Fi
    void noWifiCard()
    {
        if (!pretend->running)
            QSKIP("dbus-daemon or python3-dbusmock is missing");
        wiredDevice();
        NetworkStatus status;
        QTRY_VERIFY(status.hasEthernet());
        QVERIFY(!status.hasWifi());
        WifiHardware card;
        QVERIFY(card.known());
        QVERIFY(!card.present());
        for (const QString &page : {QStringLiteral("NetworkPage.qml"), QStringLiteral("EthernetPage.qml"),
                                    QStringLiteral("AdvancedNetworkPage.qml"), QStringLiteral("ProxyPage.qml")}) {
            const QStringList shown = shownText(page);
            QVERIFY2(!shown.isEmpty(), qPrintable(page));
            for (const QString &line : shown)
                QVERIFY2(!line.contains(QLatin1String("Wi-Fi")), qPrintable(page + QLatin1String(": ") + line));
        }
    }

    void pretendNetwork()
    {
        if (!pretend->running)
            QSKIP("dbus-daemon or python3-dbusmock is missing");
        const QString wired = wiredDevice();
        QVERIFY(!wired.isEmpty());

        // the window is open when a USB Wi-Fi key goes in
        QFile layoutFile(QStringLiteral(SOURCE_DIR "/data/layout.json"));
        QVERIFY(layoutFile.open(QIODevice::ReadOnly));
        Router router;
        router.setLayout(parseLayout(layoutFile.readAll(), QStringLiteral("en")));
        WifiHardware card;
        QVERIFY(card.known());
        router.setRequirement(QStringLiteral("wifi"), card.present());
        connect(&card, &WifiHardware::changed, &router, [&router](bool present) {
            router.setRequirement(QStringLiteral("wifi"), present);
        });
        router.navigate(QStringLiteral("network/wifi"));
        QCOMPARE(router.route(), QStringLiteral("network"));

        const QString wifi = callMock(QStringLiteral("AddWiFiDevice"), {QStringLiteral("wlan0"), QStringLiteral("wlan0"), 30u});
        QVERIFY(!wifi.isEmpty());
        QTRY_VERIFY(router.has(QStringLiteral("wifi")));
        router.navigate(QStringLiteral("network/wifi"));
        QCOMPARE(router.route(), QStringLiteral("network/wifi"));
        callMock(QStringLiteral("AddAccessPoint"), {wifi, QStringLiteral("ap1"), QStringLiteral("Home"),
                                                    QStringLiteral("00:11:22:33:44:01"), 2u, 2412u, 54000u,
                                                    QVariant::fromValue<uchar>(40), 0x100u});
        callMock(QStringLiteral("AddAccessPoint"), {wifi, QStringLiteral("ap2"), QStringLiteral("Home"),
                                                    QStringLiteral("00:11:22:33:44:02"), 2u, 5180u, 54000u,
                                                    QVariant::fromValue<uchar>(85), 0x100u});
        callMock(QStringLiteral("AddAccessPoint"), {wifi, QStringLiteral("ap3"), QStringLiteral("Cafe"),
                                                    QStringLiteral("00:11:22:33:44:03"), 2u, 2437u, 54000u,
                                                    QVariant::fromValue<uchar>(60), 0u});

        NetworkStatus status;
        QTRY_VERIFY(status.available());
        QTRY_VERIFY(status.hasWifi());
        QTRY_VERIFY(status.hasEthernet());
        QTRY_COMPARE(status.adapters().size(), 2);
        QCOMPARE(status.ethernet().size(), 1);
        QCOMPARE(status.ethernet().constFirst().toMap().value(QStringLiteral("interface")).toString(), QStringLiteral("eth0"));
        QVERIFY(!status.connected());
        QCOMPARE(status.statusText(), QStringLiteral("Not connected"));

        WifiNetworks networks;
        QVERIFY(networks.available());
        QTRY_COMPARE(networks.rowCount(), 2);
        const QModelIndex first = networks.index(0, 0);
        QCOMPARE(networks.data(first, WifiNetworks::SsidRole).toString(), QStringLiteral("Home"));
        QCOMPARE(networks.data(first, WifiNetworks::StrengthRole).toInt(), 85);
        QVERIFY(networks.data(first, WifiNetworks::SecuredRole).toBool());
        QVERIFY(networks.needsPassword(QStringLiteral("Home")));
        QVERIFY(!networks.needsPassword(QStringLiteral("Cafe")));
        QVERIFY(!networks.join(QStringLiteral("Home"), QStringLiteral("short"), true).isEmpty()); // too short
        QVERIFY(!networks.join(QStringLiteral("Nobody"), QString(), true).isEmpty()); // not in range

        KnownNetworks known;
        QCOMPARE(known.rowCount(), 0);
        QCOMPARE(networks.join(QStringLiteral("Cafe"), QString(), true), QString());
        QTRY_COMPARE(known.rowCount(), 1);
        QTRY_VERIFY(networks.data(networks.index(0, 0), WifiNetworks::ConnectedRole).toBool());
        QCOMPARE(networks.data(networks.index(0, 0), WifiNetworks::SsidRole).toString(), QStringLiteral("Cafe"));
        QVERIFY(networks.data(networks.index(0, 0), WifiNetworks::KnownRole).toBool());
        QTRY_VERIFY(status.connected());
        QCOMPARE(status.primaryType(), QStringLiteral("wifi"));
        QCOMPARE(status.primaryPage(), QStringLiteral("network/wifi"));

        known.forget(known.data(known.index(0, 0), KnownNetworks::UuidRole).toString());
        QTRY_COMPARE(known.rowCount(), 0);

        // the switch: off, then on again
        status.setWifiEnabled(false);
        QTRY_VERIFY(!status.wifiEnabled());
        QVERIFY(!networks.enabled());
        status.setWifiEnabled(true);
        QTRY_VERIFY(status.wifiEnabled());

        // the cable plugged in as well: it carries the connection
        QVERIFY(!status.onCable());
        QDBusPendingReply<QDBusObjectPath> added = NetworkManager::addConnection(wiredSettings());
        added.waitForFinished();
        QVERIFY2(added.isValid(), qPrintable(added.error().message()));
        const QString wiredConnection = added.value().path();
        const QString active = callMock(QStringLiteral("AddActiveConnection"),
                                        {QStringList{wired}, wiredConnection, QStringLiteral("/"), QStringLiteral("Wired"), 2u});
        QVERIFY(!active.isEmpty());
        QTRY_COMPARE(status.primaryType(), QStringLiteral("ethernet"));
        QTRY_VERIFY(status.onCable());

        // encrypted DNS, then other settings saved on top of it keep it
        const QString uuid = QStringLiteral("8a2c7e1e-5d1f-4c55-9a35-1f8f0c3e7a11");
        QCOMPARE(status.setDnsConfig(uuid, {{QStringLiteral("dnsAuto"), false},
                                            {QStringLiteral("dns4"), QStringList{QStringLiteral("1.1.1.1"), QStringLiteral("1.0.0.1")}},
                                            {QStringLiteral("dns6"), QStringList{QStringLiteral("2606:4700:4700::1111"),
                                                                                 QStringLiteral("2606:4700:4700::1001")}},
                                            {QStringLiteral("dnsEncryption"), QStringLiteral("only")}}),
                 QString());
        const auto raw = [&wiredConnection]() {
            QDBusInterface iface(QStringLiteral("org.freedesktop.NetworkManager"), wiredConnection,
                                 QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection"),
                                 QDBusConnection::systemBus());
            QDBusReply<NMVariantMapMap> reply = iface.call(QStringLiteral("GetSettings"));
            return reply.value();
        };
        QCOMPARE(raw().value(QStringLiteral("connection")).value(QStringLiteral("dns-over-tls")).toInt(), 2);
        QCOMPARE(raw().value(QStringLiteral("ipv4")).value(QStringLiteral("dns-data")).toStringList().value(0),
                 QStringLiteral("1.1.1.1#cloudflare-dns.com"));
        QCOMPARE(status.setMetered(uuid, true), QString());
        QCOMPARE(raw().value(QStringLiteral("connection")).value(QStringLiteral("dns-over-tls")).toInt(), 2);
        QCOMPARE(raw().value(QStringLiteral("ipv6")).value(QStringLiteral("dns-data")).toStringList().value(0),
                 QStringLiteral("2606:4700:4700::1111#cloudflare-dns.com"));
        QCOMPARE(status.ipConfig(uuid).value(QStringLiteral("dnsPreset")).toString(), QStringLiteral("cloudflare"));
        QCOMPARE(status.ipConfig(uuid).value(QStringLiteral("dnsEncryption")).toString(), QStringLiteral("only"));
        // (back to automatic is not checked here: dbusmock merges an update
        // into what it has, where NetworkManager replaces it)

        // network profile: private by default for a cable, public once marked
        QCOMPARE(status.networkProfile(uuid), QStringLiteral("private"));
        QCOMPARE(status.setNetworkProfile(uuid, QStringLiteral("public")), QString());
        QCOMPARE(raw().value(QStringLiteral("connection")).value(QStringLiteral("zone")).toString(), QStringLiteral("public"));
        QCOMPARE(status.networkProfile(uuid), QStringLiteral("public"));
        QCOMPARE(status.wifiSummary(), QStringLiteral("Not in use, this PC is connected by cable"));
        QVERIFY(shownText(QStringLiteral("WifiPage.qml")).contains(QStringLiteral("Connected by cable")));

        // pages load and draw from this network without a warning
        loadPages();

        // and the key comes out again
        removeDevice(wifi);
        QTRY_VERIFY(!router.has(QStringLiteral("wifi")));
        QTRY_VERIFY(!status.hasWifi());
    }

private:
    QString m_wired;

    QString wiredDevice()
    {
        if (m_wired.isEmpty())
            m_wired = callMock(QStringLiteral("AddEthernetDevice"), {QStringLiteral("eth0"), QStringLiteral("eth0"), 30u});
        return m_wired;
    }

    // dbusmock has no call for this: drop it from Devices, say so, then remove it
    void removeDevice(const QString &path)
    {
        QList<QDBusObjectPath> devices;
        for (const auto &device : NetworkManager::networkInterfaces()) {
            if (device->uni() != path)
                devices.append(QDBusObjectPath(device->uni()));
        }
        callMock(QStringLiteral("SetProperty"), {mockPath, QStringLiteral("org.freedesktop.NetworkManager"),
                                                 QStringLiteral("Devices"),
                                                 QVariant::fromValue(QDBusVariant(QVariant::fromValue(devices)))});
        callMock(QStringLiteral("EmitSignal"), {QStringLiteral("org.freedesktop.NetworkManager"), QStringLiteral("DeviceRemoved"),
                                                QStringLiteral("o"), QVariantList{QVariant::fromValue(QDBusObjectPath(path))}});
        callMock(QStringLiteral("RemoveObject"), {path});
    }

    // the text a page shows, read from its visible items
    QStringList shownText(const QString &page)
    {
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/network/") + page));
        std::unique_ptr<QObject> object(component.create());
        if (!object) {
            qWarning("%s: %s", qPrintable(page), qPrintable(component.errorString()));
            return {};
        }
        object->setProperty("width", 1000);
        object->setProperty("height", 760);
        QTest::qWait(300);
        QStringList shown;
        QList<QQuickItem *> items{qobject_cast<QQuickItem *>(object.get())};
        while (!items.isEmpty()) {
            QQuickItem *item = items.takeFirst();
            if (!item || !item->isVisible())
                continue;
            const QVariant text = item->property("text");
            if (text.typeId() == QMetaType::QString && !text.toString().isEmpty())
                shown.append(text.toString());
            items += item->childItems();
        }
        return shown;
    }
    void loadPages()
    {
        static QStringList warnings;
        warnings.clear();
        const auto previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
            // the pages' own warnings; the libraries' chatter about the
            // pretend devices, and the shared components, are judged
            // by their own tests
            if (type != QtDebugMsg && type != QtInfoMsg && message.contains(QLatin1String("pages/network/")))
                warnings.append(message);
        });
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        const QStringList pages = QDir(QStringLiteral(SOURCE_DIR "/src/qml/pages/network"))
                                      .entryList({QStringLiteral("*Page.qml")}, QDir::Files);
        QVERIFY(pages.size() >= 9);
        for (const QString &page : pages) {
            QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/network/") + page));
            QVERIFY2(!component.isError(), qPrintable(component.errorString()));
            std::unique_ptr<QObject> object(component.create());
            QVERIFY2(object, qPrintable(page + QLatin1String(": ") + component.errorString()));
            object->setProperty("width", 1000);
            object->setProperty("height", 760);
            QTest::qWait(100);
            QVERIFY2(warnings.isEmpty(), qPrintable(page + QLatin1String(": ") + warnings.join(QLatin1Char('\n'))));
        }
        qInstallMessageHandler(previous);
    }
};

int main(int argc, char *argv[])
{
    // the pretend NetworkManager must be on the bus before anything in
    // here opens a connection to it
    QGuiApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("raye-settings");
    PretendNetwork network;
    pretend = &network;
    network.start();
    TestNetwork test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "test_network.moc"
