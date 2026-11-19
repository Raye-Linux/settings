// SPDX-License-Identifier: GPL-3.0-or-later
// The index as the package installs it: layout.json, every
// data/search/*.json, synonyms.json. Every row a page names with
// searchId must be in the index, and every row of the index must be a
// searchId of its page.
//
// Until every page package has landed its rows, the anchors that do not
// meet are listed and the test passes; ST-27 makes it strict
// (strictAnchors below, or RAYE_SEARCH_STRICT=1 to try it today).
#include "layout.h"
#include "searchindex.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

namespace {

constexpr bool strictAnchors = false;

bool strict()
{
    return strictAnchors || qEnvironmentVariable("RAYE_SEARCH_STRICT") == QLatin1String("1");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

SearchIndex::Sources packageSources(const QString &language)
{
    SearchIndex::Sources s;
    s.layout = readFile(QStringLiteral(LAYOUT_FILE));
    s.language = language;
    const QDir dir(QStringLiteral(SOURCE_DIR "/data/search"));
    const auto files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        if (info.fileName() == QLatin1String("synonyms.json"))
            s.synonyms = readFile(info.filePath());
        else
            s.fragments.append({info.fileName(), readFile(info.filePath())});
    }
    return s;
}

// The page a QML file draws: the page whose component it is, else the
// page of the same folder whose name it starts with (DisplayPage.qml
// for DisplayArrangement.qml). Empty when there is none.
QString pageOfFile(const Layout &layout, const QString &relative)
{
    const QString folder = QFileInfo(relative).path();
    const QString base = QFileInfo(relative).completeBaseName();
    QString best;
    qsizetype bestLength = 0;
    for (const Category &category : layout.categories) {
        for (const Page &page : category.pages) {
            if (page.component == relative)
                return page.id;
            if (page.component.isEmpty() || QFileInfo(page.component).path() != folder)
                continue;
            QString stem = QFileInfo(page.component).completeBaseName();
            if (stem.endsWith(QLatin1String("Page")))
                stem.chop(4);
            if (!stem.isEmpty() && base.startsWith(stem) && stem.size() > bestLength) {
                best = page.id;
                bestLength = stem.size();
            }
        }
    }
    return best;
}

struct Anchor {
    QString file;
    QString page; // empty when the file belongs to no page
    QString id;   // searchId
};

QList<Anchor> qmlAnchors(const Layout &layout)
{
    static const QRegularExpression searchId(QStringLiteral("\\bsearchId\\s*:\\s*\"([^\"]+)\""));
    QList<Anchor> anchors;
    const QString root = QStringLiteral(SOURCE_DIR "/src/qml");
    QDirIterator it(root, {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = QDir(root).relativeFilePath(path);
        // the components only declare the property
        if (relative.startsWith(QLatin1String("ui/")))
            continue;
        const QString text = QString::fromUtf8(readFile(path));
        auto matches = searchId.globalMatch(text);
        while (matches.hasNext())
            anchors.append({relative, pageOfFile(layout, relative), matches.next().captured(1)});
    }
    return anchors;
}

void report(const QString &what, const QStringList &missing)
{
    if (missing.isEmpty())
        return;
    const QString text = what + QLatin1String(":\n  ") + missing.join(QStringLiteral("\n  "));
    if (strict())
        QFAIL(qPrintable(text));
    qInfo("%s", qPrintable(text));
}

} // namespace

class TestSearchIndex : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fragmentsAreClean()
    {
        // a broken fragment, a row of a page that is not in the layout,
        // or a synonym that leads nowhere is a mistake today, not later
        SearchIndex index;
        QString error;
        QVERIFY2(index.load(packageSources(QStringLiteral("en")), &error), qPrintable(error));
        QVERIFY2(index.warnings().isEmpty(), qPrintable(index.warnings().join(QLatin1Char('\n'))));
    }

    void everyPageIsFoundByItsName_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("en");
        QTest::newRow("fr") << QStringLiteral("fr");
    }
    void everyPageIsFoundByItsName()
    {
        QFETCH(QString, language);
        SearchIndex index;
        QVERIFY(index.load(packageSources(language)));
        const Layout layout = parseLayout(readFile(QStringLiteral(LAYOUT_FILE)), language);
        QStringList lost;
        for (const Category &category : layout.categories) {
            for (const Page &page : category.pages) {
                bool found = false;
                for (const auto &r : index.search(page.name, 3))
                    found = found || r.id == page.id;
                if (!found)
                    lost.append(page.id + QLatin1String(" (") + page.name + QLatin1Char(')'));
            }
        }
        QVERIFY2(lost.isEmpty(), qPrintable(lost.join(QStringLiteral(", "))));
    }

    void anchorsMeet()
    {
        SearchIndex index;
        QVERIFY(index.load(packageSources(QStringLiteral("en"))));
        const Layout layout = parseLayout(readFile(QStringLiteral(LAYOUT_FILE)), QStringLiteral("en"));
        const QList<Anchor> anchors = qmlAnchors(layout);

        // every searchId of a page has its row in the index
        QStringList notIndexed;
        QSet<QString> drawn;
        for (const Anchor &a : anchors) {
            if (a.page.isEmpty()) {
                // a file of no page (the frame, a dialog): its row may be
                // under any page, but it must be somewhere
                bool somewhere = false;
                for (const SearchEntry &e : index.entries())
                    somewhere = somewhere || e.anchor == a.id;
                if (!somewhere)
                    notIndexed.append(a.file + QLatin1String(": ") + a.id);
                drawn.insert(a.id);
                continue;
            }
            const QString id = a.page + QLatin1Char('#') + a.id;
            drawn.insert(id);
            drawn.insert(a.id);
            if (!index.entry(id))
                notIndexed.append(a.file + QLatin1String(": ") + id);
        }
        report(QStringLiteral("searchId in a page, missing from data/search"), notIndexed);

        // every row of the index is drawn by its page
        QStringList notDrawn;
        for (const SearchEntry &e : index.entries()) {
            if (e.kind == SearchEntry::Row && !e.anchor.isEmpty() && !drawn.contains(e.id) && !drawn.contains(e.anchor))
                notDrawn.append(e.id);
        }
        report(QStringLiteral("row in data/search that no page draws"), notDrawn);
    }

    // a row can sit on a category's own page (Personalization's desktop style)
    void landingRows()
    {
        SearchIndex index;
        QVERIFY(index.load(packageSources(QStringLiteral("en"))));
        const SearchEntry *row = index.entry(QStringLiteral("personalization#desktop-style"));
        QVERIFY(row);
        QCOMPARE(row->categoryId, QStringLiteral("personalization"));
        QCOMPARE(row->context[0], QStringLiteral("Personalization"));
        bool found = false;
        for (const auto &r : index.search(QStringLiteral("windows 10 style"), 3))
            found = found || r.id == row->id;
        QVERIFY(found);
    }

    void synonymAnchorsMeet()
    {
        // synonyms.json may point at a row before the row lands: listed
        // until then
        const QJsonDocument doc = QJsonDocument::fromJson(
            readFile(QStringLiteral(SOURCE_DIR "/data/search/synonyms.json")));
        QVERIFY(doc.isObject());
        SearchIndex index;
        QVERIFY(index.load(packageSources(QStringLiteral("en"))));
        QStringList waiting;
        for (const QJsonValue &v : doc.object().value(QStringLiteral("targets")).toArray()) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (id.contains(QLatin1Char('#')) && !index.entry(id))
                waiting.append(id);
        }
        report(QStringLiteral("synonym targets waiting for their row"), waiting);
    }
};

QTEST_GUILESS_MAIN(TestSearchIndex)
#include "test_search_index.moc"
