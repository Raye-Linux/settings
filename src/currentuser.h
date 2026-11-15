// SPDX-License-Identifier: GPL-3.0-or-later
// Who is signed in, for the launch screen and the navigation pane: the
// name to show, the account picture and whether the account is an
// administrator. Read from AccountsService, the way the sign-in screen
// reads it; nothing here changes the account (Accounts › Your info
// does that).
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class CurrentUser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // the login name ("alex")
    Q_PROPERTY(QString userName READ userName NOTIFY changed)
    // what the account calls itself ("Alex Martin"), empty if not set
    Q_PROPERTY(QString realName READ realName NOTIFY changed)
    // the name to show: the real name, else the login name
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
    // the account picture, empty when there is none (the page draws
    // the initials then)
    Q_PROPERTY(QUrl picture READ picture NOTIFY changed)
    Q_PROPERTY(bool administrator READ administrator NOTIFY changed)
    // "Sudo user" or "Standard user", the line under the name
    Q_PROPERTY(QString accountLabel READ accountLabel NOTIFY changed)

public:
    struct Info {
        QString userName;
        QString realName;
        QString iconFile;
        bool administrator = false;
    };

    // Reads the account of `uid` on `bus` (the system bus by default).
    // QML makes one per window with the first constructor.
    explicit CurrentUser(QObject *parent = nullptr);
    CurrentUser(const QDBusConnection &bus, uint uid, QObject *parent = nullptr);

    QString userName() const { return m_info.userName; }
    QString realName() const { return m_info.realName; }
    QString displayName() const;
    QUrl picture() const;
    bool administrator() const { return m_info.administrator; }
    QString accountLabel() const;

    // Read the account again (AccountsService also says when it changed).
    Q_INVOKABLE void reload();

    // The AccountsService properties of a user as an Info. AccountType
    // 1 is an administrator.
    static Info fromProperties(const QVariantMap &properties);
    // The passwd entry when AccountsService does not answer: the full
    // name is the first field of GECOS ("Alex Martin,,,").
    static Info fromPasswd(const QString &login, const QString &gecos);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void accountChanged();

private:
    Info readAccount();
    void watch(const QString &path);

    QDBusConnection m_bus;
    uint m_uid;
    QString m_watched;
    Info m_info;
    // bumped when the same file holds a new picture, so it is read again
    int m_pictureRevision = 0;
};
