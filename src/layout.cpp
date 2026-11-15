// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString localText(const QJsonObject &o, const QString &key, const QString &lang)
{
    const QString translated = o.value(key + QLatin1Char('_') + lang).toString();
    return translated.isEmpty() ? o.value(key).toString() : translated;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &v : value.toArray()) {
        const QString s = v.toString();
        if (!s.isEmpty())
            list.append(s);
    }
    return list;
}

Page readPage(const QJsonObject &o, const QString &lang)
{
    Page page;
    page.id = o.value(QStringLiteral("id")).toString();
    page.icon = o.value(QStringLiteral("icon")).toString();
    page.iconFallback = o.value(QStringLiteral("icon_fallback")).toString();
    page.name = localText(o, QStringLiteral("name"), lang);
    page.summary = localText(o, QStringLiteral("summary"), lang);
    page.type = o.value(QStringLiteral("type")).toString();
    page.component = o.value(QStringLiteral("component")).toString();
    page.kcm = o.value(QStringLiteral("kcm")).toString();
    page.aliases = stringList(o.value(QStringLiteral("aliases")));
    page.requirement = o.value(QStringLiteral("requires")).toString();
    if (page.type.isEmpty())
        page.type = page.component.isEmpty() ? QStringLiteral("kcm") : QStringLiteral("qml");
    return page;
}

} // namespace

namespace {

QString windowModule(const Page &page)
{
    if (!page.kcm.isEmpty())
        return page.kcm;
    for (const QString &alias : page.aliases) {
        if (!alias.startsWith(QLatin1String("kcm_mobile_")))
            return alias;
    }
    return {};
}

} // namespace

QString Page::target() const
{
    const QString module = windowModule(*this);
    return module.isEmpty() ? QStringLiteral("raye:") + id : module;
}

bool Page::opensInWindow() const
{
    return !windowModule(*this).isEmpty();
}

const Category *Layout::category(const QString &id) const
{
    for (const Category &c : categories) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

const Page *Layout::page(const QString &id) const
{
    for (const Category &c : categories) {
        for (const Page &p : c.pages) {
            if (p.id == id)
                return &p;
        }
    }
    return nullptr;
}

const Page *Layout::pageForModule(const QString &kcm) const
{
    if (kcm.isEmpty())
        return nullptr;
    for (const Category &c : categories) {
        for (const Page &p : c.pages) {
            if (p.kcm == kcm || p.aliases.contains(kcm))
                return &p;
        }
    }
    return nullptr;
}

int Layout::categoryIndexOf(const QString &target) const
{
    for (int i = 0; i < categories.size(); ++i) {
        const Category &c = categories.at(i);
        if (c.id == target)
            return i;
        for (const Page &p : c.pages) {
            if (p.id == target || p.kcm == target || p.aliases.contains(target)
                || p.target() == target)
                return i;
        }
    }
    return -1;
}

Layout parseLayout(const QByteArray &json, const QString &lang, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = parseError.errorString();
        return {};
    }

    Layout layout;
    const QJsonObject root = doc.object();
    for (const QJsonValue &value : root.value(QStringLiteral("categories")).toArray()) {
        const QJsonObject c = value.toObject();
        Category category;
        category.id = c.value(QStringLiteral("id")).toString();
        if (category.id.isEmpty())
            continue; // a category nobody can link to is a typo
        category.icon = c.value(QStringLiteral("icon")).toString();
        category.iconFallback = c.value(QStringLiteral("icon_fallback")).toString();
        category.name = localText(c, QStringLiteral("name"), lang);
        category.summary = localText(c, QStringLiteral("summary"), lang);
        category.landing = c.value(QStringLiteral("landing")).toString();
        // "summary_without": {"requires": "wifi", "summary": "Ethernet, VPN"}
        const QJsonObject bare = c.value(QStringLiteral("summary_without")).toObject();
        category.bareRequirement = bare.value(QStringLiteral("requires")).toString();
        if (!category.bareRequirement.isEmpty())
            category.bareSummary = localText(bare, QStringLiteral("summary"), lang);
        for (const QJsonValue &e : c.value(QStringLiteral("pages")).toArray()) {
            const Page page = readPage(e.toObject(), lang);
            if (page.id.isEmpty())
                continue; // same: no id, no link
            category.pages.append(page);
        }
        layout.categories.append(category);
    }
    if (layout.categories.isEmpty()) {
        if (error)
            *error = QStringLiteral("no categories in layout");
        return {};
    }

    layout.homeAliases = stringList(root.value(QStringLiteral("home")).toObject().value(QStringLiteral("aliases")));

    // A tile names what it opens; its words come from there unless it
    // says its own.
    for (const QJsonValue &value : root.value(QStringLiteral("tiles")).toArray()) {
        const QJsonObject t = value.toObject();
        Tile tile;
        tile.target = t.value(QStringLiteral("target")).toString();
        if (tile.target.isEmpty())
            continue;
        if (const Category *c = layout.category(tile.target)) {
            tile.icon = c->icon;
            tile.iconFallback = c->iconFallback;
            tile.name = c->name;
            tile.summary = c->summary;
            tile.bareRequirement = c->bareRequirement;
            tile.bareSummary = c->bareSummary;
        } else if (const Page *p = layout.page(tile.target)) {
            tile.icon = p->icon;
            tile.iconFallback = p->iconFallback;
            tile.name = p->name;
            tile.summary = p->summary;
        } else {
            continue; // a tile that opens nothing
        }
        const QString icon = t.value(QStringLiteral("icon")).toString();
        if (!icon.isEmpty())
            tile.icon = icon;
        const QString fallback = t.value(QStringLiteral("icon_fallback")).toString();
        if (!fallback.isEmpty())
            tile.iconFallback = fallback;
        const QString name = localText(t, QStringLiteral("name"), lang);
        if (!name.isEmpty())
            tile.name = name;
        const QString summary = localText(t, QStringLiteral("summary"), lang);
        if (!summary.isEmpty()) {
            tile.summary = summary;
            tile.bareRequirement.clear();
        }
        layout.tiles.append(tile);
    }
    return layout;
}
