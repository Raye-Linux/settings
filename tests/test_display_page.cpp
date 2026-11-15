// SPDX-License-Identifier: GPL-3.0-or-later
// System > Display, as the page itself: every row data/search/display.json
// advertises must really be drawn by DisplayPage.qml, and nothing the page
// draws may be missing from the index.
//
// WHY THIS FILE EXISTS. test_display.cpp proves DisplayConfig::makePrimary,
// the logic behind "Make this my main display". It says nothing about the
// page. The bug that was fixed was not in the logic -- makePrimary already
// worked -- it was that the switch sat inside the closed "Multiple
// displays" expander, so nobody could reach it. Putting it back there, or
// dropping the row altogether, leaves makePrimary untouched: the 42 tests
// of this repository all stayed green when the row was removed (measured
// 2026-11-06). tests/test_search_index.cpp notices the mismatch but only
// prints it -- strictAnchors is false until ST-27 -- so it cannot fail
// either. Personalization > Taskbar has this check (test_taskbar.cpp
// page()); Display did not.
#include "layout.h"

#include <KLocalizedContext>
#include <KLocalizedString>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

namespace {

QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg
        && (qstrcmp(context.category, "qml") == 0 || qstrncmp(context.category, "qt.qml", 6) == 0
            || message.contains(QLatin1String("qrc:")))) {
        qmlWarnings << message;
    }
    fprintf(stderr, "%s\n", qPrintable(message));
}

// The anchors data/search/display.json claims for one page, in order and
// without repeats. The file is a flat array and carries the rows of the
// Night light page too, so the page id is what selects them.
QStringList indexedAnchors(const QString &pageId, QString *error)
{
    QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/display.json"));
    if (!fragment.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("data/search/display.json cannot be read");
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(fragment.readAll(), &parse);
    if (!doc.isArray()) {
        *error = QStringLiteral("data/search/display.json is not an array: ") + parse.errorString();
        return {};
    }
    QStringList anchors;
    for (const QJsonValue &v : doc.array()) {
        const QString id = v.toObject().value(QStringLiteral("id")).toString();
        if (!id.startsWith(pageId + QLatin1Char('#')))
            continue;
        if (v.toObject().value(QStringLiteral("label")).toString().isEmpty()) {
            *error = id + QStringLiteral(" has no label");
            return {};
        }
        if (v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty()) {
            *error = id + QStringLiteral(" has no label_fr");
            return {};
        }
        const QString anchor = id.section(QLatin1Char('#'), 1);
        if (anchors.contains(anchor)) {
            *error = id + QStringLiteral(" is in the file twice");
            return {};
        }
        anchors << anchor;
    }
    return anchors;
}

} // namespace

class TestDisplayPage : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // the page is in the layout under the id the search rows use
    void pageIsInTheLayout()
    {
        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        QString component;
        for (const Category &category : layout.categories)
            for (const Page &page : category.pages)
                if (page.id == QLatin1String("system/display"))
                    component = page.component;
        QCOMPARE(component, QStringLiteral("pages/system/DisplayPage.qml"));
    }

    void rowsAreOnThePage()
    {
        QString error;
        const QStringList indexed = indexedAnchors(QStringLiteral("system/display"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!indexed.isEmpty());
        // the row the Display page was fixed for: a search hit that leads
        // to nothing is worse than no hit at all
        QVERIFY2(indexed.contains(QLatin1String("main-display")),
                 "data/search/display.json no longer advertises main-display");

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        QStringList drawn;
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            QQuickView view(&engine, nullptr);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1120, 760);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/system/DisplayPage.qml")));
            QCOMPARE(view.status(), QQuickView::Ready);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(300);

            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            // a row may hold a nested searchId of the same name (brightness)
            for (const QString &id : ids.toStringList())
                if (!drawn.contains(id))
                    drawn << id;
        }
        qInstallMessageHandler(previous);

        QVERIFY2(drawn.contains(QLatin1String("main-display")),
                 "\"Make this my main display\" is not on the Display page: it was hidden "
                 "inside the closed \"Multiple displays\" expander before, and search sends "
                 "people to system/display#main-display");
        for (const QString &id : indexed)
            QVERIFY2(drawn.contains(id), qPrintable(id + QStringLiteral(" is not on the page")));
        for (const QString &id : drawn)
            QVERIFY2(indexed.contains(id), qPrintable(id + QStringLiteral(" is not in data/search/display.json")));
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestDisplayPage)
#include "test_display_page.moc"
