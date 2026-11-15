// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Language & region, and Typing.
//
// Language & region writes what the desktop's own language module
// writes, read by the session at sign-in (plasma-localerc):
//   [Translations] LANGUAGE   display language ("fr", "en_US")
//   [Formats] LANG            the locale that goes with it ("fr_FR.UTF-8")
//   [Formats] LC_NUMERIC, LC_TIME, LC_MONETARY, LC_MEASUREMENT, LC_PAPER,
//             LC_ADDRESS, LC_NAME, LC_TELEPHONE   the regional format: the
//             language's own locale, unless the user picked another
// A new display language also asks the system to take it (locale1), because
// the sign-in screen and the system services read the system's language.
// Only languages that can show (a locale is installed and the desktop is
// translated) are offered; adding one, with its locale and language
// pack, is the desktop module's job ("More language options").
//
// Typing reads the keyboard layouts (kxkbrc [Layout] LayoutList,
// VariantList, Use) and sets key repeat (kcminputrc [Keyboard]
// RepeatDelay, RepeatRate); layouts are added in the keyboard module's
// window.
#pragma once

#include <KSharedConfig>
#include <QDateTime>
#include <QLocale>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class RegionSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{ code: "fr", locale: "fr_FR.UTF-8", name: "Français (France)" }], by name
    Q_PROPERTY(QVariantList languages READ languages NOTIFY changed)
    // the code of the display language ("fr", "en_US")
    Q_PROPERTY(QString language READ language NOTIFY changed)
    Q_PROPERTY(QString languageName READ languageName NOTIFY changed)
    // [{ locale: "fr_CA.UTF-8", name: "Français (Canada)" }], by name
    Q_PROPERTY(QVariantList formats READ formats NOTIFY changed)
    // the regional format in use, "" when it follows the display language
    Q_PROPERTY(QString format READ format NOTIFY changed)
    // the locale the samples are drawn with (the format, else the language's)
    Q_PROPERTY(QString effectiveFormat READ effectiveFormat NOTIFY changed)
    // { shortDate, longDate, shortTime, longTime, number, currency, firstDay }
    Q_PROPERTY(QVariantMap samples READ samples NOTIFY changed)
    // the language or format changed since this session started
    Q_PROPERTY(bool signOutNeeded READ signOutNeeded NOTIFY changed)

public:
    struct Language {
        QString code;   // "fr"
        QString locale; // "fr_FR.UTF-8"
        QString name;   // "Français (France)"
    };

    explicit RegionSettings(QObject *parent = nullptr);

    QVariantList languages() const;
    QString language() const;
    QString languageName() const;
    QVariantList formats() const;
    QString format() const;
    QString effectiveFormat() const;
    QVariantMap samples() const;
    bool signOutNeeded() const;

    Q_INVOKABLE void reload();
    // a code of `languages`; anything else is refused
    Q_INVOKABLE void setLanguage(const QString &code);
    // a locale of `formats`, or "" to follow the display language
    Q_INVOKABLE void setFormat(const QString &locale);
    // the desktop's language module, in its own window
    Q_INVOKABLE void openMoreOptions();
    Q_INVOKABLE void signOut();

    // the tests hand the installed locales and translations in
    void setInstalled(const QStringList &locales, const QStringList &translations);

    // `locale -a` output -> "fr_FR.UTF-8" for each UTF-8 locale, sorted,
    // no "C" or "POSIX"; "fr_FR.utf8" is written the usual way
    static QStringList parseLocales(const QString &output);
    // "fr_FR.UTF-8" -> "Français (France)"
    static QString nameOf(const QString &locale);
    // the languages that can show: English always, then each translated
    // language with an installed locale (the one of `preferredTerritory`
    // when there is one); `translations` holds codes such as "fr", "pt_BR"
    static QList<Language> usableLanguages(const QStringList &locales, const QStringList &translations,
                                           const QString &preferredTerritory);
    static QVariantMap samplesFor(const QString &locale, const QDateTime &when);
    // the locale names regional formats are written to
    static const QStringList formatKeys;

Q_SIGNALS:
    void changed();
    // LANG=... and LC_...=... lines the system's language is asked to take
    void systemLocaleRequested(const QStringList &entries);

private:
    void readInstalled();
    void write(const QString &group, const QString &key, const QString &value);
    void askSystemToFollow(const QString &language, const QString &formats);

    KSharedConfigPtr m_config;
    QStringList m_locales;
    QList<Language> m_languages;
    // what the session started with
    QString m_startLanguage;
    QString m_startFormat;
};

class KeyboardLayouts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{ layout: "fr", variant: "", name: "French" }]
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY changed)
    // milliseconds before a held key repeats
    Q_PROPERTY(int repeatDelay READ repeatDelay WRITE setRepeatDelay NOTIFY changed)
    // repeats per second
    Q_PROPERTY(int repeatRate READ repeatRate WRITE setRepeatRate NOTIFY changed)

public:
    static constexpr int minDelay = 100;
    static constexpr int maxDelay = 1000;
    static constexpr int minRate = 2;
    static constexpr int maxRate = 50;

    explicit KeyboardLayouts(QObject *parent = nullptr);

    QVariantList layouts() const;
    int repeatDelay() const;
    void setRepeatDelay(int ms);
    int repeatRate() const;
    void setRepeatRate(int perSecond);

    Q_INVOKABLE void reload();
    // add, remove and order layouts, the switch shortcut: the keyboard
    // module's own window
    Q_INVOKABLE void openLayouts();

    // the layouts of the files: the user's choice, else the system's
    // (XKBLAYOUT of /etc/default/keyboard), else "us"
    static QList<QPair<QString, QString>> layoutsFrom(const KSharedConfigPtr &kxkbrc, const QString &systemKeyboardFile);
    // "fr", "" -> "French"; "fr", "bepo" -> "French (BEPO)" (names from the
    // XKB rules list, translated when the desktop has the words)
    static QString layoutName(const QString &layout, const QString &variant, const QString &rulesFile);

Q_SIGNALS:
    void changed();

private:
    void writeInput(const QString &key, int value);

    KSharedConfigPtr m_kxkb;
    KSharedConfigPtr m_input;
};
