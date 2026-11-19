// SPDX-License-Identifier: GPL-3.0-or-later
// The Taskbar page's logic on plain values: what a panel reads as, and
// the scripts that change it (taskbar.h).
#include "taskbar.h"

#include <QJsonDocument>

#include <algorithm>

using namespace TaskbarLogic;

namespace TaskbarLogic
{

namespace {

const Widget *first(const Panel &panel, const QString &plugin)
{
    for (const Widget &w : panel.widgets) {
        if (w.plugin == plugin)
            return &w;
    }
    return nullptr;
}

QList<const Widget *> all(const Panel &panel, const QString &plugin)
{
    QList<const Widget *> list;
    for (const Widget &w : panel.widgets) {
        if (w.plugin == plugin)
            list.append(&w);
    }
    return list;
}

// The taskbars the user owns. The ones raye-taskbar-sync made for the
// other screens are left to it: it copies the main taskbar's settings
// to them, and builds them again when their applets are not its own.
QList<Panel> ownPanels(const State &state)
{
    QList<Panel> list;
    for (const Panel &p : state.panels) {
        if (!p.secondary)
            list.append(p);
    }
    return list;
}

bool isTaskView(const Widget &w)
{
    return w.plugin == icon && w.root.value(QStringLiteral("url")).toString().endsWith(taskViewDesktop);
}

const Widget *tasks(const Panel &panel)
{
    for (const Widget &w : panel.widgets) {
        if (w.plugin == iconTasks || w.plugin == labelTasks)
            return &w;
    }
    return nullptr;
}

// the spacer that keeps Start away from the left edge
const Widget *leadingSpacer(const Panel &panel)
{
    // the widgets are left to right: a spacer first is before Start
    if (!panel.widgets.isEmpty() && panel.widgets.first().plugin == spacer)
        return &panel.widgets.first();
    return nullptr;
}

// where a new applet lands right after `w`: the panel inserts it next
// to the applet under the point, after it when the point is on its
// right half
QJsonObject addAfter(const Panel &panel, const Widget &w, const QString &plugin)
{
    return {{QStringLiteral("op"), QStringLiteral("add")},
            {QStringLiteral("panel"), panel.id},
            {QStringLiteral("plugin"), plugin},
            {QStringLiteral("x"), w.x + std::max(1.0, w.width - 2)},
            {QStringLiteral("y"), w.y + w.height / 2}};
}

QJsonObject removeOp(const Widget &w)
{
    return {{QStringLiteral("op"), QStringLiteral("remove")}, {QStringLiteral("widget"), w.id}};
}

QJsonObject configOp(const Widget &w, const QJsonObject &values, const QStringList &group = {QStringLiteral("General")})
{
    return {{QStringLiteral("op"), QStringLiteral("config")},
            {QStringLiteral("widget"), w.id},
            {QStringLiteral("group"), QJsonArray::fromStringList(group)},
            {QStringLiteral("values"), values}};
}

QJsonArray trayListOps(const State &state, const std::function<bool(QStringList &, QStringList &, QStringList &)> &edit)
{
    QJsonArray ops;
    for (const Panel &panel : ownPanels(state)) {
        for (const Widget *w : all(panel, tray)) {
            QStringList extra = toList(w->general.value(QStringLiteral("extraItems")));
            QStringList shown = toList(w->general.value(QStringLiteral("shownItems")));
            QStringList hidden = toList(w->general.value(QStringLiteral("hiddenItems")));
            const QStringList before[] = {extra, shown, hidden};
            if (!edit(extra, shown, hidden))
                continue;
            QJsonObject values;
            if (extra != before[0])
                values.insert(QStringLiteral("extraItems"), QJsonArray::fromStringList(extra));
            if (shown != before[1])
                values.insert(QStringLiteral("shownItems"), QJsonArray::fromStringList(shown));
            if (hidden != before[2])
                values.insert(QStringLiteral("hiddenItems"), QJsonArray::fromStringList(hidden));
            if (!values.isEmpty())
                ops.append(configOp(*w, values));
        }
    }
    return ops;
}

} // namespace

bool toBool(const QVariant &value, bool fallback)
{
    if (!value.isValid() || value.isNull())
        return fallback;
    if (value.typeId() == QMetaType::Bool)
        return value.toBool();
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("1"))
        return true;
    if (text == QLatin1String("false") || text == QLatin1String("0"))
        return false;
    return fallback;
}

QStringList toList(const QVariant &value)
{
    QStringList list;
    if (value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList)
        list = value.toStringList();
    else
        list = value.toString().split(QLatin1Char(','));
    for (QString &s : list)
        s = s.trimmed();
    list.removeAll(QString());
    return list;
}

State parseState(const QByteArray &json)
{
    State state;
    const QJsonArray panels = QJsonDocument::fromJson(json).object().value(QStringLiteral("panels")).toArray();
    for (const QJsonValue &pv : panels) {
        const QJsonObject p = pv.toObject();
        Panel panel;
        panel.id = p.value(QStringLiteral("id")).toInt(-1);
        panel.screen = p.value(QStringLiteral("screen")).toInt(0);
        panel.hiding = p.value(QStringLiteral("hiding")).toString();
        panel.location = p.value(QStringLiteral("location")).toString();
        panel.secondary = p.value(QStringLiteral("role")).toString() == QLatin1String("secondary");
        for (const QJsonValue &wv : p.value(QStringLiteral("widgets")).toArray()) {
            const QJsonObject o = wv.toObject();
            Widget w;
            w.id = o.value(QStringLiteral("id")).toInt(-1);
            w.plugin = o.value(QStringLiteral("plugin")).toString();
            w.x = o.value(QStringLiteral("x")).toDouble();
            w.y = o.value(QStringLiteral("y")).toDouble();
            w.width = o.value(QStringLiteral("width")).toDouble();
            w.height = o.value(QStringLiteral("height")).toDouble();
            w.general = o.value(QStringLiteral("general")).toObject().toVariantMap();
            w.root = o.value(QStringLiteral("root")).toObject().toVariantMap();
            panel.widgets.append(w);
        }
        std::stable_sort(panel.widgets.begin(), panel.widgets.end(), [](const Widget &a, const Widget &b) {
            return a.x < b.x;
        });
        // the panel's own order wins over where the applets seem to be
        QList<int> order;
        for (const QString &id : p.value(QStringLiteral("order")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts))
            order.append(id.toInt());
        if (!order.isEmpty()) {
            const auto rank = [&order](const Widget &w) {
                const qsizetype i = order.indexOf(w.id);
                return i < 0 ? order.size() : i;
            };
            std::stable_sort(panel.widgets.begin(), panel.widgets.end(), [&rank](const Widget &a, const Widget &b) {
                return rank(a) < rank(b);
            });
            for (qsizetype i = 1; i < panel.widgets.size(); ++i) {
                if (panel.widgets.at(i).x <= panel.widgets.at(i - 1).x)
                    panel.laidOut = false;
            }
        }
        state.panels.append(panel);
    }
    return state;
}

const Panel *mainPanel(const State &state)
{
    for (const Panel &p : state.panels) {
        if (!p.secondary && p.screen == 0 && p.location == QLatin1String("bottom"))
            return &p;
    }
    for (const Panel &p : state.panels) {
        if (!p.secondary && first(p, kickoff))
            return &p;
    }
    return state.panels.isEmpty() ? nullptr : &state.panels.first();
}

QString search(const Panel &panel)
{
    return first(panel, milou) ? QStringLiteral("icon") : QStringLiteral("hidden");
}

bool taskView(const Panel &panel)
{
    return std::any_of(panel.widgets.cbegin(), panel.widgets.cend(), isTaskView);
}

QString alignment(const Panel &panel)
{
    const Widget *lead = leadingSpacer(panel);
    return lead && toBool(lead->general.value(QStringLiteral("expanding")), true) ? QStringLiteral("center")
                                                                                   : QStringLiteral("left");
}

bool autoHide(const Panel &panel)
{
    return panel.hiding == QLatin1String("autohide");
}

bool showDesktopCorner(const Panel &panel)
{
    return first(panel, showDesktop) != nullptr;
}

QString combine(const Panel &panel)
{
    const Widget *t = tasks(panel);
    if (!t || t->plugin == iconTasks)
        return QStringLiteral("always");
    const int grouping = t->general.value(QStringLiteral("groupingStrategy"), 1).toInt();
    return grouping == 0 ? QStringLiteral("never") : QStringLiteral("full");
}

QStringList requiredTrayItems()
{
    // without it no notification shows at all
    return {QStringLiteral("org.kde.plasma.notifications")};
}

QList<TrayItem> systemIcons(const Panel &panel)
{
    QList<TrayItem> items;
    const Widget *t = first(panel, tray);
    if (!t)
        return items;
    const QStringList extra = toList(t->general.value(QStringLiteral("extraItems")));
    QStringList ids = toList(t->general.value(QStringLiteral("knownItems")));
    for (const QString &id : extra) {
        if (!ids.contains(id))
            ids.append(id);
    }
    for (const QString &id : std::as_const(ids)) {
        if (!requiredTrayItems().contains(id))
            items.append({id, extra.contains(id)});
    }
    return items;
}

QList<TrayItem> otherIcons(const Panel &panel, const QStringList &running)
{
    QList<TrayItem> items;
    const Widget *t = first(panel, tray);
    if (!t)
        return items;
    QSet<QString> applets;
    for (const char *key : {"knownItems", "extraItems"}) {
        const QStringList ids = toList(t->general.value(QString::fromLatin1(key)));
        applets.unite(QSet<QString>(ids.cbegin(), ids.cend()));
    }
    const QStringList shown = toList(t->general.value(QStringLiteral("shownItems")));
    QStringList ids = running;
    ids += shown;
    ids += toList(t->general.value(QStringLiteral("hiddenItems")));
    ids.removeDuplicates();
    for (const QString &id : std::as_const(ids)) {
        if (!applets.contains(id))
            items.append({id, shown.contains(id)});
    }
    return items;
}

QJsonArray searchOps(const State &state, const QString &mode)
{
    QJsonArray ops;
    for (const Panel &panel : ownPanels(state)) {
        const Widget *start = first(panel, kickoff);
        if (!start)
            continue;
        const auto found = all(panel, milou);
        if (mode == QLatin1String("icon") && !panel.laidOut)
            continue;
        if (mode == QLatin1String("hidden")) {
            for (const Widget *w : found)
                ops.append(removeOp(*w));
        } else if (mode == QLatin1String("icon") && found.isEmpty()) {
            ops.append(addAfter(panel, *start, milou));
        }
    }
    return ops;
}

QJsonArray taskViewOps(const State &state, bool on, const QString &desktopUrl)
{
    QJsonArray ops;
    for (const Panel &panel : ownPanels(state)) {
        const Widget *start = first(panel, kickoff);
        if (!start)
            continue;
        QList<const Widget *> found;
        for (const Widget &w : panel.widgets) {
            if (isTaskView(w))
                found.append(&w);
        }
        if (!on) {
            for (const Widget *w : found)
                ops.append(removeOp(*w));
        } else if (found.isEmpty() && panel.laidOut) {
            // after search when there is one, as Windows orders them
            const Widget *s = first(panel, milou);
            QJsonObject add = addAfter(panel, s ? *s : *start, icon);
            add.insert(QStringLiteral("args"), QJsonArray{desktopUrl});
            ops.append(add);
        }
    }
    return ops;
}

QJsonArray alignmentOps(const State &state, const QString &mode)
{
    QJsonArray ops;
    const bool centre = mode == QLatin1String("center");
    if (!centre && mode != QLatin1String("left"))
        return ops;
    for (const Panel &panel : ownPanels(state)) {
        if (!first(panel, kickoff) || alignment(panel) == mode)
            continue;
        const Widget *lead = leadingSpacer(panel);
        if (lead) {
            // a spacer of no width: Start sits at the left edge
            ops.append(configOp(*lead, {{QStringLiteral("expanding"), centre}, {QStringLiteral("length"), centre ? -1 : 0}}));
        } else if (centre && !panel.widgets.isEmpty() && panel.laidOut) {
            const Widget &front = panel.widgets.first();
            ops.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("add")},
                                   {QStringLiteral("panel"), panel.id},
                                   {QStringLiteral("plugin"), spacer},
                                   {QStringLiteral("x"), front.x + 1},
                                   {QStringLiteral("y"), front.y + front.height / 2},
                                   {QStringLiteral("values"), QJsonObject{{QStringLiteral("expanding"), true}}}});
        }
    }
    return ops;
}

QJsonArray autoHideOps(const State &state, bool on)
{
    QJsonArray ops;
    for (const Panel &panel : ownPanels(state)) {
        if (autoHide(panel) == on)
            continue;
        ops.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("hiding")},
                               {QStringLiteral("panel"), panel.id},
                               {QStringLiteral("value"), on ? QStringLiteral("autohide") : QStringLiteral("none")}});
    }
    return ops;
}

QJsonArray showDesktopCornerOps(const State &state, bool on)
{
    QJsonArray ops;
    for (const Panel &panel : ownPanels(state)) {
        const auto found = all(panel, showDesktop);
        if (!on) {
            for (const Widget *w : found)
                ops.append(removeOp(*w));
        } else if (found.isEmpty()) {
            // no place given: at the end, in the corner
            ops.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("add")},
                                   {QStringLiteral("panel"), panel.id},
                                   {QStringLiteral("plugin"), showDesktop}});
        }
    }
    return ops;
}

QJsonArray combineOps(const State &state, const QString &mode)
{
    QJsonArray ops;
    QString plugin;
    QJsonObject values;
    // settings only the task manager with labels reads
    QJsonArray drop;
    if (mode == QLatin1String("always")) {
        plugin = iconTasks;
        values = {{QStringLiteral("groupingStrategy"), 1}};
        drop = {QStringLiteral("onlyGroupWhenFull"), QStringLiteral("separateLaunchers"), QStringLiteral("fill"),
                QStringLiteral("taskMaxWidth")};
    } else if (mode == QLatin1String("full") || mode == QLatin1String("never")) {
        plugin = labelTasks;
        // pinned apps and their windows share one button; the buttons hug
        // their icon and label (narrow, as on Windows) and do not fill the
        // bar, so it stays centred
        values = {{QStringLiteral("groupingStrategy"), mode == QLatin1String("full") ? 1 : 0},
                  {QStringLiteral("onlyGroupWhenFull"), true},
                  {QStringLiteral("separateLaunchers"), false},
                  {QStringLiteral("fill"), false},
                  {QStringLiteral("taskMaxWidth"), 0}};
    } else {
        return ops;
    }
    for (const Panel &panel : ownPanels(state)) {
        const Widget *t = tasks(panel);
        if (!t)
            continue;
        if (t->plugin != plugin) {
            if (!panel.laidOut)
                continue;
            ops.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("replace")},
                                   {QStringLiteral("panel"), panel.id},
                                   {QStringLiteral("widget"), t->id},
                                   {QStringLiteral("plugin"), plugin},
                                   {QStringLiteral("values"), values},
                                   {QStringLiteral("drop"), drop}});
            continue;
        }
        // what the task manager uses when a key is not written
        static const QVariantMap defaults = {{QStringLiteral("groupingStrategy"), 1},
                                             {QStringLiteral("onlyGroupWhenFull"), true},
                                             {QStringLiteral("separateLaunchers"), true},
                                             {QStringLiteral("fill"), true},
                                             {QStringLiteral("taskMaxWidth"), 1}};
        QJsonObject differ;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const QVariant now = t->general.value(it.key(), defaults.value(it.key()));
            const bool same = it.value().isBool() ? toBool(now, !it.value().toBool()) == it.value().toBool()
                                                  : now.toString() == QString::number(it.value().toInt());
            if (!same)
                differ.insert(it.key(), it.value());
        }
        if (!differ.isEmpty())
            ops.append(configOp(*t, differ));
    }
    return ops;
}

QJsonArray systemIconOps(const State &state, const QString &id, bool on)
{
    if (requiredTrayItems().contains(id))
        return {};
    return trayListOps(state, [&](QStringList &extra, QStringList &, QStringList &) {
        if (on == extra.contains(id))
            return false;
        if (on)
            extra.append(id);
        else
            extra.removeAll(id);
        return true;
    });
}

QJsonArray otherIconOps(const State &state, const QString &id, bool on)
{
    return trayListOps(state, [&](QStringList &, QStringList &shown, QStringList &hidden) {
        // on: always on the taskbar; off: under the arrow with the others
        QStringList &add = on ? shown : hidden;
        QStringList &drop = on ? hidden : shown;
        if (add.contains(id) && !drop.contains(id))
            return false;
        drop.removeAll(id);
        if (!add.contains(id))
            add.append(id);
        return true;
    });
}

QString readScript()
{
    return QStringLiteral(R"js(
function rayeState() {
    var out = { panels: [] };
    var ps = panels();
    for (var i = 0; i < ps.length; ++i) {
        var p = ps[i];
        p.currentConfigGroup = ["Raye"];
        var role = p.readConfig("Role", "");
        p.currentConfigGroup = [];
        p.currentConfigGroup = ["General"];
        var order = p.readConfig("AppletOrder", "");
        p.currentConfigGroup = [];
        var panel = { id: p.id, screen: p.screen, hiding: p.hiding, location: p.location, role: String(role),
                      order: String(order), widgets: [] };
        var ws = p.widgets();
        for (var j = 0; j < ws.length; ++j) {
            var w = ws[j];
            var g = w.geometry;
            var item = { id: w.id, plugin: w.type, x: g.x, y: g.y, width: g.width, height: g.height, general: {}, root: {} };
            w.currentConfigGroup = ["General"];
            var keys = w.configKeys;
            for (var k = 0; k < keys.length; ++k)
                item.general[keys[k]] = w.readConfig(keys[k]);
            w.currentConfigGroup = [];
            keys = w.configKeys;
            for (var k = 0; k < keys.length; ++k)
                item.root[keys[k]] = w.readConfig(keys[k]);
            panel.widgets.push(item);
        }
        out.panels.push(panel);
    }
    return JSON.stringify(out);
}
print(rayeState());
)js");
}

QString applyScript(const QJsonArray &ops)
{
    return QStringLiteral(R"js(
var ops = %1;
function rayeWidget(id) {
    var ps = panels();
    for (var i = 0; i < ps.length; ++i) {
        var ws = ps[i].widgets();
        for (var j = 0; j < ws.length; ++j)
            if (ws[j].id == id)
                return ws[j];
    }
    return null;
}
function rayePanel(id) {
    var ps = panels();
    for (var i = 0; i < ps.length; ++i)
        if (ps[i].id == id)
            return ps[i];
    return null;
}
function rayeWrite(w, group, values) {
    if (!w || !values)
        return;
    w.currentConfigGroup = group;
    for (var key in values)
        w.writeConfig(key, values[key]);
    w.reloadConfig();
}
for (var i = 0; i < ops.length; ++i) {
    var op = ops[i];
    if (op.op == "config") {
        rayeWrite(rayeWidget(op.widget), op.group, op.values);
    } else if (op.op == "remove") {
        var gone = rayeWidget(op.widget);
        if (gone)
            gone.remove();
    } else if (op.op == "add") {
        var p = rayePanel(op.panel);
        if (!p)
            continue;
        var added = op.x === undefined ? p.addWidget(op.plugin)
                                       : p.addWidget(op.plugin, op.x, op.y, -1, -1, op.args || []);
        rayeWrite(added, ["General"], op.values);
    } else if (op.op == "replace") {
        var old = rayeWidget(op.widget);
        var host = rayePanel(op.panel);
        if (!old || !host)
            continue;
        var g = old.geometry;
        old.currentConfigGroup = ["General"];
        var keys = old.configKeys;
        var values = {};
        for (var k = 0; k < keys.length; ++k)
            values[keys[k]] = old.readConfig(keys[k]);
        for (var key in op.values)
            values[key] = op.values[key];
        for (var d = 0; op.drop && d < op.drop.length; ++d)
            delete values[op.drop[d]];
        var made = host.addWidget(op.plugin, g.x + 1, g.y + g.height / 2);
        rayeWrite(made, ["General"], values);
        old.remove();
    } else if (op.op == "hiding") {
        var hp = rayePanel(op.panel);
        if (hp)
            hp.hiding = op.value;
    }
}
)js")
               .arg(QString::fromUtf8(QJsonDocument(ops).toJson(QJsonDocument::Compact)))
        + readScript();
}

QString multiDisplayButtons(const QString &value)
{
    static const QStringList known = {QStringLiteral("all"), QStringLiteral("main-and-window"), QStringLiteral("window-only")};
    return known.contains(value) ? value : QStringLiteral("all");
}

QString multiDisplayApps(const QString &buttons)
{
    const QString b = multiDisplayButtons(buttons);
    if (b == QLatin1String("main-and-window"))
        return QStringLiteral("main-and-open");
    if (b == QLatin1String("window-only"))
        return QStringLiteral("open");
    return QStringLiteral("all");
}

bool has(const Panel &panel, const QString &plugin)
{
    return first(panel, plugin) != nullptr;
}

static QString oneOf(const QString &value, const QStringList &known)
{
    return known.contains(value) ? value : known.first();
}

QString shellSearch(const QString &value)
{
    return oneOf(value, {QStringLiteral("box"), QStringLiteral("iconlabel"), QStringLiteral("icon"), QStringLiteral("hidden")});
}

QString shellAlignment(const QString &value)
{
    return oneOf(value, {QStringLiteral("center"), QStringLiteral("left")});
}

QString shellCombine(const QString &value)
{
    return oneOf(value, {QStringLiteral("always"), QStringLiteral("full"), QStringLiteral("never")});
}

// the shell also reads its older spelling "whenfull"
QString shellSmallButtons(const QString &value)
{
    if (value == QLatin1String("whenfull"))
        return QStringLiteral("full");
    return oneOf(value, {QStringLiteral("never"), QStringLiteral("always"), QStringLiteral("full")});
}

} // namespace TaskbarLogic
