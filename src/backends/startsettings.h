// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Start and its Folders page: rayeshellrc [Start], which
// the Start menu follows at once (KConfig::Notify).
//
// The shell lists some keys it does not read yet. A row for one of those
// would be a switch that does nothing, so the pages ask `followed` first:
// the key's property is named somewhere in the Start menu's own files, or
// the row is not offered. Nothing here has to change the day the menu
// starts reading them.
#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class StartSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "default", "pins" or "recommendations"
    Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY changed)
    Q_PROPERTY(bool recommended READ recommended WRITE setRecommended NOTIFY changed)
    Q_PROPERTY(bool recentApps READ recentApps WRITE setRecentApps NOTIFY changed)
    Q_PROPERTY(bool mostUsed READ mostUsed WRITE setMostUsed NOTIFY changed)
    Q_PROPERTY(QStringList folders READ folders NOTIFY changed)
    Q_PROPERTY(bool recentAppsFollowed READ recentAppsFollowed CONSTANT)
    Q_PROPERTY(bool mostUsedFollowed READ mostUsedFollowed CONSTANT)
    Q_PROPERTY(bool foldersFollowed READ foldersFollowed CONSTANT)

public:
    explicit StartSettings(QObject *parent = nullptr);
    // for the tests: the Start menu's QML in a folder of their own
    explicit StartSettings(const QString &menuFolder, QObject *parent = nullptr);

    QString layout() const;
    void setLayout(const QString &layout);
    bool recommended() const;
    void setRecommended(bool on);
    bool recentApps() const;
    void setRecentApps(bool on);
    bool mostUsed() const;
    void setMostUsed(bool on);
    QStringList folders() const;
    Q_INVOKABLE void setFolder(const QString &id, bool on);

    bool recentAppsFollowed() const { return m_read.contains(QLatin1String("startRecentApps")); }
    bool mostUsedFollowed() const { return m_read.contains(QLatin1String("startMostUsed")); }
    bool foldersFollowed() const { return m_read.contains(QLatin1String("startFolders")); }

    // every folder Start can show, in the order it shows them
    static QStringList folderIds();
    // which of `names` are named in the QML and JavaScript under `folder`
    static QStringList namedIn(const QString &folder, const QStringList &names);

Q_SIGNALS:
    void changed();

private:
    bool readBool(const char *key, bool fallback) const;
    void write(const char *key, const QVariant &value);

    KSharedConfigPtr m_config;
    KConfigWatcher::Ptr m_watcher;
    QStringList m_read;
};
