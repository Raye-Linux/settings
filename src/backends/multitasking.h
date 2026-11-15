// SPDX-License-Identifier: GPL-3.0-or-later
// The small System pages that only read and write a few settings of
// other programs: Multitasking (window manager and taskbar), Advanced
// (Files, taskbar, terminal) and Remote Desktop (the remote desktop
// service). Every change is written at once and the program that reads
// it is told, as Windows applies a change without an Apply button.
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// System > Multitasking. The keys are the ones the window manager and
// the shell read (shell.md 3.6-3.8):
//   kwinrc [Windows] ElectricBorderTiling, ElectricBorderMaximize
//   kwinrc [Script-org.raye.snap] SnapLayoutsOnHover, SnapBar, SnapAssist
//   kwinrc [TabBox] DesktopMode (0 all desktops, 1 this one), ShowDesktopMode
//   rayeshellrc [Taskbar] AllDesktops (only once the shell ships the key)
class MultitaskingSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool snapWindows READ snapWindows WRITE setSnapWindows NOTIFY changed)
    Q_PROPERTY(bool snapLayoutsOnHover READ snapLayoutsOnHover WRITE setSnapLayoutsOnHover NOTIFY changed)
    Q_PROPERTY(bool snapBar READ snapBar WRITE setSnapBar NOTIFY changed)
    Q_PROPERTY(bool snapAssist READ snapAssist WRITE setSnapAssist NOTIFY changed)
    Q_PROPERTY(bool altTabAllDesktops READ altTabAllDesktops WRITE setAltTabAllDesktops NOTIFY changed)
    Q_PROPERTY(bool altTabShowsDesktop READ altTabShowsDesktop WRITE setAltTabShowsDesktop NOTIFY changed)
    Q_PROPERTY(bool taskbarAllDesktops READ taskbarAllDesktops WRITE setTaskbarAllDesktops NOTIFY changed)
    // the taskbar knows the setting (its key is in the system defaults)
    Q_PROPERTY(bool taskbarAllDesktopsAvailable READ taskbarAllDesktopsAvailable NOTIFY changed)

public:
    explicit MultitaskingSettings(QObject *parent = nullptr);

    bool snapWindows() const;
    void setSnapWindows(bool on);
    bool snapLayoutsOnHover() const;
    void setSnapLayoutsOnHover(bool on);
    bool snapBar() const;
    void setSnapBar(bool on);
    bool snapAssist() const;
    void setSnapAssist(bool on);
    bool altTabAllDesktops() const;
    void setAltTabAllDesktops(bool on);
    bool altTabShowsDesktop() const;
    void setAltTabShowsDesktop(bool on);
    bool taskbarAllDesktops() const;
    void setTaskbarAllDesktops(bool on);
    bool taskbarAllDesktopsAvailable() const;

    // read the files again (another program changed them)
    Q_INVOKABLE void reload();
    // the list of desktops: add, rename, order (its own window for now)
    Q_INVOKABLE void openDesktops();

    // Is `key` of `group` set in the system's defaults of `file` (not in
    // the user's own copy)? That is how a page knows a reader exists.
    static bool systemDefines(const QString &file, const QString &group, const QString &key);

Q_SIGNALS:
    void changed();

private:
    void writeKwin(const QString &group, const QString &key, const QVariant &value);

    KSharedConfigPtr m_kwin;
    KSharedConfigPtr m_shell;
};

// System > Advanced.
//   dolphinrc [Raye] ShowExtensions, ShowHidden (read by Files, explorer.md 3.18)
//   rayeshellrc [Taskbar] EndTask (shell.md 3.8)
//   kdeglobals [General] TerminalService, TerminalApplication
class AdvancedSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool showExtensions READ showExtensions WRITE setShowExtensions NOTIFY changed)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY changed)
    Q_PROPERTY(bool endTask READ endTask WRITE setEndTask NOTIFY changed)
    // [{ id: "org.kde.konsole.desktop", name, icon }], by name
    Q_PROPERTY(QVariantList terminals READ terminals NOTIFY changed)
    // the desktop id of the terminal in use
    Q_PROPERTY(QString terminal READ terminal WRITE setTerminal NOTIFY changed)

public:
    struct Terminal {
        QString id;
        QString name;
        QString icon;
        QString exec;
    };

    explicit AdvancedSettings(QObject *parent = nullptr);

    bool showExtensions() const;
    void setShowExtensions(bool on);
    bool showHidden() const;
    void setShowHidden(bool on);
    bool endTask() const;
    void setEndTask(bool on);
    QVariantList terminals() const;
    QString terminal() const;
    void setTerminal(const QString &id);

    Q_INVOKABLE void reload();

    // the terminal apps installed: desktop entries in the TerminalEmulator
    // category, shown in menus, the user's own entries first
    static QList<Terminal> findTerminals();
    // the command of an Exec= line without its field codes:
    // "konsole" for "konsole %u"
    static QString commandOf(const QString &exec);

Q_SIGNALS:
    void changed();

private:
    KSharedConfigPtr m_files;
    KSharedConfigPtr m_shell;
    KSharedConfigPtr m_globals;
    QList<Terminal> m_terminals;
};

// System > Remote Desktop: the state of the remote desktop service, who
// may connect, and the address another PC types. Users, passwords and
// the certificate are set in its own module window.
//   krdpserverrc [General] ListenPort (3389), Users, SystemUserEnabled
//   user unit app-org.kde.krdpserver.service
class RemoteDesktop : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    // waiting for the service to start or stop
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // someone may connect: a user with a password, or this PC's accounts
    Q_PROPERTY(bool hasUsers READ hasUsers NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    // "192.168.1.20:3389", one per network this PC is on
    Q_PROPERTY(QStringList addresses READ addresses NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit RemoteDesktop(QObject *parent = nullptr);

    bool installed() const { return m_installed; }
    bool running() const { return m_running; }
    bool busy() const { return m_busy; }
    bool hasUsers() const;
    int port() const;
    QStringList addresses() const { return m_addresses; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    // start it now and at every sign-in, or stop it for good
    Q_INVOKABLE void setEnabled(bool on);
    // users, passwords, port: the service's own settings window
    Q_INVOKABLE void openOptions();

    // "address:port" for each, IPv6 in brackets
    static QStringList formatAddresses(const QStringList &ips, int port);
    // the addresses another PC on the network can reach: no loopback,
    // no link-local, interfaces that are up
    static QStringList localAddresses();
    static const QString unit;

Q_SIGNALS:
    void changed();

private:
    void readState();
    void callSystemd(const QString &method, const QVariantList &arguments);

    KSharedConfigPtr m_config;
    bool m_installed = false;
    bool m_running = false;
    bool m_busy = false;
    int m_pending = 0;
    QStringList m_addresses;
    QString m_error;
};
