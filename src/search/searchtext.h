// SPDX-License-Identifier: GPL-3.0-or-later
// The text side of the search: how a label, a keyword and what the
// user typed are brought to one form before they are compared.
//
// "Écran", "ecran" and "ÉCRAN" are the same word; "Wi-Fi" is "wi fi";
// "d'écran" is "d ecran". Words are compared by prefix ("blu" finds
// Bluetooth), and a word of five letters or more may carry one typo
// ("blutooth").
#pragma once

#include <QString>
#include <QStringList>

namespace SearchText
{

// Lower case, no accents, every run of anything that is not a letter or
// a digit turned into one space, no space at either end.
QString normalize(const QString &text);

// normalize(), then cut at the spaces.
QStringList words(const QString &text);

// How a typed word matches a word of the index.
enum class WordMatch {
    None,
    Fuzzy, // one letter off (typed, missing, extra or swapped)
    Prefix, // the start of the word
    Exact,
};

// `typed` and `word` are both normalized words.
WordMatch matchWord(const QString &typed, const QString &word);

// Is the Damerau distance between the two at most one?
bool withinOneEdit(const QString &a, const QString &b);

// The shortest typed word that may carry a typo.
constexpr int fuzzyMinLength = 5;

// The label with the parts that match the typed words in a heavier
// weight, as rich text (the rest escaped). Used by the suggestions.
QString highlight(const QString &label, const QStringList &typedWords);

} // namespace SearchText
