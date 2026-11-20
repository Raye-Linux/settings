// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Desktop environment: KDE Plasma (the Raye desktop) or
// Hyprland, the desktop this account signs in to.
//
// Switching now (switchNow): the open apps are written down, the
// desktop-session helper (pkexec, no password: it only ever acts for the
// caller) makes the other desktop the next one and arms ONE automatic
// sign-in back into it for this account, this session is left (Plasma
// saves and closes its apps; on Hyprland each window is asked to close
// first), and the new desktop opens the same apps again (desktop-restore,
// started with the session). Two compositors cannot hand running windows
// over: the apps start again, they do not move.
//
// Switching later (choose): only the next sign-in changes.
//
// A desktop that is not there is installed first (PackageKit, which asks
// for the password itself). Nothing is ever removed by itself: removing
// Hyprland is its own action, offered when both are installed and neither
// the session in use nor the next one is Hyprland. Plasma is never offered
// for removal: the Hyprland desktop is Plasma's shell on Hyprland.
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>

// Where KWin's script answers (org.raye.settings.DesktopSwitch on the
// session bus): one for the whole app, so a page that is gone is never
// called; it hands the answer to whoever waits for it.
class DesktopSwitchBus : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.raye.settings.DesktopSwitch")

public:
    static DesktopSwitchBus *instance();
    // false when the name or the object could not be had
    bool registered() const { return m_registered; }
    std::function<void(const QStringList &)> waiting;
    // which question is open: an old timeout must not answer a new one
    int generation = 0;

public Q_SLOTS:
    // KWin's answer (desktop ids, one per line)
    Q_SCRIPTABLE void apps(const QString &list);

private:
    explicit DesktopSwitchBus(QObject *parent = nullptr);
    bool m_registered = false;
};

class DesktopEnvironment : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the desktop of the session Settings runs in: "plasma", "hyprland", "" (another)
    Q_PROPERTY(QString current READ current CONSTANT)
    // the desktop the next sign-in opens
    Q_PROPERTY(QString next READ next NOTIFY changed)
    Q_PROPERTY(bool plasmaInstalled READ plasmaInstalled NOTIFY changed)
    Q_PROPERTY(bool hyprlandInstalled READ hyprlandInstalled NOTIFY changed)
    // the next sign-in opens another desktop than this one
    Q_PROPERTY(bool signOutNeeded READ signOutNeeded NOTIFY changed)
    Q_PROPERTY(bool canRemoveHyprland READ canRemoveHyprland NOTIFY changed)
    // a switch right now can be offered: this session is Plasma or Hyprland
    Q_PROPERTY(bool canSwitchNow READ canSwitchNow NOTIFY changed)
    // "", "installing", "switching" (the next sign-in), "preparing" (a
    // switch now, before leaving), "leaving", "removing"
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyDesktop READ busyDesktop NOTIFY busyChanged)
    // 0-100 while packages move, -1 when not known
    Q_PROPERTY(int busyPercent READ busyPercent NOTIFY busyChanged)
    // the names of the apps open now (Settings itself left out)
    Q_PROPERTY(QStringList openApps READ openApps NOTIFY openAppsChanged)

public:
    // (operation "install" or "remove", package names, progress 0-100, done(ok, error text))
    using PackageRunner = std::function<void(const QString &, const QStringList &, std::function<void(int)>,
                                             std::function<void(bool, const QString &)>)>;
    // the desktop ids of the open apps, given to done (asynchronous)
    using AppLister = std::function<void(const QString &desktop, std::function<void(const QStringList &)>)>;
    // leave the session (plasma: sign out; hyprland: close the windows, then exit)
    using Leaver = std::function<void(const QString &desktop)>;

    explicit DesktopEnvironment(QObject *parent = nullptr);
    // for the tests: the session files' folder, the helper command, the
    // desktop names of the session (XDG_CURRENT_DESKTOP), the session id,
    // the config file and what installs packages, lists apps and leaves
    DesktopEnvironment(const QString &sessionDir, const QStringList &helper, const QString &currentDesktop,
                       const QString &sessionId, const QString &config, PackageRunner packages, AppLister apps,
                       Leaver leaver, QObject *parent = nullptr);

    QString current() const { return m_current; }
    QString next() const;
    bool plasmaInstalled() const { return m_plasma; }
    bool hyprlandInstalled() const { return m_hyprland; }
    bool signOutNeeded() const;
    bool canRemoveHyprland() const;
    bool canSwitchNow() const { return !m_current.isEmpty(); }
    QString busy() const { return m_busy; }
    QString busyDesktop() const { return m_busyDesktop; }
    int busyPercent() const { return m_busyPercent; }
    QStringList openApps() const { return m_openAppNames; }

    Q_INVOKABLE bool installed(const QString &desktop) const;
    // the next sign-in opens this (installed) desktop
    Q_INVOKABLE bool choose(const QString &desktop);
    // install it; then, when now is true, switch to it right away, else
    // make it the next sign-in's
    Q_INVOKABLE bool install(const QString &desktop, bool now);
    // go to this desktop now: save the apps, arm one sign-in, leave. The
    // page plays its transition between readyToLeave and leave().
    Q_INVOKABLE bool switchNow(const QString &desktop);
    Q_INVOKABLE void leave();
    Q_INVOKABLE bool removeHyprland();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshOpenApps();

    // XDG_CURRENT_DESKTOP -> "plasma", "hyprland" or ""
    static QString desktopOf(const QString &xdgCurrentDesktop);
    static QString packageOf(const QString &desktop);
    static QString sessionFileOf(const QString &desktop);
    // how this desktop signs out: {program, arguments...}; "dbus:<service>
    // <path> <interface> <method>" for a D-Bus call
    static QStringList signOutCommand(const QString &desktop, const QString &sessionId);
    // a window's class (Hyprland) -> the desktop id of its app, from
    // (desktop id, StartupWMClass) pairs; "" when none fits
    static QString desktopIdForClass(const QString &windowClass, const QList<QPair<QString, QString>> &apps);
    // what desktop-restore reads: the desktop, the time, the apps
    static QByteArray restoreList(const QString &desktop, qint64 when, const QStringList &ids);
    static QString restoreFile();
    // KWin's script that hands the open apps to apps() over D-Bus
    static QByteArray kwinListScript();

Q_SIGNALS:
    void changed();
    void busyChanged();
    void openAppsChanged();
    // action "install", "switch" (next sign-in), "switch-now" or "remove";
    // dismissed: the password box was closed
    void finished(const QString &action, const QString &desktop, bool ok, bool dismissed, const QString &message);
    // everything is ready: play the transition, then call leave()
    void readyToLeave(const QString &desktop);
    // this PC cannot switch right away (another account signs in by
    // itself, another file decides): the next sign-in was set instead
    void switchNowUnavailable(const QString &desktop);

private:
    void setBusy(const QString &what, const QString &desktop, int percent = -1);
    void runHelper(const QStringList &arguments, std::function<void(int code, const QString &error)> done);
    void switchTo(const QString &desktop);
    void setOpenApps(const QStringList &ids);
    void rememberNext(const QString &desktop);
    static PackageRunner packageKit();
    static AppLister liveApps(DesktopEnvironment *self);
    static Leaver liveLeaver();

    QString m_sessionDir;
    QStringList m_helper;
    QString m_current;
    QString m_sessionId;
    KSharedConfigPtr m_config;
    PackageRunner m_packages;
    AppLister m_apps;
    Leaver m_leaver;
    bool m_plasma = false;
    bool m_hyprland = false;
    QString m_busy;
    QString m_busyDesktop;
    int m_busyPercent = -1;
    QStringList m_openAppIds;
    QStringList m_openAppNames;
    QTimer m_stillHere;
};
