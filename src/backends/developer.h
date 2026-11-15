// SPDX-License-Identifier: GPL-3.0-or-later
// System > Advanced > For developers: Developer Mode, Device Portal,
// device discovery and the OpenSSH server.
//
// Developer Mode is the user's own (raye-developerrc, and a session
// environment file that turns on Raye apps' debug logs). Device Portal
// is a user service run by the device-portal program, which this class
// only asks (status, enable, set-login...). The SSH server and the
// firewall need root: the developer helper, through pkexec.
#pragma once

#include <QFileInfo>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QProcess;

class DeveloperSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool developerMode READ developerMode WRITE setDeveloperMode NOTIFY changed)

    Q_PROPERTY(bool portalEnabled READ portalEnabled NOTIFY changed)
    Q_PROPERTY(bool portalBusy READ portalBusy NOTIFY changed)
    // "https://192.168.1.20:50443", IPv4 first
    Q_PROPERTY(QStringList portalAddresses READ portalAddresses NOTIFY changed)
    // SHA-256 of the certificate, "AB:CD:..."
    Q_PROPERTY(QString portalFingerprint READ portalFingerprint NOTIFY changed)
    Q_PROPERTY(QString portalUser READ portalUser NOTIFY changed)
    Q_PROPERTY(int pairedDevices READ pairedDevices NOTIFY changed)
    // the code shown after pairCode(), "" before
    Q_PROPERTY(QString pairCode READ pairCode NOTIFY changed)
    // [{ time, event, address, detail }], newest first
    Q_PROPERTY(QVariantList portalLog READ portalLog NOTIFY changed)
    Q_PROPERTY(bool allowScreenshot READ allowScreenshot WRITE setAllowScreenshot NOTIFY changed)
    Q_PROPERTY(bool discovery READ discovery WRITE setDiscovery NOTIFY changed)

    Q_PROPERTY(bool sshInstalled READ sshInstalled NOTIFY changed)
    Q_PROPERTY(bool sshRunning READ sshRunning NOTIFY changed)
    Q_PROPERTY(bool sshKeysOnly READ sshKeysOnly NOTIFY changed)
    // passwords are refused by a file of somebody else's: our switch cannot change it
    Q_PROPERTY(bool sshKeysOnlyFixed READ sshKeysOnlyFixed NOTIFY changed)
    Q_PROPERTY(bool discoveryAvailable READ discoveryAvailable NOTIFY changed)
    Q_PROPERTY(bool sshBusy READ sshBusy NOTIFY changed)
    // 0 to 100 while OpenSSH installs, -1 when the progress is not known
    Q_PROPERTY(int sshInstallPercent READ sshInstallPercent NOTIFY changed)

    // "ssh alex@192.168.1.20", the line to type on another computer
    Q_PROPERTY(QString sshCommand READ sshCommand NOTIFY changed)

    // what went wrong last, for an InfoBar; "" when nothing did
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit DeveloperSettings(QObject *parent = nullptr);

    bool developerMode() const { return m_developerMode; }
    void setDeveloperMode(bool on);

    bool portalEnabled() const { return m_portalEnabled; }
    bool portalBusy() const { return m_portalBusy; }
    QStringList portalAddresses() const { return m_addresses; }
    QString portalFingerprint() const { return m_fingerprint; }
    QString portalUser() const { return m_user; }
    int pairedDevices() const { return m_paired; }
    QString pairCode() const { return m_pairCode; }
    QVariantList portalLog() const { return m_log; }
    bool allowScreenshot() const { return m_allowScreenshot; }
    void setAllowScreenshot(bool on);
    bool discovery() const { return m_discovery; }
    void setDiscovery(bool on);

    bool sshInstalled() const;
    bool sshRunning() const { return m_sshRunning; }
    bool sshKeysOnly() const;
    bool sshKeysOnlyFixed() const;
    bool discoveryAvailable() const { return QFileInfo(m_avahi).isExecutable(); }
    bool sshBusy() const { return m_sshBusy; }
    int sshInstallPercent() const { return m_sshPercent; }
    QString sshCommand() const;

    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setPortalEnabled(bool on);
    // the password goes to the portal on its standard input, never as an argument
    Q_INVOKABLE void setPortalLogin(const QString &user, const QString &password);
    Q_INVOKABLE void newPairCode();
    Q_INVOKABLE void forgetPairedDevices();
    Q_INVOKABLE void installSsh();
    Q_INVOKABLE void setSshEnabled(bool on);
    Q_INVOKABLE void setSshKeysOnly(bool on);

    // reads `device-portal status`; false when it is not JSON
    bool readStatus(const QByteArray &json);

    // For the tests.
    void setPortalCommand(const QStringList &command) { m_portal = command; }
    void setHelperCommand(const QStringList &command) { m_helper = command; }
    void setSystemctlCommand(const QStringList &command) { m_systemctl = command; }
    void setSshPaths(const QString &sshd, const QString &keysOnlyFile)
    {
        m_sshd = sshd;
        m_keysFile = keysOnlyFile;
    }
    void setAvahiPath(const QString &path) { m_avahi = path; }
    QString environmentFile() const;

Q_SIGNALS:
    void changed();
    // setPortalLogin() went through, or not ("short": password too short)
    void loginSaved(bool ok, const QString &reason);

private:
    using Done = std::function<void(int code, const QByteArray &out)>;
    void run(const QStringList &command, const QByteArray &input, Done done);
    void portal(const QStringList &args, Done done, const QByteArray &input = {});
    void helper(const QStringList &args, std::function<void(bool ok)> done);
    void refreshSsh();
    void writeEnvironment(bool on);
    QString passwordSettingFile() const;

    QStringList m_portal;
    QStringList m_helper;
    QStringList m_systemctl;
    QString m_sshd;
    QString m_keysFile;
    QString m_avahi = QStringLiteral("/usr/bin/avahi-publish-service");

    bool m_developerMode = false;
    bool m_portalEnabled = false;
    bool m_portalBusy = false;
    QStringList m_addresses;
    QString m_fingerprint;
    QString m_user;
    int m_paired = 0;
    QString m_pairCode;
    QVariantList m_log;
    bool m_allowScreenshot = false;
    bool m_discovery = false;
    bool m_firewall = false;
    bool m_sshRunning = false;
    bool m_sshBusy = false;
    int m_sshPercent = -1;
    QString m_error;
};
