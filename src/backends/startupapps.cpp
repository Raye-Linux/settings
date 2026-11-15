// SPDX-License-Identifier: GPL-3.0-or-later
#include "startupapps.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

namespace {

const QString desktopEntry = QStringLiteral("Desktop Entry");

// Is the entry meant for this desktop at all?
bool forThisDesktop(const KConfigGroup &g)
{
    const QStringList only = g.readXdgListEntry("OnlyShowIn");
    if (!only.isEmpty() && !only.contains(QLatin1String("KDE")))
        return false;
    return !g.readXdgListEntry("NotShowIn").contains(QLatin1String("KDE"));
}

bool startsIn(const KConfigGroup &g)
{
    if (g.readEntry("Hidden", false))
        return false;
    return g.readEntry("X-GNOME-Autostart-enabled", true);
}

QStringList desktopFiles(const QString &folder)
{
    return QDir(folder).entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
}

} // namespace

StartupApps::StartupApps(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

QString StartupApps::userFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/autostart");
}

QStringList StartupApps::systemFolders()
{
    QStringList folders = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    // the first one is the user's own
    if (!folders.isEmpty())
        folders.removeFirst();
    for (QString &folder : folders)
        folder += QLatin1String("/autostart");
    return folders;
}

QList<StartupEntry> StartupApps::scan(const QString &userFolder, const QStringList &systemFolders)
{
    // file name -> the system file that counts (the first folder wins)
    QHash<QString, QString> system;
    for (const QString &folder : systemFolders) {
        for (const QString &name : desktopFiles(folder)) {
            if (!system.contains(name))
                system.insert(name, folder + QLatin1Char('/') + name);
        }
    }
    QHash<QString, QString> user;
    for (const QString &name : desktopFiles(userFolder))
        user.insert(name, userFolder + QLatin1Char('/') + name);

    QStringList names = system.keys() + user.keys();
    names.removeDuplicates();

    QList<StartupEntry> entries;
    for (const QString &name : std::as_const(names)) {
        StartupEntry e;
        e.fileName = name;
        e.systemPath = system.value(name);
        e.path = user.value(name, e.systemPath);

        KDesktopFile file(e.path);
        KConfigGroup g = file.desktopGroup();
        // a user file that only says Hidden=true borrows the rest from the system's
        KDesktopFile *details = &file;
        std::unique_ptr<KDesktopFile> systemFile;
        if (!g.hasKey("Exec") && !e.systemPath.isEmpty() && e.path != e.systemPath) {
            systemFile = std::make_unique<KDesktopFile>(e.systemPath);
            details = systemFile.get();
        }
        const KConfigGroup d = details->desktopGroup();
        if (!d.hasKey("Exec") || (details->readType() != QLatin1String("Application") && !details->readType().isEmpty()))
            continue;
        if (!forThisDesktop(d) || !details->tryExec())
            continue;
        e.enabled = startsIn(g) && startsIn(d);
        // pieces of the desktop: listed only when someone turned them off
        const bool plumbing = KDesktopFile(e.systemPath.isEmpty() ? e.path : e.systemPath).noDisplay();
        if (plumbing && !e.systemPath.isEmpty() && e.enabled)
            continue;
        e.name = details->readName();
        if (e.name.isEmpty())
            e.name = QFileInfo(name).completeBaseName();
        e.comment = details->readComment();
        if (e.comment.isEmpty())
            e.comment = details->readGenericName();
        e.iconName = details->readIcon();
        entries.append(e);
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(entries.begin(), entries.end(), [&collator](const StartupEntry &a, const StartupEntry &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    return entries;
}

bool StartupApps::writeEnabled(const StartupEntry &entry, const QString &userFolder, bool enabled)
{
    if (entry.fileName.isEmpty() || entry.fileName.contains(QLatin1Char('/')))
        return false;
    const QString userPath = userFolder + QLatin1Char('/') + entry.fileName;
    const bool hasUserFile = QFileInfo::exists(userPath);

    if (enabled && hasUserFile && !entry.systemPath.isEmpty()) {
        // the user's file only hides the system's: going back is removing it
        KDesktopFile user(userPath);
        if (!user.desktopGroup().hasKey("Exec"))
            return QFile::remove(userPath);
    }
    if (!hasUserFile) {
        if (enabled)
            return true; // nothing turns it off
        if (entry.systemPath.isEmpty() || !QDir().mkpath(userFolder))
            return false;
        if (!QFile::copy(entry.systemPath, userPath))
            return false;
        QFile::setPermissions(userPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    KDesktopFile file(userPath);
    KConfigGroup g = file.desktopGroup();
    g.writeEntry("Hidden", !enabled);
    if (enabled)
        g.deleteEntry("X-GNOME-Autostart-enabled");
    return file.sync();
}

int StartupApps::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant StartupApps::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const StartupEntry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return e.name;
    case FileNameRole:
        return e.fileName;
    case CommentRole:
        return e.comment;
    case IconNameRole:
        return e.iconName;
    case EnabledRole:
        return e.enabled;
    }
    return {};
}

QHash<int, QByteArray> StartupApps::roleNames() const
{
    return {{FileNameRole, "fileName"},
            {NameRole, "name"},
            {CommentRole, "comment"},
            {IconNameRole, "iconName"},
            {EnabledRole, "starts"}};
}

void StartupApps::setSortOrder(const QString &order)
{
    if (order == m_sortOrder || (order != QLatin1String("name") && order != QLatin1String("status")))
        return;
    m_sortOrder = order;
    beginResetModel();
    sort();
    endResetModel();
    Q_EMIT sortOrderChanged();
}

void StartupApps::sort()
{
    if (m_sortOrder == QLatin1String("status")) {
        // scan() already sorted by name: a stable sort keeps that order inside each group
        std::stable_sort(m_entries.begin(), m_entries.end(), [](const StartupEntry &a, const StartupEntry &b) {
            return a.enabled && !b.enabled;
        });
    } else {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(m_entries.begin(), m_entries.end(), [&collator](const StartupEntry &a, const StartupEntry &b) {
            return collator.compare(a.name, b.name) < 0;
        });
    }
}

void StartupApps::reload()
{
    const int before = m_entries.size();
    beginResetModel();
    m_entries = scan(userFolder(), systemFolders());
    sort();
    endResetModel();
    if (before != m_entries.size())
        Q_EMIT countChanged();
}

bool StartupApps::setEnabled(const QString &fileName, bool enabled)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        StartupEntry &e = m_entries[i];
        if (e.fileName != fileName)
            continue;
        if (e.enabled == enabled)
            return true;
        if (!writeEnabled(e, userFolder(), enabled))
            return false;
        e.enabled = enabled;
        // keep the row where it is: a list that jumps under the pointer is worse
        Q_EMIT dataChanged(index(i), index(i), {EnabledRole});
        return true;
    }
    return false;
}

bool StartupApps::addApp(const QString &desktopFile)
{
    const QFileInfo info(desktopFile);
    if (!info.isAbsolute() || !info.isFile() || info.isSymLink() || info.suffix() != QLatin1String("desktop"))
        return false;
    KDesktopFile source(desktopFile);
    if (!source.hasApplicationType() || !source.desktopGroup().hasKey("Exec"))
        return false;
    const QString folder = userFolder();
    if (!QDir().mkpath(folder))
        return false;
    const QString target = folder + QLatin1Char('/') + info.fileName();
    if (QFileInfo::exists(target) && !QFile::remove(target))
        return false;
    if (!QFile::copy(desktopFile, target))
        return false;
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    KDesktopFile copy(target);
    KConfigGroup g = copy.desktopGroup();
    g.writeEntry("Hidden", false);
    g.deleteEntry("X-GNOME-Autostart-enabled");
    copy.sync();
    reload();
    return true;
}
