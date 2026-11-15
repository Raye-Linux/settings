// SPDX-License-Identifier: GPL-3.0-or-later
// Sign-in options > Fingerprint: fprintd's reader, the fingers it knows,
// and adding one. A reader must be claimed while it is used and released
// after; the claim lasts for one thing at a time, so nothing here holds
// it while the page just sits there.
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class FingerprintSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // "right-index-finger" and so on, as fprintd names them
    Q_PROPERTY(QStringList fingers READ fingers NOTIFY changed)
    Q_PROPERTY(bool enrolling READ enrolling NOTIFY changed)
    // the last thing the reader said, in words for the page
    Q_PROPERTY(QString message READ message NOTIFY changed)
    // touches done and touches needed; 0 needed when the reader does not say
    Q_PROPERTY(int stagesDone READ stagesDone NOTIFY changed)
    Q_PROPERTY(int stagesNeeded READ stagesNeeded NOTIFY changed)
    // the sign-in rules of the system accept a finger
    Q_PROPERTY(bool signInEnabled READ signInEnabled NOTIFY changed)

public:
    explicit FingerprintSettings(QObject *parent = nullptr);
    FingerprintSettings(const QDBusConnection &bus, const QString &login, QObject *parent = nullptr);

    bool available() const { return !m_device.isEmpty(); }
    QStringList fingers() const { return m_fingers; }
    bool enrolling() const { return m_enrolling; }
    QString message() const { return m_message; }
    int stagesDone() const { return m_stagesDone; }
    int stagesNeeded() const { return m_stagesNeeded; }
    bool signInEnabled() const;
    // turns the rule on or off through pkexec
    Q_INVOKABLE void setSignInEnabled(bool on);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startEnroll(const QString &finger);
    Q_INVOKABLE void cancelEnroll();
    Q_INVOKABLE void removeAll();
    Q_INVOKABLE static QString fingerName(const QString &finger);
    // true when the status ends the enrolment; `message` follows it
    static bool statusEnds(const QString &status);
    static QString statusMessage(const QString &status);

Q_SIGNALS:
    void changed();
    void enrolled();

private Q_SLOTS:
    void enrollStatus(const QString &status, bool done);

private:
    bool callDevice(const QString &method, const QVariantList &arguments = {}, int timeout = 5000);
    void stop();

    QDBusConnection m_bus;
    QString m_login;
    QString m_device;
    QStringList m_fingers;
    bool m_enrolling = false;
    QString m_message;
    int m_stagesDone = 0;
    int m_stagesNeeded = 0;
public:
    // for the tests
    QString pamFile = QStringLiteral("/etc/pam.d/common-auth");
    QStringList helper{QStringLiteral("pkexec"), QStringLiteral(RAYE_LIBEXECDIR "/fingerprint-signin")};
};
