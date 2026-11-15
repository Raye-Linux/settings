// SPDX-License-Identifier: GPL-3.0-or-later
// What this PC is, for System, About and the launch screen: model and
// chassis (DMI), processor (/proc/cpuinfo), memory (/proc/meminfo),
// graphics (PCI ids under /sys), disks (/sys/block), pen and touch
// (/proc/bus/input/devices), the Raye version (os-release) and the
// date it was installed. Nothing here needs root.
//
// The files are read once, in a thread, when the object is made; the
// properties are empty until then and `ready` says when they are there.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class DeviceInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    // "Dell XPS 13 9310", empty when the firmware does not say
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(QString manufacturer READ manufacturer NOTIFY changed)
    // laptop, desktop, tablet, all-in-one: the glyph of the PC
    Q_PROPERTY(QString chassis READ chassis NOTIFY changed)
    Q_PROPERTY(QString processor READ processor NOTIFY changed)
    Q_PROPERTY(int cores READ cores NOTIFY changed)
    Q_PROPERTY(int threads READ threads NOTIFY changed)
    // what the system can use, in bytes, and the size of the memory
    // fitted ("16 GB") worked out from it
    Q_PROPERTY(qint64 memoryUsable READ memoryUsable NOTIFY changed)
    Q_PROPERTY(qint64 memoryInstalled READ memoryInstalled NOTIFY changed)
    // "16.0 GB (15.6 GB usable)"
    Q_PROPERTY(QString memory READ memory NOTIFY changed)
    Q_PROPERTY(QStringList graphics READ graphics NOTIFY changed)
    // ["477 GB SSD Samsung SSD 980"], internal disks only
    Q_PROPERTY(QStringList disks READ disks NOTIFY changed)
    // the disk Raye is on, in bytes
    Q_PROPERTY(qint64 storageTotal READ storageTotal NOTIFY changed)
    Q_PROPERTY(qint64 storageFree READ storageFree NOTIFY changed)
    Q_PROPERTY(QString systemType READ systemType NOTIFY changed)
    Q_PROPERTY(QString penAndTouch READ penAndTouch NOTIFY changed)
    // the machine id, written the way Windows writes a device id
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY changed)
    // os-release: "Raye Linux", "0.5 Beta Alpenglow"
    Q_PROPERTY(QString edition READ edition NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QDateTime installedOn READ installedOn NOTIFY changed)
    // "raye" when the Raye kernel runs, "standard" otherwise
    Q_PROPERTY(QString kernelKind READ kernelKind NOTIFY changed)
    Q_PROPERTY(QString kernelRelease READ kernelRelease NOTIFY changed)

public:
    // Everything read from the files under one root ("/" for real).
    struct Facts {
        QString model;
        QString manufacturer;
        QString chassis = QStringLiteral("desktop");
        QString processor;
        int cores = 0;
        int threads = 0;
        qint64 memoryUsable = 0;
        qint64 memoryInstalled = 0;
        QStringList graphics;
        QStringList disks;
        qint64 storageTotal = 0;
        qint64 storageFree = 0;
        QString architecture;
        // "", "touch", "pen", "pen-touch"
        QString penAndTouch;
        QString deviceId;
        QString edition;
        QString version;
        QDateTime installedOn;
        QString kernelRelease;
    };

    // `root` is where /proc, /sys, /etc and /usr/share are read (the
    // tests give a folder that looks like a laptop); empty is "/".
    explicit DeviceInfo(const QString &root = QString(), QObject *parent = nullptr);
    ~DeviceInfo() override;

    bool ready() const { return m_ready; }
    QString model() const { return m_facts.model; }
    QString manufacturer() const { return m_facts.manufacturer; }
    QString chassis() const { return m_facts.chassis; }
    QString processor() const { return m_facts.processor; }
    int cores() const { return m_facts.cores; }
    int threads() const { return m_facts.threads; }
    qint64 memoryUsable() const { return m_facts.memoryUsable; }
    qint64 memoryInstalled() const { return m_facts.memoryInstalled; }
    QString memory() const;
    QStringList graphics() const { return m_facts.graphics; }
    QStringList disks() const { return m_facts.disks; }
    qint64 storageTotal() const { return m_facts.storageTotal; }
    qint64 storageFree() const { return m_facts.storageFree; }
    QString systemType() const;
    QString penAndTouch() const;
    QString deviceId() const { return m_facts.deviceId; }
    QString edition() const { return m_facts.edition; }
    QString version() const { return m_facts.version; }
    QDateTime installedOn() const { return m_facts.installedOn; }
    QString kernelKind() const;
    QString kernelRelease() const { return m_facts.kernelRelease; }

    // The device specifications as text, the way Copy puts them on the
    // clipboard ("Processor\tAMD Ryzen 7 ...").
    Q_INVOKABLE QString specificationsText(const QString &deviceName) const;
    Q_INVOKABLE QString rayeSpecificationsText() const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    // Hardware details: is this module of the system information tool
    // installed, and open it in a window of its own. Only the modules of
    // that tool are accepted.
    Q_INVOKABLE static bool hasDetails(const QString &module);
    Q_INVOKABLE static bool openDetails(const QString &module);
    static bool isDetailsModule(const QString &module);
    // "477 GB", "1.82 TB", in the words of the language in use
    Q_INVOKABLE static QString formatSize(qint64 bytes);

    // The parsing, public for the tests.
    static Facts read(const QString &root);
    static QString cleanVendor(const QString &vendor);
    static QString modelOf(const QString &vendor, const QString &product, const QString &version,
                           const QString &boardVendor, const QString &boardName);
    static QString chassisOf(int smbiosType);
    struct Cpu {
        QString name;
        int cores = 0;
        int threads = 0;
    };
    static Cpu parseCpuinfo(const QString &text);
    static QString cleanProcessor(const QString &name);
    static qint64 parseMemTotal(const QString &meminfo);
    static qint64 installedMemory(qint64 usable);
    // "pen-touch", "touch", "pen" or "" from /proc/bus/input/devices
    static QString parseInputDevices(const QString &text);
    static QString deviceIdOf(const QString &machineId);
    static QHash<QString, QString> parseOsRelease(const QString &text);
    // A PCI id ("10de", "2484") to "NVIDIA GeForce RTX 3070", using pci.ids.
    static QString gpuName(const QString &vendorId, const QString &deviceId, const QString &pciIds);

Q_SIGNALS:
    void changed();

private:
    void setFacts(const Facts &facts);

    Facts m_facts;
    bool m_ready = false;
    class QThread *m_thread = nullptr;
};
