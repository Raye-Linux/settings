// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchindex.h"

#include "layout.h"
#include "router.h"
#include "searchtext.h"

#include <KPluginMetaData>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

namespace {

constexpr int En = 0;
constexpr int Fr = 1;

// what a fragment or the synonyms may weigh: a page has a few dozen
// rows, nobody needs a megabyte
constexpr qint64 maxFileSize = 1024 * 1024;
constexpr int maxVariants = 12;

std::function<ModuleKeywords(const QString &)> s_moduleKeywordSource;

// ranking, best first (see searchindex.h)
constexpr int scoreTarget = 1100;
// below a page or category whose name starts with what was typed
constexpr int scoreTargetPrefix = 840;
constexpr int scoreLabelExact = 1000;
constexpr int scoreLabelPrefix = 850;
constexpr int scoreLabelWords = 700;
constexpr int scoreKeyword = 500;
constexpr int scoreContext = 300;
constexpr int scoreFuzzyBase = 100;
constexpr int otherLanguagePenalty = 40;
constexpr int synonymPenalty = 5;

int kindBonus(SearchEntry::Kind kind)
{
    switch (kind) {
    case SearchEntry::Page: return 20;
    case SearchEntry::Category: return 10;
    case SearchEntry::Home: return 10;
    case SearchEntry::Row: return 0;
    }
    return 0;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList list;
    if (value.isString()) {
        // "a, b; c" as module metadata writes it
        const auto parts = value.toString().split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString s = part.trimmed();
            if (!s.isEmpty())
                list.append(s);
        }
        return list;
    }
    for (const QJsonValue &v : value.toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty())
            list.append(s);
    }
    return list;
}

void appendWords(QStringList &into, const QStringList &phrases)
{
    for (const QString &phrase : phrases) {
        const QStringList ws = SearchText::words(phrase);
        for (const QString &w : ws) {
            if (!into.contains(w))
                into.append(w);
        }
    }
}

SearchText::WordMatch bestIn(const QList<quint8> &matches, const QList<int> &words)
{
    quint8 best = 0;
    for (int id : words) {
        best = qMax(best, matches.at(id));
        if (best == quint8(SearchText::WordMatch::Exact))
            break;
    }
    return SearchText::WordMatch(best);
}

// " a b " contains " b ": whole words only
qsizetype findWords(const QString &haystack, const QString &needle)
{
    const QString h = QLatin1Char(' ') + haystack + QLatin1Char(' ');
    const qsizetype at = h.indexOf(QLatin1Char(' ') + needle + QLatin1Char(' '));
    return at;
}

// Is every typed word, in order, the start of the next words of `term`?
bool startsTerm(const QStringList &typed, const QStringList &term)
{
    if (typed.isEmpty() || typed.size() > term.size())
        return false;
    for (qsizetype i = 0; i < typed.size(); ++i) {
        if (i + 1 < typed.size() ? typed.at(i) != term.at(i) : !term.at(i).startsWith(typed.at(i)))
            return false;
    }
    return true;
}

QByteArray readSmallFile(const QString &path)
{
    QFile file(path);
    if (file.size() > maxFileSize || !file.open(QIODevice::ReadOnly))
        return {};
    return file.read(maxFileSize);
}

// The system's data folders, never the one in the home folder.
QStringList systemDataDirs()
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList dirs;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        if (QDir::cleanPath(dir) != QDir::cleanPath(home))
            dirs.append(dir);
    }
    return dirs;
}

} // namespace

const SearchEntry *SearchIndex::entry(const QString &id) const
{
    const auto it = m_byId.constFind(id);
    return it == m_byId.constEnd() ? nullptr : &m_entries.at(it.value());
}

void SearchIndex::setAvailability(std::function<bool(const QString &)> available)
{
    m_available = std::move(available);
}

QList<int> SearchIndex::wordIds(const QStringList &words)
{
    QList<int> ids;
    ids.reserve(words.size());
    for (const QString &word : words) {
        auto it = m_wordIds.constFind(word);
        if (it == m_wordIds.constEnd()) {
            it = m_wordIds.insert(word, int(m_words.size()));
            m_words.append(word);
        }
        if (!ids.contains(it.value()))
            ids.append(it.value());
    }
    return ids;
}

SearchIndex::WordMatches SearchIndex::matchesFor(const QString &typed) const
{
    WordMatches matches(m_words.size(), 0);
    bool plain = false;
    for (qsizetype i = 0; i < m_words.size(); ++i) {
        matches[i] = quint8(SearchText::matchWord(typed, m_words.at(i)));
        plain = plain || matches[i] >= quint8(SearchText::WordMatch::Prefix);
    }
    // a word the index has is not a typo: "night" must not bring every
    // "light" and "right" along
    if (plain) {
        for (quint8 &m : matches) {
            if (m == quint8(SearchText::WordMatch::Fuzzy))
                m = quint8(SearchText::WordMatch::None);
        }
    }
    return matches;
}

void SearchIndex::addEntry(const SearchEntry &entry)
{
    if (entry.id.isEmpty() || m_byId.contains(entry.id)) {
        m_warnings.append(QStringLiteral("%1 is in the index twice").arg(entry.id));
        return;
    }
    m_byId.insert(entry.id, int(m_entries.size()));
    m_entries.append(entry);
    for (int lang : {En, Fr}) {
        Prepared p;
        const QString label = entry.label[lang].isEmpty() ? entry.label[En] : entry.label[lang];
        p.label = SearchText::normalize(label);
        p.labelWords = wordIds(SearchText::words(label));
        QStringList keywordWords;
        appendWords(keywordWords, entry.keywords[lang]);
        p.keywordWords = wordIds(keywordWords);
        p.contextWords = wordIds(SearchText::words(entry.context[lang]));
        m_prepared[lang].append(p);
    }
}

bool SearchIndex::load(const Sources &sources, QString *error)
{
    m_entries.clear();
    m_prepared[En].clear();
    m_prepared[Fr].clear();
    m_byId.clear();
    m_words.clear();
    m_wordIds.clear();
    m_equivalents.clear();
    m_targets.clear();
    m_warnings.clear();
    for (int lang : {En, Fr}) {
        m_categoryNames[lang].clear();
        m_pageNames[lang].clear();
    }
    m_french = sources.language.startsWith(QLatin1String("fr"));

    QString layoutError;
    const Layout layouts[2] = {parseLayout(sources.layout, QStringLiteral("en"), &layoutError),
                               parseLayout(sources.layout, QStringLiteral("fr"))};
    if (layouts[En].isEmpty()) {
        if (error)
            *error = layoutError.isEmpty() ? QStringLiteral("the layout has no category") : layoutError;
        return false;
    }

    SearchEntry home;
    home.id = home.pageId = QStringLiteral("home");
    home.kind = SearchEntry::Home;
    home.label[En] = QStringLiteral("Home");
    home.label[Fr] = QStringLiteral("Accueil");
    home.keywords[En] = {QStringLiteral("start page")};
    home.keywords[Fr] = {QStringLiteral("page d'accueil")};
    home.icon = QStringLiteral("org.raye.settings-home");
    home.iconFallback = QStringLiteral("go-home");
    addEntry(home);
    m_pageNames[En].insert(home.id, home.label[En]);
    m_pageNames[Fr].insert(home.id, home.label[Fr]);

    for (qsizetype c = 0; c < layouts[En].categories.size(); ++c) {
        const Category &category = layouts[En].categories.at(c);
        const Category &frCategory = layouts[Fr].categories.at(c);
        m_categoryNames[En].insert(category.id, category.name);
        m_categoryNames[Fr].insert(category.id, frCategory.name);

        SearchEntry entry;
        entry.id = entry.pageId = entry.categoryId = category.id;
        entry.kind = SearchEntry::Category;
        entry.label[En] = category.name;
        entry.label[Fr] = frCategory.name;
        entry.keywords[En] = QStringList{category.summary};
        entry.keywords[Fr] = QStringList{frCategory.summary};
        entry.icon = category.icon;
        entry.iconFallback = category.iconFallback;
        // A category whose landing page is also one of its pages, under the
        // same name, would show twice ("Raye Update" twice): the page stays.
        bool landingIsAPage = false;
        for (const Page &page : category.pages) {
            if (!category.landing.isEmpty() && page.component == category.landing && page.name == category.name)
                landingIsAPage = true;
        }
        if (!landingIsAPage)
            addEntry(entry);

        for (qsizetype p = 0; p < category.pages.size(); ++p) {
            const Page &page = category.pages.at(p);
            const Page &frPage = frCategory.pages.at(p);
            m_pageNames[En].insert(page.id, page.name);
            m_pageNames[Fr].insert(page.id, frPage.name);

            SearchEntry pageEntry;
            pageEntry.id = pageEntry.pageId = page.id;
            pageEntry.categoryId = category.id;
            pageEntry.kind = SearchEntry::Page;
            pageEntry.label[En] = page.name;
            pageEntry.label[Fr] = frPage.name;
            pageEntry.keywords[En] = QStringList{page.summary};
            pageEntry.keywords[Fr] = QStringList{frPage.summary};
            pageEntry.context[En] = category.name;
            pageEntry.context[Fr] = frCategory.name;
            pageEntry.icon = page.icon;
            pageEntry.iconFallback = page.iconFallback;
            pageEntry.requirement = page.requirement;
            if (sources.moduleKeywords) {
                QStringList modules = page.aliases;
                if (page.kcm.startsWith(QLatin1String("kcm_")))
                    modules.prepend(page.kcm);
                for (const QString &module : std::as_const(modules)) {
                    const ModuleKeywords words = sources.moduleKeywords(module);
                    pageEntry.keywords[En] += words.english;
                    pageEntry.keywords[Fr] += words.french;
                }
            }
            addEntry(pageEntry);
        }
    }

    for (const Fragment &fragment : sources.fragments) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(fragment.json, &parseError);
        if (doc.isNull()) {
            m_warnings.append(QStringLiteral("%1: %2").arg(fragment.name, parseError.errorString()));
            continue;
        }
        const QJsonArray rows = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &value : rows) {
            const QJsonObject o = value.toObject();
            SearchEntry row;
            row.id = o.value(QStringLiteral("id")).toString();
            row.pageId = row.id.section(QLatin1Char('#'), 0, 0);
            row.anchor = row.id.section(QLatin1Char('#'), 1);
            row.kind = SearchEntry::Row;
            row.label[En] = o.value(QStringLiteral("label")).toString();
            row.label[Fr] = o.value(QStringLiteral("label_fr")).toString();
            row.keywords[En] = stringList(o.value(QStringLiteral("keywords")));
            row.keywords[Fr] = stringList(o.value(QStringLiteral("keywords_fr")));
            if (row.id.isEmpty() || row.label[En].isEmpty()) {
                m_warnings.append(QStringLiteral("%1: an entry without id or label").arg(fragment.name));
                continue;
            }
            const bool atHome = row.pageId == QLatin1String("home") || row.pageId.startsWith(QLatin1String("home/"));
            const Page *page = atHome ? nullptr : layouts[En].page(row.pageId);
            if (!atHome && !page) {
                m_warnings.append(QStringLiteral("%1: %2 is not a page of the layout").arg(fragment.name, row.id));
                continue;
            }
            if (atHome) {
                row.context[En] = m_pageNames[En].value(QStringLiteral("home"));
                row.context[Fr] = m_pageNames[Fr].value(QStringLiteral("home"));
                row.icon = QStringLiteral("org.raye.settings-home");
                row.iconFallback = QStringLiteral("go-home");
            } else {
                row.categoryId = row.pageId.section(QLatin1Char('/'), 0, 0);
                for (int lang : {En, Fr})
                    row.context[lang] = m_pageNames[lang].value(row.pageId) + QLatin1Char(' ')
                                        + m_categoryNames[lang].value(row.categoryId);
                row.icon = page->icon;
                row.iconFallback = page->iconFallback;
                row.requirement = page->requirement;
            }
            const QString icon = o.value(QStringLiteral("icon")).toString();
            if (!icon.isEmpty())
                row.icon = icon;
            // a row of a page can need more than the page does
            const QString requirement = o.value(QStringLiteral("requires")).toString();
            if (!requirement.isEmpty())
                row.requirement = requirement;
            addEntry(row);
        }
    }

    if (!sources.synonyms.isEmpty()) {
        const QJsonObject o = QJsonDocument::fromJson(sources.synonyms).object();
        if (o.isEmpty())
            m_warnings.append(QStringLiteral("synonyms.json cannot be read"));
        for (const QJsonValue &group : o.value(QStringLiteral("equivalents")).toArray()) {
            QStringList terms;
            for (const QString &t : stringList(group)) {
                const QString n = SearchText::normalize(t);
                if (!n.isEmpty() && !terms.contains(n))
                    terms.append(n);
            }
            if (terms.size() > 1)
                m_equivalents.append(terms);
        }
        for (const QJsonValue &value : o.value(QStringLiteral("targets")).toArray()) {
            const QJsonObject t = value.toObject();
            Target target;
            target.id = t.value(QStringLiteral("id")).toString();
            for (const QString &term : stringList(t.value(QStringLiteral("terms")))) {
                const QString n = SearchText::normalize(term);
                if (!n.isEmpty())
                    target.terms.append(n);
            }
            if (!entry(target.id) && !entry(target.id.section(QLatin1Char('#'), 0, 0))) {
                m_warnings.append(QStringLiteral("synonyms.json: %1 is not in the index").arg(target.id));
                continue;
            }
            if (!target.terms.isEmpty())
                m_targets.append(target);
        }
    }
    return true;
}

int SearchIndex::scoreEntry(int index, const QString &query, const QList<const WordMatches *> &typed, bool french) const
{
    const Prepared &p = m_prepared[french ? Fr : En].at(index);
    if (p.label.isEmpty() || typed.isEmpty())
        return 0;
    if (query == p.label)
        return scoreLabelExact;
    if (p.label.startsWith(query))
        return scoreLabelPrefix;

    using SearchText::WordMatch;
    int tier = scoreLabelWords;
    bool fuzzy = false;
    int exact = 0;
    for (const WordMatches *matches : typed) {
        const WordMatch inLabel = bestIn(*matches, p.labelWords);
        if (inLabel >= WordMatch::Prefix) {
            exact += inLabel == WordMatch::Exact;
            continue;
        }
        const WordMatch inKeywords = bestIn(*matches, p.keywordWords);
        if (inKeywords >= WordMatch::Prefix) {
            tier = qMin(tier, scoreKeyword);
            continue;
        }
        const WordMatch inContext = bestIn(*matches, p.contextWords);
        if (inContext >= WordMatch::Prefix) {
            tier = qMin(tier, scoreContext);
            continue;
        }
        // nothing but a typo left
        if (inLabel == WordMatch::Fuzzy) {
            fuzzy = true;
        } else if (inKeywords == WordMatch::Fuzzy) {
            fuzzy = true;
            tier = qMin(tier, scoreKeyword);
        } else if (inContext == WordMatch::Fuzzy) {
            fuzzy = true;
            tier = qMin(tier, scoreContext);
        } else {
            return 0;
        }
    }
    if (fuzzy)
        return scoreFuzzyBase + tier / 10;
    return tier + 2 * exact;
}

QStringList SearchIndex::variants(const QString &query) const
{
    QStringList out{query};
    for (const QStringList &group : m_equivalents) {
        for (const QString &term : group) {
            const qsizetype at = findWords(query, term);
            if (at < 0)
                continue;
            for (const QString &other : group) {
                if (other == term)
                    continue;
                QString v = query;
                v.replace(at, term.size(), other);
                if (!out.contains(v))
                    out.append(v);
                if (out.size() >= maxVariants)
                    return out;
            }
        }
    }
    return out;
}

QString SearchIndex::breadcrumbOf(const SearchEntry &entry) const
{
    const int lang = m_french ? Fr : En;
    const QString separator = QStringLiteral(" › ");
    switch (entry.kind) {
    case SearchEntry::Home:
    case SearchEntry::Category:
        return {};
    case SearchEntry::Page:
        return m_categoryNames[lang].value(entry.categoryId);
    case SearchEntry::Row:
        if (entry.categoryId.isEmpty())
            return m_pageNames[lang].value(QStringLiteral("home"));
        return m_categoryNames[lang].value(entry.categoryId) + separator + m_pageNames[lang].value(entry.pageId);
    }
    return {};
}

QList<SearchResult> SearchIndex::search(const QString &rawQuery, int limit) const
{
    QElapsedTimer timer;
    timer.start();
    const QString query = SearchText::normalize(rawQuery);
    QList<SearchResult> results;
    if (query.isEmpty()) {
        m_lastMicroseconds = timer.nsecsElapsed() / 1000;
        return results;
    }
    const QStringList queryWords = query.split(QLatin1Char(' '));

    QList<int> scores(m_entries.size(), 0);
    const QStringList forms = variants(query);
    // each distinct typed word against each distinct word of the index,
    // once
    QHash<QString, WordMatches> matched;
    // what is shown heavier: the typed words and their synonyms ("wifi"
    // lights up "Wi-Fi")
    QStringList shownWords = queryWords;
    for (const QString &form : forms) {
        for (const QString &word : form.split(QLatin1Char(' '))) {
            if (!matched.contains(word))
                matched.insert(word, matchesFor(word));
            if (!shownWords.contains(word))
                shownWords.append(word);
        }
    }
    for (qsizetype v = 0; v < forms.size(); ++v) {
        const QString &form = forms.at(v);
        QList<const WordMatches *> typed;
        for (const QString &word : form.split(QLatin1Char(' ')))
            typed.append(&*matched.constFind(word));
        const int penalty = v == 0 ? 0 : synonymPenalty;
        for (int i = 0; i < m_entries.size(); ++i) {
            int mine = scoreEntry(i, form, typed, m_french);
            int other = scoreEntry(i, form, typed, !m_french);
            // a synonym stands for a name: matched in a keyword, "sans
            // fil" (for wifi) would bring every font with "sans" along
            if (v > 0) {
                mine = mine >= scoreLabelWords ? mine : 0;
                other = other >= scoreLabelWords ? other : 0;
            }
            int best = qMax(mine, other > 0 ? other - otherLanguagePenalty : 0);
            if (best > 0)
                best += kindBonus(m_entries.at(i).kind) - penalty;
            scores[i] = qMax(scores[i], best);
        }
    }

    // words that lead straight to one place
    for (const Target &target : m_targets) {
        int score = 0;
        for (const QString &term : target.terms) {
            if (term == query)
                score = qMax(score, scoreTarget);
            else if (query.size() >= 3 && startsTerm(queryWords, term.split(QLatin1Char(' '))))
                score = qMax(score, scoreTargetPrefix);
        }
        if (score == 0)
            continue;
        auto it = m_byId.constFind(target.id);
        if (it == m_byId.constEnd())
            it = m_byId.constFind(target.id.section(QLatin1Char('#'), 0, 0));
        if (it != m_byId.constEnd())
            scores[it.value()] = qMax(scores[it.value()], score);
    }

    const int lang = m_french ? Fr : En;
    auto labelOf = [this, lang](int i) -> const QString & {
        const SearchEntry &e = m_entries.at(i);
        return e.label[lang].isEmpty() ? e.label[En] : e.label[lang];
    };
    // rank first, then build only the rows that are given back
    QList<int> found;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (scores.at(i) <= 0)
            continue;
        const SearchEntry &e = m_entries.at(i);
        if (!e.requirement.isEmpty() && m_available && !m_available(e.requirement))
            continue;
        found.append(i);
    }
    std::sort(found.begin(), found.end(), [&](int a, int b) {
        if (scores.at(a) != scores.at(b))
            return scores.at(a) > scores.at(b);
        if (labelOf(a).size() != labelOf(b).size())
            return labelOf(a).size() < labelOf(b).size();
        // then in the order of the layout
        return a < b;
    });
    if (limit > 0 && found.size() > limit)
        found.resize(limit);

    results.reserve(found.size());
    for (int i : std::as_const(found)) {
        const SearchEntry &e = m_entries.at(i);
        SearchResult r;
        r.id = e.id;
        r.pageId = e.pageId;
        r.anchor = e.anchor;
        r.label = labelOf(i);
        // a short list gets its highlighting now; the model does the
        // long one row by row, when a row is drawn
        if (limit > 0)
            r.highlighted = SearchText::highlight(r.label, shownWords);
        r.breadcrumb = breadcrumbOf(e);
        r.categoryId = e.categoryId;
        r.categoryName = e.kind == SearchEntry::Home || (e.kind == SearchEntry::Row && e.categoryId.isEmpty())
                             ? m_pageNames[lang].value(QStringLiteral("home"))
                             : m_categoryNames[lang].value(e.categoryId);
        r.icon = e.icon;
        r.iconFallback = e.iconFallback;
        r.kind = e.kind;
        r.score = scores.at(i);
        results.append(r);
    }
    m_lastMicroseconds = timer.nsecsElapsed() / 1000;
    return results;
}

QString SearchIndex::sessionLanguage()
{
    const QString wanted = qEnvironmentVariable("LANGUAGE").section(QLatin1Char(':'), 0, 0);
    const QString name = wanted.isEmpty() ? QLocale().name() : wanted;
    return name.startsWith(QLatin1String("fr")) ? QStringLiteral("fr") : QStringLiteral("en");
}

ModuleKeywords SearchIndex::installedModuleKeywords(const QString &moduleId)
{
    static const QRegularExpression valid(QStringLiteral("^kcm_[A-Za-z0-9_.-]+$"));
    ModuleKeywords words;
    if (!valid.match(moduleId).hasMatch())
        return words;
    static const QStringList namespaces = {
        QStringLiteral("plasma/kcms/systemsettings"),
        QStringLiteral("plasma/kcms/systemsettings_qwidgets"),
        QStringLiteral("plasma/kcms/kinfocenter"),
        QStringLiteral("plasma/kcms"),
    };
    for (const QString &ns : namespaces) {
        const KPluginMetaData data = KPluginMetaData::findPluginById(ns, moduleId);
        if (!data.isValid())
            continue;
        const QJsonObject raw = data.rawData();
        const QJsonObject plugin = raw.value(QStringLiteral("KPlugin")).toObject();
        words.english = stringList(raw.value(QStringLiteral("X-KDE-Keywords")));
        words.french = stringList(raw.value(QStringLiteral("X-KDE-Keywords[fr]")));
        const QString name = plugin.value(QStringLiteral("Name")).toString();
        const QString frName = plugin.value(QStringLiteral("Name[fr]")).toString();
        if (!name.isEmpty())
            words.english.append(name);
        if (!frName.isEmpty())
            words.french.append(frName);
        break;
    }
    return words;
}

QStringList SearchIndex::installedSourcePaths()
{
    QString layout = qEnvironmentVariable("RAYE_SETTINGS_LAYOUT");
    QStringList searchDirs = qEnvironmentVariable("RAYE_SETTINGS_SEARCH_DIR").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const QStringList dataDirs = systemDataDirs();
    if (layout.isEmpty()) {
        for (const QString &dir : dataDirs) {
            const QString path = dir + QStringLiteral("/raye-settings/layout.json");
            if (QFile::exists(path)) {
                layout = path;
                break;
            }
        }
    }
    if (searchDirs.isEmpty()) {
        for (const QString &dir : dataDirs)
            searchDirs.append(dir + QStringLiteral("/raye-settings/search"));
    }
    return QStringList{layout} + searchDirs;
}

SearchIndex::Sources SearchIndex::installedSources()
{
    Sources sources;
    sources.language = sessionLanguage();
    sources.moduleKeywords = &SearchIndex::installedModuleKeywords;

    const QStringList paths = installedSourcePaths();
    if (!paths.first().isEmpty())
        sources.layout = readSmallFile(paths.first());

    QSet<QString> seen;
    for (const QString &dir : paths.mid(1)) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo &info : files) {
            // the first folder that has a file wins, as XDG says
            if (seen.contains(info.fileName()))
                continue;
            seen.insert(info.fileName());
            const QByteArray json = readSmallFile(info.filePath());
            if (info.fileName() == QLatin1String("synonyms.json"))
                sources.synonyms = json;
            else
                sources.fragments.append({info.fileName(), json});
        }
    }
    return sources;
}

void SearchIndex::setModuleKeywordSource(std::function<ModuleKeywords(const QString &)> source)
{
    s_moduleKeywordSource = std::move(source);
}

SearchIndex *SearchIndex::shared()
{
    static std::unique_ptr<SearchIndex> index;
    static Requirements requirements;
    if (!index) {
        index = std::make_unique<SearchIndex>();
        Sources sources = installedSources();
        if (s_moduleKeywordSource)
            sources.moduleKeywords = s_moduleKeywordSource;
        QString error;
        if (!index->load(sources, &error))
            qWarning("raye-settings: search has no index: %s", qPrintable(error));
        for (const QString &warning : index->warnings())
            qWarning("raye-settings: search: %s", qPrintable(warning));
        // the window's router knows when a Wi-Fi key is plugged in;
        // the KRunner plugin has none and reads the computer itself
        index->setAvailability([](const QString &requirement) {
            if (const Router *router = Router::instance())
                return router->has(requirement);
            return requirements.has(requirement);
        });
    }
    return index.get();
}
