// SPDX-License-Identifier: GPL-3.0-or-later
// The pages of the portal. Every value from the PC goes in as text,
// never as markup.
"use strict";

const T = JSON.parse(document.getElementById("strings").textContent);
const CSRF = document.querySelector('meta[name="csrf"]').content;
const LANG = document.body.dataset.lang;
const content = document.getElementById("content");
let timer = null;

function el(tag, attrs, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs || {})) {
        if (key === "class") node.className = value;
        else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
        else if (value !== false && value !== null && value !== undefined) node.setAttribute(key, value === true ? "" : value);
    }
    for (const child of children.flat()) {
        if (child === null || child === undefined || child === false) continue;
        node.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return node;
}

function say(key, values) {
    return (T[key] || key).replace(/\{(\w+)\}/g, (_, k) => (values && k in values ? values[k] : ""));
}

async function api(path, options) {
    const opts = Object.assign({ credentials: "same-origin", headers: {} }, options || {});
    if (opts.method === "POST") opts.headers["X-CSRF-Token"] = CSRF;
    const reply = await fetch(path, opts);
    if (reply.status === 401) {
        location.href = "/login?expired=1";
        throw new Error("signed out");
    }
    return reply;
}

async function getJson(path) {
    const reply = await api(path);
    if (!reply.ok) throw new Error(String(reply.status));
    return reply.json();
}

function postJson(path, value) {
    return api(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(value) });
}

function number(n, digits) {
    return n.toLocaleString(LANG, { minimumFractionDigits: digits || 0, maximumFractionDigits: digits || 0 });
}

function percent(n) { return `${number(n, 1)} %`; }

function bytes(n) {
    const units = LANG === "fr" ? ["o", "Ko", "Mo", "Go", "To"] : ["B", "KB", "MB", "GB", "TB"];
    let i = 0;
    while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
    return `${number(n, i > 0 && n < 10 ? 1 : 0)} ${units[i]}`;
}

function rate(n) { return `${bytes(n)}/s`; }

function duration(seconds) {
    const d = Math.floor(seconds / 86400), h = Math.floor(seconds % 86400 / 3600), m = Math.floor(seconds % 3600 / 60);
    return [d && say("days", { n: d }), (d || h) && say("hours", { n: h }), say("minutes", { n: m })].filter(Boolean).join(" ");
}

function when(seconds) {
    return new Date(seconds * 1000).toLocaleString(LANG, { dateStyle: "short", timeStyle: "medium" });
}

function notice(textValue, kind) {
    const box = el("div", { class: `notice ${kind || ""}`, role: kind === "bad" ? "alert" : "status" }, textValue);
    content.prepend(box);
    setTimeout(() => box.remove(), 6000);
}

function card(label, value, sub, percent) {
    return el("div", { class: "card" },
        el("div", { class: "label" }, label),
        el("div", { class: "value" }, value),
        sub ? el("div", { class: "sub" }, sub) : null,
        percent !== undefined ? meter(percent) : null);
}

// widths go through the style object: the page's policy refuses style attributes
function meter(percent) {
    const fill = el("i");
    fill.style.width = `${Math.min(100, Math.max(0, percent))}%`;
    return el("div", { class: "bar" }, fill);
}

// A question with an optional password field; resolves to the
// password ("" when none was asked), or null when cancelled.
function ask(message, withPassword) {
    return new Promise((resolve) => {
        const field = withPassword ? el("input", { type: "password", autocomplete: "current-password", "aria-label": T.sudo_password }) : null;
        const dialog = el("dialog", {},
            el("p", {}, message),
            withPassword ? el("label", { class: "stack" }, T.sudo_password, field) : null,
            el("div", { class: "buttons" },
                el("button", { class: "accent", onclick: () => { dialog.close(); resolve(field ? field.value : ""); } }, T.confirm),
                el("button", { onclick: () => { dialog.close(); resolve(null); } }, T.cancel)));
        dialog.addEventListener("cancel", () => resolve(null));
        dialog.addEventListener("close", () => dialog.remove());
        document.body.append(dialog);
        dialog.showModal();
        if (field) field.focus();
    });
}

// ---- pages ---------------------------------------------------------------

const pages = {};

pages.overview = async () => {
    const draw = async () => {
        const s = await getJson("/api/overview");
        const used = s.memory.total ? 100 * s.memory.used / s.memory.total : 0;
        const tiles = [
            card(T.device_name, s.host),
            card(T.version, s.version),
            card(T.uptime, duration(s.uptime)),
            card(T.cpu, `${Math.round(s.cpu)} %`, say("cores", { n: s.cores }), s.cpu),
            card(T.memory, `${bytes(s.memory.used)} / ${bytes(s.memory.total)}`, null, used),
            s.temperature !== null ? card(T.temperature, `${number(s.temperature, 1)} °C`) : null,
            card(T.network, `↓ ${rate(s.network.down)}`, `↑ ${rate(s.network.up)}`),
            ...s.disks.map((d) => card(`${T.disk} ${d.mount}`, `${bytes(d.used)} / ${bytes(d.total)}`, d.device, 100 * d.used / d.total)),
        ];
        content.replaceChildren(el("div", { class: "grid" }, tiles));
    };
    await draw();
    timer = setInterval(draw, 2000);
};

pages.performance = async () => {
    const series = { cpu: [], memory: [], down: [], up: [] };
    const size = 60;
    const canvases = {};
    const values = {};
    const block = (key, label) => {
        canvases[key] = el("canvas", { width: 900, height: 140, role: "img", "aria-label": label });
        values[key] = el("div", { class: "value" });
        return el("div", { class: "card chart" }, el("div", { class: "label" }, label), values[key], canvases[key]);
    };
    content.replaceChildren(el("div", { class: "cards" },
        block("cpu", T.cpu), block("memory", T.memory), block("down", T.received), block("up", T.sent)));
    const plot = (key, max) => {
        const canvas = canvases[key];
        const ctx = canvas.getContext("2d");
        const w = canvas.width, h = canvas.height, data = series[key];
        ctx.clearRect(0, 0, w, h);
        ctx.strokeStyle = "#ffffff12";
        for (let i = 1; i < 4; i++) { ctx.beginPath(); ctx.moveTo(0, h * i / 4); ctx.lineTo(w, h * i / 4); ctx.stroke(); }
        if (data.length < 2) return;
        const top = max || Math.max(1, ...data) * 1.2;
        const x = (i) => w - (data.length - 1 - i) * (w / (size - 1));
        const y = (v) => h - 2 - (h - 4) * Math.min(1, v / top);
        const gradient = ctx.createLinearGradient(0, 0, 0, h);
        gradient.addColorStop(0, "#7b3cff66");
        gradient.addColorStop(1, "#7b3cff05");
        ctx.beginPath();
        ctx.moveTo(x(0), h);
        data.forEach((v, i) => ctx.lineTo(x(i), y(v)));
        ctx.lineTo(x(data.length - 1), h);
        ctx.fillStyle = gradient;
        ctx.fill();
        ctx.beginPath();
        data.forEach((v, i) => (i ? ctx.lineTo(x(i), y(v)) : ctx.moveTo(x(i), y(v))));
        ctx.strokeStyle = "#9b6bff";
        ctx.lineWidth = 2;
        ctx.stroke();
    };
    const tick = async () => {
        const s = await getJson("/api/stats");
        const memory = s.memory.total ? 100 * s.memory.used / s.memory.total : 0;
        for (const [key, value] of [["cpu", s.cpu], ["memory", memory], ["down", s.network.down], ["up", s.network.up]]) {
            series[key].push(value);
            if (series[key].length > size) series[key].shift();
        }
        values.cpu.textContent = `${Math.round(s.cpu)} %` + (s.temperature !== null ? `  ·  ${number(s.temperature, 1)} °C` : "");
        values.memory.textContent = `${bytes(s.memory.used)} / ${bytes(s.memory.total)}`;
        values.down.textContent = rate(s.network.down);
        values.up.textContent = rate(s.network.up);
        plot("cpu", 100); plot("memory", 100); plot("down"); plot("up");
    };
    await tick();
    timer = setInterval(tick, 1000);
};

pages.processes = async () => {
    const filter = el("input", { type: "search", placeholder: T.filter, "aria-label": T.filter });
    const body = el("tbody");
    content.replaceChildren(
        el("div", { class: "toolbar" }, filter, el("span", { class: "grow" })),
        el("table", {}, el("thead", {}, el("tr", {},
            el("th", {}, T.name), el("th", { class: "num" }, T.pid), el("th", {}, T.owner),
            el("th", { class: "num" }, T.cpu), el("th", { class: "num" }, T.memory), el("th", {}))), body));
    let last = { processes: [], me: -1 };
    const draw = () => {
        const needle = filter.value.trim().toLowerCase();
        const rows = last.processes.filter((p) => !needle || p.name.toLowerCase().includes(needle)
            || p.command.toLowerCase().includes(needle) || String(p.pid) === needle).slice(0, 300);
        body.replaceChildren(...rows.map((p) => el("tr", {},
            el("td", { title: p.command }, p.name),
            el("td", { class: "num" }, p.pid),
            el("td", {}, p.user === null ? T.other_account : p.user),
            el("td", { class: "num" }, percent(p.cpu)),
            el("td", { class: "num" }, bytes(p.memory)),
            el("td", {}, p.endable
                ? el("button", { class: "danger", "data-pid": p.pid, onclick: () => end(p) }, T.end_task) : ""))));
    };
    const end = async (p) => {
        const mine = p.uid === last.me;
        const password = await ask(mine ? say("end_task_ask", { name: p.name }) : `${say("end_task_ask", { name: p.name })} ${T.end_task_root}`, !mine);
        if (password === null) return;
        const reply = await postJson("/api/kill", { pid: p.pid, password });
        const result = (await reply.json()).result;
        if (result === "ok") notice(say("ended", { name: p.name }), "good");
        else notice(T[["denied", "locked", "unavailable"].includes(result) ? result : "failed"], "bad");
        await refresh();
    };
    const refresh = async () => { last = await getJson("/api/processes"); draw(); };
    filter.addEventListener("input", draw);
    await refresh();
    timer = setInterval(refresh, 3000);
};

pages.apps = async () => {
    const list = await getJson("/api/apps");
    const filter = el("input", { type: "search", placeholder: T.filter, "aria-label": T.filter });
    const body = el("tbody");
    const draw = () => {
        const needle = filter.value.trim().toLowerCase();
        body.replaceChildren(...list.filter((a) => !needle || a.name.toLowerCase().includes(needle))
            .map((a) => el("tr", {}, el("td", {}, a.name), el("td", { class: "muted" }, a.comment),
                el("td", {}, a.kind === "flatpak" ? T.flatpak_app : T.system_app))));
    };
    filter.addEventListener("input", draw);
    content.replaceChildren(
        el("div", { class: "toolbar" }, filter, el("span", { class: "muted" }, say("installed", { count: list.length }))),
        el("table", {}, el("thead", {}, el("tr", {}, el("th", {}, T.name), el("th", {}, T.description), el("th", {}, T.kind))), body));
    draw();
};

pages.files = async () => {
    let path = new URLSearchParams(location.search).get("path") || "";
    const hidden = el("input", { type: "checkbox", id: "hidden" });
    const where = el("span", { class: "muted grow" });
    const picker = el("input", { type: "file", hidden: true });
    const body = el("tbody");
    const up = el("button", { onclick: () => go(path.split("/").slice(0, -1).join("/")) }, T.up_folder);
    content.replaceChildren(
        el("div", { class: "toolbar" }, up, where,
            el("label", { class: "check" }, hidden, T.show_hidden),
            el("button", { class: "accent", onclick: () => picker.click() }, T.send_file), picker),
        el("table", {}, el("thead", {}, el("tr", {},
            el("th", {}, T.name), el("th", { class: "num" }, T.size), el("th", {}, T.modified), el("th", {}))), body));
    const go = async (next) => {
        const reply = await api(`/api/files?path=${encodeURIComponent(next)}`);
        if (!reply.ok) { notice(T.failed, "bad"); return; }
        const data = await reply.json();
        path = data.path;
        history.replaceState(null, "", path ? `?path=${encodeURIComponent(path)}` : location.pathname);
        where.textContent = `${T.home}${path ? " / " + path.split("/").join(" / ") : ""}`;
        up.disabled = !path;
        body.replaceChildren(...data.entries.filter((e) => hidden.checked || !e.hidden).map((e) => {
            const full = path ? `${path}/${e.name}` : e.name;
            return el("tr", {},
                el("td", {}, e.kind === "folder"
                    ? el("button", { class: "link folder", onclick: () => go(full) }, e.name)
                    : e.name),
                el("td", { class: "num" }, e.kind === "file" ? bytes(e.size) : ""),
                el("td", { class: "muted" }, when(e.modified)),
                el("td", {}, e.kind === "file"
                    ? el("a", { href: `/api/download?path=${encodeURIComponent(full)}`, download: e.name }, T.download) : ""));
        }));
    };
    hidden.addEventListener("change", () => go(path));
    picker.addEventListener("change", async () => {
        for (const file of picker.files) {
            const reply = await api(`/api/upload?path=${encodeURIComponent(path)}&name=${encodeURIComponent(file.name)}`,
                { method: "POST", headers: { "Content-Type": "application/octet-stream" }, body: file });
            if (reply.ok) notice(say("sent_file", { name: (await reply.json()).name }), "good");
            else notice(T.failed, "bad");
        }
        picker.value = "";
        go(path);
    });
    await go(path);
};

pages.logs = async () => {
    const level = el("select", { "aria-label": T.level },
        el("option", { value: "" }, T.all_levels), el("option", { value: "4" }, T.warnings), el("option", { value: "3" }, T.errors));
    const unit = el("input", { placeholder: T.unit, "aria-label": T.unit, size: 18 });
    const search = el("input", { type: "search", placeholder: T.search, "aria-label": T.search });
    const body = el("tbody");
    const load = async () => {
        const q = new URLSearchParams({ priority: level.value, unit: unit.value, q: search.value, lines: "300" });
        const rows = await getJson(`/api/logs?${q}`);
        body.replaceChildren(...(rows.length ? rows.map((r) => el("tr", { class: `p${r.priority}` },
            el("td", { class: "num" }, when(r.time)), el("td", {}, r.source), el("td", { class: "message" }, r.message)))
            : [el("tr", {}, el("td", { colspan: 3, class: "muted" }, T.no_entries))]));
    };
    content.replaceChildren(
        el("div", { class: "toolbar" }, level, unit, search, el("button", { onclick: load }, T.refresh)),
        el("table", {}, el("thead", {}, el("tr", {}, el("th", {}, T.time), el("th", {}, T.kind), el("th", {}, T.message))), body));
    level.addEventListener("change", load);
    search.addEventListener("keydown", (e) => { if (e.key === "Enter") load(); });
    unit.addEventListener("keydown", (e) => { if (e.key === "Enter") load(); });
    await load();
};

pages.network = async () => {
    const draw = async () => {
        const n = await getJson("/api/network");
        content.replaceChildren(
            el("div", { class: "grid" }, card(T.received, rate(n.down)), card(T.sent, rate(n.up))),
            el("h2", {}, T.addresses),
            el("table", {}, el("thead", {}, el("tr", {}, el("th", {}, T.interface), el("th", {}, T.state),
                el("th", {}, T.addresses), el("th", {}, T.mac))),
                el("tbody", {}, n.interfaces.map((i) => el("tr", {}, el("td", {}, i.name), el("td", {}, i.state),
                    el("td", {}, i.addresses.join(", ")), el("td", { class: "muted" }, i.mac))))));
    };
    await draw();
    timer = setInterval(draw, 3000);
};

pages.screen = async () => {
    const state = await getJson("/api/screen");
    const picture = el("img", { class: "shot", alt: "", hidden: true });
    const save = el("a", { hidden: true, download: "screen.png" }, el("button", {}, T.save_picture));
    const off = el("div", { class: "notice", hidden: state.allowed }, T.screen_off);
    const take = el("button", { class: "accent", disabled: !state.allowed, onclick: async () => {
        take.disabled = true;
        let allowed = true;
        try {
            const reply = await api("/api/screenshot", { method: "POST" });
            if (reply.status === 403) { allowed = false; off.hidden = false; return; }
            if (!reply.ok) { notice(T.failed, "bad"); return; }
            const url = URL.createObjectURL(await reply.blob());
            picture.src = url; picture.hidden = false;
            save.href = url; save.hidden = false;
        } finally { take.disabled = !allowed; }
    } }, T.take_screenshot);
    content.replaceChildren(el("p", { class: "hint" }, T.screen_note), off, el("div", { class: "toolbar" }, take, save), picture);
};

pages.power = async () => {
    const act = async (action, question, done) => {
        const password = await ask(question, true);
        if (password === null) return;
        const reply = await postJson("/api/power", { action, password });
        const result = (await reply.json()).result;
        if (result === "ok") content.replaceChildren(el("div", { class: "notice good" }, done));
        else notice(T[["denied", "locked", "unavailable"].includes(result) ? result : "failed"], "bad");
    };
    content.replaceChildren(
        el("p", { class: "hint" }, T.power_hint),
        el("div", { class: "toolbar" },
            el("button", { onclick: () => act("reboot", T.restart_ask, T.restarting) }, T.restart),
            el("button", { class: "danger", onclick: () => act("poweroff", T.shut_down_ask, T.shutting_down) }, T.shut_down)));
};

(pages[document.body.dataset.page] || pages.overview)().catch((error) => {
    if (error.message !== "signed out") notice(T.error, "bad");
});
