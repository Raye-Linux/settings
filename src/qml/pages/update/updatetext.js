// SPDX-License-Identifier: GPL-3.0-or-later
// What Raye Update is doing, as the page shows it, and the update
// history sorted the way the history page lists it. The words are in
// the QML files; this only decides which ones.
.pragma library

// One of: "paused", "armed", "restart", "installing", "install",
// "downloading", "checking", "waiting", "error", "available", "uptodate",
// "incomplete", "never". "install": downloaded, and none of it needs a
// restart; "installing": those install now, while the PC is in use.
// "incomplete": nothing to install, in what could be read. It looks exactly
// like up to date, and is not (update/docs/state.md).
// `u` is UpdateStatus (or an object with the same keys), `now` in ms.
function pageState(u, now) {
    if (!u)
        return "never"
    if (u.pausedUntil * 1000 > now)
        return "paused"
    const phase = u.phase || u.state
    if (phase === "armed")
        return "armed"
    if (u.installingNow)
        return "installing"
    // updates that install without a restart come first: "Restart now"
    // only once they are in, or a restart would cut them off
    if (phase === "ready" && (u.pending || []).some(p => !needsRestart(p)))
        // they install on their own right after the download: no "Install
        // now" in between, unless they wait (battery, metered network, game)
        return u.waitReason ? "install" : "installing"
    if (u.restartNeeded)
        return "restart"
    if (phase === "downloading" || phase === "checking" || phase === "waiting" || phase === "error")
        return phase
    if (waitingCount(u) > 0)
        return "available"
    if (!(u.checked > 0))
        return "never"
    return u.checkedEverything === false ? "incomplete" : "uptodate"
}

// One pending update installs only when the PC restarts. Written by
// raye-update since 0.3; before that every update did.
function needsRestart(p) {
    return !p || p.restart !== false
}

// The device already runs the offered firmware or a newer one (fwupd can
// still list a dbx it just wrote).
function firmwareCurrent(have, offered) {
    if (have === offered)
        return true
    const a = String(have || "").match(/\d+/g), b = String(offered || "").match(/\d+/g)
    if (!a || !b || String(have).replace(/\d+/g, "") !== String(offered).replace(/\d+/g, ""))
        return false
    for (let i = 0; i < Math.max(a.length, b.length); ++i) {
        const x = i < a.length ? parseInt(a[i], 10) : -1, y = i < b.length ? parseInt(b[i], 10) : -1
        if (x !== y)
            return x > y
    }
    return true
}

// The list itself when there is one (version 2), the count otherwise.
function waitingCount(u) {
    if (!u)
        return 0
    return (u.pending ? u.pending.length : u.count) + (u.firmware ? u.firmware.length : 0)
}

// Raye's own archive, however apt reaches it: the words for a repository
// belong to the reader, the file only names its address.
function isRayeSource(uri) {
    return /\/mirrors\/raye\.list$/.test(uri)
        || /^[a-z+]+:\/\/([^\/]*\.)?(rayelinux\.com|raye-linux\.github\.io)(\/|$)/.test(uri)
}

// What to add next to the status when a place updates come from could not
// be read: "raye", or the problem of another one ("offline", "missing",
// "unsigned", "invalid", "unknown"), or "" when there is nothing to say.
// Not during a check, whose answer replaces it, nor with an error, which
// says it in its own words.
function sourceNotice(u) {
    if (!u)
        return ""
    const phase = u.phase || u.state
    if (phase === "checking" || phase === "error")
        return ""
    const broken = u.brokenSources || []
    if (broken.some(s => isRayeSource(s.uri)))
        return "raye"
    if (broken.length > 0) {
        const known = ["offline", "missing", "unsigned", "invalid"]
        return known.indexOf(broken[0].problem) >= 0 ? broken[0].problem : "unknown"
    }
    return u.checkedEverything === false ? "unknown" : ""
}

// Why "Install now" has to wait: "battery" when Raye Update holds the
// install back because the PC runs on a low battery, else "".
function installWait(u) {
    return u && u.reason === "battery" ? "battery" : ""
}

// 0..1 while downloading, -1 when it cannot be told (bar runs on its own)
function downloadProgress(u) {
    if (!u || !(u.totalBytes > 0))
        return -1
    return Math.max(0, Math.min(1, u.downloadedBytes / u.totalBytes))
}

// The history as rows grouped by kind, kinds in Windows' order, newest
// first in each: [{ kind, rows: [{ name, version, time }] }].
// Firmware entries count devices, not packages: one row each.
const kinds = ["kernel", "driver", "firmware", "system"]

function historyGroups(history) {
    const groups = {}
    for (const entry of history || []) {
        for (const p of entry.packages || []) {
            const kind = kinds.indexOf(p.kind) >= 0 ? p.kind : "system"
            ;(groups[kind] = groups[kind] || []).push({ name: p.name, version: p.to, time: entry.time })
        }
        if (entry.firmware > 0)
            (groups.firmware = groups.firmware || []).push({ name: "", version: "", time: entry.time,
                                                             devices: entry.firmware })
    }
    const out = []
    for (const kind of kinds) {
        if (groups[kind])
            out.push({ kind: kind, rows: groups[kind].sort((a, b) => b.time - a.time) })
    }
    return out
}

// Hours a person can pick for active hours: the end is at most 18 hours
// after the start, and never the start itself.
function validActiveHours(start, end) {
    if (start < 0 || start > 23 || end < 0 || end > 23 || start === end)
        return false
    return (end - start + 24) % 24 <= 18
}

// What the firmware page offers for one device update: "armed" (it
// installs at the next restart), "restart" (downloaded, waiting),
// "working" (Raye Update is checking or downloading), else "download".
function firmwareAction(u, deviceId, now) {
    const state = pageState(u, now)
    const fetched = !!u && (u.firmware || []).some(f => f.deviceId === deviceId)
    if (fetched && state === "armed")
        return "armed"
    if (fetched && state === "restart")
        return "restart"
    if (state === "checking" || state === "downloading")
        return "working"
    return "download"
}
