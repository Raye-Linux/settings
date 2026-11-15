// SPDX-License-Identifier: GPL-3.0-or-later
#include "installedapps.h"
#include "defaultapps.h"

#include <KLocalizedString>
#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include <flatpak.h>

#include <algorithm>
#include <memory>

namespace {

QString withoutArch(const QString &package)
{
    return package.section(QLatin1Char(':'), 0, 0).trimmed();
}

QString run(const QString &program, const QStringList &arguments, int timeout = 30000)
{
    const QString path = QStandardPaths::findExecutable(program);
    if (path.isEmpty())
        return {};
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(path, arguments);
    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

// the folders of .desktop files the system's packages put there:
// Flatpak's exports and the user's own folder are not packages
QStringList packageFolders()
{
    QStringList folders;
    const QString home = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    for (const QString &folder : AppEntries::folders()) {
        if (folder.startsWith(home + QLatin1Char('/')) || folder.contains(QLatin1String("/flatpak/")))
            continue;
        folders << folder;
    }
    return folders;
}

QList<InstalledApp> flatpakAppsOf(FlatpakInstallation *installation, const QString &kind)
{
    QList<InstalledApp> apps;
    if (!installation)
        return apps;
    GError *error = nullptr;
    GPtrArray *refs = flatpak_installation_list_installed_refs_by_kind(installation, FLATPAK_REF_KIND_APP, nullptr, &error);
    if (error)
        g_error_free(error);
    if (!refs)
        return apps;
    for (guint i = 0; i < refs->len; ++i) {
        auto *installed = static_cast<FlatpakInstalledRef *>(g_ptr_array_index(refs, i));
        auto *ref = FLATPAK_REF(installed);
        InstalledApp app;
        app.source = QStringLiteral("store");
        app.appId = QString::fromUtf8(flatpak_ref_get_name(ref));
        app.id = QStringLiteral("flatpak:%1:%2").arg(kind, app.appId);
        char *formatted = flatpak_ref_format_ref(ref);
        app.ref = QString::fromUtf8(formatted);
        g_free(formatted);
        app.name = QString::fromUtf8(flatpak_installed_ref_get_appdata_name(installed));
        if (app.name.isEmpty())
            app.name = app.appId.section(QLatin1Char('.'), -1);
        app.version = QString::fromUtf8(flatpak_installed_ref_get_appdata_version(installed));
        app.size = static_cast<qint64>(flatpak_installed_ref_get_installed_size(installed));
        const QString deploy = QString::fromUtf8(flatpak_installed_ref_get_deploy_dir(installed));
        if (!deploy.isEmpty())
            app.installed = QFileInfo(deploy).lastModified();
        app.iconName = app.appId;
        app.desktopFile = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                 QStringLiteral("applications/") + app.appId + QLatin1String(".desktop"));
        apps << app;
    }
    g_ptr_array_unref(refs);
    return apps;
}

QList<InstalledApp> flatpakApps()
{
    QList<InstalledApp> apps;
    GError *error = nullptr;
    FlatpakInstallation *user = flatpak_installation_new_user(nullptr, &error);
    g_clear_error(&error);
    apps << flatpakAppsOf(user, QStringLiteral("user"));
    if (user)
        g_object_unref(user);
    FlatpakInstallation *system = flatpak_installation_new_system(nullptr, &error);
    g_clear_error(&error);
    apps << flatpakAppsOf(system, QStringLiteral("system"));
    if (system)
        g_object_unref(system);
    return apps;
}

QList<InstalledApp> packageApps()
{
    const QStringList folders = packageFolders();
    QStringList paths;
    for (const AppEntry &e : AppEntries::scan(folders))
        paths << e.path;
    if (paths.isEmpty())
        return {};
    // the paths dpkg knows are the real ones (no symbolic links in between)
    QHash<QString, QString> canonical;
    QStringList asked;
    for (const QString &p : std::as_const(paths)) {
        const QString c = QFileInfo(p).canonicalFilePath();
        canonical.insert(c, p);
        asked << c;
        if (c != p) {
            canonical.insert(p, p);
            asked << p;
        }
    }
    const QHash<QString, QString> found =
        InstalledApps::parsePackageOwners(run(QStringLiteral("dpkg-query"), QStringList{QStringLiteral("-S")} + asked).toUtf8());
    QHash<QString, QString> owners;
    for (auto it = found.cbegin(); it != found.cend(); ++it)
        owners.insert(canonical.value(it.key(), it.key()), it.value());
    QList<InstalledApp> apps = InstalledApps::systemApps(folders, owners);

    QStringList packages;
    for (const InstalledApp &a : std::as_const(apps))
        packages << a.package;
    packages.removeDuplicates();
    const auto info = InstalledApps::parsePackageInfo(
        run(QStringLiteral("dpkg-query"),
            QStringList{QStringLiteral("-W"), QStringLiteral("-f=${binary:Package}\\t${Version}\\t${Installed-Size}\\n")} + packages)
            .toUtf8());
    // what the desktop is made of cannot be removed from here
    QSet<QString> core{QStringLiteral("raye-settings"), QStringLiteral("raye-store"), QStringLiteral("raye-update"),
                       QStringLiteral("raye-desktop"), QStringLiteral("raye-system")};
    const QString depends = run(QStringLiteral("dpkg-query"),
                                {QStringLiteral("-W"), QStringLiteral("-f=${Depends}, ${Pre-Depends}\\n"),
                                 QStringLiteral("raye-desktop"), QStringLiteral("raye-system")});
    for (const QString &name : InstalledApps::parseDepends(depends))
        core.insert(name);

    for (InstalledApp &a : apps) {
        const auto row = info.value(a.package, {QString(), -1});
        a.version = InstalledApps::displayVersion(row.first);
        a.size = row.second;
        a.removable = !core.contains(a.package);
        for (const QString &list : {a.package + QLatin1String(".list"), a.package + QLatin1String(":amd64.list")}) {
            const QFileInfo f(QStringLiteral("/var/lib/dpkg/info/") + list);
            if (f.exists()) {
                a.installed = f.lastModified();
                break;
            }
        }
    }
    return apps;
}

QList<InstalledApp> windowsPrograms()
{
    const QString output = run(QStringLiteral("raye-windows"), {QStringLiteral("list"), QStringLiteral("--json")});
    QList<InstalledApp> apps = InstalledApps::parseWindowsList(output.toUtf8());
    if (!apps.isEmpty())
        InstalledApps::applyWindowsProfiles(apps, run(QStringLiteral("/usr/libexec/raye-windows-profile"), {}).toUtf8());
    return apps;
}

QCollator collator()
{
    QCollator c;
    c.setCaseSensitivity(Qt::CaseInsensitive);
    return c;
}

} // namespace

InstalledApps::InstalledApps(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

InstalledApps::~InstalledApps()
{
    if (m_uninstaller)
        m_uninstaller->disconnect(this);
}

// --- parsing ---

QHash<QString, QString> InstalledApps::parsePackageOwners(const QByteArray &output)
{
    QHash<QString, QString> owners;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw);
        if (line.startsWith(QLatin1String("diversion ")) || line.isEmpty())
            continue;
        const int colon = line.indexOf(QLatin1String(": /"));
        if (colon <= 0)
            continue;
        const QString packages = line.left(colon);
        const QString path = line.mid(colon + 2).trimmed();
        owners.insert(path, withoutArch(packages.section(QLatin1Char(','), 0, 0)));
    }
    return owners;
}

QHash<QString, QPair<QString, qint64>> InstalledApps::parsePackageInfo(const QByteArray &output)
{
    QHash<QString, QPair<QString, qint64>> info;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &raw : lines) {
        const QStringList fields = QString::fromUtf8(raw).split(QLatin1Char('\t'));
        if (fields.size() < 3 || fields.at(0).isEmpty())
            continue;
        bool ok = false;
        const qint64 kib = fields.at(2).trimmed().toLongLong(&ok);
        info.insert(withoutArch(fields.at(0)), {fields.at(1).trimmed(), ok ? kib * 1024 : -1});
    }
    return info;
}

QStringList InstalledApps::parseDepends(const QString &depends)
{
    static const QRegularExpression separator(QStringLiteral("[,|\\n]"));
    QStringList names;
    for (QString part : depends.split(separator, Qt::SkipEmptyParts)) {
        part = part.trimmed();
        // "plasma-desktop (>= 4:6.6)" or "foo:any [amd64]"
        part = part.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('('), 0, 0);
        part = withoutArch(part);
        if (!part.isEmpty() && !names.contains(part))
            names << part;
    }
    return names;
}

QList<InstalledApp> InstalledApps::parseWindowsList(const QByteArray &json)
{
    QList<InstalledApp> apps;
    const QJsonArray list = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &value : list) {
        const QJsonObject o = value.toObject();
        InstalledApp a;
        a.source = QStringLiteral("windows");
        a.space = o.value(QStringLiteral("space")).toString();
        a.key = o.value(QStringLiteral("key")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        if (a.space.isEmpty() || a.key.isEmpty() || a.name.isEmpty())
            continue;
        a.id = QStringLiteral("windows:") + a.space + QLatin1Char('/') + a.key;
        a.publisher = o.value(QStringLiteral("publisher")).toString();
        a.version = o.value(QStringLiteral("version")).toString();
        const QJsonValue size = o.value(QStringLiteral("size"));
        a.size = size.isDouble() ? static_cast<qint64>(size.toDouble()) : -1;
        a.installed = QDateTime(QDate::fromString(o.value(QStringLiteral("installDate")).toString(), Qt::ISODate), QTime(12, 0));
        const QString icon = o.value(QStringLiteral("icon")).toString();
        if (QDir::isAbsolutePath(icon))
            a.iconSource = icon;
        a.iconName = QStringLiteral("raye-windows-program");
        a.removable = o.value(QStringLiteral("removable")).toBool(true);
        const QJsonArray desktop = o.value(QStringLiteral("desktopFiles")).toArray();
        if (!desktop.isEmpty())
            a.desktopFile = desktop.first().toString();
        apps << a;
    }
    return apps;
}

void InstalledApps::applyWindowsProfiles(QList<InstalledApp> &apps, const QByteArray &json)
{
    const QJsonObject spaces = QJsonDocument::fromJson(json).object();
    for (InstalledApp &a : apps) {
        if (a.source != QLatin1String("windows"))
            continue;
        const bool optimized = spaces.value(a.space).toObject().value(QStringLiteral("optimized")).toBool();
        a.profile = optimized ? QStringLiteral("optimized") : QStringLiteral("default");
    }
}

QList<InstalledApp> InstalledApps::systemApps(const QStringList &folders, const QHash<QString, QString> &owners)
{
    QList<InstalledApp> apps;
    const QHash<QString, AppEntry> entries = AppEntries::scan(folders);
    for (const AppEntry &e : entries) {
        if (e.noDisplay)
            continue;
        const QString package = owners.value(e.path);
        if (package.isEmpty())
            continue;
        InstalledApp a;
        a.source = QStringLiteral("system");
        a.id = QStringLiteral("desktop:") + e.id;
        a.name = e.name;
        a.iconName = e.iconName;
        a.desktopFile = e.path;
        a.appId = e.id.endsWith(QLatin1String(".desktop")) ? e.id.chopped(8) : e.id;
        a.package = package;
        apps << a;
    }
    return apps;
}

QList<InstalledApp> InstalledApps::arrange(const QList<InstalledApp> &all, const QString &filter, const QString &search,
                                           const QString &order)
{
    QList<InstalledApp> rows;
    const QString needle = search.trimmed();
    for (const InstalledApp &a : all) {
        if (filter != QLatin1String("all") && a.source != filter)
            continue;
        if (!needle.isEmpty() && !a.name.contains(needle, Qt::CaseInsensitive)
            && !a.publisher.contains(needle, Qt::CaseInsensitive))
            continue;
        rows << a;
    }
    const QCollator c = collator();
    auto byName = [&c](const InstalledApp &a, const InstalledApp &b) {
        const int r = c.compare(a.name, b.name);
        return r != 0 ? r < 0 : a.id < b.id;
    };
    if (order == QLatin1String("size")) {
        std::sort(rows.begin(), rows.end(), [&byName](const InstalledApp &a, const InstalledApp &b) {
            if (a.size != b.size)
                return a.size > b.size; // unknown (-1) last
            return byName(a, b);
        });
    } else if (order == QLatin1String("date")) {
        std::sort(rows.begin(), rows.end(), [&byName](const InstalledApp &a, const InstalledApp &b) {
            if (a.installed != b.installed) {
                if (!a.installed.isValid() || !b.installed.isValid())
                    return a.installed.isValid();
                return a.installed > b.installed;
            }
            return byName(a, b);
        });
    } else {
        std::sort(rows.begin(), rows.end(), byName);
    }
    return rows;
}

QString InstalledApps::appDataFolder(const QString &home, const QString &appId)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+$"));
    if (home.isEmpty() || !QDir::isAbsolutePath(home) || !valid.match(appId).hasMatch())
        return {};
    return QDir::cleanPath(home + QLatin1String("/.var/app/") + appId);
}

QString InstalledApps::displayVersion(const QString &packageVersion)
{
    QString v = packageVersion.trimmed();
    // epoch
    const int colon = v.indexOf(QLatin1Char(':'));
    if (colon >= 0 && colon < 4)
        v = v.mid(colon + 1);
    // packaging revision, then repacking and pre-release marks
    for (const QChar mark : {QLatin1Char('-'), QLatin1Char('+'), QLatin1Char('~')}) {
        const int at = v.indexOf(mark);
        if (at > 0)
            v.truncate(at);
    }
    return v;
}

QVariantMap InstalledApps::parseProgress(const QByteArray &line)
{
    const QJsonObject o = QJsonDocument::fromJson(line.trimmed()).object();
    if (o.isEmpty())
        return {};
    return {{QStringLiteral("state"), o.value(QStringLiteral("state")).toString()},
            {QStringLiteral("percent"), o.value(QStringLiteral("percent")).toInt(-1)},
            {QStringLiteral("message"), o.value(QStringLiteral("message")).toString()}};
}

// --- model ---

int InstalledApps::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant InstalledApps::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const InstalledApp &a = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return a.name;
    case IdRole:
        return a.id;
    case SourceRole:
        return a.source;
    case IconNameRole:
        return a.iconName;
    case IconSourceRole:
        return a.iconSource.isEmpty() ? QUrl() : QUrl::fromLocalFile(a.iconSource);
    case PublisherRole:
        return a.publisher;
    case VersionRole:
        return a.version;
    case InstalledRole:
        return a.installed;
    case SizeRole:
        return a.size;
    case RemovableRole:
        return a.removable;
    case DesktopFileRole:
        return a.desktopFile;
    case AppIdRole:
        return a.appId;
    }
    return {};
}

QHash<int, QByteArray> InstalledApps::roleNames() const
{
    return {{IdRole, "appKey"},           {SourceRole, "source"},       {NameRole, "name"},
            {IconNameRole, "iconName"},   {IconSourceRole, "iconSource"}, {PublisherRole, "publisher"},
            {VersionRole, "version"},     {InstalledRole, "installed"}, {SizeRole, "size"},
            {RemovableRole, "removable"}, {DesktopFileRole, "desktopFile"}, {AppIdRole, "appId"}};
}

void InstalledApps::setFilter(const QString &filter)
{
    static const QStringList known{QStringLiteral("all"), QStringLiteral("store"), QStringLiteral("system"), QStringLiteral("windows")};
    if (filter == m_filter || !known.contains(filter))
        return;
    m_filter = filter;
    rearrange();
    Q_EMIT filterChanged();
}

void InstalledApps::setSortOrder(const QString &order)
{
    static const QStringList known{QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("date")};
    if (order == m_sortOrder || !known.contains(order))
        return;
    m_sortOrder = order;
    rearrange();
    Q_EMIT sortOrderChanged();
}

void InstalledApps::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    rearrange();
    Q_EMIT searchTextChanged();
}

void InstalledApps::rearrange()
{
    const int before = m_rows.size();
    beginResetModel();
    m_rows = arrange(m_all, m_filter, m_searchText, m_sortOrder);
    endResetModel();
    if (before != m_rows.size())
        Q_EMIT countChanged();
}

void InstalledApps::setLoaded(const QList<InstalledApp> &apps)
{
    m_all = apps;
    m_loading = false;
    rearrange();
    Q_EMIT countChanged();
    Q_EMIT loadingChanged();
}

void InstalledApps::reload()
{
    const int generation = ++m_generation;
    if (!m_loading) {
        m_loading = true;
        Q_EMIT loadingChanged();
    }
    QPointer<InstalledApps> self(this);
    QThread *thread = QThread::create([self, generation] {
        QList<InstalledApp> apps = flatpakApps();
        apps << packageApps() << windowsPrograms();
        QMetaObject::invokeMethod(qApp, [self, generation, apps] {
            if (self && self->m_generation == generation)
                self->setLoaded(apps);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

const InstalledApp *InstalledApps::find(const QString &id) const
{
    for (const InstalledApp &a : m_all) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

QVariantMap InstalledApps::app(const QString &id) const
{
    const InstalledApp *a = find(id);
    if (!a)
        return {};
    return {{QStringLiteral("appKey"), a->id},
            {QStringLiteral("source"), a->source},
            {QStringLiteral("name"), a->name},
            {QStringLiteral("iconName"), a->iconName},
            {QStringLiteral("iconSource"), a->iconSource.isEmpty() ? QUrl() : QUrl::fromLocalFile(a->iconSource)},
            {QStringLiteral("publisher"), a->publisher},
            {QStringLiteral("version"), a->version},
            {QStringLiteral("installed"), a->installed},
            {QStringLiteral("size"), a->size},
            {QStringLiteral("removable"), a->removable},
            {QStringLiteral("desktopFile"), a->desktopFile},
            {QStringLiteral("appId"), a->appId},
            {QStringLiteral("profile"), a->profile}};
}

bool InstalledApps::open(const QString &id)
{
    const InstalledApp *a = find(id);
    if (!a || a->desktopFile.isEmpty() || !QFileInfo(a->desktopFile).isFile())
        return false;
    const QString launcher = QStandardPaths::findExecutable(QStringLiteral("kioclient"));
    if (launcher.isEmpty())
        return false;
    return QProcess::startDetached(launcher, {QStringLiteral("exec"), a->desktopFile});
}

bool InstalledApps::uninstall(const QString &id)
{
    const InstalledApp *a = find(id);
    if (!a || !a->removable || !m_busyId.isEmpty())
        return false;
    QString program;
    QStringList arguments;
    if (a->source == QLatin1String("windows")) {
        program = QStringLiteral("raye-windows");
        arguments = {QStringLiteral("uninstall"), a->space, a->key, QStringLiteral("--json")};
    } else {
        program = QStringLiteral("raye-store");
        arguments = {QStringLiteral("--uninstall"), a->source == QLatin1String("store") ? a->ref : a->appId};
    }
    const QString path = QStandardPaths::findExecutable(program);
    if (path.isEmpty()) {
        Q_EMIT uninstallFinished(id, false, QString());
        return false;
    }

    m_busyId = id;
    m_busyPercent = 0;
    Q_EMIT busyChanged();
    auto *process = new QProcess(this);
    m_uninstaller = process;
    auto lastMessage = std::make_shared<QString>();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, lastMessage] {
        while (process->canReadLine()) {
            const QVariantMap step = parseProgress(process->readLine());
            if (step.isEmpty())
                continue;
            const int percent = step.value(QStringLiteral("percent")).toInt();
            if (percent >= 0 && percent != m_busyPercent) {
                m_busyPercent = qMin(percent, 100);
                Q_EMIT busyChanged();
            }
            const QString message = step.value(QStringLiteral("message")).toString();
            if (!message.isEmpty())
                *lastMessage = message;
        }
    });
    connect(process, &QProcess::finished, this, [this, process, id, lastMessage](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        m_busyId.clear();
        m_busyPercent = 0;
        Q_EMIT busyChanged();
        Q_EMIT uninstallFinished(id, ok, ok ? QString() : *lastMessage);
        process->deleteLater();
        if (ok)
            reload();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, id](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_busyId.clear();
        Q_EMIT busyChanged();
        Q_EMIT uninstallFinished(id, false, QString());
        process->deleteLater();
    });
    process->start(path, arguments);
    return true;
}

bool InstalledApps::terminate(const QString &id)
{
    const InstalledApp *a = find(id);
    if (!a || a->source != QLatin1String("store") || a->appId.isEmpty())
        return false;
    const QString flatpak = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
    if (flatpak.isEmpty())
        return false;
    QProcess process;
    process.start(flatpak, {QStringLiteral("kill"), a->appId});
    // "not running" is an error for flatpak, and fine here
    process.waitForFinished(5000);
    return true;
}

bool InstalledApps::resetData(const QString &id)
{
    const InstalledApp *a = find(id);
    if (!a || a->source != QLatin1String("store"))
        return false;
    const QString folder = appDataFolder(QDir::homePath(), a->appId);
    if (folder.isEmpty())
        return false;
    terminate(id);
    const QFileInfo info(folder);
    if (!info.exists())
        return true;
    // never through a link: only the app's own folder goes
    if (info.isSymLink() || !info.isDir())
        return false;
    return QDir(folder).removeRecursively();
}
