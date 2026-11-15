// SPDX-License-Identifier: GPL-3.0-or-later
// The firewall switch of Device security. Raye's firewall is ufw: on, it
// blocks the connections other computers start to this PC and lets every
// one this PC starts go out. The state is read from ufw's own settings
// file (readable by anyone); turning it on or off asks for an
// administrator's password and runs Settings' firewall helper.
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class Firewall : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // the last change did not go through (not the password prompt closed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)

public:
    explicit Firewall(QObject *parent = nullptr);

    bool installed() const { return m_installed; }
    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    bool failed() const { return m_failed; }

    Q_INVOKABLE void refresh();
    // false when a change is already running
    Q_INVOKABLE bool setEnabled(bool on);
    // the rules, in the firewall module's own window
    Q_INVOKABLE bool canOpenRules() const;
    Q_INVOKABLE void openRules() const;

    // ufw.conf: 1 for ENABLED=yes, 0 otherwise
    static int parseConf(const QString &text);

    // For the tests.
    void setConfPath(const QString &path) { m_confPath = path; }
    void setHelperCommand(const QStringList &command) { m_helper = command; }

Q_SIGNALS:
    void changed();
    void finished(bool ok);

private:
    QString m_confPath;
    QStringList m_helper;
    bool m_installed = false;
    bool m_enabled = false;
    bool m_busy = false;
    bool m_failed = false;
};
