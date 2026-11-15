// SPDX-License-Identifier: GPL-3.0-or-later
#include "categorymodel.h"
#include "router.h"

#include <QIcon>
#include <QVariantMap>

namespace {

// The Raye glyph when it is installed, the stock one until then, so a
// fresh checkout still shows an icon on every row.
QString iconFor(const QString &raye, const QString &fallback)
{
    if (raye.isEmpty())
        return fallback;
    if (fallback.isEmpty() || QIcon::hasThemeIcon(raye))
        return raye;
    return fallback;
}

QVariantMap pageMap(const Page &page)
{
    return QVariantMap{
        // what the window has always read, so old links keep working
        {QStringLiteral("kcm"), page.target()},
        {QStringLiteral("name"), page.name},
        {QStringLiteral("icon"), iconFor(page.icon, page.iconFallback)},
        // the rest of the page
        {QStringLiteral("id"), page.id},
        {QStringLiteral("summary"), page.summary},
        {QStringLiteral("type"), page.type},
        {QStringLiteral("component"), page.component},
        {QStringLiteral("module"), page.kcm},
        {QStringLiteral("requires"), page.requirement},
    };
}

QString summaryHere(const QString &summary, const QString &requirement, const QString &bare)
{
    const Router *router = Router::instance();
    if (requirement.isEmpty() || bare.isEmpty() || !router || router->has(requirement))
        return summary;
    return bare;
}

} // namespace

CategoryModel::CategoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void CategoryModel::follow(Router *router)
{
    if (!router)
        return;
    connect(router, &Router::requirementsChanged, this, [this] {
        Q_EMIT tilesChanged();
        if (rowCount() > 0)
            Q_EMIT dataChanged(index(0), index(rowCount() - 1), {SummaryRole, EntriesRole, PagesRole});
    });
}

bool CategoryModel::load(const QByteArray &json, const QString &lang)
{
    QString error;
    Layout parsed = parseLayout(json, lang, &error);
    if (parsed.isEmpty()) {
        m_error = error;
        return false;
    }
    beginResetModel();
    m_layout = std::move(parsed);
    m_error.clear();
    endResetModel();
    Q_EMIT tilesChanged();
    return true;
}

QVariantList CategoryModel::tiles() const
{
    QVariantList tiles;
    for (const Tile &t : m_layout.tiles) {
        tiles.append(QVariantMap{
            {QStringLiteral("target"), t.target},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("summary"), summaryHere(t.summary, t.bareRequirement, t.bareSummary)},
            {QStringLiteral("icon"), iconFor(t.icon, t.iconFallback)},
        });
    }
    return tiles;
}

QVariantMap CategoryModel::pageOf(const QString &id) const
{
    const Page *page = m_layout.page(id);
    return page ? pageMap(*page) : QVariantMap{};
}

int CategoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_layout.categories.size());
}

QVariant CategoryModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Category &c = m_layout.categories.at(index.row());
    switch (role) {
    case IdRole:
        return c.id;
    case Qt::DisplayRole:
    case NameRole:
        return c.name;
    case IconRole:
        return iconFor(c.icon, c.iconFallback);
    case SummaryRole:
        return summaryHere(c.summary, c.bareRequirement, c.bareSummary);
    case LandingRole:
        return c.landing;
    case EntriesRole:
    case PagesRole: {
        // pages this computer has the hardware for; the window of today
        // leaves out the ones it would click on with nothing to show
        const Router *router = Router::instance();
        QVariantList pages;
        for (const Page &p : c.pages) {
            if (router && !router->has(p.requirement))
                continue;
            if (role == EntriesRole && !p.opensInWindow())
                continue;
            pages.append(pageMap(p));
        }
        return pages;
    }
    }
    return {};
}

QHash<int, QByteArray> CategoryModel::roleNames() const
{
    return {{IdRole, "categoryId"},   {NameRole, "name"},       {IconRole, "iconName"},
            {SummaryRole, "summary"}, {LandingRole, "landing"}, {EntriesRole, "entries"},
            {PagesRole, "pages"}};
}
