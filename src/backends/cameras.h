// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Cameras: the cameras this PC has, read from the kernel. The
// page shows the picture with Qt Multimedia, which lists only what it
// can open; this list says what is plugged in, so a camera that cannot
// be opened gets a sentence instead of disappearing.
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class CameraList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList names READ names NOTIFY changed)

public:
    explicit CameraList(QObject *parent = nullptr);
    // for the tests: another /sys/class/video4linux
    explicit CameraList(const QString &sysClass, QObject *parent = nullptr);

    QStringList names() const { return m_names; }
    Q_INVOKABLE void refresh();

    // A webcam shows up as several nodes. Index 0 is its picture; the
    // others carry metadata and cannot be opened as a camera.
    static QStringList captureDevices(const QString &sysClass);

Q_SIGNALS:
    void changed();

private:
    QString m_sysClass;
    QStringList m_names;
};
