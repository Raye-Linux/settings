// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Taskbar. The taskbar is a Plasma panel; every change
// is made at once to the running panels through the shell's scripting
// interface (org.kde.PlasmaShell.evaluateScript), the same way the
// desktop layout builds them, and Plasma saves it in its own file:
//
//   Search            the search applet (org.kde.milou) after Start, or none
//   Task view         an icon applet starting org.raye.taskview.desktop
//   Alignment         the spacer before Start: expanding (centre) or not (left);
//                     always left with Raye's taskbar in the Windows 10 style
//                     ([Appearance] Look), which keeps the key as it is
//   Automatically hide  the panel's hiding mode
//   Far corner        the show-desktop applet at the end of the panel
//   Combine buttons   icontasks (icons only) or taskmanager (labels), and
//                     its groupingStrategy / onlyGroupWhenFull / taskMaxWidth
//   Tray icons        the tray's extraItems (system icons, on or off) and
//                     shownItems / hiddenItems (apps, on the bar or under ^)
//
// With Raye's own taskbar (org.raye.start, org.raye.taskbar, org.raye.clock
// on the main panel) these settings are not panel changes but keys of
// rayeshellrc [Taskbar], which those applets follow at once: Search,
// TaskView, Alignment, Combine, Badges, Flashing, ShowDesktopCorner,
// MultiDisplayApps. Each part decides for itself: Search and Task view go
// to rayeshellrc when Raye's Start is there, and so on.
//
// A change is made on every panel of the user's that has the part. The
// taskbars raye-taskbar-sync puts on the other screens are its own: it
// copies the main one's settings to them. The two multi-display choices belong to the
// program that puts a taskbar on each screen: they are written to
// raye-taskbarrc [Taskbar] AllDisplays, MultiDisplayButtons, and that
// program (/usr/libexec/raye-taskbar-sync) is run. It also runs after
// every other change: it copies the main taskbar's settings to the
// taskbars of the other screens.
#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace TaskbarLogic
{
struct Widget {
    int id = -1;
    QString plugin;
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    // the applet's [General] settings, and the ones above it ([Configuration])
    QVariantMap general;
    QVariantMap root;
};

struct Panel {
    int id = -1;
    int screen = 0;
    QString hiding;
    QString location;
    // a taskbar raye-taskbar-sync made for another screen ([Raye] Role=secondary)
    bool secondary = false;
    // left to right, in the panel's own order (its AppletOrder)
    QList<Widget> widgets;
    // the applets' places agree with that order. Not so while the panel
    // is not shown (a locked screen): a new applet is placed by where
    // the others are, so nothing is added then.
    bool laidOut = true;
};

struct State {
    QList<Panel> panels;
};

// The JSON the read script prints.
State parseState(const QByteArray &json);

// The panel the page shows: the user's own taskbar (not one made for
// another screen) at the bottom of the main screen (screen 0), else the
// first one with Start, else the first one.
const Panel *mainPanel(const State &state);

// A value as Plasma's scripting returns it ("true", "1", true, 1).
bool toBool(const QVariant &value, bool fallback);
// A list as Plasma's scripting returns it: a string with commas or a list.
QStringList toList(const QVariant &value);

// What the page shows, read from one panel.
QString search(const Panel &panel);              // "hidden", "icon"
bool taskView(const Panel &panel);
QString alignment(const Panel &panel);           // "center", "left"
bool autoHide(const Panel &panel);
bool showDesktopCorner(const Panel &panel);
QString combine(const Panel &panel);             // "always", "full", "never"

// Tray items: the system ones (plasmoids, required ones left out) and the
// apps (status notifier items, running now or remembered by the tray).
struct TrayItem {
    QString id;
    bool on = false;
};
// The tray's own applets, minus the ones the desktop cannot do without.
QList<TrayItem> systemIcons(const Panel &panel);
// `running` are the ids of the apps with a tray icon now.
QList<TrayItem> otherIcons(const Panel &panel, const QStringList &running);
// Applets that stay in the tray whatever the page says.
QStringList requiredTrayItems();

// The operations that make a change, on every panel that has the part
// (never on a secondary one).
// Each is a JSON object the apply script understands:
//   { op: "config",  widget, group: [..], values: {..} }
//   { op: "remove",  widget }
//   { op: "add",     panel, plugin, x, y, args: [..], values: {..} }  (no x: at the end)
//   { op: "replace", panel, widget, plugin, values: {..} }   same settings, other applet
//   { op: "hiding",  panel, value }
QJsonArray searchOps(const State &state, const QString &mode);
QJsonArray taskViewOps(const State &state, bool on, const QString &desktopUrl);
QJsonArray alignmentOps(const State &state, const QString &mode);
QJsonArray autoHideOps(const State &state, bool on);
QJsonArray showDesktopCornerOps(const State &state, bool on);
QJsonArray combineOps(const State &state, const QString &mode);
QJsonArray systemIconOps(const State &state, const QString &id, bool on);
QJsonArray otherIconOps(const State &state, const QString &id, bool on);

// The script that prints every panel as JSON.
QString readScript();
// The script that runs `ops`, then prints the panels as they are after.
QString applyScript(const QJsonArray &ops);

// raye-taskbarrc [Taskbar] MultiDisplayButtons: a known value or "all".
QString multiDisplayButtons(const QString &value);
// the same choice in rayeshellrc's words (MultiDisplayApps)
QString multiDisplayApps(const QString &buttons);

// whether a panel holds an applet
bool has(const Panel &panel, const QString &plugin);
// rayeshellrc [Taskbar] values: a known one or the default
QString shellSearch(const QString &value);        // box, iconlabel, icon, hidden
QString shellAlignment(const QString &value);     // center, left
QString shellCombine(const QString &value);       // always, full, never

const QString milou = QStringLiteral("org.kde.milou");
const QString kickoff = QStringLiteral("org.kde.plasma.kickoff");
const QString spacer = QStringLiteral("org.kde.plasma.panelspacer");
const QString iconTasks = QStringLiteral("org.kde.plasma.icontasks");
const QString labelTasks = QStringLiteral("org.kde.plasma.taskmanager");
const QString showDesktop = QStringLiteral("org.kde.plasma.showdesktop");
const QString icon = QStringLiteral("org.kde.plasma.icon");
const QString tray = QStringLiteral("org.kde.plasma.systemtray");
const QString taskViewDesktop = QStringLiteral("org.raye.taskview.desktop");
const QString rayeStart = QStringLiteral("org.raye.start");
const QString rayeTasks = QStringLiteral("org.raye.taskbar");
const QString rayeClock = QStringLiteral("org.raye.clock");
}

class TaskbarSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the shell answered and has a taskbar
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    Q_PROPERTY(bool taskView READ taskView WRITE setTaskView NOTIFY changed)
    Q_PROPERTY(QString alignment READ alignment WRITE setAlignment NOTIFY changed)
    // the Windows 10 style keeps Raye's taskbar on the left
    Q_PROPERTY(bool alignmentFixed READ alignmentFixed NOTIFY changed)
    Q_PROPERTY(bool autoHide READ autoHide WRITE setAutoHide NOTIFY changed)
    Q_PROPERTY(bool showDesktopCorner READ showDesktopCorner WRITE setShowDesktopCorner NOTIFY changed)
    Q_PROPERTY(QString combine READ combine WRITE setCombine NOTIFY changed)
    Q_PROPERTY(bool allDisplays READ allDisplays WRITE setAllDisplays NOTIFY changed)
    Q_PROPERTY(QString multiDisplayButtons READ multiDisplayButtons WRITE setMultiDisplayButtons NOTIFY changed)
    Q_PROPERTY(bool badges READ badges WRITE setBadges NOTIFY changed)
    Q_PROPERTY(bool flashing READ flashing WRITE setFlashing NOTIFY changed)
    // the parts of Raye's own taskbar on the main panel
    Q_PROPERTY(bool rayeStart READ rayeStart NOTIFY changed)
    Q_PROPERTY(bool rayeTasks READ rayeTasks NOTIFY changed)
    Q_PROPERTY(bool rayeClock READ rayeClock NOTIFY changed)
    // [{ id, name, icon, on }], by name
    Q_PROPERTY(QVariantList systemIcons READ systemIcons NOTIFY changed)
    Q_PROPERTY(QVariantList otherIcons READ otherIcons NOTIFY changed)
    // more than one screen is connected
    Q_PROPERTY(bool severalDisplays READ severalDisplays NOTIFY changed)

public:
    // Runs a script in the shell and hands back what it printed; `ok` is
    // false when the shell could not be reached.
    using Shell = std::function<void(const QString &script, const std::function<void(bool ok, const QString &output)> &done)>;
    // The name and icon of a tray item.
    using Describe = std::function<QPair<QString, QString>(const QString &id)>;

    explicit TaskbarSettings(QObject *parent = nullptr);
    // For the tests: a shell of their own, no D-Bus.
    TaskbarSettings(Shell shell, Describe describe, QObject *parent = nullptr);

    bool ready() const { return m_ready; }
    bool available() const;
    QString search() const;
    void setSearch(const QString &mode);
    bool taskView() const;
    void setTaskView(bool on);
    QString alignment() const;
    void setAlignment(const QString &mode);
    bool alignmentFixed() const;
    bool autoHide() const;
    void setAutoHide(bool on);
    bool showDesktopCorner() const;
    void setShowDesktopCorner(bool on);
    QString combine() const;
    void setCombine(const QString &mode);
    bool allDisplays() const;
    void setAllDisplays(bool on);
    QString multiDisplayButtons() const;
    void setMultiDisplayButtons(const QString &mode);
    bool badges() const;
    void setBadges(bool on);
    bool flashing() const;
    void setFlashing(bool on);
    bool rayeStart() const { return mainHas(TaskbarLogic::rayeStart); }
    bool rayeTasks() const { return mainHas(TaskbarLogic::rayeTasks); }
    bool rayeClock() const { return mainHas(TaskbarLogic::rayeClock); }
    QVariantList systemIcons() const;
    QVariantList otherIcons() const;
    bool severalDisplays() const { return m_screens > 1; }

    Q_INVOKABLE void setSystemIcon(const QString &id, bool on);
    Q_INVOKABLE void setOtherIcon(const QString &id, bool on);
    // read the panels again
    Q_INVOKABLE void reload();

    // For the tests: the apps with a tray icon now, the program run after
    // a multi-display change ("" for none).
    void setRunningApps(const QMap<QString, QPair<QString, QString>> &apps);
    void setSyncProgram(const QString &program) { m_syncProgram = program; }
    // For the tests: the page's own backend, given a shell of theirs.
    void useShell(Shell shell, Describe describe);
    const TaskbarLogic::State &state() const { return m_state; }

Q_SIGNALS:
    void changed();

private:
    using Change = std::function<QJsonArray(const TaskbarLogic::State &)>;
    void queue(Change change);
    void next();
    void take(bool ok, const QString &output, bool changing);
    void writeTaskbarrc(const QString &key, const QVariant &value);
    bool mainHas(const QString &plugin) const;
    QString shellValue(const char *key, const QString &fallback) const;
    bool shellBool(const char *key, bool fallback) const;
    void writeShell(const char *key, const QVariant &value);
    // runs raye-taskbar-sync `runs` times in a row, then reads the panels again
    void runSync(int runs = 1);
    void forgetTaskViewCopies();
    void readRunningApps();
    void countScreens();

    Shell m_shell;
    Describe m_describe;
    KSharedConfigPtr m_taskbarrc;
    KSharedConfigPtr m_shellrc;
    KConfigWatcher::Ptr m_shellWatcher;
    TaskbarLogic::State m_state;
    QList<Change> m_queue;
    // id -> name, icon of the apps with a tray icon now
    QMap<QString, QPair<QString, QString>> m_apps;
    QString m_syncProgram;
    bool m_ready = false;
    bool m_reached = false;
    bool m_running = false;
    int m_screens = 1;
};
