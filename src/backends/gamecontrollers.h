// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Controllers: the game controllers plugged in or paired, as
// the kernel lists them (the input devices with a jsN handler), and a
// tester that shows the buttons and sticks of one as they move.
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QFileSystemWatcher;
class QSocketNotifier;

struct Controller {
    QString name;
    QString device;    // "/dev/input/js0"
    QString bus;       // "usb", "bluetooth" or ""
};

class GameControllers : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, DeviceRole, BusRole };

    explicit GameControllers(QObject *parent = nullptr);
    // for tests: another /proc/bus/input/devices
    explicit GameControllers(const QString &devicesFile, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

    // the controllers in the text of /proc/bus/input/devices
    static QList<Controller> parse(const QByteArray &devices);

Q_SIGNALS:
    void countChanged();

private:
    QString m_file;
    QList<Controller> m_list;
    QFileSystemWatcher *m_watcher = nullptr;
};

// The live state of one controller, read from its joystick device.
class ControllerTester : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    // one bool per button, one -1..1 per axis
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY stateChanged)
    Q_PROPERTY(QVariantList axes READ axes NOTIFY stateChanged)
    // false when the device could not be read
    Q_PROPERTY(bool working READ working NOTIFY deviceChanged)

public:
    explicit ControllerTester(QObject *parent = nullptr);
    ~ControllerTester() override;

    QString device() const { return m_device; }
    void setDevice(const QString &device);
    QVariantList buttons() const;
    QVariantList axes() const;
    bool working() const { return m_fd >= 0; }

    // applies one js_event (type, number, value) to the state
    void apply(quint8 type, quint8 number, qint16 value);
    void resize(int buttons, int axes);

Q_SIGNALS:
    void deviceChanged();
    void stateChanged();

private:
    void close();
    void readEvents();

    QString m_device;
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QList<bool> m_buttons;
    QList<qreal> m_axes;
};
