// SPDX-License-Identifier: GPL-3.0-or-later
// The security chip and the disk: does the chip still open the disk by
// itself, and "Use the security chip again" when it does not (after a
// firmware or kernel change the disk asks for its password at start-up).
//
// The state comes from `raye-kernel-setup tpm-status`, run without a
// password (ok, stale, none). The rebind runs
// `pkexec raye-kernel-setup rebind` and hands it the disk password on
// its standard input, through a pipe of our own: never in its arguments
// or its environment, never in a buffer we do not wipe.
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QProcess;

class Tpm : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "ok" the chip opens the disk, "stale" it no longer does, "none" no
    // disk is bound to the chip, "unknown" nobody could tell
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // how the last rebind went: "", "done", "wrong-password",
    // "cancelled" (the password prompt was closed), "failed"
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)

public:
    explicit Tpm(QObject *parent = nullptr);
    ~Tpm() override;

    QString state() const { return m_state; }
    bool busy() const { return m_busy; }
    QString result() const { return m_result; }

    // asks the helper again; refreshed() once it has answered
    Q_INVOKABLE void refresh();
    // Starts the rebind with this disk password. False (and the result
    // set at once) when it cannot be sent: empty, a line break in it,
    // or a rebind already running.
    Q_INVOKABLE bool rebind(const QString &password);
    Q_INVOKABLE void clearResult();

    // The first line of `tpm-status`.
    static QString parseStatus(const QByteArray &output);

    // For the tests: the commands run, and the flag the boot check leaves
    // when the chip did not open the disk.
    QStringList statusCommand() const { return m_statusCommand; }
    QStringList rebindCommand() const { return m_rebindCommand; }
    void setStatusCommand(const QStringList &command) { m_statusCommand = command; }
    void setRebindCommand(const QStringList &command) { m_rebindCommand = command; }
    void setNeedsPasswordFlag(const QString &path) { m_needsPasswordFlag = path; }

Q_SIGNALS:
    void stateChanged();
    void busyChanged();
    void resultChanged();
    void refreshed();
    void rebindFinished(const QString &result);

private:
    void setState(const QString &state);
    void setBusy(bool busy);
    void finish(const QString &result);

    QString m_state = QStringLiteral("unknown");
    bool m_busy = false;
    QString m_result;
    QStringList m_statusCommand;
    QStringList m_rebindCommand;
    QString m_needsPasswordFlag;
    QProcess *m_status = nullptr;
};
