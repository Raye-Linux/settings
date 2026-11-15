// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Installed apps: every app on this PC, wherever it came from.
//
//  - Store apps: Flatpak apps of the user's and the system's
//    installations (libflatpak: name, version, size, install date).
//  - System apps: the .desktop entries of the system's packages; the
//    package is asked to dpkg-query (version, installed size), the
//    install date is the time dpkg wrote its file list. A package the
//    Raye metapackages depend on cannot be uninstalled from here (that
//    would take the desktop with it), as Windows greys out Uninstall
//    for its own parts.
//  - Windows programs: `raye-windows list --json`.
// Uninstalling is not done here: Store and system apps go through
// `raye-store --uninstall <ref or id>`, Windows programs through
// `raye-windows uninstall <space> <key>`. Both print JSON lines
// ({"state": "removing|done|failed", "percent": n, "message": ".."}).
// The list is read in a thread: dpkg and Flatpak take a moment.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QProcess;

struct InstalledApp {
    QString id;      // "flatpak:org.mozilla.firefox", "desktop:org.kde.kate.desktop", "windows:<space>/<key>"
    QString source;  // "store", "system" or "windows"
    QString name;
    QString iconName;   // an icon theme name...
    QString iconSource; // ...or a picture file (Windows programs)
    QString publisher;
    QString version;
    QDateTime installed;
    qint64 size = -1; // bytes, -1 when not known
    bool removable = true;
    QString desktopFile; // to open the app
    QString appId;       // Flatpak app id, or desktop id without .desktop
    QString ref;         // Flatpak ref, the one the Store removes
    QString package;     // the system package
    QString space;       // Windows program space and key
    QString key;
    QString profile;     // Windows programs: "optimized" (a profile Raye checked) or "default"
};

class InstalledApps : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // "all", "store", "system" or "windows"
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    // "name", "size" or "date" (newest first)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    // the app being removed ("" when none), and how far it is
    Q_PROPERTY(QString busyId READ busyId NOTIFY busyChanged)
    Q_PROPERTY(int busyPercent READ busyPercent NOTIFY busyChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        SourceRole,
        NameRole,
        IconNameRole,
        IconSourceRole,
        PublisherRole,
        VersionRole,
        InstalledRole,
        SizeRole,
        RemovableRole,
        DesktopFileRole,
        AppIdRole,
    };
    Q_ENUM(Roles)

    explicit InstalledApps(QObject *parent = nullptr);
    ~InstalledApps() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int total() const { return m_all.size(); }
    bool loading() const { return m_loading; }
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString sortOrder() const { return m_sortOrder; }
    void setSortOrder(const QString &order);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QString busyId() const { return m_busyId; }
    int busyPercent() const { return m_busyPercent; }

    Q_INVOKABLE void reload();
    // one app as a map of the roles' names, {} when unknown
    Q_INVOKABLE QVariantMap app(const QString &id) const;
    Q_INVOKABLE bool open(const QString &id);
    Q_INVOKABLE bool uninstall(const QString &id);
    // Store apps only: stop it, and delete what it saved (it starts afresh)
    Q_INVOKABLE bool terminate(const QString &id);
    Q_INVOKABLE bool resetData(const QString &id);

    // --- the parts the tests drive ---
    // "pkg1, pkg2: /path" lines of `dpkg-query -S` -> path to first package
    static QHash<QString, QString> parsePackageOwners(const QByteArray &output);
    // "name\tversion\tinstalled size in KiB" lines -> name to {version, bytes}
    static QHash<QString, QPair<QString, qint64>> parsePackageInfo(const QByteArray &output);
    // a Depends: field -> the package names in it (every alternative)
    static QStringList parseDepends(const QString &depends);
    // `raye-windows list --json`
    static QList<InstalledApp> parseWindowsList(const QByteArray &json);
    // `raye-windows-profile` JSON ({space: {"optimized": bool}}) onto the list
    static void applyWindowsProfiles(QList<InstalledApp> &apps, const QByteArray &json);
    // the .desktop entries of these folders that a package owns
    // (`owners`: path -> package), one row each
    static QList<InstalledApp> systemApps(const QStringList &folders, const QHash<QString, QString> &owners);
    // filter, search and order the list as the page shows it
    static QList<InstalledApp> arrange(const QList<InstalledApp> &all, const QString &filter, const QString &search,
                                       const QString &order);
    // ~/.var/app/<id>, or "" when the id is not one a Flatpak app can have
    static QString appDataFolder(const QString &home, const QString &appId);
    // a package version as people read it: "1:25.12.3-0build1" -> "25.12.3"
    // (the packaging's own marks are not the app's version)
    static QString displayVersion(const QString &packageVersion);
    // one {"state", "percent", "message"} line of an uninstaller
    static QVariantMap parseProgress(const QByteArray &line);

Q_SIGNALS:
    void countChanged();
    void loadingChanged();
    void filterChanged();
    void sortOrderChanged();
    void searchTextChanged();
    void busyChanged();
    // after uninstall(): ok, and the uninstaller's last words when it failed
    void uninstallFinished(const QString &id, bool ok, const QString &message);

private:
    void rearrange();
    void setLoaded(const QList<InstalledApp> &apps);
    const InstalledApp *find(const QString &id) const;

    QList<InstalledApp> m_all;
    QList<InstalledApp> m_rows;
    QString m_filter = QStringLiteral("all");
    QString m_sortOrder = QStringLiteral("name");
    QString m_searchText;
    bool m_loading = false;
    int m_generation = 0;
    QString m_busyId;
    int m_busyPercent = 0;
    QPointer<QProcess> m_uninstaller;
};
