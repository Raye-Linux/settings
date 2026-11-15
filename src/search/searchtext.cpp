// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchtext.h"

#include <QList>

namespace SearchText
{

namespace {

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber();
}

// Letters that do not come apart into a base letter and a mark.
QString foldLetter(QChar c)
{
    switch (c.unicode()) {
    case 0x00e6: return QStringLiteral("ae"); // æ
    case 0x0153: return QStringLiteral("oe"); // œ
    case 0x00df: return QStringLiteral("ss"); // ß
    case 0x00f8: return QStringLiteral("o");  // ø
    case 0x0142: return QStringLiteral("l");  // ł
    case 0x0111: return QStringLiteral("d");  // đ
    }
    return QString(c);
}

// The folded form of one word, and nothing else.
QString foldWord(const QString &word)
{
    const QString decomposed = word.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_Enclosing
            || c.category() == QChar::Mark_SpacingCombining)
            continue;
        if (isWordChar(c))
            out += foldLetter(c);
    }
    return out;
}

} // namespace

QString normalize(const QString &text)
{
    // fold first: a letter with its accent apart must stay one word
    const QString decomposed = text.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString out;
    out.reserve(decomposed.size());
    bool space = false;
    for (const QChar c : decomposed) {
        const auto category = c.category();
        if (category == QChar::Mark_NonSpacing || category == QChar::Mark_Enclosing
            || category == QChar::Mark_SpacingCombining)
            continue;
        if (isWordChar(c)) {
            if (space && !out.isEmpty())
                out += QLatin1Char(' ');
            space = false;
            out += foldLetter(c);
        } else {
            space = true;
        }
    }
    return out;
}

QStringList words(const QString &text)
{
    const QString n = normalize(text);
    return n.isEmpty() ? QStringList() : n.split(QLatin1Char(' '));
}

namespace {

bool withinOneEditView(QStringView a, QStringView b)
{
    const qsizetype la = a.size();
    const qsizetype lb = b.size();
    if (qAbs(la - lb) > 1)
        return false;
    if (la == lb) {
        // one letter changed, or two neighbours swapped
        qsizetype first = -1;
        qsizetype count = 0;
        for (qsizetype i = 0; i < la; ++i) {
            if (a.at(i) != b.at(i)) {
                if (count == 0)
                    first = i;
                if (++count > 2)
                    return false;
            }
        }
        if (count <= 1)
            return true;
        return first + 1 < la && a.at(first) == b.at(first + 1) && a.at(first + 1) == b.at(first)
               && a.mid(first + 2) == b.mid(first + 2);
    }
    // one letter more on one side
    const QStringView longer = la > lb ? a : b;
    const QStringView shorter = la > lb ? b : a;
    qsizetype i = 0;
    while (i < shorter.size() && shorter.at(i) == longer.at(i))
        ++i;
    return shorter.mid(i) == longer.mid(i + 1);
}

} // namespace

bool withinOneEdit(const QString &a, const QString &b)
{
    return withinOneEditView(a, b);
}

WordMatch matchWord(const QString &typed, const QString &word)
{
    if (typed.isEmpty())
        return WordMatch::None;
    if (typed == word)
        return WordMatch::Exact;
    if (word.startsWith(typed))
        return WordMatch::Prefix;
    if (typed.size() < fuzzyMinLength)
        return WordMatch::None;
    if (withinOneEditView(typed, word))
        return WordMatch::Fuzzy;
    // the start of a longer word, with one typo: "bluet" in "bluetooth"
    // typed as "blyet"
    if (word.size() > typed.size()) {
        for (qsizetype n = typed.size() - 1; n <= typed.size() + 1 && n < word.size(); ++n) {
            if (withinOneEditView(typed, QStringView(word).left(n)))
                return WordMatch::Fuzzy;
        }
    }
    return WordMatch::None;
}

QString highlight(const QString &label, const QStringList &typedWords)
{
    // cut the label into words as it is written, mark the ones that a
    // typed word starts (or matches with one typo)
    struct Span {
        qsizetype start;
        qsizetype length;
        bool marked;
    };
    // a typo is shown only for a typed word that is not plainly there:
    // "night" must not light up "light" in "Night light"
    QStringList plain;
    for (const QString &typed : typedWords) {
        for (const QString &w : words(label)) {
            if (matchWord(typed, w) >= WordMatch::Prefix) {
                plain.append(typed);
                break;
            }
        }
    }
    QList<Span> spans;
    qsizetype i = 0;
    while (i < label.size()) {
        const bool word = isWordChar(label.at(i)) || label.at(i).category() == QChar::Mark_NonSpacing;
        qsizetype j = i;
        while (j < label.size()
               && (isWordChar(label.at(j)) || label.at(j).category() == QChar::Mark_NonSpacing) == word)
            ++j;
        // the length of the heavier part of this word, from its start
        qsizetype heavy = 0;
        if (word) {
            const QString folded = foldWord(label.mid(i, j - i));
            for (const QString &typed : typedWords) {
                const WordMatch m = matchWord(typed, folded);
                if (m == WordMatch::Exact || (m == WordMatch::Fuzzy && !plain.contains(typed))) {
                    heavy = j - i;
                    break;
                }
                if (m == WordMatch::Prefix) {
                    // only the typed part is heavier: "Blu" + "etooth"
                    qsizetype k = i;
                    qsizetype matched = 0;
                    while (k < j && matched < typed.size()) {
                        matched += foldWord(label.mid(k, 1)).size();
                        ++k;
                    }
                    heavy = qMax(heavy, k - i);
                }
            }
        }
        if (heavy > 0)
            spans.append({i, heavy, true});
        if (heavy < j - i)
            spans.append({i + heavy, j - i - heavy, false});
        i = j;
    }
    QString out;
    for (const Span &span : std::as_const(spans)) {
        const QString text = label.mid(span.start, span.length).toHtmlEscaped();
        if (span.marked)
            out += QStringLiteral("<b>") + text + QStringLiteral("</b>");
        else
            out += text;
    }
    return out;
}

} // namespace SearchText
