// SPDX-License-Identifier: GPL-3.0-or-later
#include "defaultapps.h"

#include <KConfig>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

const QString defaultGroup = QStringLiteral("Default Applications");
const QString addedGroup = QStringLiteral("Added Associations");
const QString schemePrefix = QStringLiteral("x-scheme-handler/");

QString desktopName()
{
    // "KDE" in a Plasma session; the file is then kde-mimeapps.list
    const QString current = qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("KDE"));
    return current.split(QLatin1Char(':')).value(0).toLower();
}

QString roleTitle(const QString &id)
{
    if (id == QLatin1String("browser"))
        return i18nc("@label default app role", "Web browser");
    if (id == QLatin1String("email"))
        return i18nc("@label default app role", "Email");
    if (id == QLatin1String("music"))
        return i18nc("@label default app role", "Music player");
    if (id == QLatin1String("video"))
        return i18nc("@label default app role", "Video player");
    if (id == QLatin1String("photos"))
        return i18nc("@label default app role", "Photo viewer");
    if (id == QLatin1String("files"))
        return i18nc("@label default app role", "Files");
    if (id == QLatin1String("terminal"))
        return i18nc("@label default app role", "Terminal");
    if (id == QLatin1String("text"))
        return i18nc("@label default app role", "Text editor");
    if (id == QLatin1String("pdf"))
        return i18nc("@label default app role", "PDF reader");
    if (id == QLatin1String("maps"))
        return i18nc("@label default app role", "Maps");
    return id;
}

QString roleIcon(const QString &id)
{
    static const QHash<QString, QString> icons = {
        {QStringLiteral("browser"), QStringLiteral("internet-web-browser")},
        {QStringLiteral("email"), QStringLiteral("internet-mail")},
        {QStringLiteral("music"), QStringLiteral("audio-x-generic")},
        {QStringLiteral("video"), QStringLiteral("video-x-generic")},
        {QStringLiteral("photos"), QStringLiteral("image-x-generic")},
        {QStringLiteral("files"), QStringLiteral("system-file-manager")},
        {QStringLiteral("terminal"), QStringLiteral("utilities-terminal")},
        {QStringLiteral("text"), QStringLiteral("text-x-generic")},
        {QStringLiteral("pdf"), QStringLiteral("application-pdf")},
        {QStringLiteral("maps"), QStringLiteral("find-location")},
    };
    return icons.value(id);
}

bool forThisDesktop(const KConfigGroup &g)
{
    const QStringList only = g.readXdgListEntry("OnlyShowIn");
    if (!only.isEmpty() && !only.contains(QLatin1String("KDE")))
        return false;
    return !g.readXdgListEntry("NotShowIn").contains(QLatin1String("KDE"));
}

QCollator nameCollator()
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator;
}

void sortApps(QList<AppEntry> &apps)
{
    const QCollator collator = nameCollator();
    std::sort(apps.begin(), apps.end(), [&collator](const AppEntry &a, const AppEntry &b) {
        if (a.preference != b.preference)
            return a.preference > b.preference;
        return collator.compare(a.name, b.name) < 0;
    });
}

} // namespace

// --- AppEntries ---

QStringList AppEntries::folders()
{
    QStringList folders;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        folders << dir + QLatin1String("/applications");
    return folders;
}

QHash<QString, AppEntry> AppEntries::scan(const QStringList &folders)
{
    QHash<QString, AppEntry> apps;
    QSet<QString> seen;
    for (const QString &folder : folders) {
        QDirIterator it(folder, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QString id = QDir(folder).relativeFilePath(path);
            id.replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seen.contains(id))
                continue;
            seen.insert(id);
            KDesktopFile file(path);
            const KConfigGroup g = file.desktopGroup();
            if (g.readEntry("Hidden", false) || !file.hasApplicationType() || !forThisDesktop(g))
                continue;
            AppEntry e;
            e.id = id;
            e.path = path;
            e.name = file.readName();
            e.comment = file.readComment();
            e.iconName = file.readIcon();
            e.exec = g.readEntry("Exec");
            e.mimeTypes = g.readXdgListEntry("MimeType");
            e.categories = g.readXdgListEntry("Categories");
            e.noDisplay = file.noDisplay();
            e.preference = g.readEntry("X-KDE-InitialPreference", 0);
            if (e.name.isEmpty() || e.exec.isEmpty())
                continue;
            apps.insert(id, e);
        }
    }
    return apps;
}

// --- DefaultApps ---

DefaultApps::DefaultApps(QObject *parent)
    : QObject(parent)
{
    reload();
}

void DefaultApps::reload()
{
    m_apps = AppEntries::scan(AppEntries::folders());
    Q_EMIT changed();
}

QList<DefaultApps::Role> DefaultApps::roleTable()
{
    return {
        {QStringLiteral("browser"),
         {QStringLiteral("x-scheme-handler/http"), QStringLiteral("x-scheme-handler/https"), QStringLiteral("text/html"),
          QStringLiteral("application/xhtml+xml")}},
        {QStringLiteral("email"), {QStringLiteral("x-scheme-handler/mailto")}},
        {QStringLiteral("music"),
         {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
          QStringLiteral("audio/x-vorbis+ogg"), QStringLiteral("audio/mp4"), QStringLiteral("audio/x-wav"),
          QStringLiteral("audio/aac")}},
        {QStringLiteral("video"),
         {QStringLiteral("video/mp4"), QStringLiteral("video/x-matroska"), QStringLiteral("video/webm"),
          QStringLiteral("video/quicktime"), QStringLiteral("video/x-msvideo"), QStringLiteral("video/mpeg")}},
        {QStringLiteral("photos"),
         {QStringLiteral("image/jpeg"), QStringLiteral("image/png"), QStringLiteral("image/gif"),
          QStringLiteral("image/webp"), QStringLiteral("image/bmp"), QStringLiteral("image/tiff")}},
        {QStringLiteral("files"), {QStringLiteral("inode/directory")}},
        // apps are found by their category, not by a type
        {QStringLiteral("terminal"), {}},
        {QStringLiteral("text"), {QStringLiteral("text/plain")}},
        {QStringLiteral("pdf"), {QStringLiteral("application/pdf")}},
        {QStringLiteral("maps"), {QStringLiteral("x-scheme-handler/geo")}},
    };
}

QStringList DefaultApps::mimeappsFiles()
{
    const QString own = desktopName() + QLatin1String("-mimeapps.list");
    const QString common = QStringLiteral("mimeapps.list");
    QStringList files;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation))
        files << dir + QLatin1Char('/') + own << dir + QLatin1Char('/') + common;
    for (const QString &dir : AppEntries::folders())
        files << dir + QLatin1Char('/') + own << dir + QLatin1Char('/') + common;
    return files;
}

QString DefaultApps::defaultIn(const QStringList &files, const QString &mimeType, const QHash<QString, AppEntry> &apps)
{
    for (const QString &path : files) {
        if (!QFileInfo::exists(path))
            continue;
        KConfig file(path, KConfig::SimpleConfig);
        const QStringList ids = file.group(defaultGroup).readXdgListEntry(mimeType);
        for (const QString &id : ids) {
            if (apps.contains(id))
                return id;
        }
    }
    return {};
}

bool DefaultApps::writeDefaults(const QString &userFile, const QString &desktopFile, const QStringList &types,
                                const QString &appId)
{
    if (types.isEmpty() || appId.isEmpty())
        return false;
    {
        KConfig file(userFile, KConfig::SimpleConfig);
        KConfigGroup defaults = file.group(defaultGroup);
        KConfigGroup added = file.group(addedGroup);
        for (const QString &type : types) {
            defaults.writeXdgListEntry(type, {appId});
            QStringList list = added.readXdgListEntry(type);
            list.removeAll(appId);
            list.prepend(appId);
            added.writeXdgListEntry(type, list);
        }
        if (!file.sync())
            return false;
    }
    // the desktop's own list is read first: a line there would hide the choice
    if (QFileInfo::exists(desktopFile)) {
        KConfig file(desktopFile, KConfig::SimpleConfig);
        KConfigGroup defaults = file.group(defaultGroup);
        bool touched = false;
        for (const QString &type : types) {
            if (defaults.hasKey(type)) {
                defaults.deleteEntry(type);
                touched = true;
            }
        }
        if (touched && !file.sync())
            return false;
    }
    return true;
}

QString DefaultApps::commandOf(const QString &exec)
{
    static const QRegularExpression fieldCode(QStringLiteral("\\s*%[fFuUdDnNickvm]"));
    QString command = exec;
    command.remove(fieldCode);
    return command.trimmed();
}

QList<AppEntry> DefaultApps::appsForType(const QString &mimeType) const
{
    QMimeDatabase db;
    QStringList accepted{mimeType};
    const QMimeType type = db.mimeTypeForName(mimeType);
    if (type.isValid()) {
        accepted << type.name() << type.aliases() << type.allAncestors();
        accepted.removeAll(QStringLiteral("application/octet-stream"));
    }
    const QString current = defaultFor(mimeType);
    QList<AppEntry> list;
    for (const AppEntry &app : m_apps) {
        if (app.noDisplay && app.id != current)
            continue;
        for (const QString &t : app.mimeTypes) {
            if (accepted.contains(t)) {
                list << app;
                break;
            }
        }
    }
    sortApps(list);
    return list;
}

QList<AppEntry> DefaultApps::appsFor(const Role &role) const
{
    if (role.id == QLatin1String("terminal")) {
        QList<AppEntry> list;
        for (const AppEntry &app : m_apps) {
            if (app.categories.contains(QLatin1String("TerminalEmulator")) && !app.noDisplay)
                list << app;
        }
        sortApps(list);
        return list;
    }
    return appsForType(role.types.value(0));
}

QString DefaultApps::defaultFor(const QString &mimeType) const
{
    const QStringList files = mimeappsFiles();
    QString id = defaultIn(files, mimeType, m_apps);
    if (!id.isEmpty())
        return id;
    // a type nobody named: what opens its parent ("text/x-csrc" -> "text/plain")
    QMimeDatabase db;
    const QMimeType type = db.mimeTypeForName(mimeType);
    QStringList parents;
    if (type.isValid())
        parents = type.allAncestors();
    parents.removeAll(QStringLiteral("application/octet-stream"));
    // the app that says it opens the type itself comes before a parent's default
    QList<AppEntry> direct;
    for (const AppEntry &app : m_apps) {
        if (!app.noDisplay && app.mimeTypes.contains(mimeType))
            direct << app;
    }
    if (!direct.isEmpty()) {
        sortApps(direct);
        return direct.first().id;
    }
    for (const QString &parent : std::as_const(parents)) {
        id = defaultIn(files, parent, m_apps);
        if (!id.isEmpty())
            return id;
    }
    for (const QString &parent : std::as_const(parents)) {
        QList<AppEntry> list;
        for (const AppEntry &app : m_apps) {
            if (!app.noDisplay && app.mimeTypes.contains(parent))
                list << app;
        }
        if (!list.isEmpty()) {
            sortApps(list);
            return list.first().id;
        }
    }
    return {};
}

QString DefaultApps::currentFor(const Role &role) const
{
    if (role.id == QLatin1String("terminal")) {
        const KConfigGroup g = KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->group(QStringLiteral("General"));
        QString service = g.readEntry("TerminalService");
        if (!service.isEmpty() && !service.endsWith(QLatin1String(".desktop")))
            service += QLatin1String(".desktop");
        if (m_apps.contains(service))
            return service;
        const QString command = g.readEntry("TerminalApplication", QStringLiteral("konsole")).section(QLatin1Char(' '), 0, 0);
        for (const AppEntry &app : appsFor(role)) {
            if (commandOf(app.exec).section(QLatin1Char(' '), 0, 0) == command)
                return app.id;
        }
        return {};
    }
    return defaultFor(role.types.value(0));
}

QVariantMap DefaultApps::appMap(const QString &appId) const
{
    const AppEntry app = m_apps.value(appId);
    return {{QStringLiteral("appId"), app.id}, {QStringLiteral("appName"), app.name}, {QStringLiteral("appIcon"), app.iconName}};
}

QVariantList DefaultApps::roles() const
{
    QVariantList list;
    for (const Role &role : roleTable()) {
        QVariantMap row = appMap(currentFor(role));
        row.insert(QStringLiteral("id"), role.id);
        row.insert(QStringLiteral("title"), roleTitle(role.id));
        row.insert(QStringLiteral("iconName"), roleIcon(role.id));
        list << row;
    }
    return list;
}

QVariantList DefaultApps::candidates(const QString &roleId) const
{
    QVariantList list;
    for (const Role &role : roleTable()) {
        if (role.id != roleId)
            continue;
        const QString current = currentFor(role);
        for (const AppEntry &app : appsFor(role)) {
            list << QVariantMap{{QStringLiteral("id"), app.id},
                                {QStringLiteral("name"), app.name},
                                {QStringLiteral("iconName"), app.iconName},
                                {QStringLiteral("current"), app.id == current}};
        }
    }
    return list;
}

bool DefaultApps::writeUser(const QStringList &types, const QString &appId)
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!QDir().mkpath(folder))
        return false;
    return writeDefaults(folder + QLatin1String("/mimeapps.list"),
                         folder + QLatin1Char('/') + desktopName() + QLatin1String("-mimeapps.list"), types, appId);
}

void DefaultApps::refreshCache() const
{
    // KDE apps read associations through their service cache: bring it up to date
    const QString builder = QStandardPaths::findExecutable(QStringLiteral("kbuildsycoca6"));
    if (!builder.isEmpty())
        QProcess::startDetached(builder, {});
}

bool DefaultApps::setRoleDefault(const QString &roleId, const QString &appId)
{
    if (!m_apps.contains(appId))
        return false;
    for (const Role &role : roleTable()) {
        if (role.id != roleId)
            continue;
        KSharedConfigPtr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        KConfigGroup general = globals->group(QStringLiteral("General"));
        if (role.id == QLatin1String("terminal")) {
            general.writeEntry("TerminalApplication", commandOf(m_apps.value(appId).exec), KConfig::Notify);
            general.writeEntry("TerminalService", appId, KConfig::Notify);
            globals->sync();
            Q_EMIT changed();
            return true;
        }
        if (!writeUser(role.types, appId))
            return false;
        if (role.id == QLatin1String("browser")) {
            general.writeEntry("BrowserApplication", appId, KConfig::Notify);
            globals->sync();
        }
        refreshCache();
        Q_EMIT changed();
        return true;
    }
    return false;
}

QVariantList DefaultApps::searchTypes(const QString &text, int limit) const
{
    QString needle = text.trimmed().toLower();
    if (needle.startsWith(QLatin1Char('.')))
        needle.remove(0, 1);
    if (needle.isEmpty())
        return {};

    struct Hit {
        int rank;
        QString label;
        QString type;
        QString description;
    };
    QList<Hit> hits;

    QMimeDatabase db;
    for (const QMimeType &type : db.allMimeTypes()) {
        const QStringList suffixes = type.suffixes();
        if (suffixes.isEmpty())
            continue;
        // the type whose own extension it is, then one that also takes
        // it, then extensions starting with it, then names
        int rank = -1;
        if (type.preferredSuffix() == needle)
            rank = 0;
        else if (suffixes.contains(needle))
            rank = 1;
        else if (std::any_of(suffixes.cbegin(), suffixes.cend(), [&needle](const QString &s) { return s.startsWith(needle); }))
            rank = 2;
        else if (type.name().contains(needle) || type.comment().toLower().contains(needle))
            rank = 3;
        if (rank < 0)
            continue;
        QString shown = type.preferredSuffix();
        for (const QString &suffix : suffixes) {
            if (suffix == needle || (rank == 2 && suffix.startsWith(needle))) {
                shown = suffix;
                break;
            }
        }
        hits << Hit{rank, QLatin1Char('.') + shown, type.name(), type.comment()};
    }

    // link types: the schemes some installed app says it opens
    QSet<QString> schemes;
    for (const AppEntry &app : m_apps) {
        for (const QString &t : app.mimeTypes) {
            if (t.startsWith(schemePrefix))
                schemes.insert(t.mid(schemePrefix.size()).toLower());
        }
    }
    for (const QString &scheme : std::as_const(schemes)) {
        if (scheme.isEmpty() || !scheme.contains(needle))
            continue;
        hits << Hit{scheme == needle ? 0 : 2, scheme.toUpper(), schemePrefix + scheme,
                    i18nc("@info a kind of link, %1 is its name (HTTPS, MAILTO)", "%1 link", scheme.toUpper())};
    }

    const QCollator collator = nameCollator();
    std::sort(hits.begin(), hits.end(), [&collator](const Hit &a, const Hit &b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return collator.compare(a.label, b.label) < 0;
    });

    QVariantList list;
    for (const Hit &hit : std::as_const(hits)) {
        if (list.size() >= limit)
            break;
        QVariantMap row = appMap(defaultFor(hit.type));
        row.insert(QStringLiteral("type"), hit.type);
        row.insert(QStringLiteral("label"), hit.label);
        row.insert(QStringLiteral("description"), hit.description);
        list << row;
    }
    return list;
}

QVariantList DefaultApps::candidatesForType(const QString &mimeType) const
{
    const QString current = defaultFor(mimeType);
    QVariantList list;
    for (const AppEntry &app : appsForType(mimeType)) {
        list << QVariantMap{{QStringLiteral("id"), app.id},
                            {QStringLiteral("name"), app.name},
                            {QStringLiteral("iconName"), app.iconName},
                            {QStringLiteral("current"), app.id == current}};
    }
    return list;
}

bool DefaultApps::setTypeDefault(const QString &mimeType, const QString &appId)
{
    if (!m_apps.contains(appId) || mimeType.isEmpty() || !mimeType.contains(QLatin1Char('/')))
        return false;
    if (!writeUser({mimeType}, appId))
        return false;
    refreshCache();
    Q_EMIT changed();
    return true;
}
