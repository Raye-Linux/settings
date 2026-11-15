// SPDX-License-Identifier: GPL-3.0-or-later
// System > Advanced > For developers: whether Windows programs print
// their debug messages. The launcher reads debug=on|off from
// /etc/raye/windows.conf, then ~/.config/raye/windows.conf (last line
// wins, the person's file after the system's).
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class WindowsDebug : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool on READ on WRITE setOn NOTIFY changed)

public:
    explicit WindowsDebug(QObject *parent = nullptr);
    // for tests: other files than the real ones
    WindowsDebug(const QString &systemFile, const QString &userFile, QObject *parent = nullptr);

    bool on() const;
    void setOn(bool on);

    // the debug= value of one file's text, "" when it says nothing
    static QString valueIn(const QString &text);
    // text with its debug= lines replaced by one, the rest kept
    static QString withValue(const QString &text, bool on);

Q_SIGNALS:
    void changed();

private:
    QString m_systemFile;
    QString m_userFile;
};
