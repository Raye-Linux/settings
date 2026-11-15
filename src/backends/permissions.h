// SPDX-License-Identifier: GPL-3.0-or-later
// App permissions: which Store apps may use the camera, the microphone,
// the location, take screenshots, keep running in the background, or
// reach all your files.
//
// Store apps run in a sandbox: the first five are the answers the
// desktop's permission store keeps for them (the D-Bus service
// org.freedesktop.impl.portal.PermissionStore, the same list the "Allow
// <app> to use your camera?" prompt writes to). "Files" is the access to
// the whole home folder or the whole computer an app was built with,
// taken back with a per-user override, the way `flatpak override --user`
// writes it. Apps installed outside the Store are not limited by either.
//
// "Let apps use your camera" off turns every app's answer to no and
// remembers which ones were yes, so turning it on again gives them back.
#pragma once

#include <QAbstractListModel>
#include <QDBusConnection>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <QDBusMessage>

class AppPermissions : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    // "camera", "microphone", "location", "screenshot", "background", "files"
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    // "Let apps use your camera"
    Q_PROPERTY(bool masterEnabled READ masterEnabled WRITE setMasterEnabled NOTIFY masterEnabledChanged)
    // the permission store answered (always true for files)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        AllowedRole,
    };
    Q_ENUM(Roles)

    explicit AppPermissions(QObject *parent = nullptr);

    QString kind() const { return m_kind; }
    void setKind(const QString &kind);
    bool masterEnabled() const;
    void setMasterEnabled(bool on);
    bool available() const { return m_available; }
    int count() const { return int(m_apps.size()); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    // the user flipped one app's switch
    Q_INVOKABLE bool setAllowed(const QString &appId, bool allowed);
    Q_INVOKABLE bool isAllowed(const QString &appId) const;
    // the Store apps' own settings page (permissions of one app)
    Q_INVOKABLE bool canOpenAppSettings() const;
    Q_INVOKABLE void openAppSettings() const;

    // The value the store keeps for "allowed" or "not allowed", keeping
    // what else it holds (the location keeps when it was last used).
    static QStringList storeValue(const QString &kind, bool allowed, const QStringList &old);
    static bool storeAllows(const QString &kind, const QStringList &value);
    // A filesystems= list of a Flatpak app: does it reach all the home
    // folder or the whole computer, and with which entries?
    static QStringList broadFilesystems(const QStringList &filesystems);
    // filesystems= of an override: the old list without our "!home",
    // "!host" when allowing, with them when not
    static QStringList overrideFilesystems(const QStringList &old, const QStringList &broad, bool allowed);

    // For the tests: another bus than the session's.
    void setBus(const QDBusConnection &bus);

Q_SIGNALS:
    void kindChanged();
    void masterEnabledChanged();
    void availableChanged();
    void countChanged();
    // the list was read again (from the store or from the disk)
    void loaded();

private Q_SLOTS:
    void storeChanged(const QDBusMessage &message);

private:
    struct App {
        QString id;
        QString name;
        QString icon;
        bool allowed = false;
        // files only: what the app was built with
        QStringList broad;
        QStringList value;
    };

    void loadStore();
    void loadFiles();
    void setApps(QList<App> apps);
    void setAvailable(bool available);
    bool writeStore(const QString &appId, const QStringList &value);
    bool writeFiles(App &app, bool allowed);
    void connectStore();
    QString table() const;
    QString storeId() const;
    QString configKey(const QString &suffix) const;

    QString m_kind;
    QList<App> m_apps;
    bool m_available = false;
    QDBusConnection m_bus;
    QString m_connectedTable;
};
