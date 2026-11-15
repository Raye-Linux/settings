// SPDX-License-Identifier: GPL-3.0-or-later
// The results of a search, for QML: the suggestions under the search
// box (8 rows) and the "Search results" page (all of them, grouped by
// category).
//
//   SearchModel { id: results; query: field.text; limit: 8 }
//
// A new query is looked up 120 ms after the last key, the way Windows
// waits for the user to stop typing; searchNow() does not wait.
#pragma once

#include "searchindex.h"

#include <QAbstractListModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class SearchModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // 0: every result
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    // results of one category together, the categories in the order of
    // their best result
    Q_PROPERTY(bool grouped READ grouped WRITE setGrouped NOTIFY groupedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // has the current query been looked up yet?
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    // the query the rows are for (what the user typed, as typed)
    Q_PROPERTY(QString shownQuery READ shownQuery NOTIFY countChanged)

public:
    enum Role {
        EntryIdRole = Qt::UserRole + 1,
        PageIdRole,
        AnchorRole,
        LabelRole,
        HighlightedRole,
        BreadcrumbRole,
        IconNameRole,
        IconFallbackRole,
        CategoryIdRole,
        CategoryNameRole,
        KindRole,
        ScoreRole,
    };

    explicit SearchModel(QObject *parent = nullptr);

    // the tests hand their own index; QML uses the installed one
    void setIndex(const SearchIndex *index);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    bool grouped() const { return m_grouped; }
    void setGrouped(bool grouped);
    int count() const { return int(m_results.size()); }
    bool pending() const { return m_timer.isActive(); }
    QString shownQuery() const { return m_shownQuery; }

    Q_INVOKABLE void searchNow();
    // the id of a row ("system/display#night-light"), "" out of range
    Q_INVOKABLE QString entryIdAt(int row) const;
    // the category of a row, "" out of range (the results page starts a
    // group where it changes)
    Q_INVOKABLE QString categoryNameAt(int row) const;

Q_SIGNALS:
    void queryChanged();
    void limitChanged();
    void groupedChanged();
    void countChanged();
    void pendingChanged();

private:
    const SearchIndex *activeIndex() const;

    const SearchIndex *m_index = nullptr;
    QString m_query;
    QString m_shownQuery;
    int m_limit = 0;
    bool m_grouped = false;
    QStringList m_shownWords;
    // the highlighting of a row is made when the row is first drawn
    mutable QList<SearchResult> m_results;
    QTimer m_timer;
};
