// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskbar.h"
#include "shelllook.h"

#include <KConfigGroup>
#include <KConfigWatcher>
#include <KPluginMetaData>
#include <QCollator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>

using namespace TaskbarLogic;

// ---- TaskbarSettings --------------------------------------------------

namespace {

const QString taskbarGroup = QStringLiteral("Taskbar");

void runInShell(const QString &script, const std::function<void(bool, const QString &)> &done)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                       QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    call << script;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 15000));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            qWarning() << "taskbar: the shell did not run the script:" << reply.error().message();
            done(false, QString());
            return;
        }
        done(true, reply.value());
    });
}

// the name and the icon a tray applet gives itself, in the user's language
QPair<QString, QString> describeApplet(const QString &id)
{
    KPluginMetaData data = KPluginMetaData::findPluginById(QStringLiteral("plasma/applets"), id);
    if (!data.isValid()) {
        const QString json = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                    QStringLiteral("plasma/plasmoids/%1/metadata.json").arg(id));
        if (!json.isEmpty())
            data = KPluginMetaData::fromJsonFile(json);
    }
    if (!data.isValid() || data.name().isEmpty())
        return {id, QString()};
    return {data.name(), data.iconName()};
}

QVariantList toVariant(const QList<TrayItem> &items, const std::function<QPair<QString, QString>(const QString &)> &describe)
{
    struct Row {
        QString id, name, icon;
        bool on;
    };
    QList<Row> rows;
    for (const TrayItem &item : items) {
        const auto [name, icon] = describe(item.id);
        rows.append({item.id, name.isEmpty() ? item.id : name, icon, item.on});
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    QVariantList list;
    for (const Row &r : std::as_const(rows))
        list.append(QVariantMap{{QStringLiteral("id"), r.id},
                                {QStringLiteral("name"), r.name},
                                {QStringLiteral("icon"), r.icon},
                                {QStringLiteral("on"), r.on}});
    return list;
}

} // namespace

TaskbarSettings::TaskbarSettings(QObject *parent)
    : TaskbarSettings(runInShell, describeApplet, parent)
{
    m_syncProgram = QStringLiteral("/usr/libexec/raye-taskbar-sync");
    countScreens();
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::screenAdded, this, &TaskbarSettings::countScreens);
        connect(app, &QGuiApplication::screenRemoved, this, &TaskbarSettings::countScreens);
    }
    readRunningApps();
}

TaskbarSettings::TaskbarSettings(Shell shell, Describe describe, QObject *parent)
    : QObject(parent)
    , m_shell(std::move(shell))
    , m_describe(std::move(describe))
    , m_taskbarrc(KSharedConfig::openConfig(QStringLiteral("raye-taskbarrc")))
    , m_shellrc(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
{
    // the taskbar's own menus and other programs write rayeshellrc too
    m_shellWatcher = KConfigWatcher::create(m_shellrc);
    connect(m_shellWatcher.data(), &KConfigWatcher::configChanged, this, [this] {
        Q_EMIT changed();
    });
    reload();
}

void TaskbarSettings::useShell(Shell shell, Describe describe)
{
    m_shell = std::move(shell);
    m_describe = std::move(describe);
    reload();
}

void TaskbarSettings::countScreens()
{
    const int count = int(QGuiApplication::screens().size());
    if (count != m_screens) {
        m_screens = count;
        Q_EMIT changed();
    }
}

bool TaskbarSettings::available() const
{
    return m_reached && mainPanel(m_state) != nullptr;
}

bool TaskbarSettings::mainHas(const QString &plugin) const
{
    const Panel *p = mainPanel(m_state);
    return p && TaskbarLogic::has(*p, plugin);
}

QString TaskbarSettings::shellValue(const char *key, const QString &fallback) const
{
    return m_shellrc->group(taskbarGroup).readEntry(key, fallback);
}

bool TaskbarSettings::shellBool(const char *key, bool fallback) const
{
    return m_shellrc->group(taskbarGroup).readEntry(key, fallback);
}

// rayeshellrc is followed live by the Raye applets (KConfig::Notify)
void TaskbarSettings::writeShell(const char *key, const QVariant &value)
{
    KConfigGroup g = m_shellrc->group(taskbarGroup);
    g.writeEntry(key, value, KConfig::Notify);
    m_shellrc->sync();
    Q_EMIT changed();
}

QString TaskbarSettings::search() const
{
    if (rayeStart())
        return shellSearch(shellValue("Search", QStringLiteral("box")));
    const Panel *p = mainPanel(m_state);
    return p ? TaskbarLogic::search(*p) : QStringLiteral("hidden");
}

void TaskbarSettings::setSearch(const QString &mode)
{
    if (rayeStart()) {
        if (shellSearch(mode) == mode && mode != search())
            writeShell("Search", mode);
        return;
    }
    queue([mode](const State &s) { return searchOps(s, mode); });
}

bool TaskbarSettings::taskView() const
{
    if (rayeStart())
        return shellBool("TaskView", true);
    const Panel *p = mainPanel(m_state);
    return p && TaskbarLogic::taskView(*p);
}

void TaskbarSettings::setTaskView(bool on)
{
    if (rayeStart()) {
        if (on != taskView())
            writeShell("TaskView", on);
        return;
    }
    QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, taskViewDesktop);
    if (path.isEmpty())
        path = QStringLiteral("/usr/share/applications/") + taskViewDesktop;
    const QString url = QUrl::fromLocalFile(path).toString();
    queue([on, url](const State &s) { return taskViewOps(s, on, url); });
}

bool TaskbarSettings::alignmentFixed() const
{
    return rayeTasks() && ShellLook::isWindows10(m_shellrc);
}

QString TaskbarSettings::alignment() const
{
    if (alignmentFixed())
        return QStringLiteral("left");
    if (rayeTasks())
        return shellAlignment(shellValue("Alignment", QStringLiteral("center")));
    const Panel *p = mainPanel(m_state);
    return p ? TaskbarLogic::alignment(*p) : QStringLiteral("center");
}

void TaskbarSettings::setAlignment(const QString &mode)
{
    if (alignmentFixed())
        return;
    if (rayeTasks()) {
        if (shellAlignment(mode) == mode && mode != alignment())
            writeShell("Alignment", mode);
        return;
    }
    queue([mode](const State &s) { return alignmentOps(s, mode); });
}

bool TaskbarSettings::autoHide() const
{
    const Panel *p = mainPanel(m_state);
    return p && TaskbarLogic::autoHide(*p);
}

void TaskbarSettings::setAutoHide(bool on)
{
    queue([on](const State &s) { return autoHideOps(s, on); });
}

bool TaskbarSettings::showDesktopCorner() const
{
    if (rayeClock())
        return shellBool("ShowDesktopCorner", true);
    const Panel *p = mainPanel(m_state);
    return p && TaskbarLogic::showDesktopCorner(*p);
}

void TaskbarSettings::setShowDesktopCorner(bool on)
{
    if (rayeClock()) {
        if (on != showDesktopCorner())
            writeShell("ShowDesktopCorner", on);
        return;
    }
    queue([on](const State &s) { return showDesktopCornerOps(s, on); });
}

QString TaskbarSettings::combine() const
{
    if (rayeTasks())
        return shellCombine(shellValue("Combine", QStringLiteral("always")));
    const Panel *p = mainPanel(m_state);
    return p ? TaskbarLogic::combine(*p) : QStringLiteral("always");
}

void TaskbarSettings::setCombine(const QString &mode)
{
    if (rayeTasks()) {
        if (shellCombine(mode) == mode && mode != combine())
            writeShell("Combine", mode);
        return;
    }
    queue([mode](const State &s) { return combineOps(s, mode); });
}

bool TaskbarSettings::allDisplays() const
{
    return m_taskbarrc->group(taskbarGroup).readEntry("AllDisplays", true);
}

void TaskbarSettings::setAllDisplays(bool on)
{
    if (on == allDisplays())
        return;
    writeTaskbarrc(QStringLiteral("AllDisplays"), on);
    // a taskbar made for another screen gets its tray and the main one's
    // size only on a second run
    runSync(on ? 2 : 1);
}

QString TaskbarSettings::multiDisplayButtons() const
{
    return TaskbarLogic::multiDisplayButtons(m_taskbarrc->group(taskbarGroup).readEntry("MultiDisplayButtons", QString()));
}

void TaskbarSettings::setMultiDisplayButtons(const QString &mode)
{
    if (TaskbarLogic::multiDisplayButtons(mode) != mode || mode == multiDisplayButtons())
        return;
    writeTaskbarrc(QStringLiteral("MultiDisplayButtons"), mode);
    // the Raye taskbar reads the same choice from rayeshellrc
    writeShell("MultiDisplayApps", TaskbarLogic::multiDisplayApps(mode));
    runSync();
}

bool TaskbarSettings::badges() const
{
    return shellBool("Badges", true);
}

void TaskbarSettings::setBadges(bool on)
{
    if (on != badges())
        writeShell("Badges", on);
}

bool TaskbarSettings::flashing() const
{
    return shellBool("Flashing", true);
}

void TaskbarSettings::setFlashing(bool on)
{
    if (on != flashing())
        writeShell("Flashing", on);
}

QVariantList TaskbarSettings::systemIcons() const
{
    const Panel *p = mainPanel(m_state);
    return p ? toVariant(TaskbarLogic::systemIcons(*p), m_describe) : QVariantList();
}

QVariantList TaskbarSettings::otherIcons() const
{
    const Panel *p = mainPanel(m_state);
    if (!p)
        return {};
    const auto apps = m_apps;
    return toVariant(TaskbarLogic::otherIcons(*p, m_apps.keys()), [apps](const QString &id) {
        return apps.value(id, {id, QString()});
    });
}

void TaskbarSettings::setSystemIcon(const QString &id, bool on)
{
    queue([id, on](const State &s) { return systemIconOps(s, id, on); });
}

void TaskbarSettings::setOtherIcon(const QString &id, bool on)
{
    queue([id, on](const State &s) { return otherIconOps(s, id, on); });
}

void TaskbarSettings::setRunningApps(const QMap<QString, QPair<QString, QString>> &apps)
{
    m_apps = apps;
    Q_EMIT changed();
}

void TaskbarSettings::reload()
{
    m_taskbarrc->reparseConfiguration();
    m_shellrc->reparseConfiguration();
    // an empty change: nothing to do, only the panels read again
    m_queue.append(Change());
    next();
}

void TaskbarSettings::queue(Change change)
{
    m_queue.append(std::move(change));
    next();
}

void TaskbarSettings::next()
{
    if (m_running || m_queue.isEmpty())
        return;
    const Change change = m_queue.takeFirst();
    QString script;
    if (!change) {
        script = readScript();
    } else {
        // worked out on the panels as they are now, after the changes before
        const QJsonArray ops = change(m_state);
        if (ops.isEmpty()) {
            Q_EMIT changed();
            next();
            return;
        }
        script = applyScript(ops);
    }
    m_running = true;
    QPointer<TaskbarSettings> self(this);
    const bool changing = bool(change);
    m_shell(script, [self, changing](bool ok, const QString &output) {
        if (self)
            self->take(ok, output, changing);
    });
}

void TaskbarSettings::take(bool ok, const QString &output, bool changing)
{
    m_running = false;
    m_ready = true;
    m_reached = ok;
    if (ok) {
        // the script prints the panels last
        const QString text = output.trimmed();
        const qsizetype start = text.lastIndexOf(QLatin1String("{\"panels\""));
        m_state = parseState(start >= 0 ? text.mid(start).toUtf8() : QByteArray());
    }
    Q_EMIT changed();
    // the taskbars of the other screens follow the main one
    if (ok && changing) {
        forgetTaskViewCopies();
        runSync();
    }
    next();
}

void TaskbarSettings::forgetTaskViewCopies()
{
    // the icon applet copies the entry it starts to plasma_icons, and
    // leaves the copy behind when it is removed. A new button may not
    // have written where its copy is yet: clean up only once no task
    // view button is left.
    for (const Panel &p : std::as_const(m_state.panels)) {
        if (TaskbarLogic::taskView(p))
            return;
    }
    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasma_icons"));
    const QStringList copies = dir.entryList({QStringLiteral("org.raye.taskview*.desktop")}, QDir::Files);
    for (const QString &name : copies)
        QFile::remove(dir.filePath(name));
}

void TaskbarSettings::writeTaskbarrc(const QString &key, const QVariant &value)
{
    KConfigGroup g = m_taskbarrc->group(taskbarGroup);
    g.writeEntry(key, value, KConfig::Notify);
    m_taskbarrc->sync();
    Q_EMIT changed();
}

void TaskbarSettings::runSync(int runs)
{
    if (m_syncProgram.isEmpty() || runs < 1)
        return;
    const QFileInfo program(m_syncProgram);
    if (!program.isFile() || !program.isExecutable())
        return; // the program that places the taskbars is not there yet
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, runs] {
        process->deleteLater();
        if (runs > 1) {
            runSync(runs - 1);
            return;
        }
        // a taskbar may have come or gone
        reload();
    });
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(m_syncProgram, {});
}

void TaskbarSettings::readRunningApps()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.StatusNotifierWatcher"), QStringLiteral("/StatusNotifierWatcher"),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call << QStringLiteral("org.kde.StatusNotifierWatcher") << QStringLiteral("RegisteredStatusNotifierItems");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError())
            return;
        // "service/path", or a bare service with the usual path
        const QStringList items = reply.value().variant().toStringList();
        for (const QString &item : items) {
            const qsizetype slash = item.indexOf(QLatin1Char('/'));
            const QString service = slash < 0 ? item : item.left(slash);
            const QString path = slash < 0 ? QStringLiteral("/StatusNotifierItem") : item.mid(slash);
            QDBusMessage all = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                              QStringLiteral("GetAll"));
            all << QStringLiteral("org.kde.StatusNotifierItem");
            auto *one = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(all, 2000), this);
            connect(one, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *o) {
                o->deleteLater();
                const QDBusPendingReply<QVariantMap> props = *o;
                if (props.isError())
                    return;
                const QVariantMap p = props.value();
                const QString id = p.value(QStringLiteral("Id")).toString();
                if (id.isEmpty())
                    return;
                QString title = p.value(QStringLiteral("Title")).toString();
                m_apps.insert(id, {title.isEmpty() ? id : title, p.value(QStringLiteral("IconName")).toString()});
                Q_EMIT changed();
            });
        }
    });
}
