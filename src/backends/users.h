// SPDX-License-Identifier: GPL-3.0-or-later
// The people on this PC, through AccountsService: your name and picture,
// and adding, changing and removing the other accounts. Changing another
// account makes AccountsService ask an administrator's password (polkit),
// so every call here can wait as long as a person takes to answer it.
#pragma once

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class AccountUsers : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the other people: { uid, userName, realName, displayName, picture, administrator }
    Q_PROPERTY(QVariantList others READ others NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // what went wrong the last time, in words for the page; empty if nothing did
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList avatars READ avatars CONSTANT)

public:
    explicit AccountUsers(QObject *parent = nullptr);
    AccountUsers(const QDBusConnection &bus, uint uid, QObject *parent = nullptr);

    QVariantList others() const { return m_others; }
    bool available() const { return m_available; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QStringList avatars() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void setMyName(const QString &realName);
    // a file of the user's: cropped square and made small first, the
    // sign-in screen reads it for every account on the PC
    Q_INVOKABLE void setMyPicture(const QUrl &file);
    Q_INVOKABLE void addUser(const QString &realName, const QString &login, const QString &password, bool administrator);
    Q_INVOKABLE void setAdministrator(qlonglong uid, bool administrator);
    Q_INVOKABLE void setPasswordOf(qlonglong uid, const QString &password);
    Q_INVOKABLE void removeUser(qlonglong uid, bool deleteFiles);

    // "Alex Martin" gives "alex"; nothing usable gives an empty string
    Q_INVOKABLE static QString suggestLogin(const QString &realName);
    // what is wrong with a login name, or empty when it can be used
    Q_INVOKABLE QString loginProblem(const QString &login) const;
    Q_INVOKABLE static QString passwordProblem(const QString &password, const QString &confirm);

    // a password as /etc/shadow keeps it, in the system's own format
    static QString hashPassword(const QString &password);
    // the picture written to a file of its own; empty when the file is no image
    static QString preparePicture(const QString &file, const QString &outputFolder);

Q_SIGNALS:
    void changed();
    // the last change went through
    void done();

private Q_SLOTS:
    void usersChanged();

private:
    QString pathOf(qlonglong uid) const;
    void call(const QString &path, const QString &interface, const QString &method, const QVariantList &arguments,
              std::function<void(const QDBusMessage &)> next = {});
    void failed(const QDBusMessage &reply);
    void finish();

    QDBusConnection m_bus;
    uint m_uid;
    QVariantList m_others;
    bool m_available = false;
    bool m_busy = false;
    QString m_error;
};
