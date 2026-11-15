// Plasma's scripting interface, as much of it as the Taskbar page uses,
// for test_taskbar: applets sit 40 px wide in a row, and one added at a
// point goes before or after the applet under it, as the panel does.
// `store` holds the panels (tests/taskbar/desktop.json).
var printed = [];
function print(text) { printed.push(String(text)); }
// new applets get ids no applet has, as in Plasma
var nextId = 1000;
store.panels.forEach(function(p) { p.widgets.forEach(function(w) { nextId = Math.max(nextId, w.id + 1); }); });
function layout(p) {
    for (var i = 0; i < p.widgets.length; ++i) {
        var w = p.widgets[i];
        w.x = i * 44; w.y = 0; w.width = 40; w.height = 48;
    }
}
store.panels.forEach(layout);
function wrapWidget(p, w) {
    return {
        id: w.id, type: w.plugin, currentConfigGroup: [],
        get geometry() { return { x: w.x, y: w.y, width: w.width, height: w.height }; },
        group: function() { return this.currentConfigGroup.length ? w.general : w.root; },
        get configKeys() { return Object.keys(this.group()); },
        readConfig: function(key, fallback) { var g = this.group(); return key in g ? g[key] : fallback; },
        writeConfig: function(key, value) { this.group()[key] = value; },
        reloadConfig: function() {},
        remove: function() { p.widgets.splice(p.widgets.indexOf(w), 1); layout(p); }
    };
}
function wrapPanel(p) {
    return {
        id: p.id, screen: p.screen, location: p.location, currentConfigGroup: [],
        readConfig: function(key, fallback) {
            if (this.currentConfigGroup[0] == "Raye" && key == "Role" && p.role)
                return p.role;
            if (this.currentConfigGroup[0] == "General" && key == "AppletOrder")
                return p.widgets.map(function(w) { return w.id; }).join(";");
            return fallback;
        },
        get hiding() { return p.hiding; },
        set hiding(value) { p.hiding = value; },
        widgets: function() { return p.widgets.map(function(w) { return wrapWidget(p, w); }); },
        addWidget: function(plugin, x, y, width, height, args) {
            var w = { id: nextId++, plugin: plugin, general: {}, root: {} };
            if (args && args.length)
                w.root.url = args[0];
            var at = p.widgets.length;
            if (x !== undefined && x >= 0) {
                for (var i = 0; i < p.widgets.length; ++i) {
                    var o = p.widgets[i];
                    if (x >= o.x && x < o.x + o.width) { at = x > o.x + o.width / 2 ? i + 1 : i; break; }
                }
            }
            p.widgets.splice(at, 0, w);
            layout(p);
            return wrapWidget(p, w);
        }
    };
}
function panels() { return store.panels.map(wrapPanel); }
