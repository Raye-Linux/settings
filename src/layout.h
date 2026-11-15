// SPDX-License-Identifier: GPL-3.0-or-later
// Reads the layout of Raye Settings: the categories of the launch
// screen, the pages inside each one, and how a page is drawn (our own
// QML, or a settings module hosted in a Raye page).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct Page {
    QString id;           // path, "system/display"
    QString icon;         // Raye glyph, "org.raye.settings-display"
    QString iconFallback; // icon to use until the Raye one is installed
    QString name;         // in the language asked for
    QString summary;      // one line under the name
    QString type;         // "qml", "kcm" or "action"
    QString component;    // QML file of the page, relative to src/qml
    QString kcm;          // module the page hosts, or a Raye page id
    QStringList aliases;  // module ids that land on this page
    QString requirement;  // hardware the page needs; empty: always shown

    // What the window opens for this page: the module, else the first
    // module it answers for, else the page itself. The phone modules
    // (kcm_mobile_*) are aliases only: a link to one lands here, but the
    // window never picks one to show for the page.
    QString target() const;
    // Does the window of today have something to open for this page (a
    // module, or a Raye page it already knows)? False for the pages
    // that are only a placeholder until the new window lands.
    bool opensInWindow() const;
};

struct Category {
    QString id;
    QString icon;
    QString iconFallback;
    QString name;
    QString summary;
    QString landing; // QML file of the landing page, empty: built from the list
    // said instead of the summary on a PC without that hardware
    QString bareRequirement;
    QString bareSummary;
    QList<Page> pages;
};

// A card on the launch screen. Points at a category or a single page.
struct Tile {
    QString target;
    QString icon;
    QString iconFallback;
    QString name;
    QString summary;
    QString bareRequirement;
    QString bareSummary;
};

struct Layout {
    QList<Category> categories;
    QList<Tile> tiles;
    // module ids that open the launch screen itself
    QStringList homeAliases;

    bool isEmpty() const { return categories.isEmpty(); }
    const Category *category(const QString &id) const;
    const Page *page(const QString &id) const;
    // The page a module id belongs to, by its own id or by an alias.
    const Page *pageForModule(const QString &kcm) const;
    // Row of the category holding this page id, module id or category
    // id; -1 when nothing matches.
    int categoryIndexOf(const QString &target) const;
};

// Parses the layout file. `lang` is a language code such as "fr" or
// "en"; names fall back to English when there is no translation.
// Returns an empty layout and fills `error` when the file is unusable.
Layout parseLayout(const QByteArray &json, const QString &lang, QString *error = nullptr);
