// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Lock screen: the picture on the lock screen, and the
// same picture on the sign-in screen.
//
// The lock screen reads the user's kscreenlockerrc. The sign-in screen runs
// before anyone is signed in, under its own account, so it cannot read a
// picture in a home folder: data/libexec/signin-background keeps a copy
// outside it and names that copy in the theme's theme.conf.user. Settings
// opens the picture as the user and hands the helper its contents on the
// standard input, so the helper running as root never opens a path someone
// gave it.
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QProcess;

class LockScreenSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the picture chosen; empty: the Raye picture
    Q_PROPERTY(QUrl image READ image NOTIFY changed)
    // [url], newest first, at most five
    Q_PROPERTY(QVariantList recentImages READ recentImages NOTIFY changed)
    // the sign-in screen shows a picture of the user's
    Q_PROPERTY(bool onSignIn READ onSignIn NOTIFY changed)
    // asking for the password, or the helper is at work
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit LockScreenSettings(QObject *parent = nullptr);

    QUrl image() const;
    QVariantList recentImages() const;
    bool onSignIn() const;
    bool busy() const { return m_helper != nullptr; }

    Q_INVOKABLE void setPicture(const QUrl &image);
    Q_INVOKABLE void useRayePicture();
    Q_INVOKABLE void setOnSignIn(bool on);

    // the words of the theme file, public for the tests
    static QString backgroundIn(const QByteArray &themeConfig);

    // for the tests: the command in front of the helper's arguments, and
    // the theme file it writes
    void setHelperCommand(const QStringList &command) { m_command = command; }
    void setThemeConfigPath(const QString &path) { m_themeConfig = path; }

Q_SIGNALS:
    void changed();
    void busyChanged();
    // "cancelled": the password was not given, "picture": the file is not a
    // picture the sign-in screen can show, "failed": anything else
    void signInFailed(const QString &reason);

private:
    void runHelper(const QStringList &arguments, const QString &input);
    void remember(const QUrl &image);

    KSharedConfigPtr m_config;
    KSharedConfigPtr m_settings;
    QStringList m_command;
    QString m_themeConfig;
    QProcess *m_helper = nullptr;
};
