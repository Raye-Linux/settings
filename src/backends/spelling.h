// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Typing > Spelling. KDE's spell checker keeps its
// settings in ~/.config/KDE/Sonnet.conf, read by every app that checks
// spelling as you type when it starts:
//   checkerEnabledByDefault  underline misspelled words
//   autodetectLanguage       guess the language of the text
//   defaultLanguage          else, this dictionary
// Nothing here works without the spell checker's plugins, so the page
// only shows these settings when they are installed.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class SpellingSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool highlight READ highlight WRITE setHighlight NOTIFY changed)
    // a dictionary ("fr_FR"), or "" to detect the language
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    // [{ code, name }] of the installed dictionaries, by name
    Q_PROPERTY(QVariantList languages READ languages NOTIFY changed)

public:
    explicit SpellingSettings(QObject *parent = nullptr);

    bool available() const;
    bool highlight() const;
    void setHighlight(bool on);
    QString language() const;
    void setLanguage(const QString &code);
    QVariantList languages() const;

    // the dictionaries (an .aff next to a .dic) in these folders
    static QStringList dictionariesIn(const QStringList &dirs);

    // For the tests
    void setDictionaryDirs(const QStringList &dirs);
    void setPluginDir(const QString &dir);

Q_SIGNALS:
    void changed();

private:
    QString file() const;

    QStringList m_dictionaryDirs;
    QString m_pluginDir;
};
