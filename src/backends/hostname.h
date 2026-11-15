// SPDX-License-Identifier: GPL-3.0-or-later
// The name of this PC, and "Rename this PC". systemd's hostnamed keeps
// it (org.freedesktop.hostname1 on the system bus): the pretty name is
// what the user typed, the static name the same in lower case. polkit
// asks for the administrator password when it has to.
//
// The rules are Windows' ones, so a name that works here also works on
// a Windows network: letters, numbers and hyphens, at most 15, not only
// numbers.
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class Hostname : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the name to show: the pretty name, else the static one
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    static constexpr int maxLength = 15;

    explicit Hostname(QObject *parent = nullptr);
    // `bus` is for the tests (a private bus with a fake hostnamed)
    Hostname(const QDBusConnection &bus, QObject *parent);

    QString name() const { return m_name; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void reload();
    // Renames the PC; renamed() tells how it went.
    Q_INVOKABLE void rename(const QString &name);

    // "" when the name is fine, else "empty", "too-long", "characters",
    // "digits" (only numbers) or "hyphen" (first or last)
    Q_INVOKABLE static QString problemWith(const QString &name);
    // the sentence the dialog shows for a problem ("" for none)
    Q_INVOKABLE static QString problemText(const QString &problem);
    static QString staticNameOf(const QString &name);

Q_SIGNALS:
    void changed();
    void busyChanged();
    // ok, or why not ("denied" when the password was not given, "failed")
    void renamed(bool ok, const QString &error);

private:
    void setBusy(bool busy);
    void finish(bool ok, const QString &error);

    QDBusConnection m_bus;
    QString m_name;
    bool m_busy = false;
};
