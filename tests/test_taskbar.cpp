// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Taskbar: what each setting reads from the panels and
// the changes it makes to them. The shell is played by a small copy of
// Plasma's scripting interface in a JavaScript engine, so the scripts
// the page sends are run for real: the panels below are the ones Raye
// builds (the main screen's with the tray, a second screen's without).
// raye-taskbarrc is written in a config folder of the test's own. Then
// the page is loaded as Settings shows it.
#include "taskbar.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using namespace TaskbarLogic;

namespace {

QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg
        && (qstrcmp(context.category, "qml") == 0 || qstrncmp(context.category, "qt.qml", 6) == 0
            || message.contains(QLatin1String("qrc:")))) {
        qmlWarnings << message;
    }
    fprintf(stderr, "%s\n", qPrintable(message));
}

// The panels of a fresh Raye desktop with two screens: the main
// screen's with the tray, a second screen's without.
QByteArray readFile(const char *name)
{
    QFile f(QStringLiteral(SOURCE_DIR "/tests/taskbar/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot read tests/taskbar/%s", name);
    return f.readAll();
}
const QByteArray desktop = readFile("desktop.json");

const QByteArray fakeShell = readFile("shell.js");

// Runs scripts on a copy of the panels kept between calls.
struct FakeShell {
    QByteArray store = desktop;
    QStringList scripts;
    bool reachable = true;

    void run(const QString &script, const std::function<void(bool, const QString &)> &done)
    {
        scripts << script;
        if (!reachable) {
            done(false, QString());
            return;
        }
        QJSEngine engine;
        engine.globalObject().setProperty(QStringLiteral("store"), engine.evaluate(QStringLiteral("(") + QString::fromUtf8(store) + QStringLiteral(")")));
        QJSValue result = engine.evaluate(QString::fromUtf8(fakeShell));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        result = engine.evaluate(script);
        QVERIFY2(!result.isError(), qPrintable(result.toString() + QLatin1Char('\n') + script));
        store = engine.evaluate(QStringLiteral("JSON.stringify(store)")).toString().toUtf8();
        done(true, engine.evaluate(QStringLiteral("printed.join('\\n')")).toString());
    }

    TaskbarSettings::Shell shell()
    {
        return [this](const QString &script, const std::function<void(bool, const QString &)> &done) {
            run(script, done);
        };
    }

    // the plugins of a panel, left to right
    QStringList plugins(int panel) const
    {
        QStringList list;
        for (const Panel &p : parseState(store).panels) {
            if (p.id != panel)
                continue;
            for (const Widget &w : p.widgets)
                list << w.plugin.section(QLatin1Char('.'), -1);
        }
        return list;
    }

    Panel panel(int id) const
    {
        for (const Panel &p : parseState(store).panels) {
            if (p.id == id)
                return p;
        }
        return {};
    }
};

QPair<QString, QString> describe(const QString &id)
{
    static const QMap<QString, QString> names = {{QStringLiteral("org.kde.plasma.volume"), QStringLiteral("Audio Volume")},
                                                 {QStringLiteral("org.kde.plasma.clipboard"), QStringLiteral("Clipboard")},
                                                 {QStringLiteral("org.kde.plasma.battery"), QStringLiteral("Power and Battery")}};
    return {names.value(id, id), QString()};
}

// a copy: the panels a FakeShell hands out are made on each call
std::optional<Widget> find(const Panel &panel, const QString &plugin)
{
    for (const Widget &w : panel.widgets) {
        if (w.plugin == plugin)
            return w;
    }
    return std::nullopt;
}

} // namespace

class TestTaskbar : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_home;
    QString m_data; // XDG_DATA_HOME

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_home = m_dir.filePath(QStringLiteral("config"));
        qputenv("XDG_CONFIG_HOME", m_home.toUtf8());
        m_data = m_dir.filePath(QStringLiteral("data"));
        qputenv("XDG_DATA_HOME", m_data.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(QStringLiteral("xdg")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void init()
    {
        QDir(m_home).removeRecursively();
        QDir().mkpath(m_home);
        KSharedConfig::openConfig(QStringLiteral("raye-taskbarrc"))->reparseConfiguration();
        KSharedConfig::openConfig(QStringLiteral("rayeshellrc"))->reparseConfiguration();
    }

    void values()
    {
        QCOMPARE(toBool(QStringLiteral("true"), false), true);
        QCOMPARE(toBool(QStringLiteral("0"), true), false);
        QCOMPARE(toBool(QVariant(), true), true);
        QCOMPARE(toBool(QStringLiteral("maybe"), false), false);
        QCOMPARE(toList(QStringLiteral("a, b,,c")), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(toList(QVariantList{QStringLiteral("a"), QStringLiteral("b")}), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(toList(QString()), QStringList());
        QCOMPARE(TaskbarLogic::multiDisplayButtons(QStringLiteral("window-only")), QStringLiteral("window-only"));
        QCOMPARE(TaskbarLogic::multiDisplayButtons(QStringLiteral("nonsense")), QStringLiteral("all"));
    }

    // what a fresh desktop reads as: Windows' defaults, minus search
    void readsTheDesktop()
    {
        const State state = parseState(desktop);
        QCOMPARE(state.panels.size(), 2);
        const Panel *main = mainPanel(state);
        QVERIFY(main);
        QCOMPARE(main->id, 3);
        QCOMPARE(TaskbarLogic::search(*main), QStringLiteral("hidden"));
        QVERIFY(!TaskbarLogic::taskView(*main));
        QCOMPARE(TaskbarLogic::alignment(*main), QStringLiteral("center"));
        QVERIFY(!TaskbarLogic::autoHide(*main));
        QVERIFY(TaskbarLogic::showDesktopCorner(*main));
        QCOMPARE(TaskbarLogic::combine(*main), QStringLiteral("always"));
    }

    // the main panel is the one on screen 0, wherever it is in the list
    void mainPanelIsOnTheMainScreen()
    {
        State state = parseState(desktop);
        std::swap(state.panels[0], state.panels[1]);
        QCOMPARE(mainPanel(state)->id, 3);
        // a taskbar made for another screen is never the main one
        state.panels[0].screen = 0;
        state.panels[1].screen = 1;
        QVERIFY(state.panels[0].secondary);
        QCOMPARE(mainPanel(state)->id, 3);
        state.panels[0].secondary = false;
        QCOMPARE(mainPanel(state)->id, 30);
        state.panels[0].screen = 2;
        state.panels[1].screen = 2;
        QCOMPARE(mainPanel(state)->id, 30); // no screen 0: the first with Start
        QVERIFY(!mainPanel(State()));
    }

    void trayLists()
    {
        const Panel main = parseState(desktop).panels.at(0);
        const QList<TrayItem> system = TaskbarLogic::systemIcons(main);
        QStringList ids;
        for (const TrayItem &i : system)
            ids << i.id + (i.on ? QStringLiteral("+") : QStringLiteral("-"));
        // notifications cannot be turned off: no row for it
        QCOMPARE(ids, (QStringList{QStringLiteral("org.kde.plasma.volume+"), QStringLiteral("org.kde.plasma.clipboard+"),
                                   QStringLiteral("org.kde.plasma.battery+"), QStringLiteral("org.kde.plasma.mediacontroller-")}));
        QVERIFY(systemIconOps(parseState(desktop), QStringLiteral("org.kde.plasma.notifications"), false).isEmpty());

        // the apps: running now, or remembered by the tray
        const QList<TrayItem> other = TaskbarLogic::otherIcons(main, {QStringLiteral("discord"), QStringLiteral("org.kde.plasma.volume")});
        ids.clear();
        for (const TrayItem &i : other)
            ids << i.id + (i.on ? QStringLiteral("+") : QStringLiteral("-"));
        QCOMPARE(ids, (QStringList{QStringLiteral("discord-"), QStringLiteral("chrome_status_icon_1-")}));
    }

    // a setting already as asked changes nothing
    void nothingToDo()
    {
        const State state = parseState(desktop);
        QVERIFY(searchOps(state, QStringLiteral("hidden")).isEmpty());
        QVERIFY(taskViewOps(state, false, QString()).isEmpty());
        QVERIFY(alignmentOps(state, QStringLiteral("center")).isEmpty());
        QVERIFY(alignmentOps(state, QStringLiteral("sideways")).isEmpty());
        QVERIFY(autoHideOps(state, false).isEmpty());
        QVERIFY(combineOps(state, QStringLiteral("always")).isEmpty());
        QVERIFY(combineOps(state, QStringLiteral("sometimes")).isEmpty());
        QVERIFY(systemIconOps(state, QStringLiteral("org.kde.plasma.volume"), true).isEmpty());
    }

    void search()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        QVERIFY(taskbar.available());
        taskbar.setSearch(QStringLiteral("icon"));
        QCOMPARE(taskbar.search(), QStringLiteral("icon"));
        // right after Start, on both taskbars
        QCOMPARE(shell.plugins(3).mid(0, 4), (QStringList{QStringLiteral("panelspacer"), QStringLiteral("kickoff"),
                                                           QStringLiteral("milou"), QStringLiteral("icontasks")}));
        // the second screen's taskbar is raye-taskbar-sync's: left as it is
        QVERIFY(!shell.plugins(30).contains(QStringLiteral("milou")));
        // asking again adds nothing
        taskbar.setSearch(QStringLiteral("icon"));
        QCOMPARE(shell.plugins(3).count(QStringLiteral("milou")), 1);
        taskbar.setSearch(QStringLiteral("hidden"));
        QCOMPARE(taskbar.search(), QStringLiteral("hidden"));
        QVERIFY(!shell.plugins(3).contains(QStringLiteral("milou")));
        QVERIFY(!shell.plugins(30).contains(QStringLiteral("milou")));
    }

    void taskView()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        taskbar.setSearch(QStringLiteral("icon"));
        taskbar.setTaskView(true);
        QVERIFY(taskbar.taskView());
        // Start, search, task view, the apps
        QCOMPARE(shell.plugins(3).mid(1, 4), (QStringList{QStringLiteral("kickoff"), QStringLiteral("milou"),
                                                           QStringLiteral("icon"), QStringLiteral("icontasks")}));
        const auto button = find(shell.panel(3), TaskbarLogic::icon);
        QVERIFY(button);
        QVERIFY(button->root.value(QStringLiteral("url")).toString().endsWith(QLatin1String("/org.raye.taskview.desktop")));
        QVERIFY(button->root.value(QStringLiteral("url")).toString().startsWith(QLatin1String("file:///")));
        // the copies the icon applet made of its entry go with the last
        // button, other icons stay
        const QString icons = m_data + QStringLiteral("/plasma_icons/");
        QDir().mkpath(icons);
        for (const char *name : {"org.raye.taskview.desktop", "org.raye.taskview (1).desktop", "org.kde.kate.desktop"})
            QFile(icons + QString::fromLatin1(name)).open(QIODevice::WriteOnly);
        taskbar.setAutoHide(true); // the button is still there: its copy too
        QVERIFY(QFile::exists(icons + QStringLiteral("org.raye.taskview.desktop")));
        taskbar.setAutoHide(false);
        taskbar.setTaskView(false);
        QVERIFY(!taskbar.taskView());
        QVERIFY(!QFile::exists(icons + QStringLiteral("org.raye.taskview.desktop")));
        QVERIFY(!QFile::exists(icons + QStringLiteral("org.raye.taskview (1).desktop")));
        QVERIFY(QFile::exists(icons + QStringLiteral("org.kde.kate.desktop")));
        QVERIFY(!shell.plugins(3).contains(QStringLiteral("icon")));
    }

    // an icon applet of the user's own is not the task view button
    void otherIconAppletsStay()
    {
        State state = parseState(desktop);
        Widget mine;
        mine.id = 99;
        mine.plugin = TaskbarLogic::icon;
        mine.root.insert(QStringLiteral("url"), QStringLiteral("file:///usr/share/applications/org.kde.kate.desktop"));
        state.panels[0].widgets.append(mine);
        QVERIFY(!TaskbarLogic::taskView(state.panels[0]));
        QVERIFY(taskViewOps(state, false, QString()).isEmpty());
    }

    void alignment()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        taskbar.setAlignment(QStringLiteral("left"));
        QCOMPARE(taskbar.alignment(), QStringLiteral("left"));
        const auto lead = std::optional<Widget>(shell.panel(3).widgets.first());
        QCOMPARE(lead->plugin, TaskbarLogic::spacer);
        QCOMPARE(toBool(lead->general.value(QStringLiteral("expanding")), true), false);
        QCOMPARE(lead->general.value(QStringLiteral("length")).toInt(), 0);
        QCOMPARE(TaskbarLogic::alignment(shell.panel(30)), QStringLiteral("center"));
        taskbar.setAlignment(QStringLiteral("center"));
        QCOMPARE(taskbar.alignment(), QStringLiteral("center"));
        QCOMPARE(shell.panel(3).widgets.first().general.value(QStringLiteral("length")).toInt(), -1);
    }

    // the panel's order wins over the places the applets seem to have;
    // when they disagree (a locked screen) nothing is added or replaced
    void orderAndPlaces()
    {
        QByteArray json = desktop;
        json.replace("\"id\": 3, \"screen\": 0,", "\"id\": 3, \"order\": \"6;4;5;7;8;20;21\", \"screen\": 0,");
        State state = parseState(json);
        QCOMPARE(state.panels[0].widgets.first().plugin, TaskbarLogic::iconTasks);
        QVERIFY(!state.panels[0].laidOut); // every applet at x 0
        QVERIFY(state.panels[1].laidOut); // no order known: as given
        QVERIFY(searchOps(state, QStringLiteral("icon")).isEmpty());
        QVERIFY(taskViewOps(state, true, QString()).isEmpty());
        QVERIFY(combineOps(state, QStringLiteral("never")).isEmpty());
        // what needs no place still happens
        QCOMPARE(autoHideOps(state, true).size(), 1);
        QCOMPARE(showDesktopCornerOps(state, false).size(), 1);

        for (int i = 0; i < state.panels[0].widgets.size(); ++i)
            state.panels[0].widgets[i].x = i * 50;
        state.panels[0].laidOut = true;
        QCOMPARE(searchOps(state, QStringLiteral("icon")).size(), 1);
    }

    // a taskbar with no spacer before Start gets one to be centred
    void centreAddsTheSpacer()
    {
        State state = parseState(desktop);
        state.panels.removeLast();
        state.panels[0].widgets.removeFirst();
        QCOMPARE(TaskbarLogic::alignment(state.panels[0]), QStringLiteral("left"));
        const QJsonArray ops = alignmentOps(state, QStringLiteral("center"));
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops.at(0).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("add"));
        QCOMPARE(ops.at(0).toObject().value(QStringLiteral("plugin")).toString(), TaskbarLogic::spacer);
        QVERIFY(alignmentOps(state, QStringLiteral("left")).isEmpty());
    }

    void autoHide()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        // after a change, the taskbars of the other screens are told
        const QString marker = m_dir.filePath(QStringLiteral("synced-autohide"));
        const QString program = m_dir.filePath(QStringLiteral("sync-autohide"));
        {
            QFile f(program);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\necho run >> " + marker.toUtf8() + "\n");
        }
        QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        taskbar.setSyncProgram(program);
        taskbar.setAutoHide(true);
        QTRY_VERIFY(QFile::exists(marker));
        QVERIFY(taskbar.autoHide());
        QCOMPARE(shell.panel(3).hiding, QStringLiteral("autohide"));
        QCOMPARE(shell.panel(30).hiding, QStringLiteral("none")); // copied by the sync program
        taskbar.setAutoHide(false);
        QCOMPARE(shell.panel(3).hiding, QStringLiteral("none"));
        QTRY_COMPARE(QFile(marker).size(), qint64(8));
        QTest::qWait(200);
    }

    void showDesktopCorner()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        taskbar.setShowDesktopCorner(false);
        QVERIFY(!taskbar.showDesktopCorner());
        QVERIFY(!shell.plugins(3).contains(QStringLiteral("showdesktop")));
        taskbar.setShowDesktopCorner(true);
        QVERIFY(taskbar.showDesktopCorner());
        // in the corner: last
        QCOMPARE(shell.plugins(3).last(), QStringLiteral("showdesktop"));
        QVERIFY(!shell.plugins(30).contains(QStringLiteral("showdesktop")));
    }

    void combine()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        taskbar.setCombine(QStringLiteral("never"));
        QCOMPARE(taskbar.combine(), QStringLiteral("never"));
        // the same place, the same pinned apps, with labels now
        QCOMPARE(shell.plugins(3).mid(1, 2), (QStringList{QStringLiteral("kickoff"), QStringLiteral("taskmanager")}));
        auto t = find(shell.panel(3), TaskbarLogic::labelTasks);
        QVERIFY(t);
        QCOMPARE(t->general.value(QStringLiteral("launchers")).toString(),
                 QStringLiteral("applications:org.kde.dolphin.desktop,preferred://browser"));
        QCOMPARE(t->general.value(QStringLiteral("groupingStrategy")).toInt(), 0);
        QCOMPARE(toBool(t->general.value(QStringLiteral("separateLaunchers")), true), false);
        QCOMPARE(toBool(t->general.value(QStringLiteral("fill")), true), false);
        QCOMPARE(t->general.value(QStringLiteral("taskMaxWidth")).toInt(), 0); // narrow: icon and label
        QVERIFY(!find(shell.panel(30), TaskbarLogic::labelTasks));

        // labels kept: only the grouping changes, the applet stays
        const int id = t->id;
        taskbar.setCombine(QStringLiteral("full"));
        QCOMPARE(taskbar.combine(), QStringLiteral("full"));
        t = find(shell.panel(3), TaskbarLogic::labelTasks);
        QCOMPARE(t->id, id);
        QCOMPARE(t->general.value(QStringLiteral("groupingStrategy")).toInt(), 1);
        QVERIFY(toBool(t->general.value(QStringLiteral("onlyGroupWhenFull")), false));

        taskbar.setCombine(QStringLiteral("always"));
        QCOMPARE(taskbar.combine(), QStringLiteral("always"));
        QCOMPARE(shell.plugins(3).at(2), QStringLiteral("icontasks"));
        QCOMPARE(find(shell.panel(3), TaskbarLogic::iconTasks)->general.value(QStringLiteral("launchers")).toString(),
                 QStringLiteral("applications:org.kde.dolphin.desktop,preferred://browser"));
        // nothing of the labelled task manager stays behind
        QVERIFY(!find(shell.panel(3), TaskbarLogic::iconTasks)->general.contains(QStringLiteral("fill")));
        QVERIFY(!find(shell.panel(3), TaskbarLogic::iconTasks)->general.contains(QStringLiteral("separateLaunchers")));
        QVERIFY(!find(shell.panel(3), TaskbarLogic::iconTasks)->general.contains(QStringLiteral("taskMaxWidth")));
    }

    void trayIcons()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        QMap<QString, QPair<QString, QString>> apps;
        apps.insert(QStringLiteral("discord"), {QStringLiteral("Discord"), QStringLiteral("discord")});
        taskbar.setRunningApps(apps);

        const QVariantList system = taskbar.systemIcons();
        QCOMPARE(system.size(), 4);
        // by name
        QCOMPARE(system.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Audio Volume"));
        taskbar.setSystemIcon(QStringLiteral("org.kde.plasma.clipboard"), false);
        QVERIFY(!toList(find(shell.panel(3), TaskbarLogic::tray)->general.value(QStringLiteral("extraItems")))
                     .contains(QStringLiteral("org.kde.plasma.clipboard")));
        taskbar.setSystemIcon(QStringLiteral("org.kde.plasma.mediacontroller"), true);
        QVERIFY(toList(find(shell.panel(3), TaskbarLogic::tray)->general.value(QStringLiteral("extraItems")))
                    .contains(QStringLiteral("org.kde.plasma.mediacontroller")));

        const QVariantList other = taskbar.otherIcons();
        QCOMPARE(other.size(), 2);
        QCOMPARE(other.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("chrome_status_icon_1"));
        QCOMPARE(other.last().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Discord"));
        taskbar.setOtherIcon(QStringLiteral("chrome_status_icon_1"), true);
        auto tray = find(shell.panel(3), TaskbarLogic::tray);
        QCOMPARE(toList(tray->general.value(QStringLiteral("shownItems"))), QStringList{QStringLiteral("chrome_status_icon_1")});
        QCOMPARE(toList(tray->general.value(QStringLiteral("hiddenItems"))), QStringList());
        taskbar.setOtherIcon(QStringLiteral("chrome_status_icon_1"), false);
        tray = find(shell.panel(3), TaskbarLogic::tray);
        QCOMPARE(toList(tray->general.value(QStringLiteral("shownItems"))), QStringList());
        QCOMPARE(toList(tray->general.value(QStringLiteral("hiddenItems"))), QStringList{QStringLiteral("chrome_status_icon_1")});
    }

    // the shell is gone: the page says so and changes nothing
    void shellUnreachable()
    {
        FakeShell shell;
        shell.reachable = false;
        TaskbarSettings taskbar(shell.shell(), describe);
        QVERIFY(taskbar.ready());
        QVERIFY(!taskbar.available());
        QCOMPARE(taskbar.search(), QStringLiteral("hidden"));
        QVERIFY(taskbar.systemIcons().isEmpty());
        const qsizetype sent = shell.scripts.size();
        taskbar.setAutoHide(true); // nothing known, nothing to change
        QCOMPARE(shell.scripts.size(), sent);
    }

    // the two multi-display choices go to raye-taskbarrc, then the program
    // that places the taskbars runs
    // Raye's own taskbar: the settings are rayeshellrc keys the applets
    // follow, never panel changes
    void rayeTaskbar()
    {
        FakeShell shell;
        shell.store = R"({"panels": [{"id": 3, "screen": 0, "location": "bottom", "hiding": "none", "widgets": [
            {"id": 5, "plugin": "org.raye.start", "general": {}, "root": {}},
            {"id": 6, "plugin": "org.raye.taskbar", "general": {"rowWidth": "176"}, "root": {}},
            {"id": 8, "plugin": "org.kde.plasma.systemtray", "general": {"extraItems": "org.kde.plasma.volume"}, "root": {}},
            {"id": 20, "plugin": "org.raye.clock", "general": {}, "root": {}}]}]})";
        TaskbarSettings taskbar(shell.shell(), describe);
        QVERIFY(taskbar.available());
        QVERIFY(taskbar.rayeStart() && taskbar.rayeTasks() && taskbar.rayeClock());
        // the defaults of rayeshellrc
        QCOMPARE(taskbar.search(), QStringLiteral("box"));
        QVERIFY(taskbar.taskView());
        QCOMPARE(taskbar.alignment(), QStringLiteral("center"));
        QCOMPARE(taskbar.combine(), QStringLiteral("always"));
        QVERIFY(taskbar.badges() && taskbar.flashing() && taskbar.showDesktopCorner());

        const int scripts = shell.scripts.size();
        const QByteArray before = shell.store;
        taskbar.setSearch(QStringLiteral("iconlabel"));
        taskbar.setTaskView(false);
        taskbar.setAlignment(QStringLiteral("left"));
        taskbar.setCombine(QStringLiteral("never"));
        taskbar.setBadges(false);
        taskbar.setFlashing(false);
        taskbar.setShowDesktopCorner(false);
        taskbar.setSearch(QStringLiteral("nonsense"));
        taskbar.setAlignment(QStringLiteral("right"));
        QCOMPARE(shell.scripts.size(), scripts);
        QCOMPARE(shell.store, before);

        const KConfigGroup g = KConfig(m_home + QStringLiteral("/rayeshellrc"), KConfig::SimpleConfig).group(QStringLiteral("Taskbar"));
        QCOMPARE(g.readEntry("Search"), QStringLiteral("iconlabel"));
        QCOMPARE(g.readEntry("TaskView"), QStringLiteral("false"));
        QCOMPARE(g.readEntry("Alignment"), QStringLiteral("left"));
        QCOMPARE(g.readEntry("Combine"), QStringLiteral("never"));
        QCOMPARE(g.readEntry("Badges"), QStringLiteral("false"));
        QCOMPARE(g.readEntry("Flashing"), QStringLiteral("false"));
        QCOMPARE(g.readEntry("ShowDesktopCorner"), QStringLiteral("false"));
        QCOMPARE(taskbar.search(), QStringLiteral("iconlabel"));
        QCOMPARE(taskbar.alignment(), QStringLiteral("left"));
        QVERIFY(!taskbar.badges());

        // auto-hide stays a panel setting
        taskbar.setAutoHide(true);
        QTRY_VERIFY(taskbar.autoHide());

        // one choice for both: raye-taskbar-sync and the Raye taskbar
        taskbar.setSyncProgram(QString());
        taskbar.setMultiDisplayButtons(QStringLiteral("window-only"));
        QCOMPARE(KConfig(m_home + QStringLiteral("/rayeshellrc"), KConfig::SimpleConfig).group(QStringLiteral("Taskbar")).readEntry("MultiDisplayApps"),
                 QStringLiteral("open"));
        QCOMPARE(TaskbarLogic::multiDisplayApps(QStringLiteral("main-and-window")), QStringLiteral("main-and-open"));
        QCOMPARE(TaskbarLogic::multiDisplayApps(QStringLiteral("all")), QStringLiteral("all"));
        QCOMPARE(TaskbarLogic::multiDisplayApps(QStringLiteral("x")), QStringLiteral("all"));
    }

    void smallButtonsAndRecentSearches()
    {
        FakeShell shell;
        shell.store = R"({"panels": [{"id": 3, "screen": 0, "location": "bottom", "hiding": "none", "widgets": [
            {"id": 5, "plugin": "org.raye.start", "general": {}, "root": {}},
            {"id": 6, "plugin": "org.raye.taskbar", "general": {}, "root": {}},
            {"id": 20, "plugin": "org.raye.clock", "general": {}, "root": {}}]}]})";
        TaskbarSettings taskbar(shell.shell(), describe);
        QCOMPARE(taskbar.smallButtons(), QStringLiteral("never"));
        QVERIFY(taskbar.recentSearches());

        taskbar.setSmallButtons(QStringLiteral("full"));
        taskbar.setSmallButtons(QStringLiteral("tiny")); // refused
        taskbar.setRecentSearches(false);
        const KConfigGroup g = KConfig(m_home + QStringLiteral("/rayeshellrc"), KConfig::SimpleConfig).group(QStringLiteral("Taskbar"));
        QCOMPARE(g.readEntry("SmallButtons"), QStringLiteral("full"));
        QCOMPARE(g.readEntry("RecentSearches"), QStringLiteral("false"));
        QCOMPARE(taskbar.smallButtons(), QStringLiteral("full"));
        QVERIFY(!taskbar.recentSearches());

        // the shell's own spelling of the old value
        KConfig(m_home + QStringLiteral("/rayeshellrc"), KConfig::SimpleConfig).group(QStringLiteral("Taskbar")).writeEntry("SmallButtons", "whenfull");
        taskbar.reload();
        QCOMPARE(taskbar.smallButtons(), QStringLiteral("full"));
    }

    // the stock taskbar keeps its panel changes and leaves rayeshellrc alone
    void stockTaskbarLeavesShellrc()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        QVERIFY(!taskbar.rayeStart() && !taskbar.rayeTasks() && !taskbar.rayeClock());
        taskbar.setSearch(QStringLiteral("box"));
        taskbar.setAlignment(QStringLiteral("left"));
        QTRY_COMPARE(taskbar.alignment(), QStringLiteral("left"));
        QVERIFY(!QFile::exists(m_home + QStringLiteral("/rayeshellrc")));
    }

    void multipleDisplays()
    {
        FakeShell shell;
        TaskbarSettings taskbar(shell.shell(), describe);
        QVERIFY(taskbar.allDisplays());
        QCOMPARE(taskbar.multiDisplayButtons(), QStringLiteral("all"));

        const QString marker = m_dir.filePath(QStringLiteral("synced"));
        const QString program = m_dir.filePath(QStringLiteral("raye-taskbar-sync"));
        {
            QFile f(program);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\necho run >> " + marker.toUtf8() + "\n");
        }
        QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        taskbar.setSyncProgram(program);

        taskbar.setAllDisplays(false);
        QVERIFY(!taskbar.allDisplays());
        const QString file = m_home + QStringLiteral("/raye-taskbarrc");
        QCOMPARE(KConfig(file, KConfig::SimpleConfig).group(QStringLiteral("Taskbar")).readEntry("AllDisplays", true), false);
        QTRY_VERIFY(QFile::exists(marker));

        taskbar.setMultiDisplayButtons(QStringLiteral("main-and-window"));
        QCOMPARE(taskbar.multiDisplayButtons(), QStringLiteral("main-and-window"));
        QCOMPARE(KConfig(file, KConfig::SimpleConfig).group(QStringLiteral("Taskbar")).readEntry("MultiDisplayButtons"),
                 QStringLiteral("main-and-window"));
        taskbar.setMultiDisplayButtons(QStringLiteral("nonsense"));
        QCOMPARE(taskbar.multiDisplayButtons(), QStringLiteral("main-and-window"));

        // no program installed: the setting is still written, nothing fails
        taskbar.setSyncProgram(m_dir.filePath(QStringLiteral("missing")));
        taskbar.setAllDisplays(true);
        QVERIFY(taskbar.allDisplays());
        // the program has finished and the panels were read again
        QTRY_COMPARE(QFile(marker).size(), qint64(8));
        QTest::qWait(200);

        // a taskbar coming back on another screen takes two runs
        taskbar.setSyncProgram(program);
        taskbar.setAllDisplays(false);
        QTRY_COMPARE(QFile(marker).size(), qint64(12));
        taskbar.setAllDisplays(true);
        QTRY_COMPARE(QFile(marker).size(), qint64(20));
        QTest::qWait(200);
    }

    // every script the page sends is valid for the shell
    void scriptsParse()
    {
        QJSEngine engine;
        const QJSValue read = engine.evaluate(QStringLiteral("(function() {") + readScript() + QStringLiteral("})"));
        QVERIFY2(!read.isError(), qPrintable(read.toString()));
        const QJSValue apply = engine.evaluate(QStringLiteral("(function() {")
                                               + applyScript(combineOps(parseState(desktop), QStringLiteral("never")))
                                               + QStringLiteral("})"));
        QVERIFY2(!apply.isError(), qPrintable(apply.toString()));
    }

    void page()
    {
        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/taskbar.json"));
        QVERIFY(fragment.open(QIODevice::ReadOnly));
        QStringList indexed;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
            QVERIFY(id.startsWith(QLatin1String("personalization/taskbar#")));
            indexed << id.section(QLatin1Char('#'), 1);
        }
        QVERIFY(!indexed.isEmpty());

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            QQuickView view(&engine, nullptr);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1120, 760);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/personalization/TaskbarPage.qml")));
            QCOMPARE(view.status(), QQuickView::Ready);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(300);

            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            const QStringList drawn = ids.toStringList();
            for (const QString &id : std::as_const(indexed))
                QVERIFY2(drawn.contains(id), qPrintable(id + QStringLiteral(" is not on the page")));
            for (const QString &id : drawn)
                QVERIFY2(indexed.contains(id), qPrintable(id + QStringLiteral(" is not in data/search")));
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }

    // the button the task view applet starts is installed with Settings
    void taskViewEntry()
    {
        QFile f(QStringLiteral(SOURCE_DIR "/data/org.raye.taskview.desktop"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains(QLatin1String("invokeShortcut string:Overview")));
        QVERIFY(text.contains(QLatin1String("NoDisplay=true")));
        QVERIFY(text.contains(QLatin1String("Name[fr]=")));
    }
};

QTEST_MAIN(TestTaskbar)
#include "test_taskbar.moc"
