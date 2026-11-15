// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamecontrollers.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <linux/joystick.h>

namespace {

// two controllers and a keyboard, the way the kernel writes them
const QByteArray devices = R"(I: Bus=0011 Vendor=0001 Product=0001 Version=ab41
N: Name="AT Translated Set 2 keyboard"
H: Handlers=sysrq kbd event3 leds

I: Bus=0003 Vendor=045e Product=028e Version=0114
N: Name="Microsoft X-Box 360 pad"
H: Handlers=event17 js0

I: Bus=0005 Vendor=054c Product=0ce6 Version=8100
N: Name="DualSense Wireless Controller"
H: Handlers=event21 js1

I: Bus=0005 Vendor=054c Product=0ce6 Version=8100
N: Name="DualSense Wireless Controller Motion Sensors"
H: Handlers=event22
)";

} // namespace

class TestGameControllers : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readsTheKernelList()
    {
        const QList<Controller> list = GameControllers::parse(devices);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).name, QStringLiteral("Microsoft X-Box 360 pad"));
        QCOMPARE(list.at(0).device, QStringLiteral("/dev/input/js0"));
        QCOMPARE(list.at(0).bus, QStringLiteral("usb"));
        QCOMPARE(list.at(1).bus, QStringLiteral("bluetooth"));
        QCOMPARE(list.at(1).device, QStringLiteral("/dev/input/js1"));
    }

    void modelFollowsTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("devices"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(devices);
        f.close();
        GameControllers model(path);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(1), GameControllers::NameRole).toString(), QStringLiteral("DualSense Wireless Controller"));

        QSignalSpy count(&model, &GameControllers::countChanged);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(devices.left(devices.indexOf("I: Bus=0005")));
        f.close();
        model.refresh();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(count.count(), 1);
    }

    void noFileNoControllers()
    {
        GameControllers model(QStringLiteral("/nonexistent/devices"));
        QCOMPARE(model.rowCount(), 0);
    }

    void testerFollowsEvents()
    {
        ControllerTester tester;
        tester.resize(4, 2);
        tester.apply(JS_EVENT_BUTTON | JS_EVENT_INIT, 2, 0);
        tester.apply(JS_EVENT_BUTTON, 2, 1);
        tester.apply(JS_EVENT_AXIS, 1, -32767);
        tester.apply(JS_EVENT_AXIS, 0, 16384);
        QCOMPARE(tester.buttons().at(2).toBool(), true);
        QCOMPARE(tester.buttons().at(0).toBool(), false);
        QCOMPARE(tester.axes().at(1).toReal(), -1.0);
        QVERIFY(qAbs(tester.axes().at(0).toReal() - 0.5) < 0.01);
        // a button the count did not announce still shows
        tester.apply(JS_EVENT_BUTTON, 6, 1);
        QCOMPARE(tester.buttons().size(), 7);
    }

    void unreadableDeviceSaysSo()
    {
        ControllerTester tester;
        tester.setDevice(QStringLiteral("/nonexistent/js0"));
        QVERIFY(!tester.working());
        QVERIFY(tester.buttons().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestGameControllers)
#include "test_gamecontrollers.moc"
