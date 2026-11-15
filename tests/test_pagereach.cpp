// SPDX-License-Identifier: GPL-3.0-or-later
// Every row the search advertises must be REACHABLE on its page.
//
// WHY THIS FILE EXISTS. The search sends people to "page#anchor", and the
// frame answers with PageScaffold.scrollToAnchor(anchor): it looks the row
// up by its searchId, scrolls to it and flashes it. Looking the row up
// succeeds for a row that is merely instantiated -- and a row inside a
// closed Expander is instantiated. Its body is an Item of height 0 with
// clip: true and visible: height > 0, so the row is there, findRow finds
// it, the page scrolls to the closed card, flash() runs on something
// nobody can see, and scrollToAnchor returns true. The search hit leads
// to nothing, and every check in this repository agrees that all is well.
//
// Measured 2026-11-06, before the fix this file came with: 28 rows on 6
// pages sat inside a closed Expander -- system/notifications (5),
// system/power (6), system/storage (3), system/multitasking (6),
// personalization/taskbar (8). System > Display had the same defect and
// was fixed on 2026-11-06 by moving the row out of the group; this file
// takes the other road, which keeps the groups the pages are designed
// around: scrollToAnchor opens the groups above the row first.
//
// AND WHY THE EXISTING PAGE CHECKS CANNOT SEE IT. test_display_page.cpp
// and the pages() checks of test_accessibility, test_multitasking,
// test_appearance, test_bluetooth, test_datetime, test_installedapps and
// test_taskbar all compare data/search against PageScaffold.searchIds(),
// which walks the item tree -- so a row behind a closed group is counted
// as drawn. Measured 2026-11-06 in the resolute chroot: putting
// "Make this my main display" back inside the closed "Multiple displays"
// expander leaves test_display_page.cpp at 4 passed, 0 failed, the very
// regression its own header says it guards. test_search_index.cpp reads
// the QML as text, so it cannot see it either (and its anchorsMeet only
// prints until ST-27). This check is about reachability, nothing else.
//
// It asserts no row is VISIBLE, on purpose. A row may be legitimately
// hidden by the hardware the test machine has not got: "Make this my main
// display" is `visible: display.ready && page.several`, so it is invisible
// offscreen however well it is placed. Whether a group above it is closed
// does not depend on the hardware, and that is what is checked.
#include "layout.h"

#include <KLocalizedContext>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

#include <algorithm>

namespace {

// The rows data/search advertises, page by page. Both shapes of fragment
// are in the tree and both are read: a bare array (display.json) and
// {"version": 1, "entries": [...]} (update-devices.json).
QHash<QString, QStringList> indexedRows(QString *error)
{
    QHash<QString, QStringList> rows;
    const QDir dir(QStringLiteral(SOURCE_DIR "/data/search"));
    const auto files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        *error = QStringLiteral("data/search holds no fragment");
        return rows;
    }
    for (const QFileInfo &info : files) {
        if (info.fileName() == QLatin1String("synonyms.json"))
            continue;
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            *error = info.fileName() + QStringLiteral(" cannot be read");
            return rows;
        }
        QJsonParseError parse;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
        QJsonArray entries;
        if (doc.isArray())
            entries = doc.array();
        else if (doc.isObject())
            entries = doc.object().value(QStringLiteral("entries")).toArray();
        else {
            *error = info.fileName() + QStringLiteral(" is neither an array nor an object: ") + parse.errorString();
            return rows;
        }
        if (entries.isEmpty()) {
            *error = info.fileName() + QStringLiteral(" holds no entry");
            return rows;
        }
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (!id.contains(QLatin1Char('#')))
                continue; // a page, not a row
            const QString page = id.section(QLatin1Char('#'), 0, 0);
            const QString anchor = id.section(QLatin1Char('#'), 1);
            if (!anchor.isEmpty() && !rows[page].contains(anchor))
                rows[page].append(anchor);
        }
    }
    return rows;
}

// The first item carrying this searchId, in the order findRow walks the
// tree (depth first). Nothing above the scaffold's column carries one.
QQuickItem *rowWithId(QQuickItem *item, const QString &anchor)
{
    if (!item)
        return nullptr;
    const QVariant id = item->property("searchId");
    if (id.isValid() && id.toString() == anchor)
        return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = rowWithId(child, anchor))
            return found;
    }
    return nullptr;
}

// The closed groups between this row and the page. An Expander publishes
// its own searchId through its header, which is its child: an ancestor
// carrying the same anchor is that Expander and is not counted, or asking
// for a group would always report itself.
QStringList closedGroupsAbove(QQuickItem *row, const QString &anchor)
{
    QStringList groups;
    for (QQuickItem *p = row->parentItem(); p; p = p->parentItem()) {
        const QVariant expanded = p->property("expanded");
        if (!expanded.isValid() || expanded.metaType().id() != QMetaType::Bool || expanded.toBool())
            continue;
        if (p->property("searchId").toString() == anchor)
            continue;
        const QString title = p->property("title").toString();
        groups.append(title.isEmpty() ? QStringLiteral("<a group with no title>") : title);
    }
    return groups;
}

} // namespace

class TestPageReach : public QObject
{
    Q_OBJECT

    // Rows the pages did not bring to life on this machine. Counted
    // rather than passed over in silence: a check that steps over more
    // and more rows ends up proving nothing while still printing green.
    static int s_absent;
    static QStringList s_absentRows;

private Q_SLOTS:
    void rowsAreReachable_data()
    {
        QTest::addColumn<QString>("component");
        QTest::addColumn<QString>("page");
        QTest::addColumn<QStringList>("anchors");

        QString error;
        const QHash<QString, QStringList> rows = indexedRows(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        for (const Category &category : layout.categories) {
            for (const Page &page : category.pages) {
                if (page.type != QLatin1String("qml") || page.component.isEmpty())
                    continue;
                // 18 pages of the layout name a file nobody has written
                // yet (devices/printers, accounts/sign-in ...). A page
                // whose file is there must load: see pageFilesLoad below.
                if (!QFile::exists(QStringLiteral(SOURCE_DIR "/src/qml/") + page.component))
                    continue;
                const QStringList anchors = rows.value(page.id);
                if (anchors.isEmpty())
                    continue; // the search sends nobody inside this page
                QTest::newRow(qPrintable(page.id)) << page.component << page.id << anchors;
            }
        }
    }

    // A data set that quietly empties takes every check below with it and
    // the run still says it passed: QtTest prints "0 passed" and exits 0,
    // the same silence a glob that stops matching leaves behind. So the
    // set is measured here. 57 pages and 267 rows on 2026-11-06; a page
    // or a row may come and go, an empty set may not.
    void theDataSetIsNotEmpty()
    {
        QString error;
        const QHash<QString, QStringList> rows = indexedRows(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        int pages = 0;
        int anchors = 0;
        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        for (const Category &category : layout.categories) {
            for (const Page &page : category.pages) {
                if (page.type != QLatin1String("qml") || page.component.isEmpty()
                    || !QFile::exists(QStringLiteral(SOURCE_DIR "/src/qml/") + page.component)
                    || rows.value(page.id).isEmpty())
                    continue;
                ++pages;
                anchors += rows.value(page.id).size();
            }
        }
        QVERIFY2(pages >= 40, qPrintable(QStringLiteral("only %1 pages carry an indexed row; "
                                                       "57 did on 2026-11-06").arg(pages)));
        QVERIFY2(anchors >= 200, qPrintable(QStringLiteral("only %1 indexed rows on those pages; "
                                                          "267 did on 2026-11-06").arg(anchors)));
    }

    void rowsAreReachable()
    {
        QFETCH(QString, component);
        QFETCH(QString, page);
        QFETCH(QStringList, anchors);

        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQuickView view(&engine, nullptr);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1120, 760);
        view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + component));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(120);
        QQuickItem *root = view.rootObject();
        QVERIFY(root);

        for (const QString &anchor : anchors) {
            // A row may be absent for want of the hardware it belongs to:
            // network/ethernet#metered lives in a Repeater over the wired
            // adapters, and this machine has none. Absence is not the
            // subject here -- test_search_index.cpp reads the QML as text
            // and sees the row whether a device brought it to life or not.
            // What is checked is the row that IS there.
            QQuickItem *row = rowWithId(root, anchor);
            if (!row) {
                ++s_absent;
                s_absentRows << page + QLatin1Char('#') + anchor;
                continue;
            }

            // what a search result does: the frame asks the page for the row
            QVariant reached;
            QVERIFY2(QMetaObject::invokeMethod(root, "scrollToAnchor", Q_RETURN_ARG(QVariant, reached),
                                               Q_ARG(QVariant, anchor)),
                     qPrintable(page + QStringLiteral(" has no scrollToAnchor (is it a PageScaffold?)")));
            // The row is in the tree, so the frame has to be able to reach
            // it. It could not when the row sat in the page's banner
            // instead of its column: findRow searched the column alone,
            // scrollToAnchor returned false, and "Rename this PC" did
            // nothing at all when the search sent anybody to it.
            QVERIFY2(reached.toBool(),
                     qPrintable(page + QLatin1Char('#') + anchor
                                + QStringLiteral(" is on the page and the frame cannot reach it: "
                                                 "scrollToAnchor returned false")));

            // `expanded` is a plain bool and is set at once; only the
            // height of the group animates. So nothing is waited for here:
            // waiting would only hide a group that never opens.
            row = rowWithId(root, anchor);
            QVERIFY2(row, qPrintable(page + QLatin1Char('#') + anchor + QStringLiteral(" vanished after the scroll")));
            const QStringList closed = closedGroupsAbove(row, anchor);
            QVERIFY2(closed.isEmpty(),
                     qPrintable(page + QLatin1Char('#') + anchor
                                + QStringLiteral(" stays inside the closed group ") + closed.join(QStringLiteral(" inside "))
                                + QStringLiteral(": the search scrolls to it and nobody sees it")));
        }
    }

    // Measured 2026-11-06 in the resolute chroot: 4 rows had no item,
    // and all four are network/ethernet -- metered, ip-assignment, dns
    // and ethernet-properties live inside a Repeater over the wired
    // adapters, and this machine has none. The number is pinned so that a
    // page which quietly stops drawing its rows is a failure and not a
    // quieter run: every row that goes missing is stepped over by
    // rowsAreReachable, and without this the run would only get greener.
    //
    // It is declared after rowsAreReachable on purpose. QtTest runs the
    // slots in declaration order, so above it the counter would still be
    // zero and this check could never fail.
    void absentRowsAreStillFew()
    {
        // printed, not qInfo'd: QtTest keeps qInfo out of a whole run's
        // output, and a line that reports nothing is worse than no line
        fprintf(stdout, "rows advertised by data/search with no item on this machine: %d\n", s_absent);
        fflush(stdout);
        QVERIFY2(s_absent <= 8,
                 qPrintable(QStringLiteral("%1 advertised rows had no item on the page (4 on "
                                           "2026-11-06, all on network/ethernet):\n  ").arg(s_absent)
                            + s_absentRows.join(QStringLiteral("\n  "))));
    }

    // Every page file the layout names must load. The check above steps
    // over a page whose file is not written yet; a file that IS there and
    // fails to load would otherwise be stepped over the same way, which
    // is how a broken page passes for an unwritten one.
    void pageFilesLoad_data()
    {
        QTest::addColumn<QString>("component");
        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        for (const Category &category : layout.categories)
            for (const Page &page : category.pages)
                if (page.type == QLatin1String("qml") && !page.component.isEmpty()
                    && QFile::exists(QStringLiteral(SOURCE_DIR "/src/qml/") + page.component)
                    && page.requirement != QLatin1String("raye-account"))
                    QTest::newRow(qPrintable(page.id)) << page.component;
    }

    // The three pages behind "requires": "raye-account" import
    // org.raye.account, a QML module of the account repository. Nothing
    // builds that repository -- iso/tools/raye_tests_suites.py says so in
    // ACCOUNT_NO_BUILD -- so the module is installed nowhere and those
    // pages cannot load in any build of Settings. They are left out of
    // pageFilesLoad above by that declared requirement, never by reading
    // the error: a check that recognises a message turns the next real
    // breakage into a page quietly left out. Their number is pinned here,
    // so a fourth one, or one of these three starting to load, is seen.
    void onlyTheAccountPagesCannotLoad()
    {
        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        QStringList left;
        for (const Category &category : layout.categories)
            for (const Page &page : category.pages)
                if (page.type == QLatin1String("qml") && !page.component.isEmpty()
                    && QFile::exists(QStringLiteral(SOURCE_DIR "/src/qml/") + page.component)
                    && page.requirement == QLatin1String("raye-account"))
                    left << page.id;
        std::sort(left.begin(), left.end());
        QCOMPARE(left, QStringList({QStringLiteral("accounts/raye-account"),
                                    QStringLiteral("accounts/raye-backup"),
                                    QStringLiteral("accounts/your-devices")}));
    }
    void pageFilesLoad()
    {
        QFETCH(QString, component);
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQuickView view(&engine, nullptr);
        view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + component));
        QCOMPARE(view.status(), QQuickView::Ready);
    }
};

int TestPageReach::s_absent = 0;
QStringList TestPageReach::s_absentRows;

QTEST_MAIN(TestPageReach)
#include "test_pagereach.moc"
