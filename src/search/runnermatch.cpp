// SPDX-License-Identifier: GPL-3.0-or-later
#include "runnermatch.h"

#include "searchtext.h"

#include <KLocalizedString>
#include <QStandardPaths>

namespace {
constexpr int minLetters = 2;
// the score of a label typed in full, with the bonus of a page
constexpr double bestScore = 1100.0;
constexpr int strongScore = 700;
}

QList<RunnerMatch> runnerMatches(const SearchIndex &index, const QString &query, int limit)
{
    QList<RunnerMatch> matches;
    if (SearchText::normalize(query).size() < minLetters)
        return matches;
    const QList<SearchResult> results = index.search(query, limit);
    for (const SearchResult &r : results) {
        RunnerMatch m;
        m.id = r.id;
        m.text = r.label;
        m.iconName = r.icon.isEmpty() ? r.iconFallback : r.icon;
        // a name typed in full or nearly is a result to put first
        m.categoryRelevance = r.score >= strongScore ? 70 : 50;
        m.relevance = qBound(0.0, r.score / bestScore, 1.0);
        const QString where = r.breadcrumb.isEmpty() ? i18nd("raye-settings", "Settings")
                                                     : i18nd("raye-settings", "Settings › %1", r.breadcrumb);
        m.properties.insert(QStringLiteral("subtext"), where);
        m.properties.insert(QStringLiteral("category"), i18nd("raye-settings", "Settings"));
        matches.append(m);
    }
    return matches;
}

bool runnerCommand(const SearchIndex &index, const QString &id, QString *program, QStringList *arguments)
{
    if (!index.entry(id))
        return false;
    QString settings = QStandardPaths::findExecutable(QStringLiteral("raye-settings"));
    if (settings.isEmpty())
        settings = QStringLiteral("/usr/bin/raye-settings");
    if (program)
        *program = settings;
    if (arguments)
        *arguments = {QStringLiteral("--page"), id};
    return true;
}
