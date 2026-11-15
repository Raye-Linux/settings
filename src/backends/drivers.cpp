// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <PackageKit/Daemon>
#include <PackageKit/Transaction>

#include <memory>

namespace {
// a driver whose modules are built on the machine
bool buildsModules(const QString &package)
{
    static const QRegularExpression dkms(QStringLiteral("^(nvidia-driver-|bcmwl-)|-dkms$"));
    return dkms.match(package).hasMatch();
}

QProcess *start(QObject *parent, const QString &program, const QStringList &arguments)
{
    auto *p = new QProcess(parent);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::SeparateChannels);
    p->start(program, arguments);
    return p;
}
} // namespace

DriverUpdates::DriverUpdates(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();
}

bool DriverUpdates::supported() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("ubuntu-drivers")).isEmpty();
}

QList<DriverEntry> DriverUpdates::parseDevices(const QString &output)
{
    QList<DriverEntry> entries;
    QString vendor;
    QString model;
    bool inDevice = false;
    static const QRegularExpression field(QStringLiteral("^(\\w+)\\s*:\\s*(.*)$"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("== "))) {
            inDevice = true;
            vendor.clear();
            model.clear();
            continue;
        }
        const QRegularExpressionMatch m = field.match(line);
        if (!inDevice || !m.hasMatch())
            continue;
        const QString key = m.captured(1);
        const QString value = m.captured(2).trimmed();
        if (key == QLatin1String("vendor")) {
            vendor = value;
        } else if (key == QLatin1String("model")) {
            model = value;
        } else if (key == QLatin1String("driver")) {
            // "nvidia-driver-580-open - distro non-free recommended"
            const QString package = value.section(QLatin1Char(' '), 0, 0);
            const QStringList words = value.section(QLatin1String(" - "), 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (package.isEmpty() || package.startsWith(QLatin1String("oem-")))
                continue;
            DriverEntry entry;
            entry.device = model.isEmpty() ? vendor : vendor.isEmpty() ? model : vendor + QStringLiteral(" · ") + model;
            entry.package = package;
            entry.recommended = words.contains(QLatin1String("recommended"));
            entry.free = words.contains(QLatin1String("free"));
            entry.builtin = words.contains(QLatin1String("builtin"));
            entries << entry;
        }
    }
    return entries;
}

QHash<QString, QString> DriverUpdates::parseModules(const QString &output)
{
    QHash<QString, QString> modules;
    static const QRegularExpression line(QStringLiteral("^(\\S+), \\(kernel modules provided by (\\S+)\\)$"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QRegularExpressionMatch m = line.match(raw.trimmed());
        if (m.hasMatch())
            modules.insert(m.captured(1), m.captured(2));
    }
    return modules;
}

QStringList DriverUpdates::parseInstalled(const QString &output)
{
    QStringList names;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // "ii": installed; "rc" is only its configuration left behind
        if (parts.size() >= 2 && parts.at(1).startsWith(QLatin1String("ii")))
            names << parts.at(0).section(QLatin1Char(':'), 0, 0);
    }
    return names;
}

QString DriverUpdates::stateOf(const DriverEntry &entry)
{
    if (entry.installed)
        return QStringLiteral("installed");
    if (entry.builtin)
        return QStringLiteral("builtin");
    if (entry.modules.isEmpty() && buildsModules(entry.package))
        return QStringLiteral("unsigned");
    return QStringLiteral("installable");
}

QStringList DriverUpdates::packagesOf(const DriverEntry &entry)
{
    QStringList packages{entry.package};
    if (!entry.modules.isEmpty())
        packages << entry.modules;
    return packages;
}

int DriverUpdates::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant DriverUpdates::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const DriverEntry &e = m_entries.at(index.row());
    switch (role) {
    case DeviceRole:
        return e.device;
    case Qt::DisplayRole:
    case PackageRole:
        return e.package;
    case RecommendedRole:
        return e.recommended;
    case InstalledRole:
        return e.installed;
    case BuiltinRole:
        return e.builtin;
    case StateRole:
        return stateOf(e);
    }
    return {};
}

QHash<int, QByteArray> DriverUpdates::roleNames() const
{
    return {{DeviceRole, "device"},       {PackageRole, "package"}, {RecommendedRole, "recommended"},
            {InstalledRole, "installed"}, {BuiltinRole, "builtin"}, {StateRole, "state"}};
}

void DriverUpdates::refresh()
{
    if (m_checking || !m_busyPackage.isEmpty() || !supported())
        return;
    m_checking = true;
    Q_EMIT checkingChanged();
    // ubuntu-drivers reads the apt cache: a few seconds, so out of the UI
    // thread, and the two lists one after the other
    QProcess *devices = start(this, QStringLiteral("ubuntu-drivers"), {QStringLiteral("devices")});
    m_process = devices;
    connect(devices, &QProcess::finished, this, [this, devices] {
        const QString devicesOut = QString::fromUtf8(devices->readAllStandardOutput());
        devices->deleteLater();
        QProcess *list = start(this, QStringLiteral("ubuntu-drivers"), {QStringLiteral("list")});
        m_process = list;
        connect(list, &QProcess::finished, this, [this, list, devicesOut] {
            listed(devicesOut, QString::fromUtf8(list->readAllStandardOutput()));
            list->deleteLater();
        });
        connect(list, &QProcess::errorOccurred, this, [this, list, devicesOut](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                listed(devicesOut, QString());
                list->deleteLater();
            }
        });
    });
    connect(devices, &QProcess::errorOccurred, this, [this, devices](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            listed(QString(), QString());
            devices->deleteLater();
        }
    });
}

void DriverUpdates::listed(const QString &devices, const QString &modules)
{
    QList<DriverEntry> entries = parseDevices(devices);
    const QHash<QString, QString> signedModules = parseModules(modules);
    for (DriverEntry &e : entries)
        e.modules = signedModules.value(e.package);
    const int before = m_entries.size();
    beginResetModel();
    m_entries = entries;
    endResetModel();
    if (before != m_entries.size())
        Q_EMIT countChanged();
    readInstalled();
}

void DriverUpdates::readInstalled()
{
    QStringList names;
    for (const DriverEntry &e : std::as_const(m_entries))
        names << e.package;
    if (names.isEmpty()) {
        m_checking = false;
        Q_EMIT checkingChanged();
        return;
    }
    QProcess *query = start(this, QStringLiteral("dpkg-query"),
                            QStringList{QStringLiteral("-W"), QStringLiteral("-f"), QStringLiteral("${Package} ${db:Status-Abbrev}\\n")} + names);
    connect(query, &QProcess::finished, this, [this, query] {
        const QStringList installed = parseInstalled(QString::fromUtf8(query->readAllStandardOutput()));
        query->deleteLater();
        for (int i = 0; i < m_entries.size(); ++i)
            m_entries[i].installed = installed.contains(m_entries.at(i).package);
        if (!m_entries.isEmpty())
            Q_EMIT dataChanged(index(0), index(m_entries.size() - 1));
        m_checking = false;
        Q_EMIT checkingChanged();
    });
}

const DriverEntry *DriverUpdates::find(const QString &package) const
{
    for (const DriverEntry &e : m_entries) {
        if (e.package == package)
            return &e;
    }
    return nullptr;
}

bool DriverUpdates::install(const QString &package)
{
    const DriverEntry *e = find(package);
    if (!e || !m_busyPackage.isEmpty() || m_checking || stateOf(*e) != QLatin1String("installable"))
        return false;
    run(*e, true);
    return true;
}

bool DriverUpdates::remove(const QString &package)
{
    const DriverEntry *e = find(package);
    if (!e || !m_busyPackage.isEmpty() || m_checking || !e->installed)
        return false;
    run(*e, false);
    return true;
}

void DriverUpdates::run(const DriverEntry &entry, bool installing)
{
    m_busyPackage = entry.package;
    m_busyRemoving = !installing;
    m_busyPercent = 0;
    Q_EMIT busyChanged();

    // names to package ids first, then the change itself
    auto ids = std::make_shared<QStringList>();
    const auto filter = installing ? PackageKit::Transaction::FilterNotInstalled : PackageKit::Transaction::FilterInstalled;
    PackageKit::Transaction *resolve = PackageKit::Daemon::resolve(packagesOf(entry), filter | PackageKit::Transaction::FilterArch);
    m_transaction = resolve;
    connect(resolve, &PackageKit::Transaction::package, this, [ids](PackageKit::Transaction::Info, const QString &id, const QString &) {
        *ids << id;
    });
    connect(resolve, &PackageKit::Transaction::finished, this, [this, ids, installing](PackageKit::Transaction::Exit exit) {
        if (exit != PackageKit::Transaction::ExitSuccess || ids->isEmpty()) {
            done(false, QString());
            return;
        }
        PackageKit::Transaction *t = installing ? PackageKit::Daemon::installPackages(*ids)
                                                // what only the driver needed goes with it
                                                : PackageKit::Daemon::removePackages(*ids, false, true);
        m_transaction = t;
        auto error = std::make_shared<QString>();
        connect(t, &PackageKit::Transaction::percentageChanged, this, [this, t] {
            const uint p = t->percentage();
            // 101 means "not known"
            if (p <= 100 && int(p) != m_busyPercent) {
                m_busyPercent = int(p);
                Q_EMIT busyChanged();
            }
        });
        connect(t, &PackageKit::Transaction::errorCode, this, [error](PackageKit::Transaction::Error, const QString &details) {
            *error = details;
        });
        connect(t, &PackageKit::Transaction::finished, this, [this, error](PackageKit::Transaction::Exit exit) {
            done(exit == PackageKit::Transaction::ExitSuccess, *error);
        });
    });
}

void DriverUpdates::done(bool ok, const QString &message)
{
    const QString package = m_busyPackage;
    const bool removed = m_busyRemoving;
    m_busyPackage.clear();
    m_busyRemoving = false;
    m_busyPercent = 0;
    Q_EMIT busyChanged();
    Q_EMIT finished(package, removed, ok, message);
    refresh();
}
