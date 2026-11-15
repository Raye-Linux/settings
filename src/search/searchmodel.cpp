// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchmodel.h"

#include "searchtext.h"

#include <QHash>

#include <algorithm>

namespace {
constexpr int debounceMs = 120;
}

SearchModel::SearchModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(debounceMs);
    connect(&m_timer, &QTimer::timeout, this, &SearchModel::searchNow);
}

void SearchModel::setIndex(const SearchIndex *index)
{
    m_index = index;
    if (!m_query.isEmpty())
        searchNow();
}

const SearchIndex *SearchModel::activeIndex() const
{
    return m_index ? m_index : SearchIndex::shared();
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_results.size());
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_results.size())
        return {};
    SearchResult &r = m_results[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole: return r.label;
    case EntryIdRole: return r.id;
    case PageIdRole: return r.pageId;
    case AnchorRole: return r.anchor;
    case HighlightedRole:
        if (r.highlighted.isEmpty())
            r.highlighted = SearchText::highlight(r.label, m_shownWords);
        return r.highlighted;
    case BreadcrumbRole: return r.breadcrumb;
    case IconNameRole: return r.icon;
    case IconFallbackRole: return r.iconFallback;
    case CategoryIdRole: return r.categoryId;
    case CategoryNameRole: return r.categoryName;
    case KindRole: return int(r.kind);
    case ScoreRole: return r.score;
    }
    return {};
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {
        {EntryIdRole, "entryId"},
        {PageIdRole, "pageId"},
        {AnchorRole, "anchor"},
        {LabelRole, "label"},
        {HighlightedRole, "highlighted"},
        {BreadcrumbRole, "breadcrumb"},
        {IconNameRole, "iconName"},
        {IconFallbackRole, "iconFallback"},
        {CategoryIdRole, "categoryId"},
        {CategoryNameRole, "categoryName"},
        {KindRole, "kind"},
        {ScoreRole, "score"},
    };
}

void SearchModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    Q_EMIT queryChanged();
    if (query.trimmed().isEmpty()) {
        // nothing to wait for: an empty box has no results
        m_timer.stop();
        searchNow();
        return;
    }
    m_timer.start();
    Q_EMIT pendingChanged();
}

void SearchModel::setLimit(int limit)
{
    limit = qMax(0, limit);
    if (limit == m_limit)
        return;
    m_limit = limit;
    Q_EMIT limitChanged();
    if (!m_query.isEmpty())
        searchNow();
}

void SearchModel::setGrouped(bool grouped)
{
    if (grouped == m_grouped)
        return;
    m_grouped = grouped;
    Q_EMIT groupedChanged();
    if (!m_query.isEmpty())
        searchNow();
}

void SearchModel::searchNow()
{
    const bool wasPending = m_timer.isActive();
    m_timer.stop();
    QList<SearchResult> results = activeIndex()->search(m_query, m_limit);
    if (m_grouped) {
        // categories in the order of their best result, a stable sort
        // keeps the ranking inside each one
        QHash<QString, int> rank;
        for (const SearchResult &r : std::as_const(results)) {
            if (!rank.contains(r.categoryName))
                rank.insert(r.categoryName, int(rank.size()));
        }
        std::stable_sort(results.begin(), results.end(), [&rank](const SearchResult &a, const SearchResult &b) {
            return rank.value(a.categoryName) < rank.value(b.categoryName);
        });
    }
    beginResetModel();
    m_results = results;
    m_shownQuery = m_query;
    m_shownWords = SearchText::words(m_query);
    endResetModel();
    Q_EMIT countChanged();
    if (wasPending)
        Q_EMIT pendingChanged();
}

QString SearchModel::entryIdAt(int row) const
{
    return row >= 0 && row < m_results.size() ? m_results.at(row).id : QString();
}

QString SearchModel::categoryNameAt(int row) const
{
    return row >= 0 && row < m_results.size() ? m_results.at(row).categoryName : QString();
}
