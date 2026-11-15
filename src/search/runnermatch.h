// SPDX-License-Identifier: GPL-3.0-or-later
// What the Start menu gets from Settings: the answers of the search
// runner (raye-settings-runner, org.kde.krunner1 on D-Bus), and the
// command that opens one of them. Kept apart from the D-Bus side so the
// tests can check it.
#pragma once

#include "searchindex.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct RunnerMatch {
    QString id;       // an index id: "network/wifi", "system/display#night-light"
    QString text;     // the page or row name, in the session's language
    QString iconName; // a Raye glyph
    int categoryRelevance = 50; // 0..100, as KRunner ranks its groups
    double relevance = 0;       // 0..1 inside the group
    QVariantMap properties;     // "subtext": where it is ("System › Display")
};

// The best answers for what was typed; nothing for less than two letters.
QList<RunnerMatch> runnerMatches(const SearchIndex &index, const QString &query, int limit = 8);

// The program and arguments that open `id`; false when the index does
// not know the id (a match id comes from outside: it is checked, never
// passed on as it came).
bool runnerCommand(const SearchIndex &index, const QString &id, QString *program, QStringList *arguments);
