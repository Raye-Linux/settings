// SPDX-License-Identifier: GPL-3.0-or-later
#include "permissions.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <KPluginMetaData>
#include <KSharedConfig>

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <glib.h>

namespace {
const QString storeService = QStringLiteral("org.freedesktop.impl.portal.PermissionStore");
const QString storePath = QStringLiteral("/org/freedesktop/impl/portal/PermissionStore");
const QString storeInterface = QStringLiteral("org.freedesktop.impl.portal.PermissionStore");
const QString notFound = QStringLiteral("org.freedesktop.portal.Error.NotFound");
// the Store apps' own settings module
const QString flatpakModule = QStringLiteral("kcm_flatpak");
// the two filesystems= entries that mean "everything of yours"
const QStringList broadBases = {QStringLiteral("host"), QStringLiteral("home")};
constexpr int writeTimeout = 3000;

using PermissionTable = QMap<QString, QStringList>;

void registerTypes()
{
    static const bool done = [] {
        qDBusRegisterMetaType<PermissionTable>();
        return true;
    }();
    Q_UNUSED(done);
}

// An app id comes from a folder listing or the store; it names files, so
// anything that could leave the folder is refused.
bool validAppId(const QString &id)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+$"));
    return valid.match(id).hasMatch() && id.size() <= 255;
}

// "home:ro" → "home"
QString baseOf(const QString &entry)
{
    QString e = entry.trimmed();
    if (e.startsWith(QLatin1Char('!')))
        e.remove(0, 1);
    return e.section(QLatin1Char(':'), 0, 0);
}

QString userFlatpakDir()
{
    const QString env = qEnvironmentVariable("FLATPAK_USER_DIR");
    return env.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/flatpak")
                         : env;
}

QString systemFlatpakDir()
{
    const QString env = qEnvironmentVariable("FLATPAK_SYSTEM_DIR");
    return env.isEmpty() ? QStringLiteral("/var/lib/flatpak") : env;
}

// [Context] filesystems= of a keyfile, empty when there is none
QStringList keyfileFilesystems(const QString &path)
{
    QStringList entries;
    if (!QFile::exists(path))
        return entries;
    GKeyFile *file = g_key_file_new();
    if (g_key_file_load_from_file(file, QFile::encodeName(path).constData(), G_KEY_FILE_NONE, nullptr)) {
        gsize length = 0;
        gchar **list = g_key_file_get_string_list(file, "Context", "filesystems", &length, nullptr);
        for (gsize i = 0; list && i < length; ++i) {
            const QString entry = QString::fromUtf8(list[i]).trimmed();
            if (!entry.isEmpty())
                entries.append(entry);
        }
        g_strfreev(list);
    }
    g_key_file_free(file);
    return entries;
}

// Writes [Context] filesystems= of an override, keeping what else it
// holds. An empty list removes the key.
bool writeKeyfileFilesystems(const QString &path, const QStringList &entries)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    GKeyFile *file = g_key_file_new();
    if (QFile::exists(path)
        && !g_key_file_load_from_file(file, QFile::encodeName(path).constData(), G_KEY_FILE_KEEP_COMMENTS, nullptr)) {
        // a file we cannot read is not ours to rewrite
        g_key_file_free(file);
        return false;
    }
    if (entries.isEmpty()) {
        g_key_file_remove_key(file, "Context", "filesystems", nullptr);
        gsize keys = 0;
        gchar **left = g_key_file_get_keys(file, "Context", &keys, nullptr);
        g_strfreev(left);
        if (keys == 0)
            g_key_file_remove_group(file, "Context", nullptr);
    } else {
        QList<QByteArray> utf8;
        QList<const gchar *> list;
        for (const QString &entry : entries)
            utf8.append(entry.toUtf8());
        for (const QByteArray &entry : utf8)
            list.append(entry.constData());
        g_key_file_set_string_list(file, "Context", "filesystems", list.constData(), gsize(list.size()));
    }
    const bool ok = g_key_file_save_to_file(file, QFile::encodeName(path).constData(), nullptr);
    g_key_file_free(file);
    return ok;
}

// The access an app ends up with: what it was built with, then each
// override in the order Flatpak reads them.
QSet<QString> effectiveBroad(const QStringList &built, const QList<QStringList> &overrides)
{
    QSet<QString> granted;
    for (const QString &entry : built) {
        if (!entry.startsWith(QLatin1Char('!')) && broadBases.contains(baseOf(entry)))
            granted.insert(baseOf(entry));
    }
    for (const QStringList &list : overrides) {
        for (const QString &entry : list) {
            const QString base = baseOf(entry);
            if (!broadBases.contains(base))
                continue;
            if (entry.trimmed().startsWith(QLatin1Char('!')))
                granted.remove(base);
            else
                granted.insert(base);
        }
    }
    return granted;
}

// name and icon from the app's .desktop file, the id when there is none
void describe(const QString &appId, const QStringList &extraDirs, QString *name, QString *icon)
{
    const QString file = appId + QStringLiteral(".desktop");
    QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, file);
    for (const QString &dir : extraDirs) {
        if (!path.isEmpty())
            break;
        if (QFile::exists(dir + QLatin1Char('/') + file))
            path = dir + QLatin1Char('/') + file;
    }
    *name = appId;
    *icon = appId;
    if (path.isEmpty())
        return;
    KDesktopFile desktop(path);
    if (!desktop.readName().isEmpty())
        *name = desktop.readName();
    if (!desktop.readIcon().isEmpty())
        *icon = desktop.readIcon();
}

QStringList exportDirs()
{
    return {userFlatpakDir() + QStringLiteral("/exports/share/applications"),
            systemFlatpakDir() + QStringLiteral("/exports/share/applications")};
}
} // namespace

AppPermissions::AppPermissions(QObject *parent)
    : QAbstractListModel(parent)
    , m_bus(QDBusConnection::sessionBus())
{
    registerTypes();
}

void AppPermissions::setBus(const QDBusConnection &bus)
{
    if (!m_connectedTable.isEmpty())
        m_bus.disconnect(storeService, storePath, storeInterface, QStringLiteral("Changed"), this,
                         SLOT(storeChanged(QDBusMessage)));
    m_connectedTable.clear();
    m_bus = bus;
    reload();
}

void AppPermissions::setKind(const QString &kind)
{
    static const QStringList known = {QStringLiteral("camera"), QStringLiteral("microphone"), QStringLiteral("location"),
                                      QStringLiteral("screenshot"), QStringLiteral("background"), QStringLiteral("files")};
    if (kind == m_kind || !known.contains(kind))
        return;
    m_kind = kind;
    Q_EMIT kindChanged();
    Q_EMIT masterEnabledChanged();
    // once the page that set it is complete and listens
    QMetaObject::invokeMethod(this, &AppPermissions::reload, Qt::QueuedConnection);
}

QString AppPermissions::table() const
{
    if (m_kind == QLatin1String("camera") || m_kind == QLatin1String("microphone"))
        return QStringLiteral("devices");
    return m_kind;
}

QString AppPermissions::storeId() const
{
    return m_kind;
}

QString AppPermissions::configKey(const QString &suffix) const
{
    QString key = m_kind;
    if (!key.isEmpty())
        key[0] = key[0].toUpper();
    return key + suffix;
}

QStringList AppPermissions::storeValue(const QString &kind, bool allowed, const QStringList &old)
{
    if (kind == QLatin1String("location")) {
        // the accuracy the app gets, then when it last asked
        return {allowed ? QStringLiteral("EXACT") : QStringLiteral("NONE"), old.value(1, QStringLiteral("0"))};
    }
    return {allowed ? QStringLiteral("yes") : QStringLiteral("no")};
}

bool AppPermissions::storeAllows(const QString &kind, const QStringList &value)
{
    if (value.isEmpty())
        return false;
    if (kind == QLatin1String("location"))
        return !value.first().isEmpty() && value.first() != QLatin1String("NONE");
    return value.first() == QLatin1String("yes");
}

QStringList AppPermissions::broadFilesystems(const QStringList &filesystems)
{
    QStringList bases;
    for (const QString &entry : filesystems) {
        const QString base = baseOf(entry);
        if (!entry.trimmed().startsWith(QLatin1Char('!')) && broadBases.contains(base) && !bases.contains(base))
            bases.append(base);
    }
    return bases;
}

QStringList AppPermissions::overrideFilesystems(const QStringList &old, const QStringList &broad, bool allowed)
{
    QStringList result;
    for (const QString &entry : old) {
        const QString base = baseOf(entry);
        if (!broad.contains(base))
            result.append(entry);
        else if (allowed && !entry.trimmed().startsWith(QLatin1Char('!')))
            result.append(entry);
    }
    if (!allowed) {
        for (const QString &base : broad)
            result.append(QLatin1Char('!') + base);
    }
    return result;
}

bool AppPermissions::masterEnabled() const
{
    if (m_kind.isEmpty())
        return true;
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("raye-settingsrc")), QStringLiteral("Privacy"));
    return group.readEntry(configKey(QStringLiteral("Access")), true);
}

void AppPermissions::setMasterEnabled(bool on)
{
    if (m_kind.isEmpty() || on == masterEnabled())
        return;
    auto config = KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"));
    KConfigGroup group(config, QStringLiteral("Privacy"));
    const QString restoreKey = configKey(QStringLiteral("Restore"));
    if (!on) {
        QStringList allowed;
        for (const App &app : std::as_const(m_apps)) {
            if (app.allowed)
                allowed.append(app.id);
        }
        group.writeEntry(restoreKey, allowed, KConfig::Notify);
        group.writeEntry(configKey(QStringLiteral("Access")), false, KConfig::Notify);
        config->sync();
        for (const QString &id : std::as_const(allowed))
            setAllowed(id, false);
    } else {
        const QStringList restore = group.readEntry(restoreKey, QStringList());
        group.writeEntry(configKey(QStringLiteral("Access")), true, KConfig::Notify);
        group.deleteEntry(restoreKey, KConfig::Notify);
        config->sync();
        for (const QString &id : restore) {
            if (validAppId(id))
                setAllowed(id, true);
        }
    }
    Q_EMIT masterEnabledChanged();
}

int AppPermissions::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_apps.size());
}

QVariant AppPermissions::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_apps.size())
        return {};
    const App &app = m_apps.at(index.row());
    switch (role) {
    case AppIdRole:
        return app.id;
    case Qt::DisplayRole:
    case NameRole:
        return app.name;
    case IconRole:
        return app.icon;
    case AllowedRole:
        return app.allowed;
    }
    return {};
}

QHash<int, QByteArray> AppPermissions::roleNames() const
{
    return {{AppIdRole, "appId"}, {NameRole, "name"}, {IconRole, "appIcon"}, {AllowedRole, "allowed"}};
}

void AppPermissions::setAvailable(bool available)
{
    if (available == m_available)
        return;
    m_available = available;
    Q_EMIT availableChanged();
}

void AppPermissions::setApps(QList<App> apps)
{
    std::sort(apps.begin(), apps.end(), [](const App &a, const App &b) {
        const int c = QString::localeAwareCompare(a.name.toCaseFolded(), b.name.toCaseFolded());
        return c != 0 ? c < 0 : a.id < b.id;
    });
    const int before = int(m_apps.size());
    beginResetModel();
    m_apps = std::move(apps);
    endResetModel();
    if (before != m_apps.size())
        Q_EMIT countChanged();
    Q_EMIT loaded();
}

void AppPermissions::reload()
{
    if (m_kind.isEmpty())
        return;
    if (m_kind == QLatin1String("files"))
        loadFiles();
    else
        loadStore();
}

void AppPermissions::connectStore()
{
    if (m_connectedTable == table())
        return;
    if (!m_connectedTable.isEmpty())
        m_bus.disconnect(storeService, storePath, storeInterface, QStringLiteral("Changed"), this,
                         SLOT(storeChanged(QDBusMessage)));
    if (m_bus.connect(storeService, storePath, storeInterface, QStringLiteral("Changed"), this,
                      SLOT(storeChanged(QDBusMessage))))
        m_connectedTable = table();
}

void AppPermissions::storeChanged(const QDBusMessage &message)
{
    const QVariantList args = message.arguments();
    if (args.size() >= 2 && args.at(0).toString() == table() && args.at(1).toString() == storeId())
        reload();
}

void AppPermissions::loadStore()
{
    connectStore();
    QDBusMessage call = QDBusMessage::createMethodCall(storeService, storePath, storeInterface, QStringLiteral("Lookup"));
    call << table() << storeId();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    const QString kind = m_kind;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, kind] {
        watcher->deleteLater();
        if (kind != m_kind)
            return; // the page moved on to another permission
        const QDBusMessage reply = watcher->reply();
        QList<App> apps;
        if (reply.type() == QDBusMessage::ErrorMessage) {
            // no app has asked yet: the store has no entry for it
            const bool known = reply.errorName() == notFound;
            setAvailable(known);
            setApps({});
            return;
        }
        setAvailable(true);
        const PermissionTable permissions = qdbus_cast<PermissionTable>(reply.arguments().value(0));
        for (auto it = permissions.cbegin(); it != permissions.cend(); ++it) {
            // apps outside the sandbox have no id: they are not limited
            if (!validAppId(it.key()))
                continue;
            App app;
            app.id = it.key();
            app.value = it.value();
            app.allowed = storeAllows(m_kind, it.value());
            describe(app.id, exportDirs(), &app.name, &app.icon);
            apps.append(app);
        }
        setApps(std::move(apps));
    });
}

void AppPermissions::loadFiles()
{
    QList<App> apps;
    QSet<QString> seen;
    const QString user = userFlatpakDir();
    const QString system = systemFlatpakDir();
    const QList<QStringList> globalOverrides = {keyfileFilesystems(system + QStringLiteral("/overrides/global")),
                                                keyfileFilesystems(user + QStringLiteral("/overrides/global"))};
    // the user's installation wins over the system one for the same id
    for (const QString &installation : {user, system}) {
        const QDir appDir(installation + QStringLiteral("/app"));
        const QStringList ids = appDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &id : ids) {
            if (!validAppId(id) || seen.contains(id))
                continue;
            const QString metadata = appDir.filePath(id + QStringLiteral("/current/active/metadata"));
            if (!QFile::exists(metadata))
                continue;
            seen.insert(id);
            const QStringList built = keyfileFilesystems(metadata);
            const QStringList systemOverride = keyfileFilesystems(system + QStringLiteral("/overrides/") + id);
            const QStringList userOverride = keyfileFilesystems(user + QStringLiteral("/overrides/") + id);
            QStringList broad = broadFilesystems(built);
            for (const QString &base : broadFilesystems(systemOverride + userOverride)) {
                if (!broad.contains(base))
                    broad.append(base);
            }
            if (broad.isEmpty())
                continue;
            App app;
            app.id = id;
            app.broad = broad;
            app.allowed = !effectiveBroad(built, {globalOverrides.at(0), systemOverride, globalOverrides.at(1), userOverride})
                               .isEmpty();
            describe(id, exportDirs(), &app.name, &app.icon);
            apps.append(app);
        }
    }
    setAvailable(true);
    setApps(std::move(apps));
}

bool AppPermissions::isAllowed(const QString &appId) const
{
    for (const App &app : m_apps) {
        if (app.id == appId)
            return app.allowed;
    }
    return false;
}

bool AppPermissions::setAllowed(const QString &appId, bool allowed)
{
    // while "Let apps…" is off, no app gets it back one by one
    if (allowed && !masterEnabled())
        return false;
    for (int row = 0; row < m_apps.size(); ++row) {
        App &app = m_apps[row];
        if (app.id != appId)
            continue;
        if (app.allowed == allowed)
            return true;
        const bool ok = m_kind == QLatin1String("files") ? writeFiles(app, allowed)
                                                         : writeStore(app.id, storeValue(m_kind, allowed, app.value));
        if (!ok)
            return false;
        if (m_kind != QLatin1String("files"))
            app.value = storeValue(m_kind, allowed, app.value);
        app.allowed = allowed;
        const QModelIndex index = this->index(row);
        Q_EMIT dataChanged(index, index, {AllowedRole});
        return true;
    }
    return false;
}

bool AppPermissions::writeStore(const QString &appId, const QStringList &value)
{
    if (!validAppId(appId))
        return false;
    QDBusMessage call =
        QDBusMessage::createMethodCall(storeService, storePath, storeInterface, QStringLiteral("SetPermission"));
    call << table() << true << storeId() << appId << value;
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, writeTimeout);
    return reply.type() == QDBusMessage::ReplyMessage;
}

bool AppPermissions::writeFiles(App &app, bool allowed)
{
    if (!validAppId(app.id))
        return false;
    const QString user = userFlatpakDir();
    const QString system = systemFlatpakDir();
    const QString path = user + QStringLiteral("/overrides/") + app.id;
    QStringList entries = overrideFilesystems(keyfileFilesystems(path), app.broad, allowed);
    if (allowed) {
        // a system-wide override may still take it away: say it here
        const QStringList built = keyfileFilesystems(
            (QDir(user + QStringLiteral("/app/") + app.id).exists() ? user : system)
            + QStringLiteral("/app/") + app.id + QStringLiteral("/current/active/metadata"));
        const QList<QStringList> overrides = {keyfileFilesystems(system + QStringLiteral("/overrides/global")),
                                              keyfileFilesystems(system + QStringLiteral("/overrides/") + app.id),
                                              keyfileFilesystems(user + QStringLiteral("/overrides/global")), entries};
        const QSet<QString> granted = effectiveBroad(built, overrides);
        for (const QString &base : std::as_const(app.broad)) {
            if (!granted.contains(base))
                entries.append(base);
        }
    }
    return writeKeyfileFilesystems(path, entries);
}

bool AppPermissions::canOpenAppSettings() const
{
    return KPluginMetaData::findPluginById(QStringLiteral("plasma/kcms/systemsettings"), flatpakModule).isValid();
}

void AppPermissions::openAppSettings() const
{
    // in a window of its own; Raye Settings tells the kcmshell6 relay to
    // pass it through to the real one
    QProcess::startDetached(QStringLiteral("kcmshell6"), {flatpakModule});
}
