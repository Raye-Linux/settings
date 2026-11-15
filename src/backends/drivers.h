// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update > Optional updates: the drivers ubuntu-drivers knows for
// the devices of this PC, which one it recommends and which one is in.
// Installing and removing go through PackageKit, which asks for the
// administrator password itself.
//
// A driver whose kernel modules would have to be built on the machine
// is shown but not offered: with Secure Boot it needs a key enrolment
// screen, the same rule raye-drivers follows at the first start.
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QProcess;
namespace PackageKit
{
class Transaction;
}

struct DriverEntry {
    QString device;      // "NVIDIA Corporation · GA106 [GeForce RTX 3060]"
    QString package;     // "nvidia-driver-580-open"
    QString modules;     // signed kernel modules that come with it, may be empty
    bool recommended = false;
    bool free = false;
    bool builtin = false;
    bool installed = false;
};

class DriverUpdates : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    // false when ubuntu-drivers is not on this system
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // the package being installed or removed, "" when none
    Q_PROPERTY(QString busyPackage READ busyPackage NOTIFY busyChanged)
    Q_PROPERTY(bool busyRemoving READ busyRemoving NOTIFY busyChanged)
    Q_PROPERTY(int busyPercent READ busyPercent NOTIFY busyChanged)

public:
    enum Roles {
        DeviceRole = Qt::UserRole + 1,
        PackageRole,
        RecommendedRole,
        InstalledRole,
        BuiltinRole,
        // "installable", "installed", "builtin", "unsigned"
        StateRole,
    };
    Q_ENUM(Roles)

    explicit DriverUpdates(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool checking() const { return m_checking; }
    bool supported() const;
    QString busyPackage() const { return m_busyPackage; }
    bool busyRemoving() const { return m_busyRemoving; }
    int busyPercent() const { return m_busyPercent; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool install(const QString &package);
    Q_INVOKABLE bool remove(const QString &package);

    // `ubuntu-drivers devices`: one entry per driver, device by device
    static QList<DriverEntry> parseDevices(const QString &output);
    // `ubuntu-drivers list`: driver -> the package of its signed modules
    static QHash<QString, QString> parseModules(const QString &output);
    // `dpkg-query -W -f '${Package} ${db:Status-Abbrev}\n'`: installed names
    static QStringList parseInstalled(const QString &output);
    static QString stateOf(const DriverEntry &entry);
    // what to ask PackageKit for: the driver and its modules
    static QStringList packagesOf(const DriverEntry &entry);

Q_SIGNALS:
    void countChanged();
    void checkingChanged();
    void busyChanged();
    void finished(const QString &package, bool removed, bool ok, const QString &message);

private:
    void listed(const QString &devices, const QString &modules);
    void readInstalled();
    void run(const DriverEntry &entry, bool installing);
    void done(bool ok, const QString &message);
    const DriverEntry *find(const QString &package) const;

    QList<DriverEntry> m_entries;
    bool m_checking = false;
    QString m_busyPackage;
    bool m_busyRemoving = false;
    int m_busyPercent = 0;
    QPointer<QProcess> m_process;
    QPointer<PackageKit::Transaction> m_transaction;
};
