// SPDX-License-Identifier: GPL-3.0-or-later
#include "firmware.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

FirmwareDevices::FirmwareDevices(QObject *parent)
    : QObject(parent)
{
    refresh();
}

bool FirmwareDevices::supported() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("fwupdmgr")).isEmpty();
}

int FirmwareDevices::updateCount() const
{
    int n = 0;
    for (const QVariant &d : m_devices)
        n += d.toMap().value(QStringLiteral("to")).toString().isEmpty() ? 0 : 1;
    return n;
}

QVariantList FirmwareDevices::parseDevices(const QByteArray &json)
{
    QVariantList devices;
    const QJsonArray list = QJsonDocument::fromJson(json).object().value(QStringLiteral("Devices")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject d = v.toObject();
        const QJsonArray flags = d.value(QStringLiteral("Flags")).toArray();
        const QString id = d.value(QStringLiteral("DeviceId")).toString();
        const QString version = d.value(QStringLiteral("Version")).toString();
        if (id.isEmpty() || version.isEmpty() || !flags.contains(QStringLiteral("updatable")))
            continue;
        devices << QVariantMap{{QStringLiteral("name"), d.value(QStringLiteral("Name")).toString()},
                               {QStringLiteral("vendor"), d.value(QStringLiteral("Vendor")).toString()},
                               {QStringLiteral("version"), version},
                               {QStringLiteral("deviceId"), id},
                               {QStringLiteral("to"), QString()},
                               {QStringLiteral("summary"), QString()},
                               {QStringLiteral("size"), 0}};
    }
    return devices;
}

QVariantMap FirmwareDevices::parseUpdates(const QByteArray &json)
{
    QVariantMap updates;
    const QJsonArray list = QJsonDocument::fromJson(json).object().value(QStringLiteral("Devices")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject d = v.toObject();
        const QString id = d.value(QStringLiteral("DeviceId")).toString();
        // fwupd lists the newest release first
        const QJsonObject release = d.value(QStringLiteral("Releases")).toArray().first().toObject();
        const QString to = release.value(QStringLiteral("Version")).toString();
        if (id.isEmpty() || to.isEmpty())
            continue;
        updates.insert(id, QVariantMap{{QStringLiteral("to"), to},
                                       {QStringLiteral("summary"), release.value(QStringLiteral("Summary")).toString()},
                                       {QStringLiteral("size"), release.value(QStringLiteral("Size")).toDouble()}});
    }
    return updates;
}

QVariantList FirmwareDevices::merge(const QVariantList &devices, const QVariantMap &updates)
{
    QVariantList merged;
    for (const QVariant &v : devices) {
        QVariantMap d = v.toMap();
        const QVariantMap u = updates.value(d.value(QStringLiteral("deviceId")).toString()).toMap();
        if (!u.isEmpty()) {
            d.insert(QStringLiteral("to"), u.value(QStringLiteral("to")));
            d.insert(QStringLiteral("summary"), u.value(QStringLiteral("summary")));
            d.insert(QStringLiteral("size"), u.value(QStringLiteral("size")));
        }
        merged << d;
    }
    // the ones with an update first, the order fwupd gave otherwise
    std::stable_sort(merged.begin(), merged.end(), [](const QVariant &a, const QVariant &b) {
        return !a.toMap().value(QStringLiteral("to")).toString().isEmpty()
            && b.toMap().value(QStringLiteral("to")).toString().isEmpty();
    });
    return merged;
}

QProcess *FirmwareDevices::run(const QStringList &arguments)
{
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::SeparateChannels);
    p->start(QStringLiteral("fwupdmgr"), arguments);
    return p;
}

void FirmwareDevices::refresh()
{
    if (m_checking || !supported())
        return;
    m_checking = true;
    Q_EMIT changed();
    // no metadata refresh here: Raye Update does it as root when it checks
    QProcess *devices = run({QStringLiteral("get-devices"), QStringLiteral("--json")});
    connect(devices, &QProcess::finished, this, [this, devices] {
        const QVariantList found = parseDevices(devices->readAllStandardOutput());
        devices->deleteLater();
        QProcess *updates = run({QStringLiteral("get-updates"), QStringLiteral("--json"), QStringLiteral("--no-unreported-check")});
        connect(updates, &QProcess::finished, this, [this, updates, found] {
            // exit 2 is "nothing to do", and prints no JSON
            m_devices = merge(found, parseUpdates(updates->readAllStandardOutput()));
            updates->deleteLater();
            m_checking = false;
            Q_EMIT changed();
        });
    });
    connect(devices, &QProcess::errorOccurred, this, [this, devices](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        devices->deleteLater();
        m_checking = false;
        Q_EMIT changed();
    });
}
