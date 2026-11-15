// SPDX-License-Identifier: GPL-3.0-or-later
// The categories of Raye Settings for QML: the tiles of the launch
// screen, the items of the navigation pane, and the pages inside each
// category.
#pragma once

#include "layout.h"

#include <QAbstractListModel>
#include <QVariantList>

class Router;

class CategoryModel : public QAbstractListModel
{
    Q_OBJECT
    // the cards of the launch screen, in order
    Q_PROPERTY(QVariantList tiles READ tiles NOTIFY tilesChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        SummaryRole,
        LandingRole,
        // the pages the window of today can open (it reads `kcm`)
        EntriesRole,
        // every page this computer has the hardware for
        PagesRole,
    };

    explicit CategoryModel(QObject *parent = nullptr);

    // Loads the layout; returns false (and keeps the old one) on error.
    bool load(const QByteArray &json, const QString &lang);
    QString lastError() const { return m_error; }
    const Layout &layout() const { return m_layout; }

    QVariantList tiles() const;
    // summaries and page lists follow the hardware the router knows of
    void follow(Router *router);
    // the row of the category that holds this page or module, -1 if none
    Q_INVOKABLE int categoryOf(const QString &target) const { return m_layout.categoryIndexOf(target); }
    // everything QML needs about one page, empty when there is no such page
    Q_INVOKABLE QVariantMap pageOf(const QString &id) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void tilesChanged();

private:
    Layout m_layout;
    QString m_error;
};
