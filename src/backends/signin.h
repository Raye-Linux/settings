// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts > Sign-in options: changing your password, signing in without
// one, and when Raye asks for it again.
//
// Your own password goes through the passwd program, as in a terminal: it
// is PAM that checks the current one and judges the new one, so a standard
// user can change theirs and nobody can change it without knowing it. The
// program talks to a terminal only, so it gets a small one of its own, and
// the passwords travel on it, never on a command line.
//
// Automatic sign-in is a line in the sign-in screen's configuration:
// /usr/libexec/raye-settings/autologin writes it, through pkexec.
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QSocketNotifier;
class QTimer;

class SignInSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool autoLogin READ autoLogin NOTIFY changed)
    // whose account signs in by itself, empty for nobody
    Q_PROPERTY(QString autoLoginUser READ autoLoginUser NOTIFY changed)
    Q_PROPERTY(bool autoLoginBusy READ autoLoginBusy NOTIFY changed)
    // the helper failed; a closed password prompt is not a failure
    Q_PROPERTY(bool autoLoginFailed READ autoLoginFailed NOTIFY changed)
    // ask for the password again when the screen turns off
    Q_PROPERTY(bool lockWhenAway READ lockWhenAway WRITE setLockWhenAway NOTIFY changed)
    // ksmserverrc [General] loginMode: the apps open at sign-out come back
    Q_PROPERTY(bool restartApps READ restartApps WRITE setRestartApps NOTIFY changed)
    Q_PROPERTY(bool passwordBusy READ passwordBusy NOTIFY changed)
    // what passwd said, in words for the page; empty when all went well
    Q_PROPERTY(QString passwordError READ passwordError NOTIFY changed)

public:
    // passwd asks something this page has no answer for: give up after this
    int passwordTimeout = 30000;

    explicit SignInSettings(QObject *parent = nullptr);
    // for the tests: the configuration folders, the helper, and the
    // program that stands in for passwd
    SignInSettings(const QStringList &sddmConfigs, const QStringList &autologinHelper, const QString &passwdProgram,
                   const QString &powerConfig, const QString &login, QObject *parent = nullptr);
    ~SignInSettings() override;

    bool autoLogin() const { return !m_autoLoginUser.isEmpty() && m_autoLoginUser == m_login; }
    QString autoLoginUser() const { return m_autoLoginUser; }
    bool autoLoginBusy() const { return m_autoLoginBusy; }
    bool autoLoginFailed() const { return m_autoLoginFailed; }
    bool lockWhenAway() const;
    void setLockWhenAway(bool on);
    bool restartApps() const;
    void setRestartApps(bool on);
    // For the tests: a file instead of the user's ksmserverrc
    void setSessionFile(const QString &path);
    bool passwordBusy() const { return m_passwordBusy; }
    QString passwordError() const { return m_passwordError; }

    Q_INVOKABLE void setAutoLogin(bool on);
    Q_INVOKABLE void changePassword(const QString &current, const QString &replacement);
    Q_INVOKABLE void refresh();

    // [Autologin] User= from these files or folders, the way SDDM reads
    // them: in name order, the last one that says wins
    static QString autoLoginUserIn(const QStringList &configs);
    // passwd in the C locale: what it says at the end, as our own words
    static QString describePasswdOutput(const QString &output, int exitCode);

Q_SIGNALS:
    void changed();
    void passwordChanged();

private:
    void passwdOutput();
    void passwdStuck();
    void passwdFinished(int exitCode);

    KSharedConfigPtr m_power;
    KSharedConfigPtr m_session;
    QStringList m_sddmConfigs;
    QStringList m_helper;
    QString m_passwd;
    QString m_login;
    QString m_autoLoginUser;
    bool m_autoLoginBusy = false;
    bool m_autoLoginFailed = false;
    bool m_passwordBusy = false;
    QString m_passwordError;

    // the running passwd
    int m_pty = -1;
    int m_pid = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_stuck = nullptr;
    QByteArray m_output;
    QByteArray m_answerCurrent;
    QByteArray m_answerNew;
    int m_newAsked = 0;
    bool m_currentAsked = false;
};
