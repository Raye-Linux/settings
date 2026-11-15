// SPDX-License-Identifier: GPL-3.0-or-later
// The copy of the module keywords the Start menu runner reads instead
// of the plugins: what makes it stale, what it refuses to read, and an
// index built from it answering exactly like one built from the
// plugins, in English and in French.
#include "runnermatch.h"
#include "searchcache.h"
#include "searchindex.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(path.section(QLatin1Char('/'), 0, -2));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
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

// what a module could say about itself, made up from its id so every
// page gets words of its own
ModuleKeywords madeUpKeywords(const QString &module)
{
    ModuleKeywords k;
    const QString word = module.mid(4).replace(QLatin1Char('_'), QLatin1Char(' '));
    k.english = {word + QStringLiteral(" tweak"), QStringLiteral("Module ") + word};
    k.french = {QStringLiteral("réglage ") + word};
    return k;
}

ModuleKeywordMap sample()
{
    ModuleKeywordMap words;
    words.insert(QStringLiteral("kcm_kscreen"), {{QStringLiteral("projector"), QStringLiteral("Display")},
                                                 {QStringLiteral("projecteur"), QStringLiteral("Affichage")}});
    words.insert(QStringLiteral("kcm_nothing"), {});
    return words;
}

QStringList answers(const SearchIndex &index, const QStringList &queries)
{
    QStringList out;
    for (const QString &q : queries) {
        for (const RunnerMatch &m : runnerMatches(index, q)) {
            out.append(QStringLiteral("%1|%2|%3|%4|%5|%6")
                           .arg(q, m.id, m.text, m.iconName)
                           .arg(m.categoryRelevance)
                           .arg(m.relevance));
        }
    }
    return out;
}

} // namespace

class TestSearchCache : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("raye-settings/module-keywords"));
        QVERIFY(writeModuleKeywords(file, QStringLiteral("abc"), sample()));
        const auto read = readModuleKeywords(file, QStringLiteral("abc"));
        QVERIFY(read);
        QCOMPARE(read->size(), 2);
        QCOMPARE(read->value(QStringLiteral("kcm_kscreen")).english, sample().value(QStringLiteral("kcm_kscreen")).english);
        QCOMPARE(read->value(QStringLiteral("kcm_kscreen")).french, sample().value(QStringLiteral("kcm_kscreen")).french);
        QVERIFY(read->contains(QStringLiteral("kcm_nothing")));
    }

    void staleOrBrokenFilesAreNotRead()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("module-keywords"));
        QVERIFY(!readModuleKeywords(file, QStringLiteral("abc")));
        QVERIFY(writeModuleKeywords(file, QStringLiteral("abc"), sample()));
        QVERIFY(!readModuleKeywords(file, QStringLiteral("other sources")));

        const QByteArray good = readFile(file);
        writeFile(file, good.left(good.size() - 3));
        QVERIFY(!readModuleKeywords(file, QStringLiteral("abc")));
        writeFile(file, good + QByteArray("x"));
        QVERIFY(!readModuleKeywords(file, QStringLiteral("abc")));
        QByteArray otherMagic = good;
        otherMagic[0] = 'X';
        writeFile(file, otherMagic);
        QVERIFY(!readModuleKeywords(file, QStringLiteral("abc")));

        // a count that would ask for gigabytes
        QByteArray huge;
        QDataStream stream(&huge, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        QDataStream head(good);
        head.setVersion(QDataStream::Qt_6_0);
        quint32 magic = 0;
        quint32 format = 0;
        head >> magic >> format;
        stream << magic << format << QStringLiteral("abc") << qint32(2000000000);
        writeFile(file, huge);
        QVERIFY(!readModuleKeywords(file, QStringLiteral("abc")));
    }

    void stampFollowsTheSources()
    {
        QTemporaryDir dir;
        const QString layout = dir.filePath(QStringLiteral("layout.json"));
        const QString plugins = dir.filePath(QStringLiteral("plugins"));
        writeFile(layout, "{}");
        writeFile(plugins + QStringLiteral("/systemsettings/kcm_a.so"), "a");
        const QStringList roots = {layout, plugins, dir.filePath(QStringLiteral("missing"))};

        const QString first = moduleKeywordStamp(roots);
        QCOMPARE(moduleKeywordStamp(roots), first);

        writeFile(layout, "{ }");
        const QString second = moduleKeywordStamp(roots);
        QVERIFY(second != first);

        // a module two folders down comes, then changes
        writeFile(plugins + QStringLiteral("/kinfocenter/kcm_b.so"), "b");
        const QString third = moduleKeywordStamp(roots);
        QVERIFY(third != second);
        writeFile(plugins + QStringLiteral("/kinfocenter/kcm_b.so"), "bigger");
        QVERIFY(moduleKeywordStamp(roots) != third);

        writeFile(dir.filePath(QStringLiteral("missing")), "now here");
        QVERIFY(moduleKeywordStamp(roots) != third);
    }

    void cacheIsUsedWhenItMatches()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("module-keywords"));
        QVERIFY(writeModuleKeywords(file, QStringLiteral("abc"), sample()));
        int rebuilt = 0;
        const auto words = cachedModuleKeywords(file, QStringLiteral("abc"), [&] { ++rebuilt; });
        QVERIFY(words);
        QCOMPARE(rebuilt, 0);
        QCOMPARE(words->value(QStringLiteral("kcm_kscreen")).english.first(), QStringLiteral("projector"));
    }

    void staleCacheIsRebuiltOnce()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("module-keywords"));
        QVERIFY(writeModuleKeywords(file, QStringLiteral("old"), {}));
        int rebuilt = 0;
        const auto words = cachedModuleKeywords(file, QStringLiteral("new"), [&] {
            ++rebuilt;
            writeModuleKeywords(file, QStringLiteral("new"), sample());
        });
        QCOMPARE(rebuilt, 1);
        QVERIFY(words);
        QCOMPARE(words->size(), 2);

        // the rebuild could not write: nothing, the caller reads the plugins
        rebuilt = 0;
        QVERIFY(!cachedModuleKeywords(file, QStringLiteral("newer"), [&] { ++rebuilt; }));
        QCOMPARE(rebuilt, 1);
    }

    void recordedKeywordsGiveTheSameAnswers_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("en");
        QTest::newRow("fr") << QStringLiteral("fr");
    }

    void recordedKeywordsGiveTheSameAnswers()
    {
        QFETCH(QString, language);
        SearchIndex::Sources direct = packageSources(language);
        direct.moduleKeywords = &madeUpKeywords;
        SearchIndex fromPlugins;
        QVERIFY(fromPlugins.load(direct));

        const ModuleKeywordMap recorded = recordModuleKeywords(direct);
        QVERIFY(recorded.contains(QStringLiteral("kcm_kscreen")));
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("module-keywords"));
        QVERIFY(writeModuleKeywords(file, QStringLiteral("s"), recorded));
        const auto read = readModuleKeywords(file, QStringLiteral("s"));
        QVERIFY(read);

        SearchIndex::Sources cached = packageSources(language);
        cached.moduleKeywords = moduleKeywordsFrom(*read);
        SearchIndex fromCache;
        QVERIFY(fromCache.load(cached));

        const QStringList queries = {
            QStringLiteral("display"), QStringLiteral("kscreen tweak"), QStringLiteral("nightlight"),
            QStringLiteral("pulseaudio"), QStringLiteral("réglage notifications"), QStringLiteral("module cpu"),
            QStringLiteral("wifi"), QStringLiteral("écran"), QStringLiteral("souris"), QStringLiteral("blutooth"),
            QStringLiteral("mode sombre"), QStringLiteral("energyinfo"), QStringLiteral("réglage usb"),
        };
        const QStringList expected = answers(fromPlugins, queries);
        QVERIFY(expected.size() > queries.size());
        QCOMPARE(answers(fromCache, queries), expected);
    }
};

QTEST_GUILESS_MAIN(TestSearchCache)
#include "test_search_cache.moc"
