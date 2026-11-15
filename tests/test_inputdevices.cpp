// SPDX-License-Identifier: GPL-3.0-or-later
// Mouse and touchpad settings against a fake of KWin's input devices, on
// a private bus. What matters: each page sees its own kind of device,
// a value goes to every device that can take it and to no other, and a
// device that cannot do something never speaks for one that can.
#include "inputdevices.h"

#include <QDBusConnection>
#include <QDBusServer>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

class FakeDevice : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.InputDevice")
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(bool pointer MEMBER pointer)
    Q_PROPERTY(bool touchpad MEMBER touchpad)
    Q_PROPERTY(bool tabletTool MEMBER tabletTool)
    Q_PROPERTY(bool tabletPad MEMBER tabletPad)
    Q_PROPERTY(bool leftHanded MEMBER leftHanded)
    Q_PROPERTY(bool supportsLeftHanded MEMBER supportsLeftHanded)
    Q_PROPERTY(double pointerAcceleration MEMBER pointerAcceleration)
    Q_PROPERTY(bool supportsPointerAcceleration MEMBER supportsPointerAcceleration)
    Q_PROPERTY(bool tapToClick MEMBER tapToClick)
    Q_PROPERTY(int tapFingerCount MEMBER tapFingerCount)
public:
    QString name;
    bool pointer = false;
    bool touchpad = false;
    bool tabletTool = false;
    bool tabletPad = false;
    bool leftHanded = false;
    bool supportsLeftHanded = true;
    double pointerAcceleration = 0;
    bool supportsPointerAcceleration = true;
    bool tapToClick = false;
    int tapFingerCount = 0;
};

class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.InputDeviceManager")
    Q_PROPERTY(QStringList devicesSysNames MEMBER names)
public:
    QStringList names;
Q_SIGNALS:
    void deviceAdded(const QString &sysName);
    void deviceRemoved(const QString &sysName);
};

class FakeKWin : public QObject
{
    Q_OBJECT
public:
    QString address;
    FakeManager *manager = nullptr;
    QHash<QString, FakeDevice *> devices;
public Q_SLOTS:
    void start()
    {
        m_server = new QDBusServer(this);
        address = m_server->address();
        manager = new FakeManager;
        manager->setParent(this);
        const auto add = [this](const QString &sysName, const QString &name, void (*set)(FakeDevice *)) {
            auto *device = new FakeDevice;
            device->setParent(this);
            device->name = name;
            set(device);
            devices.insert(sysName, device);
            manager->names << sysName;
        };
        add(QStringLiteral("event1"), QStringLiteral("Keyboard"), [](FakeDevice *) { });
        add(QStringLiteral("event2"), QStringLiteral("Absolute pointer"), [](FakeDevice *d) {
            d->pointer = true;
            d->supportsPointerAcceleration = false;
            d->pointerAcceleration = -1;
        });
        add(QStringLiteral("event3"), QStringLiteral("Office mouse"), [](FakeDevice *d) {
            d->pointer = true;
            d->pointerAcceleration = 0.25;
        });
        add(QStringLiteral("event4"), QStringLiteral("Game mouse"), [](FakeDevice *d) { d->pointer = true; });
        add(QStringLiteral("event5"), QStringLiteral("Touchpad"), [](FakeDevice *d) {
            d->pointer = true;
            d->touchpad = true;
            d->tapFingerCount = 3;
        });
        add(QStringLiteral("event6"), QStringLiteral("Drawing pen"), [](FakeDevice *d) {
            d->pointer = true;
            d->tabletTool = true;
        });
        connect(m_server, &QDBusServer::newConnection, this, [this](const QDBusConnection &connection) {
            m_connection = connection;
            const auto how = QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals;
            m_connection.registerObject(QStringLiteral("/org/kde/KWin/InputDevice"), manager, how);
            for (auto it = devices.cbegin(); it != devices.cend(); ++it)
                m_connection.registerObject(QStringLiteral("/org/kde/KWin/InputDevice/") + it.key(), it.value(), how);
        });
    }

private:
    QDBusServer *m_server = nullptr;
    QDBusConnection m_connection{QString()};
};

class TestInputDevices : public QObject
{
    Q_OBJECT

    QThread m_thread;
    FakeKWin *m_kwin = nullptr;
    int m_connections = 0;

    QDBusConnection connect_()
    {
        return QDBusConnection::connectToPeer(m_kwin->address, QStringLiteral("input%1").arg(++m_connections));
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_thread.start();
        m_kwin = new FakeKWin;
        m_kwin->moveToThread(&m_thread);
        QMetaObject::invokeMethod(m_kwin, &FakeKWin::start, Qt::BlockingQueuedConnection);
    }

    void cleanupTestCase()
    {
        m_thread.quit();
        m_thread.wait();
    }

    void eachKindIsSorted()
    {
        QCOMPARE(InputDeviceSettings::kindOf({{QStringLiteral("pointer"), true}}), QStringLiteral("mouse"));
        // a touchpad and a pen are pointers too: they must not be mice
        QCOMPARE(InputDeviceSettings::kindOf({{QStringLiteral("pointer"), true}, {QStringLiteral("touchpad"), true}}),
                 QStringLiteral("touchpad"));
        QCOMPARE(InputDeviceSettings::kindOf({{QStringLiteral("pointer"), true}, {QStringLiteral("tabletTool"), true}}),
                 QStringLiteral("pen"));
        QCOMPARE(InputDeviceSettings::kindOf({{QStringLiteral("tabletPad"), true}}), QStringLiteral("pen"));
        QCOMPARE(InputDeviceSettings::kindOf({{QStringLiteral("keyboard"), true}}), QString());
    }

    void pageSeesOnlyItsKind()
    {
        InputDeviceSettings mouse(connect_());
        mouse.setKind(QStringLiteral("mouse"));
        QVERIFY(mouse.available());
        QCOMPARE(mouse.deviceNames(),
                 QStringList({QStringLiteral("Absolute pointer"), QStringLiteral("Office mouse"), QStringLiteral("Game mouse")}));

        InputDeviceSettings touchpad(connect_());
        touchpad.setKind(QStringLiteral("touchpad"));
        QCOMPARE(touchpad.deviceNames(), QStringList(QStringLiteral("Touchpad")));

        InputDeviceSettings pen(connect_());
        pen.setKind(QStringLiteral("pen"));
        QCOMPARE(pen.deviceNames(), QStringList(QStringLiteral("Drawing pen")));
    }

    void deviceThatCannotDoItDoesNotSpeak()
    {
        InputDeviceSettings mouse(connect_());
        mouse.setKind(QStringLiteral("mouse"));
        // the absolute pointer comes first and says -1, and it has no say
        QCOMPARE(mouse.value(QStringLiteral("pointerAcceleration")).toDouble(), 0.25);
        QVERIFY(mouse.supports(QStringLiteral("supportsPointerAcceleration")));
        QVERIFY(!mouse.supports(QStringLiteral("supportsNothingAtAll")));
    }

    void writeReachesEveryCapableDeviceAndNoOther()
    {
        InputDeviceSettings mouse(connect_());
        mouse.setKind(QStringLiteral("mouse"));
        QSignalSpy spy(&mouse, &InputDeviceSettings::changed);
        QVERIFY(mouse.setValue(QStringLiteral("pointerAcceleration"), 0.5));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m_kwin->devices.value(QStringLiteral("event3"))->pointerAcceleration, 0.5);
        QCOMPARE(m_kwin->devices.value(QStringLiteral("event4"))->pointerAcceleration, 0.5);
        QCOMPARE(m_kwin->devices.value(QStringLiteral("event2"))->pointerAcceleration, -1.0);
        QCOMPARE(m_kwin->devices.value(QStringLiteral("event5"))->pointerAcceleration, 0.0);

        QVERIFY(mouse.setValue(QStringLiteral("leftHanded"), true));
        QVERIFY(m_kwin->devices.value(QStringLiteral("event3"))->leftHanded);
        QVERIFY(!m_kwin->devices.value(QStringLiteral("event6"))->leftHanded);
        QCOMPARE(mouse.value(QStringLiteral("leftHanded")).toBool(), true);
    }

    void aPropertyNoDeviceHasWritesNothing()
    {
        InputDeviceSettings pen(connect_());
        pen.setKind(QStringLiteral("pen"));
        QVERIFY(!pen.setValue(QStringLiteral("noSuchSetting"), true));
    }

    // the value QML passes is a double or an int: KWin's type wins
    void writeKeepsThePropertysType()
    {
        InputDeviceSettings touchpad(connect_());
        touchpad.setKind(QStringLiteral("touchpad"));
        QVERIFY(touchpad.setValue(QStringLiteral("tapFingerCount"), 4.0));
        QCOMPARE(m_kwin->devices.value(QStringLiteral("event5"))->tapFingerCount, 4);
    }

    void noKWinSaysSo()
    {
        QDBusServer other;
        InputDeviceSettings mouse(QDBusConnection::connectToPeer(other.address(), QStringLiteral("inputnone")));
        mouse.setKind(QStringLiteral("mouse"));
        QVERIFY(!mouse.available());
        QVERIFY(mouse.deviceNames().isEmpty());
        QVERIFY(!mouse.setValue(QStringLiteral("leftHanded"), true));
    }
};

QTEST_GUILESS_MAIN(TestInputDevices)
#include "test_inputdevices.moc"
