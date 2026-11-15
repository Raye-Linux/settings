// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Default apps: which app opens which kind of file or link.
//
// The answer lives in mimeapps.list files ([Default Applications]),
// read in the order the XDG spec gives: $XDG_CONFIG_HOME, then each
// $XDG_CONFIG_DIRS folder, then the applications folders of the data
// dirs; in each, the desktop's own kde-mimeapps.list before
// mimeapps.list. Choices are written to the user's mimeapps.list, the
// file the desktop's own trader writes, and the user's kde-mimeapps.list
// loses any line that would hide them. The browser and the terminal are
// also named in kdeglobals, where KDE apps look for them.
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// An installed app, as its .desktop file says.
struct AppEntry {
    QString id;   // desktop file id: "org.kde.kate.desktop"
    QString path; // the file
    QString name;
    QString comment;
    QString iconName;
    QString exec;
    QStringList mimeTypes;
    QStringList categories;
    bool noDisplay = false;
    int preference = 0; // X-KDE-InitialPreference
};

class AppEntries
{
public:
    // $XDG_DATA_HOME/applications, then each $XDG_DATA_DIRS one
    static QStringList folders();
    // every application entry for this desktop, by id; the first folder
    // that has an id wins, Hidden=true removes it
    static QHash<QString, AppEntry> scan(const QStringList &folders);
};

class DefaultApps : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{ id, title, iconName (the role's), appId, appName, appIcon }]
    Q_PROPERTY(QVariantList roles READ roles NOTIFY changed)

public:
    struct Role {
        QString id;
        QStringList types; // the first one is how apps are found for the role
    };

    explicit DefaultApps(QObject *parent = nullptr);

    QVariantList roles() const;

    Q_INVOKABLE void reload();
    // the apps that can take a role: [{ id, name, iconName, current }]
    Q_INVOKABLE QVariantList candidates(const QString &role) const;
    Q_INVOKABLE bool setRoleDefault(const QString &role, const QString &appId);

    // file and link types matching the text (".png", "png", "pdf",
    // "mailto"): [{ type, label, description, appId, appName, appIcon }]
    Q_INVOKABLE QVariantList searchTypes(const QString &text, int limit = 40) const;
    Q_INVOKABLE QVariantList candidatesForType(const QString &mimeType) const;
    Q_INVOKABLE bool setTypeDefault(const QString &mimeType, const QString &appId);
    // the id of the app that opens this type today, "" when none does
    Q_INVOKABLE QString defaultFor(const QString &mimeType) const;

    // --- the parts the tests drive with their own folders ---
    static QList<Role> roleTable();
    // mimeapps.list files, the one that counts most first
    static QStringList mimeappsFiles();
    // the first default for the type, in these files, that is installed
    static QString defaultIn(const QStringList &files, const QString &mimeType, const QHash<QString, AppEntry> &apps);
    // write appId as the default for each type into the user's files
    static bool writeDefaults(const QString &userFile, const QString &desktopFile, const QStringList &types,
                              const QString &appId);
    // "Exec=konsole --foo %u" -> "konsole --foo"
    static QString commandOf(const QString &exec);

Q_SIGNALS:
    void changed();

private:
    QString currentFor(const Role &role) const;
    QList<AppEntry> appsFor(const Role &role) const;
    QList<AppEntry> appsForType(const QString &mimeType) const;
    QVariantMap appMap(const QString &appId) const;
    bool writeUser(const QStringList &types, const QString &appId);
    void refreshCache() const;

    QHash<QString, AppEntry> m_apps;
};
