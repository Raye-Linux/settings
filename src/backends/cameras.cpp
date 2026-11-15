// SPDX-License-Identifier: GPL-3.0-or-later
#include "cameras.h"

#include <QDir>
#include <QFile>

CameraList::CameraList(QObject *parent)
    : CameraList(QStringLiteral("/sys/class/video4linux"), parent)
{
}

CameraList::CameraList(const QString &sysClass, QObject *parent)
    : QObject(parent)
    , m_sysClass(sysClass)
{
    refresh();
}

void CameraList::refresh()
{
    m_names = captureDevices(m_sysClass);
    Q_EMIT changed();
}

QStringList CameraList::captureDevices(const QString &sysClass)
{
    const auto readLine = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
    };
    QStringList names;
    const QDir dir(sysClass);
    const QStringList nodes = dir.entryList({QStringLiteral("video*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &node : nodes) {
        const QString index = readLine(dir.filePath(node + QStringLiteral("/index")));
        if (!index.isEmpty() && index != QLatin1String("0"))
            continue;
        const QString name = readLine(dir.filePath(node + QStringLiteral("/name")));
        if (!name.isEmpty())
            names << name;
    }
    return names;
}
