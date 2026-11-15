// SPDX-License-Identifier: GPL-3.0-or-later
// Network names come from the air: anybody can call a network
// "<b>Free</b>". The cards read a title as rich text when it looks like
// HTML and a description as styled text, so a name goes through here
// before it is shown in one.
.pragma library

function escaped(text) {
    return String(text === undefined || text === null ? "" : text)
        .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
}

// for a card's description (styled text)
function styled(text) {
    return escaped(text)
}

// for a card's title (rich text when it looks like it): a span makes
// sure the escaped text is read as markup, so "&lt;" shows as "<"
function title(text) {
    const s = String(text === undefined || text === null ? "" : text)
    return /[<>&]/.test(s) ? "<span>" + escaped(s) + "</span>" : s
}
