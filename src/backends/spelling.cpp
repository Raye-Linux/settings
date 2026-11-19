// SPDX-License-Identifier: GPL-3.0-or-later
#include "spelling.h"

#include <QCollator>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

SpellingSettings::SpellingSettings(QObject *parent)
    : QObject(parent)
    , m_dictionaryDirs({QStringLiteral("/usr/share/hunspell"), QStringLiteral("/usr/share/myspell")})
    , m_pluginDir(QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/kf6/sonnet"))
{
}

void SpellingSettings::setDictionaryDirs(const QStringList &dirs)
{
    m_dictionaryDirs = dirs;
    Q_EMIT changed();
}

void SpellingSettings::setPluginDir(const QString &dir)
{
    m_pluginDir = dir;
    Q_EMIT changed();
}

QString SpellingSettings::file() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/KDE/Sonnet.conf");
}

bool SpellingSettings::available() const
{
    return !QDir(m_pluginDir).entryList({QStringLiteral("*.so")}, QDir::Files).isEmpty()
        && !dictionariesIn(m_dictionaryDirs).isEmpty();
}

QStringList SpellingSettings::dictionariesIn(const QStringList &dirs)
{
    QStringList codes;
    for (const QString &dir : dirs) {
        const QDir d(dir);
        for (const QString &dic : d.entryList({QStringLiteral("*.dic")}, QDir::Files | QDir::Readable)) {
            const QString code = dic.chopped(4);
            // hyph_*, th_* and the like are not spelling dictionaries
            if (code.contains(QLatin1Char('-')) || code.startsWith(QLatin1String("hyph_")) || code.startsWith(QLatin1String("th_")))
                continue;
            if (d.exists(code + QStringLiteral(".aff")) && !codes.contains(code))
                codes << code;
        }
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

QVariantList SpellingSettings::languages() const
{
    struct Entry {
        QString code, name;
    };
    QList<Entry> entries;
    for (const QString &code : dictionariesIn(m_dictionaryDirs)) {
        const QLocale locale(code);
        QString name = locale.language() == QLocale::C ? code : locale.nativeLanguageName();
        if (locale.language() != QLocale::C && code.contains(QLatin1Char('_')))
            name += QStringLiteral(" (") + locale.nativeTerritoryName() + QLatin1Char(')');
        if (!name.isEmpty())
            name[0] = name[0].toUpper();
        entries.append({code, name});
    }
    QCollator collator;
    std::sort(entries.begin(), entries.end(), [&collator](const Entry &a, const Entry &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    QVariantList list;
    for (const Entry &e : entries)
        list << QVariantMap{{QStringLiteral("code"), e.code}, {QStringLiteral("name"), e.name}};
    return list;
}

bool SpellingSettings::highlight() const
{
    return QSettings(file(), QSettings::IniFormat).value(QStringLiteral("checkerEnabledByDefault"), false).toBool();
}

void SpellingSettings::setHighlight(bool on)
{
    if (on == highlight())
        return;
    QSettings(file(), QSettings::IniFormat).setValue(QStringLiteral("checkerEnabledByDefault"), on);
    Q_EMIT changed();
}

QString SpellingSettings::language() const
{
    const QSettings s(file(), QSettings::IniFormat);
    if (s.value(QStringLiteral("autodetectLanguage"), true).toBool())
        return {};
    const QString code = s.value(QStringLiteral("defaultLanguage")).toString();
    return dictionariesIn(m_dictionaryDirs).contains(code) ? code : QString();
}

void SpellingSettings::setLanguage(const QString &code)
{
    if (code == language() || (!code.isEmpty() && !dictionariesIn(m_dictionaryDirs).contains(code)))
        return;
    QSettings s(file(), QSettings::IniFormat);
    s.setValue(QStringLiteral("autodetectLanguage"), code.isEmpty());
    if (!code.isEmpty())
        s.setValue(QStringLiteral("defaultLanguage"), code);
    Q_EMIT changed();
}
