// SPDX-License-Identifier: GPL-3.0-or-later
// Facts about this computer for the Device security, Drivers and Gaming
// pages. Everything here is read without root.
#pragma once

#include <QObject>
#include <QStringList>

class SystemInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int secureBoot READ secureBoot NOTIFY changed)        // 1 on, 0 off, -1 not UEFI
    Q_PROPERTY(bool tpm READ tpm NOTIFY changed)
    Q_PROPERTY(bool encrypted READ encrypted NOTIFY changed)
    Q_PROPERTY(bool recoveryKeyFile READ recoveryKeyFile NOTIFY changed)
    Q_PROPERTY(QStringList gpus READ gpus NOTIFY changed)
    Q_PROPERTY(QString driversDone READ driversDone NOTIFY changed)
    Q_PROPERTY(bool gamemode READ gamemode NOTIFY changed)
    Q_PROPERTY(bool mangohud READ mangohud WRITE setMangohud NOTIFY changed)
    Q_PROPERTY(QStringList windowsApps READ windowsApps NOTIFY changed)
    // running, enrolled (restart to use it), pending (key waits for the
    // blue screen), off, unavailable
    Q_PROPERTY(QString rayeKernel READ rayeKernel NOTIFY changed)
    Q_PROPERTY(QString enrollCode READ enrollCode NOTIFY changed)
    Q_PROPERTY(bool kernelBusy READ kernelBusy NOTIFY changed)

public:
    explicit SystemInfo(QObject *parent = nullptr);

    int secureBoot() const { return m_secureBoot; }
    bool tpm() const { return m_tpm; }
    bool encrypted() const { return m_encrypted; }
    bool recoveryKeyFile() const;
    QStringList gpus() const { return m_gpus; }
    QString driversDone() const { return m_driversDone; }
    bool gamemode() const { return m_gamemode; }
    bool mangohud() const;
    void setMangohud(bool on);
    QStringList windowsApps() const;
    QString rayeKernel() const { return m_rayeKernel; }
    QString enrollCode() const { return m_enrollCode; }
    bool kernelBusy() const { return m_kernelBusy; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void lookForDrivers();
    Q_INVOKABLE bool forgetWindowsApp(const QString &name);
    Q_INVOKABLE void enableRayeKernel();
    Q_INVOKABLE void disableRayeKernel();

    // the parsing, public for the tests
    static int parseSecureBoot(const QByteArray &efivar);
    static bool crypttabHasEntries(const QString &crypttab);
    static QStringList parseGpus(const QString &lspciMachine);
    static QString kernelState(const QString &uname, const QString &setupStatus);
    // What Device security asks the user to look at, in the order of the
    // page: "secure-boot" (off, though the PC could have it), "tpm" (the
    // chip no longer opens the disk), "recovery-key" (still in a file in
    // the home folder), "firewall" (off, or gone: Raye installs it).
    // Encryption is chosen when
    // installing and a PC without UEFI cannot get Secure Boot: neither is
    // something to fix here, so neither is listed.
    // firewall: 1 on, 0 off, -1 not installed.
    Q_INVOKABLE static QStringList actionsNeeded(int secureBoot, const QString &tpmState, bool recoveryKeyFile,
                                                 int firewall);

Q_SIGNALS:
    void changed();

private:
    int m_secureBoot = -1;
    bool m_tpm = false;
    bool m_encrypted = false;
    QStringList m_gpus;
    QString m_driversDone;
    bool m_gamemode = false;
    QString m_rayeKernel = QStringLiteral("unavailable");
    QString m_enrollCode;
    bool m_kernelBusy = false;
};
