// SPDX-License-Identifier: GPL-3.0-or-later
// The search of Settings: how words are compared, how results are
// ranked in English and in French, the suggestions model, the answers
// the Start menu gets, and the speed on a big index.
#include "router.h"
#include "runnermatch.h"
#include "searchindex.h"
#include "searchmodel.h"
#include "searchtext.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <limits>

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// rows a page package would install, as data/search/<page>.json (an
// apostrophe is written \u0027: moc reads it as a character literal in a
// raw string)
const QByteArray displayRows = R"({"version": 1, "entries": [
  {"id": "system/display#night-light", "label": "Night light", "label_fr": "Éclairage nocturne",
   "keywords": ["blue light", "warm"], "keywords_fr": ["lumière bleue", "soir"]},
  {"id": "system/display#scale", "label": "Scale", "label_fr": "Mise à l\u0027échelle",
   "keywords": ["bigger", "zoom", "dpi"], "keywords_fr": ["agrandir"]},
  {"id": "system/display#resolution", "label": "Display resolution", "label_fr": "Résolution de l\u0027écran"}
]})";
const QByteArray recoveryRows = R"([
  {"id": "system/recovery#restart-windows", "label": "Restart to Windows", "label_fr": "Redémarrer sous Windows"}
])";

// a fragment of one row (built here: moc loses the test class after a
// raw string with an unbalanced brace in this file)
QByteArray badRow(const char *id, const char *label)
{
    return QByteArray("[{\"id\": \"") + id + "\", \"label\": \"" + label + "\"}]";
}

SearchIndex::Sources sources(const QString &language, bool withRows = true)
{
    SearchIndex::Sources s;
    s.layout = readFile(QStringLiteral(LAYOUT_FILE));
    s.synonyms = readFile(QStringLiteral(SOURCE_DIR "/data/search/synonyms.json"));
    if (withRows)
        s.fragments = {{QStringLiteral("display.json"), displayRows}, {QStringLiteral("recovery.json"), recoveryRows}};
    s.language = language;
    return s;
}

QString first(const SearchIndex &index, const QString &query)
{
    const auto results = index.search(query, 1);
    return results.isEmpty() ? QString() : results.first().id;
}

} // namespace

class TestSearch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalize_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("accents") << QStringLiteral("Écran de verrouillage") << QStringLiteral("ecran de verrouillage");
        QTest::newRow("hyphen") << QStringLiteral("Wi-Fi") << QStringLiteral("wi fi");
        QTest::newRow("apostrophe") << QStringLiteral("Arrière-plan d’écran") << QStringLiteral("arriere plan d ecran");
        QTest::newRow("ligature") << QStringLiteral("Œuvre ÆSIR straße") << QStringLiteral("oeuvre aesir strasse");
        QTest::newRow("spaces") << QStringLiteral("  Power  &  battery ") << QStringLiteral("power battery");
        QTest::newRow("decomposed") << QStringLiteral("écran") << QStringLiteral("ecran");
        QTest::newRow("empty") << QStringLiteral(" - ") << QString();
    }
    void normalize()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(SearchText::normalize(text), expected);
    }

    void words()
    {
        QCOMPARE(SearchText::words(QStringLiteral("Night-light, warm")),
                 QStringList({QStringLiteral("night"), QStringLiteral("light"), QStringLiteral("warm")}));
        QVERIFY(SearchText::words(QString()).isEmpty());
    }

    void editDistance()
    {
        using SearchText::withinOneEdit;
        QVERIFY(withinOneEdit(QStringLiteral("bluetooth"), QStringLiteral("bluetooth")));
        QVERIFY(withinOneEdit(QStringLiteral("blutooth"), QStringLiteral("bluetooth")));   // missing
        QVERIFY(withinOneEdit(QStringLiteral("bluettooth"), QStringLiteral("bluetooth"))); // extra
        QVERIFY(withinOneEdit(QStringLiteral("bluetoith"), QStringLiteral("bluetooth")));  // changed
        QVERIFY(withinOneEdit(QStringLiteral("bluetooht"), QStringLiteral("bluetooth")));  // swapped
        QVERIFY(!withinOneEdit(QStringLiteral("bltooh"), QStringLiteral("bluetooth")));
        QVERIFY(!withinOneEdit(QStringLiteral("abcd"), QStringLiteral("badc")));
    }

    void matchWord()
    {
        using SearchText::WordMatch;
        QCOMPARE(SearchText::matchWord(QStringLiteral("display"), QStringLiteral("display")), WordMatch::Exact);
        QCOMPARE(SearchText::matchWord(QStringLiteral("blu"), QStringLiteral("bluetooth")), WordMatch::Prefix);
        QCOMPARE(SearchText::matchWord(QStringLiteral("blutooth"), QStringLiteral("bluetooth")), WordMatch::Fuzzy);
        // a typo inside the start of a longer word
        QCOMPARE(SearchText::matchWord(QStringLiteral("notifc"), QStringLiteral("notifications")), WordMatch::Fuzzy);
        // short words never carry a typo: "son" is not "sun"
        QCOMPARE(SearchText::matchWord(QStringLiteral("son"), QStringLiteral("sun")), WordMatch::None);
        QCOMPARE(SearchText::matchWord(QString(), QStringLiteral("x")), WordMatch::None);
    }

    void highlight()
    {
        QCOMPARE(SearchText::highlight(QStringLiteral("Bluetooth & devices"), {QStringLiteral("blu")}),
                 QStringLiteral("<b>Blu</b>etooth &amp; devices"));
        QCOMPARE(SearchText::highlight(QStringLiteral("Écran de verrouillage"), {QStringLiteral("ecran")}),
                 QStringLiteral("<b>Écran</b> de verrouillage"));
        QCOMPARE(SearchText::highlight(QStringLiteral("Night light"), {QStringLiteral("night"), QStringLiteral("li")}),
                 QStringLiteral("<b>Night</b> <b>li</b>ght"));
        QCOMPARE(SearchText::highlight(QStringLiteral("<script>"), {QStringLiteral("x")}),
                 QStringLiteral("&lt;script&gt;"));
    }

    void loads()
    {
        SearchIndex index;
        QString error;
        QVERIFY2(index.load(sources(QStringLiteral("en")), &error), qPrintable(error));
        QVERIFY2(index.warnings().isEmpty(), qPrintable(index.warnings().join(QLatin1Char('\n'))));
        QVERIFY(index.entry(QStringLiteral("home")));
        QVERIFY(index.entry(QStringLiteral("system")));
        QVERIFY(index.entry(QStringLiteral("system/display")));
        QCOMPARE(index.entry(QStringLiteral("system/display#night-light"))->anchor, QStringLiteral("night-light"));
        QCOMPARE(index.entry(QStringLiteral("system/display#night-light"))->icon, QStringLiteral("org.raye.settings-display"));
    }

    void badSources()
    {
        SearchIndex index;
        QString error;
        SearchIndex::Sources s;
        s.layout = "{";
        QVERIFY(!index.load(s, &error));
        QVERIFY(!error.isEmpty());

        s = sources(QStringLiteral("en"), false);
        s.fragments = {{QStringLiteral("broken.json"), QByteArray("not json")},
                       {QStringLiteral("stray.json"), badRow("nowhere/page#x", "X")},
                       {QStringLiteral("nolabel.json"), badRow("system/display#y", "")},
                       {QStringLiteral("twice.json"), badRow("system/display", "Again")}};
        QVERIFY(index.load(s, &error));
        QCOMPARE(index.warnings().size(), 4);
        QVERIFY(!index.entry(QStringLiteral("nowhere/page#x")));
        QCOMPARE(index.entry(QStringLiteral("system/display"))->label[0], QStringLiteral("Display"));
    }

    // The table of the spec (3.5, acceptance): what is typed, what must
    // come first.
    void ranking_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("expected");
        const QString en = QStringLiteral("en");
        const QString fr = QStringLiteral("fr");
        QTest::newRow("dark") << en << QStringLiteral("dark") << QStringLiteral("personalization/colors");
        QTest::newRow("dark mode") << en << QStringLiteral("Dark mode") << QStringLiteral("personalization/colors");
        QTest::newRow("wifi") << en << QStringLiteral("wifi") << QStringLiteral("network/wifi");
        QTest::newRow("wi-fi") << en << QStringLiteral("Wi-Fi") << QStringLiteral("network/wifi");
        QTest::newRow("wlan") << en << QStringLiteral("wlan") << QStringLiteral("network/wifi");
        QTest::newRow("écran") << en << QStringLiteral("écran") << QStringLiteral("system/display");
        QTest::newRow("ecran") << fr << QStringLiteral("ecran") << QStringLiteral("system/display");
        QTest::newRow("blutooth") << en << QStringLiteral("blutooth") << QStringLiteral("devices/bluetooth");
        QTest::newRow("blu") << en << QStringLiteral("blu") << QStringLiteral("devices/bluetooth");
        QTest::newRow("night light") << en << QStringLiteral("night light") << QStringLiteral("system/night-light");
        QTest::newRow("clavier") << fr << QStringLiteral("clavier") << QStringLiteral("accessibility/keyboard");
        QTest::newRow("clavier, English") << en << QStringLiteral("clavier") << QStringLiteral("accessibility/keyboard");
        QTest::newRow("restart windows") << en << QStringLiteral("restart windows")
                                         << QStringLiteral("system/recovery#restart-windows");
        QTest::newRow("tpm") << en << QStringLiteral("tpm") << QStringLiteral("privacy/device-security");
        QTest::newRow("display, French") << fr << QStringLiteral("display") << QStringLiteral("system/display");
        QTest::newRow("son") << fr << QStringLiteral("son") << QStringLiteral("system/sound");
        QTest::newRow("sombre") << fr << QStringLiteral("mode sombre") << QStringLiteral("personalization/colors");
        QTest::newRow("control panel") << en << QStringLiteral("control panel") << QStringLiteral("home");
        QTest::newRow("typo") << en << QStringLiteral("notifcations") << QStringLiteral("system/notifications");
        QTest::newRow("swap") << en << QStringLiteral("bluetooht") << QStringLiteral("devices/bluetooth");
        QTest::newRow("row keyword") << en << QStringLiteral("blue light") << QStringLiteral("system/night-light");
        QTest::newRow("row") << en << QStringLiteral("scale") << QStringLiteral("system/display#scale");
        QTest::newRow("row, French") << fr << QStringLiteral("mise a l'echelle") << QStringLiteral("system/display#scale");
        QTest::newRow("system") << en << QStringLiteral("System") << QStringLiteral("system");
        QTest::newRow("prefix") << en << QStringLiteral("dis") << QStringLiteral("system/display");
        QTest::newRow("windows exe") << en << QStringLiteral("exe") << QStringLiteral("gaming/windows-programs");
        QTest::newRow("same name, layout order") << fr << QStringLiteral("souris") << QStringLiteral("devices/mouse");
        QTest::newRow("uninstall") << en << QStringLiteral("uninstall a program") << QStringLiteral("apps/installed");
    }
    void ranking()
    {
        QFETCH(QString, language);
        QFETCH(QString, query);
        QFETCH(QString, expected);
        SearchIndex index;
        QVERIFY(index.load(sources(language)));
        const auto results = index.search(query, 5);
        QStringList ids;
        for (const auto &r : results)
            ids.append(QStringLiteral("%1 (%2)").arg(r.id).arg(r.score));
        QVERIFY2(!results.isEmpty() && results.first().id == expected, qPrintable(ids.join(QStringLiteral(", "))));
    }

    void noTypoForAKnownWord()
    {
        // "night" is in the index: "right" and "light" are not typos of it
        SearchIndex::Sources s = sources(QStringLiteral("en"), false);
        s.moduleKeywords = [](const QString &module) {
            ModuleKeywords k;
            if (module == QLatin1String("kcm_mouse"))
                k.english = {QStringLiteral("right handed")};
            return k;
        };
        SearchIndex index;
        QVERIFY(index.load(s));
        for (const auto &r : index.search(QStringLiteral("night")))
            QVERIFY2(r.id != QLatin1String("devices/mouse"), qPrintable(r.id));
        QCOMPARE(first(index, QStringLiteral("rigth")), QStringLiteral("devices/mouse"));
    }

    void synonymsStandForNames()
    {
        // "wifi" is also "sans fil": that must not find fonts by "sans"
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("en"))));
        for (const auto &r : index.search(QStringLiteral("wifi")))
            QVERIFY2(r.id != QLatin1String("personalization/fonts"), qPrintable(r.id));
    }

    void anchorTargetFallsBackToPage()
    {
        // before the Recovery page lands its rows, its target is the page
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("en"), false)));
        QCOMPARE(first(index, QStringLiteral("restart windows")), QStringLiteral("system/recovery"));
    }

    void nothingFound()
    {
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("en"))));
        QVERIFY(index.search(QStringLiteral("zzqqxxj")).isEmpty());
        QVERIFY(index.search(QStringLiteral("  ")).isEmpty());
        // every word must match somewhere
        QVERIFY(index.search(QStringLiteral("bluetooth zzqqxxj")).isEmpty());
    }

    void resultFields()
    {
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("fr"))));
        const auto rows = index.search(QStringLiteral("eclairage"), 3);
        QVERIFY(!rows.isEmpty());
        QCOMPARE(rows.first().id, QStringLiteral("system/night-light"));
        QCOMPARE(rows.first().label, QStringLiteral("Éclairage nocturne"));
        QCOMPARE(rows.first().breadcrumb, QStringLiteral("Système"));
        QCOMPARE(rows.first().highlighted, QStringLiteral("<b>Éclairage</b> nocturne"));
        const SearchResult *row = nullptr;
        for (const auto &r : rows) {
            if (r.id == QLatin1String("system/display#night-light"))
                row = &r;
        }
        QVERIFY(row);
        QCOMPARE(row->breadcrumb, QStringLiteral("Système › Écran"));
        QCOMPARE(row->categoryName, QStringLiteral("Système"));
        QCOMPARE(row->anchor, QStringLiteral("night-light"));
    }

    void hiddenPages()
    {
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("en"))));
        QCOMPARE(first(index, QStringLiteral("graphics")), QStringLiteral("system/graphics"));
        index.setAvailability([](const QString &requirement) {
            return requirement != QLatin1String("two-gpus");
        });
        for (const auto &r : index.search(QStringLiteral("graphics")))
            QVERIFY(r.id != QLatin1String("system/graphics"));
    }

    void wifiRowsNeedAWifiCard()
    {
        SearchIndex::Sources s = sources(QStringLiteral("en"), false);
        s.fragments = {{QStringLiteral("network.json"), readFile(QStringLiteral(SOURCE_DIR "/data/search/network.json"))}};
        SearchIndex index;
        QVERIFY(index.load(s));
        QVERIFY2(index.warnings().isEmpty(), qPrintable(index.warnings().join(QLatin1Char('\n'))));
        Requirements pc;
        pc.set(QStringLiteral("wifi"), false);
        pc.set(QStringLiteral("bluetooth"), true);
        index.setAvailability([&pc](const QString &requirement) { return pc.has(requirement); });
        bool airplane = false;
        for (const auto &r : index.search(QStringLiteral("airplane"))) {
            QVERIFY2(r.id != QLatin1String("network/airplane-mode#airplane-wifi"), qPrintable(r.id));
            airplane = airplane || r.id == QLatin1String("network/airplane-mode#airplane-bluetooth");
        }
        QVERIFY(airplane);
        for (const auto &r : index.search(QStringLiteral("wifi")))
            QVERIFY2(!r.id.startsWith(QLatin1String("network/wifi")), qPrintable(r.id));

        // no radio at all: no airplane mode either
        pc.set(QStringLiteral("bluetooth"), false);
        for (const auto &r : index.search(QStringLiteral("airplane")))
            QVERIFY2(!r.id.startsWith(QLatin1String("network/airplane-mode")), qPrintable(r.id));
    }

    void moduleKeywords()
    {
        SearchIndex::Sources s = sources(QStringLiteral("en"), false);
        s.moduleKeywords = [](const QString &module) {
            ModuleKeywords k;
            if (module == QLatin1String("kcm_kscreen"))
                k.english = {QStringLiteral("projector")};
            return k;
        };
        SearchIndex index;
        QVERIFY(index.load(s));
        QCOMPARE(first(index, QStringLiteral("projector")), QStringLiteral("system/display"));
    }

    void installedModuleKeywords()
    {
        const ModuleKeywords k = SearchIndex::installedModuleKeywords(QStringLiteral("kcm_colors"));
        if (k.english.isEmpty())
            QSKIP("the colours module is not installed here");
        QVERIFY(k.english.size() > 1);
        // never a path or anything that is not a module id
        QVERIFY(SearchIndex::installedModuleKeywords(QStringLiteral("../kcm_colors")).english.isEmpty());
    }

    void model()
    {
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("en"))));
        SearchModel model;
        model.setIndex(&index);
        model.setLimit(8);
        QSignalSpy counted(&model, &SearchModel::countChanged);
        model.setQuery(QStringLiteral("dis"));
        QVERIFY(model.pending());
        QCOMPARE(model.count(), 0);
        QVERIFY(counted.wait(1000));
        QVERIFY(!model.pending());
        QVERIFY(model.count() > 0 && model.count() <= 8);
        QCOMPARE(model.entryIdAt(0), QStringLiteral("system/display"));
        QCOMPARE(model.data(model.index(0), SearchModel::HighlightedRole).toString(), QStringLiteral("<b>Dis</b>play"));
        QCOMPARE(model.roleNames().value(SearchModel::BreadcrumbRole), QByteArray("breadcrumb"));
        QCOMPARE(model.entryIdAt(99), QString());

        // an empty box empties at once
        model.setQuery(QString());
        QCOMPARE(model.count(), 0);
        QVERIFY(!model.pending());
    }

    void groupedModel()
    {
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("en"))));
        SearchModel model;
        model.setIndex(&index);
        model.setGrouped(true);
        model.setQuery(QStringLiteral("mouse"));
        model.searchNow();
        QVERIFY(model.count() > 2);
        // one category's results are together
        QStringList seen;
        QString last;
        for (int i = 0; i < model.count(); ++i) {
            const QString category = model.data(model.index(i), SearchModel::CategoryNameRole).toString();
            if (category != last) {
                QVERIFY2(!seen.contains(category), qPrintable(category));
                seen.append(category);
                last = category;
            }
        }
    }

    void runner()
    {
        SearchIndex index;
        QVERIFY(index.load(sources(QStringLiteral("en"))));
        QVERIFY(runnerMatches(index, QStringLiteral("w")).isEmpty());
        const auto matches = runnerMatches(index, QStringLiteral("wifi"));
        QVERIFY(!matches.isEmpty());
        QCOMPARE(matches.first().id, QStringLiteral("network/wifi"));
        QCOMPARE(matches.first().text, QStringLiteral("Wi-Fi"));
        QCOMPARE(matches.first().iconName, QStringLiteral("org.raye.settings-wifi"));
        QCOMPARE(matches.first().categoryRelevance, 70);
        QVERIFY(matches.first().relevance > 0 && matches.first().relevance <= 1);
        QVERIFY(matches.first().properties.value(QStringLiteral("subtext")).toString().contains(QStringLiteral("Network")));
        QVERIFY(runnerMatches(index, QStringLiteral("display"), 3).size() <= 3);

        QString program;
        QStringList arguments;
        QVERIFY(runnerCommand(index, QStringLiteral("system/display#night-light"), &program, &arguments));
        QVERIFY(program.endsWith(QStringLiteral("raye-settings")));
        QCOMPARE(arguments, QStringList({QStringLiteral("--page"), QStringLiteral("system/display#night-light")}));
        // an id from outside is opened only when the index knows it
        QVERIFY(!runnerCommand(index, QStringLiteral("network/wifi; rm -rf ~"), &program, &arguments));
        QVERIFY(!runnerCommand(index, QStringLiteral("--help"), &program, &arguments));
        QVERIFY(!runnerCommand(index, QString(), &program, &arguments));
    }

    // 3.5: results in less than 16 ms on 2 000 entries
    void speed()
    {
        QByteArray rows = "[";
        const QStringList pages = {QStringLiteral("system/display"), QStringLiteral("network/wifi"),
                                   QStringLiteral("devices/mouse"), QStringLiteral("personalization/taskbar")};
        int n = 0;
        while (n < 1900) {
            if (n)
                rows += ',';
            rows += QStringLiteral(R"({"id": "%1#row-%2", "label": "Setting number %2 of the long list",
                "label_fr": "Paramètre numéro %2 de la longue liste",
                "keywords": ["option %2", "choice", "advanced value"], "keywords_fr": ["réglage %2", "valeur"]})")
                        .arg(pages.at(n % pages.size()))
                        .arg(n)
                        .toUtf8();
            ++n;
        }
        rows += "]";
        SearchIndex::Sources s = sources(QStringLiteral("fr"));
        s.fragments.append({QStringLiteral("big.json"), rows});
        SearchIndex index;
        QVERIFY(index.load(s));
        QVERIFY(index.entries().size() >= 2000);
        const QStringList queries = {QStringLiteral("a"), QStringLiteral("setting"), QStringLiteral("blutooth"),
                                     QStringLiteral("parametre numero 1234"), QStringLiteral("app"),
                                     QStringLiteral("wifi"), QStringLiteral("advanced valeu"), QStringLiteral("zzqqxx"),
                                     QStringLiteral("night light"), QStringLiteral("long list option")};
        qint64 worst = 0;
        for (const QString &q : queries) {
            // as the suggestions ask (8), and as the results page asks (all)
            // (best of three runs: a busy build machine must not fail it)
            for (int limit : {8, 0}) {
                qint64 best = std::numeric_limits<qint64>::max();
                for (int run = 0; run < 3; ++run) {
                    index.search(q, limit);
                    best = qMin(best, index.lastSearchMicroseconds());
                }
                worst = qMax(worst, best);
            }
        }
        qInfo("slowest search on %lld entries: %lld µs", qlonglong(index.entries().size()), qlonglong(worst));
#ifdef __OPTIMIZE__
        // what ships, and what the package build tests
        const qint64 budget = 16000;
#else
        // a build without optimisation runs this about ten times slower
        const qint64 budget = 4 * 16000;
#endif
        QVERIFY2(worst < budget, qPrintable(QStringLiteral("%1 µs, budget %2 µs").arg(worst).arg(budget)));
    }
};

QTEST_GUILESS_MAIN(TestSearch)
#include "test_search.moc"
