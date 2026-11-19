// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Desktop style: rayeshellrc [Appearance] Look. The
// taskbar, Start and the window frames follow it at once (KConfig::Notify):
// "windows11" (the default) or "windows10", left-aligned with square
// corners and tiles in Start.
#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class ShellLook : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString look READ look WRITE setLook NOTIFY changed)
    Q_PROPERTY(bool windows10 READ windows10 NOTIFY changed)

public:
    explicit ShellLook(QObject *parent = nullptr);

    QString look() const;
    // anything but a known word or alias is ignored
    void setLook(const QString &look);
    bool windows10() const { return look() == QLatin1String("windows10"); }

    // a value of the key as the shell reads it: its aliases (10, win10...)
    // become the word they stand for, anything else is "windows11"
    static QString normalize(const QString &value);
    static bool isWindows10(const KSharedConfigPtr &shellrc);

Q_SIGNALS:
    void changed();

private:
    KSharedConfigPtr m_config;
    KConfigWatcher::Ptr m_watcher;
};
