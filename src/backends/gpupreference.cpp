// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpupreference.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace
{
const QString entryGroup = QStringLiteral("Desktop Entry");
const QString preferKey = QStringLiteral("PrefersNonDefaultGPU");

// "kde/org.kde.foo.desktop" under a folder is the id "kde-org.kde.foo.desktop"
QString idOf(const QString &dir, const QString &path)
{
    return QDir(dir).relativeFilePath(path).replace(QLatin1Char('/'), QLatin1Char('-'));
}
} // namespace

GpuApps::GpuApps(QObject *parent)
    : GpuApps(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation),
              QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation), parent)
{
}

GpuApps::GpuApps(const QString &userDir, const QStringList &systemDirs, QObject *parent)
    : QAbstractListModel(parent)
    , m_userDir(userDir)
    , m_systemDirs(systemDirs)
{
    m_systemDirs.removeAll(m_userDir);
    reload();
}

void GpuApps::reload()
{
    // the user's folder first: its entry hides the system's
    QHash<QString, QString> paths;
    QStringList dirs = m_systemDirs;
    dirs.prepend(m_userDir);
    for (const QString &dir : std::as_const(dirs)) {
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = idOf(dir, path);
            if (!paths.contains(id))
                paths.insert(id, path);
        }
    }

    QList<App> apps;
    for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
        KDesktopFile file(it.value());
        const KConfigGroup group = file.desktopGroup();
        if (group.readEntry("Type") != QLatin1String("Application") || file.noDisplay()
            || group.readEntry("Hidden", false) || group.readEntry("Exec").isEmpty())
            continue;
        App app;
        app.id = it.key();
        app.name = file.readName();
        app.icon = file.readIcon();
        app.path = it.value();
        app.dedicated = group.readEntry(preferKey, false);
        if (!app.name.isEmpty())
            apps.append(app);
    }
    std::sort(apps.begin(), apps.end(), [](const App &a, const App &b) {
        return a.name.toCaseFolded().localeAwareCompare(b.name.toCaseFolded()) < 0;
    });
    beginResetModel();
    m_apps = apps;
    endResetModel();
    Q_EMIT countChanged();
}

int GpuApps::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_apps.size());
}

QVariant GpuApps::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const App &app = m_apps.at(index.row());
    switch (role) {
    case IdRole:
        return app.id;
    case NameRole:
    case Qt::DisplayRole:
        return app.name;
    case IconRole:
        return app.icon;
    case DedicatedRole:
        return app.dedicated;
    }
    return {};
}

QHash<int, QByteArray> GpuApps::roleNames() const
{
    return {{IdRole, "appId"}, {NameRole, "name"}, {IconRole, "iconName"}, {DedicatedRole, "dedicated"}};
}

bool GpuApps::setDedicated(int row, bool on)
{
    if (row < 0 || row >= m_apps.size())
        return false;
    App &app = m_apps[row];
    if (app.dedicated == on)
        return true;
    // an id is a file name in the user's folder, nothing more
    if (app.id.contains(QLatin1Char('/')) || app.id.startsWith(QLatin1Char('.')) || !app.id.endsWith(QLatin1String(".desktop")))
        return false;

    const QString userPath = QDir(m_userDir).filePath(app.id);
    const bool hasUserCopy = QFileInfo(app.path).absoluteFilePath() == QFileInfo(userPath).absoluteFilePath();

    if (!on && hasUserCopy) {
        KDesktopFile copy(userPath);
        if (copy.desktopGroup().readEntry(marker, false)) {
            // our copy existed only for this: back to the system's entry
            if (!QFile::remove(userPath))
                return false;
            reload();
            return true;
        }
    }

    if (!hasUserCopy) {
        if (!QDir().mkpath(m_userDir))
            return false;
        QFile::remove(userPath);
        if (!QFile::copy(app.path, userPath))
            return false;
        QFile::setPermissions(userPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        KDesktopFile copy(userPath);
        KConfigGroup group = copy.desktopGroup();
        group.writeEntry(marker, true);
        group.sync();
    }

    KDesktopFile file(userPath);
    KConfigGroup group = file.desktopGroup();
    if (on)
        group.writeEntry(preferKey, true);
    else
        group.deleteEntry(preferKey);
    if (!group.sync())
        return false;
    app.path = userPath;
    app.dedicated = on;
    Q_EMIT dataChanged(index(row), index(row), {DedicatedRole});
    return true;
}

void GpuApps::openNvidiaPanel()
{
    const QString panel = QStandardPaths::findExecutable(QStringLiteral("nvidia-settings"));
    if (!panel.isEmpty())
        QProcess::startDetached(panel, {});
}
