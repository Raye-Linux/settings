// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamecontrollers.h"

#include <QFile>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>

GameControllers::GameControllers(QObject *parent)
    // RAYE_CONTROLLERS_PREVIEW: another devices file, to draw the page with controllers
    : GameControllers(qEnvironmentVariable("RAYE_CONTROLLERS_PREVIEW", QStringLiteral("/proc/bus/input/devices")), parent)
{
    // /dev/input gains and loses a jsN node when a controller comes and goes
    m_watcher = new QFileSystemWatcher({QStringLiteral("/dev/input")}, this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &GameControllers::refresh);
}

GameControllers::GameControllers(const QString &devicesFile, QObject *parent)
    : QAbstractListModel(parent)
    , m_file(devicesFile)
{
    refresh();
}

QList<Controller> GameControllers::parse(const QByteArray &devices)
{
    static const QRegularExpression name(QStringLiteral("^N: Name=\"(.*)\"$"));
    static const QRegularExpression bus(QStringLiteral("^I: Bus=([0-9a-fA-F]{4})"));
    static const QRegularExpression handler(QStringLiteral("\\bjs([0-9]+)\\b"));
    QList<Controller> list;
    const QList<QByteArray> blocks = devices.split('\n');
    Controller current;
    bool isJoystick = false;
    const auto flush = [&] {
        if (isJoystick && !current.device.isEmpty())
            list.append(current);
        current = {};
        isJoystick = false;
    };
    for (const QByteArray &raw : blocks) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) {
            flush();
            continue;
        }
        if (auto m = name.match(line); m.hasMatch()) {
            current.name = m.captured(1);
        } else if (auto m = bus.match(line); m.hasMatch()) {
            const int code = m.captured(1).toInt(nullptr, 16);
            current.bus = code == 0x03 ? QStringLiteral("usb") : code == 0x05 ? QStringLiteral("bluetooth") : QString();
        } else if (line.startsWith(QLatin1String("H: Handlers="))) {
            if (auto m = handler.match(line); m.hasMatch()) {
                isJoystick = true;
                current.device = QStringLiteral("/dev/input/js") + m.captured(1);
            }
        }
    }
    flush();
    return list;
}

void GameControllers::refresh()
{
    QFile f(m_file);
    const QList<Controller> list = f.open(QIODevice::ReadOnly) ? parse(f.readAll()) : QList<Controller>{};
    const int before = m_list.size();
    beginResetModel();
    m_list = list;
    endResetModel();
    if (before != m_list.size())
        Q_EMIT countChanged();
}

int GameControllers::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_list.size());
}

QVariant GameControllers::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Controller &c = m_list.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return c.name;
    case DeviceRole:
        return c.device;
    case BusRole:
        return c.bus;
    }
    return {};
}

QHash<int, QByteArray> GameControllers::roleNames() const
{
    return {{NameRole, "name"}, {DeviceRole, "device"}, {BusRole, "bus"}};
}

ControllerTester::ControllerTester(QObject *parent)
    : QObject(parent)
{
}

ControllerTester::~ControllerTester()
{
    close();
}

void ControllerTester::close()
{
    delete m_notifier;
    m_notifier = nullptr;
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

void ControllerTester::setDevice(const QString &device)
{
    if (device == m_device)
        return;
    close();
    m_device = device;
    m_buttons.clear();
    m_axes.clear();
    if (!device.isEmpty()) {
        m_fd = ::open(QFile::encodeName(device).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (m_fd >= 0) {
            unsigned char buttons = 0;
            unsigned char axes = 0;
            ioctl(m_fd, JSIOCGBUTTONS, &buttons);
            ioctl(m_fd, JSIOCGAXES, &axes);
            resize(buttons, axes);
            m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
            connect(m_notifier, &QSocketNotifier::activated, this, &ControllerTester::readEvents);
        }
    }
    Q_EMIT deviceChanged();
    Q_EMIT stateChanged();
}

void ControllerTester::resize(int buttons, int axes)
{
    m_buttons = QList<bool>(buttons, false);
    m_axes = QList<qreal>(axes, 0.0);
}

void ControllerTester::readEvents()
{
    js_event event;
    bool any = false;
    while (::read(m_fd, &event, sizeof event) == sizeof event) {
        apply(event.type, event.number, event.value);
        any = true;
    }
    if (errno == ENODEV) {
        // unplugged while the page was open
        close();
        Q_EMIT deviceChanged();
    }
    if (any)
        Q_EMIT stateChanged();
}

void ControllerTester::apply(quint8 type, quint8 number, qint16 value)
{
    type &= ~JS_EVENT_INIT;
    if (type == JS_EVENT_BUTTON) {
        if (number >= m_buttons.size())
            m_buttons.resize(number + 1, false);
        m_buttons[number] = value != 0;
    } else if (type == JS_EVENT_AXIS) {
        if (number >= m_axes.size())
            m_axes.resize(number + 1, 0.0);
        m_axes[number] = qBound(-1.0, value / 32767.0, 1.0);
    }
}

QVariantList ControllerTester::buttons() const
{
    QVariantList list;
    for (bool b : m_buttons)
        list.append(b);
    return list;
}

QVariantList ControllerTester::axes() const
{
    QVariantList list;
    for (qreal a : m_axes)
        list.append(a);
    return list;
}
