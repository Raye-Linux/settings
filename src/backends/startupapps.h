// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Startup: the apps that start when you sign in.
//
// They are XDG autostart entries: the user's own in
// $XDG_CONFIG_HOME/autostart, the system's in each $XDG_CONFIG_DIRS
// folder/autostart. A user file of the same name wins over the system
// one, and Hidden=true in it turns the entry off. That is what is
// written here: turning a system entry off copies it into the user's
// folder with Hidden=true, as the desktop's own settings module does.
// Pieces of the desktop itself (NoDisplay=true in the system folder:
// the shell, the password prompt agent...) are not listed: Windows
// does not offer to turn those off either.
#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

struct StartupEntry {
    QString fileName;   // "org.kde.yakuake.desktop", the key of the entry
    QString path;       // the file that decides: the user's, else the system's
    QString systemPath; // the system file of that name, empty when there is none
    QString name;
    QString comment;
    QString iconName;
    bool enabled = true;
};

class StartupApps : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // "name" (A to Z) or "status" (the ones that start, first)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)

public:
    enum Roles {
        FileNameRole = Qt::UserRole + 1,
        NameRole,
        CommentRole,
        IconNameRole,
        EnabledRole,
    };
    Q_ENUM(Roles)

    explicit StartupApps(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString sortOrder() const { return m_sortOrder; }
    void setSortOrder(const QString &order);

    Q_INVOKABLE void reload();
    // turn the entry of that file name on or off; false when it could not be written
    Q_INVOKABLE bool setEnabled(const QString &fileName, bool enabled);
    // start this app when the user signs in: copies its .desktop file
    // (an absolute path, as Apps > Installed apps knows it)
    Q_INVOKABLE bool addApp(const QString &desktopFile);

    // The folders, in the order they count: the user's, then the system's.
    static QString userFolder();
    static QStringList systemFolders();
    // Every entry Settings shows, from these folders, sorted by name.
    static QList<StartupEntry> scan(const QString &userFolder, const QStringList &systemFolders);
    static bool writeEnabled(const StartupEntry &entry, const QString &userFolder, bool enabled);

Q_SIGNALS:
    void countChanged();
    void sortOrderChanged();

private:
    void sort();

    QList<StartupEntry> m_entries;
    QString m_sortOrder = QStringLiteral("name");
};
