// SPDX-License-Identifier: GPL-3.0-or-later
// Everything the search of Raye Settings can find, and the ranking.
//
// The index is made of:
// - Home, every category and every page of layout.json (name, summary);
// - every row a page tells about in data/search/<page>.json, one file
//   per page package, installed in /usr/share/raye-settings/search:
//
//     {"version": 1, "entries": [
//       {"id": "system/display#night-light", "label": "Night light",
//        "label_fr": "Éclairage nocturne",
//        "keywords": ["blue light", "warm", "evening"],
//        "keywords_fr": ["lumière bleue", "soir"]}
//     ]}
//
//   (a bare array of entries is read too). The id is a page id and the
//   searchId of the row after '#'; the page must be in layout.json;
// - the keywords of the settings modules a page answers for (its kcm
//   and aliases), in English and French;
// - data/search/synonyms.json: words that mean the same ("wifi",
//   "wi-fi", "wlan") and words that lead straight to one place ("dark
//   mode" goes to Colours).
//
// Ranking: label typed in full > label starting with what was typed >
// every word in the label > a keyword > the page or category name >
// one typo (only for a word the index does not have: "night" is not
// a typo of "light"). Same score: the shorter name, then the layout's
// order. Both languages are searched, the session's one first: a
// French session typing "display" still finds Écran.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

struct SearchEntry {
    enum Kind { Home, Category, Page, Row };

    QString id;          // "system/display#night-light", "system", "home"
    QString pageId;      // "system/display"; the category id for a category
    QString anchor;      // "night-light", empty for a page
    QString categoryId;  // "system", empty for Home
    Kind kind = Page;
    QString label[2];    // [English, French]
    QStringList keywords[2];
    QString context[2];  // page and category names the row sits under
    QString icon;
    QString iconFallback;
    QString requirement; // the page's "requires"
};

struct SearchResult {
    QString id;
    QString pageId;
    QString anchor;
    QString label;       // in the session's language
    QString highlighted; // the label, matched parts in <b> (empty when no limit was asked)
    QString breadcrumb;  // "System › Display"
    QString categoryId;
    QString categoryName;
    QString icon;
    QString iconFallback;
    SearchEntry::Kind kind = SearchEntry::Page;
    int score = 0;
};

// Keywords of one settings module, by language.
struct ModuleKeywords {
    QStringList english;
    QStringList french;
};

class SearchIndex
{
public:
    struct Fragment {
        QString name; // file name, for the warnings
        QByteArray json;
    };
    struct Sources {
        QByteArray layout;
        QList<Fragment> fragments;
        QByteArray synonyms;
        // keywords of a module id ("kcm_kscreen"); unset: none
        std::function<ModuleKeywords(const QString &)> moduleKeywords;
        // "en" or "fr": the language shown and searched first
        QString language = QStringLiteral("en");
    };

    SearchIndex() = default;

    // Builds the index. False with `error` set when the layout cannot be
    // read; a broken fragment is skipped and named in warnings().
    bool load(const Sources &sources, QString *error = nullptr);

    // Best first. `limit` 0: everything that matches.
    QList<SearchResult> search(const QString &query, int limit = 0) const;

    const QList<SearchEntry> &entries() const { return m_entries; }
    const SearchEntry *entry(const QString &id) const;
    QStringList warnings() const { return m_warnings; }
    QString language() const { return m_french ? QStringLiteral("fr") : QStringLiteral("en"); }

    // Pages this computer has no hardware for are left out. The default
    // asks nothing (everything is shown); see Requirements.
    void setAvailability(std::function<bool(const QString &requirement)> available);

    // The index of the installed Settings, built on first use: the layout
    // (RAYE_SETTINGS_LAYOUT or /usr/share/raye-settings/layout.json), the
    // fragments (RAYE_SETTINGS_SEARCH_DIR or .../raye-settings/search),
    // the module keywords, the session's language and its hardware.
    static SearchIndex *shared();
    // Reads what shared() reads. Files in the home folder never stand in
    // for the installed ones.
    static Sources installedSources();
    // The layout file (empty when there is none), then the search
    // folders: what installedSources() reads.
    static QStringList installedSourcePaths();
    // Where shared() takes the keywords of the settings modules from;
    // installedModuleKeywords unless set before its first call.
    static void setModuleKeywordSource(std::function<ModuleKeywords(const QString &)> source);
    // "fr" or "en", from LANGUAGE, else the locale.
    static QString sessionLanguage();
    // The keywords the installed module metadata gives for an id.
    static ModuleKeywords installedModuleKeywords(const QString &moduleId);

    // How long the last search took, in microseconds (for the tests).
    qint64 lastSearchMicroseconds() const { return m_lastMicroseconds; }

private:
    // An entry's words are numbers in m_words: what was typed is
    // compared with each distinct word once per search, not once per
    // entry.
    struct Prepared {
        QString label; // normalized
        QList<int> labelWords;
        QList<int> keywordWords;
        QList<int> contextWords;
    };
    // for each word of m_words, how one typed word matches it
    using WordMatches = QList<quint8>;
    struct Target {
        QStringList terms; // normalized
        QString id;
    };

    void addEntry(const SearchEntry &entry);
    QList<int> wordIds(const QStringList &words);
    WordMatches matchesFor(const QString &typed) const;
    int scoreEntry(int index, const QString &query, const QList<const WordMatches *> &typed, bool french) const;
    QStringList variants(const QString &query) const;
    QString breadcrumbOf(const SearchEntry &entry) const;

    QList<SearchEntry> m_entries;
    QList<Prepared> m_prepared[2];
    QHash<QString, int> m_byId;
    QStringList m_words;
    QHash<QString, int> m_wordIds;
    QList<QStringList> m_equivalents; // normalized terms
    QList<Target> m_targets;
    QHash<QString, QString> m_categoryNames[2];
    QHash<QString, QString> m_pageNames[2];
    QStringList m_warnings;
    bool m_french = false;
    std::function<bool(const QString &)> m_available;
    mutable qint64 m_lastMicroseconds = 0;
};
