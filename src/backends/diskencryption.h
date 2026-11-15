// SPDX-License-Identifier: GPL-3.0-or-later
// Turning disk encryption on or off with the PC on (Windows' device
// encryption), through /usr/libexec/raye-encryption from raye-system.
//
// The state comes from `raye-encryption status --json`, run without a
// password. Turning on or off runs the helper through pkexec and hands
// it the disk password on its standard input, through a pipe of our own.
// The recovery key the helper prints is kept here until the page has
// shown it once, then wiped.
#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QProcess;
class QTimer;

class DiskEncryption : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "", "restart" (ready, waits for a restart), "running", "paused",
    // "done", "failed"
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    // "encrypt" or "decrypt": what the phase is about
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(double percent READ percent NOTIFY changed)
    // why it cannot start now: "", "battery", "space", "boot-space",
    // "no-tpm", "unsupported-disk", "busy", "unknown-disk"
    Q_PROPERTY(QString blocked READ blocked NOTIFY changed)
    // why it stopped: "battery", "interrupted", "no-key" or a message
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool known READ known NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // the last turnOn/turnOff: "", "wrong-password", "cancelled",
    // "refused", "failed", "started"
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString recoveryKey READ recoveryKey NOTIFY recoveryKeyChanged)
    // 2 for a TPM 2.0, 1 for 1.2, 0 when there is none
    Q_PROPERTY(int tpmVersion READ tpmVersion CONSTANT)
    // Secure Boot has no keys: anything can start (setup mode)
    Q_PROPERTY(bool setupMode READ setupMode CONSTANT)

public:
    explicit DiskEncryption(QObject *parent = nullptr);
    ~DiskEncryption() override;

    QString phase() const { return m_phase; }
    QString mode() const { return m_mode; }
    double percent() const { return m_percent; }
    QString blocked() const { return m_blocked; }
    QString error() const { return m_error; }
    bool known() const { return m_known; }
    bool busy() const { return m_busy; }
    QString result() const { return m_result; }
    QString recoveryKey() const { return m_recoveryKey; }
    int tpmVersion() const;
    bool setupMode() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool turnOn(const QString &password);
    Q_INVOKABLE bool turnOff(const QString &password);
    Q_INVOKABLE void resume();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void clearResult();
    Q_INVOKABLE void forgetRecoveryKey();
    // "" when saved, else "same-disk" or "failed"
    Q_INVOKABLE QString saveRecoveryKey(const QUrl &file);
    Q_INVOKABLE bool printRecoveryKey();
    Q_INVOKABLE bool canPrint() const;

    // For the tests.
    static bool applyStatus(DiskEncryption *target, const QByteArray &json);
    static QString parseRecoveryKey(const QByteArray &output);
    static int resultFor(int exitCode, bool normalExit, QString *result);
    void setHelper(const QStringList &status, const QStringList &admin)
    {
        m_statusCommand = status;
        m_adminCommand = admin;
    }
    void setSysRoot(const QString &root) { m_sysRoot = root; }

Q_SIGNALS:
    void changed();
    void busyChanged();
    void resultChanged();
    void recoveryKeyChanged();

private:
    bool runWithPassword(const QString &action, const QString &password);
    void setBusy(bool busy);
    void setResult(const QString &result);

    QString m_phase;
    QString m_mode;
    double m_percent = 0;
    QString m_blocked;
    QString m_error;
    bool m_known = false;
    bool m_busy = false;
    QString m_result;
    QString m_recoveryKey;
    QStringList m_statusCommand;
    QStringList m_adminCommand;
    QString m_sysRoot;
    QProcess *m_status = nullptr;
    QTimer *m_poll = nullptr;
};
