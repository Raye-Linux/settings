// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Language & region and Typing: which languages and
// formats are offered, what is written where (in config folders of the
// test's own), the samples, and the keyboard layouts and key repeat.
#include "region.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(text);
}

QString entry(const QString &path, const QString &group, const QString &key)
{
    KConfig config(path, KConfig::SimpleConfig);
    return config.group(group).readEntry(key, QStringLiteral("<unset>"));
}

const QString localeOutput = QStringLiteral(
    "C\nC.utf8\nPOSIX\nen_US.utf8\nen_GB.utf8\nfr_FR.utf8\nfr_CA.utf8\nde_DE.utf8\n"
    "pt_BR.utf8\nsr_RS.utf8@latin\nja_JP.eucjp\nes_ES\n");

QStringList codes(const QList<RegionSettings::Language> &list)
{
    QStringList out;
    for (const auto &l : list)
        out << l.code;
    return out;
}

} // namespace

class TestRegion : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_home;
    QString m_system;

    QString user(const QString &file) const { return m_home + QLatin1Char('/') + file; }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_home = m_dir.filePath(QStringLiteral("config"));
        m_system = m_dir.filePath(QStringLiteral("xdg"));
        qputenv("XDG_CONFIG_HOME", m_home.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_system.toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        // setLanguage() asks the system to follow: never this machine's
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent");
        // the session started in English
        qputenv("LANG", "en_US.UTF-8");
        qunsetenv("LANGUAGE");
        qunsetenv("LC_TIME");
    }

    void init()
    {
        qputenv("LANG", "en_US.UTF-8");
        qunsetenv("LC_TIME");
        for (const QString &dir : {m_home, m_system}) {
            QDir(dir).removeRecursively();
            QDir().mkpath(dir);
        }
        for (const char *name : {"plasma-localerc", "kxkbrc", "kcminputrc"})
            KSharedConfig::openConfig(QString::fromLatin1(name))->reparseConfiguration();
    }

    void parseLocales()
    {
        const QStringList list = RegionSettings::parseLocales(localeOutput);
        QCOMPARE(list, (QStringList{QStringLiteral("de_DE.UTF-8"), QStringLiteral("en_GB.UTF-8"), QStringLiteral("en_US.UTF-8"),
                                    QStringLiteral("fr_CA.UTF-8"), QStringLiteral("fr_FR.UTF-8"), QStringLiteral("pt_BR.UTF-8"),
                                    QStringLiteral("sr_RS.UTF-8@latin")}));
        QVERIFY(RegionSettings::parseLocales(QString()).isEmpty());
        // shell words or paths are not locales
        QVERIFY(RegionSettings::parseLocales(QStringLiteral("fr_FR.utf8;rm -rf\n../x.utf8\n")).isEmpty());
    }

    void names()
    {
        QCOMPARE(RegionSettings::nameOf(QStringLiteral("fr_FR.UTF-8")), QStringLiteral("Français (France)"));
        QCOMPARE(RegionSettings::nameOf(QStringLiteral("de_DE.UTF-8")), QStringLiteral("Deutsch (Deutschland)"));
        QCOMPARE(RegionSettings::nameOf(QStringLiteral("en_US.UTF-8")), QStringLiteral("English (United States)"));
        QCOMPARE(RegionSettings::nameOf(QStringLiteral("en_GB.UTF-8")), QStringLiteral("English (United Kingdom)"));
        // not "Français canadien (Canada)": the country is said once
        QCOMPARE(RegionSettings::nameOf(QStringLiteral("fr_CA.UTF-8")), QStringLiteral("Français (Canada)"));
        QCOMPARE(RegionSettings::nameOf(QStringLiteral("pt_BR.UTF-8")), QStringLiteral("Português (Brasil)"));
    }

    void usableLanguages()
    {
        const QStringList locales = RegionSettings::parseLocales(localeOutput);
        // German has a locale but no translation; Spanish a translation but no locale
        const auto list = RegionSettings::usableLanguages(locales, {QStringLiteral("fr"), QStringLiteral("es"),
                                                                    QStringLiteral("pt_BR"), QStringLiteral("en_GB"),
                                                                    QStringLiteral("sr@latin")},
                                                          QStringLiteral("CA"));
        const QStringList c = codes(list);
        QVERIFY(c.contains(QStringLiteral("en_US")));
        QVERIFY(c.contains(QStringLiteral("fr")));
        QVERIFY(c.contains(QStringLiteral("pt_BR")));
        QVERIFY(!c.contains(QStringLiteral("es")));
        QVERIFY(!c.contains(QStringLiteral("de")));
        QVERIFY(!c.contains(QStringLiteral("sr@latin")));
        // French goes with the territory the user is in
        for (const auto &l : list) {
            if (l.code == QLatin1String("fr"))
                QCOMPARE(l.locale, QStringLiteral("fr_CA.UTF-8"));
        }
        // without a French territory, French goes with France
        for (const auto &l : RegionSettings::usableLanguages(locales, {QStringLiteral("fr")}, QStringLiteral("US"))) {
            if (l.code == QLatin1String("fr"))
                QCOMPARE(l.locale, QStringLiteral("fr_FR.UTF-8"));
        }
        // English is there even with nothing installed
        QCOMPARE(codes(RegionSettings::usableLanguages({}, {}, QString())), QStringList{QStringLiteral("en_US")});
    }

    void languageWrites()
    {
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(localeOutput), {QStringLiteral("fr")});
        QCOMPARE(r.language(), QStringLiteral("en_US"));
        QCOMPARE(r.languages().size(), 2);
        QVERIFY(!r.signOutNeeded());

        QSignalSpy changed(&r, &RegionSettings::changed);
        r.setLanguage(QStringLiteral("fr"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Translations"), QStringLiteral("LANGUAGE")),
                 QStringLiteral("fr:en_US"));
        QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), QStringLiteral("LANG")),
                 QStringLiteral("fr_FR.UTF-8"));
        QCOMPARE(r.language(), QStringLiteral("fr"));
        QCOMPARE(r.languageName(), QStringLiteral("Français (France)"));
        QVERIFY(r.signOutNeeded());

        // only a language of the list
        r.setLanguage(QStringLiteral("de"));
        r.setLanguage(QStringLiteral("fr\nLANG=x"));
        QCOMPARE(r.language(), QStringLiteral("fr"));
        QCOMPARE(changed.count(), 1);

        r.setLanguage(QStringLiteral("en_US"));
        QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Translations"), QStringLiteral("LANGUAGE")),
                 QStringLiteral("en_US"));
        QVERIFY(!r.signOutNeeded());
    }

    // The system says en_CA: Setup does that for English in Canada. The list
    // only knows English as "en_US", and the box stayed empty.
    void languageOfACanadianSession()
    {
        qputenv("LANG", "en_CA.UTF-8");
        const QString installed = localeOutput + QStringLiteral("en_CA.utf8\n");
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(installed), {QStringLiteral("fr")});
        QCOMPARE(r.language(), QStringLiteral("en_US"));

        bool inList = false;
        for (const QVariant &v : r.languages())
            inList = inList || v.toMap().value(QStringLiteral("code")).toString() == r.language();
        QVERIFY(inList);
        // and it is named for where the user is, the same as French
        QCOMPARE(r.languageName(), QStringLiteral("English (Canada)"));
    }

    void englishGoesWithTheTerritory()
    {
        const QStringList locales = RegionSettings::parseLocales(localeOutput + QStringLiteral("en_CA.utf8\n"));
        const auto canada = RegionSettings::usableLanguages(locales, {}, QStringLiteral("CA"));
        QCOMPARE(canada.size(), 1);
        QCOMPARE(canada.first().code, QStringLiteral("en_US"));
        QCOMPARE(canada.first().locale, QStringLiteral("en_CA.UTF-8"));
        // no English for this territory: the usual one
        const auto france = RegionSettings::usableLanguages(locales, {}, QStringLiteral("FR"));
        QCOMPARE(france.first().locale, QStringLiteral("en_US.UTF-8"));
    }

    // /etc/default/locale sets every LC_ variable. A LANG in plasma-localerc
    // does not override them, so the clock stayed "2:33 PM" in French.
    void aNewLanguageBringsItsFormats()
    {
        qputenv("LANG", "en_CA.UTF-8");
        qputenv("LC_TIME", "en_CA.UTF-8");
        const QString installed = localeOutput + QStringLiteral("en_CA.utf8\n");
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(installed), {QStringLiteral("fr")});
        QCOMPARE(r.format(), QString());
        QCOMPARE(r.effectiveFormat(), QStringLiteral("en_CA.UTF-8"));

        r.setLanguage(QStringLiteral("fr"));
        for (const QString &key : RegionSettings::formatKeys)
            QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), key), QStringLiteral("fr_CA.UTF-8"));
        // still "follows the language", not a choice the user made
        QCOMPARE(r.format(), QString());
        QCOMPARE(r.effectiveFormat(), QStringLiteral("fr_CA.UTF-8"));

        // and back, in the same territory
        r.setLanguage(QStringLiteral("en_US"));
        for (const QString &key : RegionSettings::formatKeys)
            QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), key), QStringLiteral("en_CA.UTF-8"));
        QVERIFY(!r.signOutNeeded());
    }

    void aChosenFormatStays()
    {
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(localeOutput), {QStringLiteral("fr")});
        r.setFormat(QStringLiteral("en_GB.UTF-8"));
        r.setLanguage(QStringLiteral("fr"));
        for (const QString &key : RegionSettings::formatKeys)
            QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), key), QStringLiteral("en_GB.UTF-8"));
        QCOMPARE(r.format(), QStringLiteral("en_GB.UTF-8"));

        // "Recommended" again: the language's own, not the system's
        r.setFormat(QString());
        for (const QString &key : RegionSettings::formatKeys)
            QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), key), QStringLiteral("fr_FR.UTF-8"));
        QCOMPARE(r.format(), QString());
    }

    // The sign-in screen and the system services read the system's language,
    // not the user's: the change has to reach it too.
    void theSystemIsAskedToFollow()
    {
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(localeOutput), {QStringLiteral("fr")});
        QSignalSpy asked(&r, &RegionSettings::systemLocaleRequested);

        r.setLanguage(QStringLiteral("fr"));
        QCOMPARE(asked.count(), 1);
        QStringList sent = asked.takeFirst().first().toStringList();
        QVERIFY(sent.contains(QStringLiteral("LANG=fr_FR.UTF-8")));
        QVERIFY(sent.contains(QStringLiteral("LC_TIME=fr_FR.UTF-8")));
        QVERIFY(sent.contains(QStringLiteral("LC_NUMERIC=fr_FR.UTF-8")));
        QVERIFY(!sent.contains(QStringLiteral("LANGUAGE=fr:en_US")));

        // a refused language asks nothing
        r.setLanguage(QStringLiteral("de"));
        QCOMPARE(asked.count(), 0);

        // a format the user chose goes with it
        r.setFormat(QStringLiteral("en_GB.UTF-8"));
        r.setLanguage(QStringLiteral("en_US"));
        QCOMPARE(asked.count(), 1);
        sent = asked.takeFirst().first().toStringList();
        QVERIFY(sent.contains(QStringLiteral("LANG=en_US.UTF-8")));
        QVERIFY(sent.contains(QStringLiteral("LC_TIME=en_GB.UTF-8")));
    }

    void formatWrites()
    {
        RegionSettings r;
        r.setInstalled(RegionSettings::parseLocales(localeOutput), {QStringLiteral("fr")});
        QCOMPARE(r.format(), QString());
        QCOMPARE(r.effectiveFormat(), QStringLiteral("en_US.UTF-8"));
        // formats with a modifier are not offered as formats
        QCOMPARE(r.formats().size(), 6);

        r.setFormat(QStringLiteral("fr_CA.UTF-8"));
        for (const QString &key : RegionSettings::formatKeys)
            QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), key), QStringLiteral("fr_CA.UTF-8"));
        QCOMPARE(r.format(), QStringLiteral("fr_CA.UTF-8"));
        QCOMPARE(r.effectiveFormat(), QStringLiteral("fr_CA.UTF-8"));
        QVERIFY(r.signOutNeeded());
        // the display language did not change
        QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), QStringLiteral("LANG")),
                 QStringLiteral("<unset>"));

        // an uninstalled locale is refused
        r.setFormat(QStringLiteral("xx_YY.UTF-8"));
        QCOMPARE(r.format(), QStringLiteral("fr_CA.UTF-8"));

        // back to "follow the language": the keys go away
        r.setFormat(QString());
        for (const QString &key : RegionSettings::formatKeys)
            QCOMPARE(entry(user(QStringLiteral("plasma-localerc")), QStringLiteral("Formats"), key), QStringLiteral("<unset>"));
        QVERIFY(!r.signOutNeeded());
    }

    void samples()
    {
        const QDateTime when(QDate(2026, 10, 31), QTime(14, 2, 5));
        const QVariantMap fr = RegionSettings::samplesFor(QStringLiteral("fr_FR.UTF-8"), when);
        QCOMPARE(fr.value(QStringLiteral("shortDate")).toString(), QStringLiteral("31/10/2026"));
        QCOMPARE(fr.value(QStringLiteral("shortTime")).toString(), QStringLiteral("14:02"));
        QCOMPARE(fr.value(QStringLiteral("longTime")).toString(), QStringLiteral("14:02:05"));
        QVERIFY(fr.value(QStringLiteral("longDate")).toString().contains(QLatin1String("octobre")));
        QVERIFY(fr.value(QStringLiteral("currency")).toString().contains(QStringLiteral("€")));
        QCOMPARE(fr.value(QStringLiteral("firstDay")).toString(), QStringLiteral("lundi"));
        const QVariantMap us = RegionSettings::samplesFor(QStringLiteral("en_US.UTF-8"), when);
        QCOMPARE(us.value(QStringLiteral("shortDate")).toString(), QStringLiteral("10/31/26"));
        QCOMPARE(us.value(QStringLiteral("number")).toString(), QStringLiteral("1,234,567.89"));
        QVERIFY(us.value(QStringLiteral("currency")).toString().startsWith(QLatin1Char('$')));
        QCOMPARE(us.value(QStringLiteral("longTime")).toString(), QStringLiteral("2:02:05 PM"));
    }

    void layouts()
    {
        const QString keyboard = m_dir.filePath(QStringLiteral("keyboard"));
        writeFile(keyboard, "XKBMODEL=\"pc105\"\nXKBLAYOUT=\"fr,us\"\nXKBVARIANT=\"bepo,\"\n");
        KSharedConfigPtr kxkb = KSharedConfig::openConfig(QStringLiteral("kxkbrc"));
        // nothing chosen in the desktop: the system's layouts
        auto list = KeyboardLayouts::layoutsFrom(kxkb, keyboard);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0), qMakePair(QStringLiteral("fr"), QStringLiteral("bepo")));
        QCOMPARE(list.at(1), qMakePair(QStringLiteral("us"), QString()));

        writeFile(user(QStringLiteral("kxkbrc")), "[Layout]\nUse=true\nLayoutList=de,ca\nVariantList=,fr\n");
        kxkb->reparseConfiguration();
        list = KeyboardLayouts::layoutsFrom(kxkb, keyboard);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(1), qMakePair(QStringLiteral("ca"), QStringLiteral("fr")));
        // Use=false: the desktop does not decide
        writeFile(user(QStringLiteral("kxkbrc")), "[Layout]\nUse=false\nLayoutList=de\n");
        kxkb->reparseConfiguration();
        QCOMPARE(KeyboardLayouts::layoutsFrom(kxkb, keyboard).at(0).first, QStringLiteral("fr"));
        // nothing anywhere: US
        QCOMPARE(KeyboardLayouts::layoutsFrom(kxkb, QStringLiteral("/nonexistent")).at(0).first, QStringLiteral("us"));
    }

    void layoutNames()
    {
        const QString rules = m_dir.filePath(QStringLiteral("evdev.lst"));
        writeFile(rules, "! model\n  pc105  Generic 105-key PC\n\n! layout\n  us   English (US)\n  fr   French\n\n"
                         "! variant\n  bepo   fr: French (BEPO)\n  intl   us: English (US, intl., with dead keys)\n");
        QCOMPARE(KeyboardLayouts::layoutName(QStringLiteral("fr"), QString(), rules), QStringLiteral("French"));
        QCOMPARE(KeyboardLayouts::layoutName(QStringLiteral("fr"), QStringLiteral("bepo"), rules), QStringLiteral("French (BEPO)"));
        QCOMPARE(KeyboardLayouts::layoutName(QStringLiteral("us"), QStringLiteral("intl"), rules),
                 QStringLiteral("English (US, intl., with dead keys)"));
        // the variant of another layout does not count
        QCOMPARE(KeyboardLayouts::layoutName(QStringLiteral("us"), QStringLiteral("bepo"), rules), QStringLiteral("us (bepo)"));
        QCOMPARE(KeyboardLayouts::layoutName(QStringLiteral("xx"), QString(), rules), QStringLiteral("xx"));
    }

    void keyRepeat()
    {
        KeyboardLayouts k;
        QCOMPARE(k.repeatDelay(), 600);
        QCOMPARE(k.repeatRate(), 25);
        QSignalSpy changed(&k, &KeyboardLayouts::changed);
        k.setRepeatDelay(250);
        k.setRepeatRate(40);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(entry(user(QStringLiteral("kcminputrc")), QStringLiteral("Keyboard"), QStringLiteral("RepeatDelay")), QStringLiteral("250"));
        QCOMPARE(entry(user(QStringLiteral("kcminputrc")), QStringLiteral("Keyboard"), QStringLiteral("RepeatRate")), QStringLiteral("40"));
        // out of range is brought back in
        k.setRepeatDelay(5);
        QCOMPARE(k.repeatDelay(), KeyboardLayouts::minDelay);
        k.setRepeatRate(1000);
        QCOMPARE(k.repeatRate(), KeyboardLayouts::maxRate);
        KeyboardLayouts again;
        QCOMPARE(again.repeatDelay(), KeyboardLayouts::minDelay);
    }

    void layoutPerWindow()
    {
        KeyboardLayouts k;
        QVERIFY(!k.layoutPerWindow());
        k.setLayoutPerWindow(true);
        QCOMPARE(entry(user(QStringLiteral("kxkbrc")), QStringLiteral("Layout"), QStringLiteral("SwitchMode")), QStringLiteral("Window"));
        QVERIFY(k.layoutPerWindow());
        k.setLayoutPerWindow(false);
        QCOMPARE(entry(user(QStringLiteral("kxkbrc")), QStringLiteral("Layout"), QStringLiteral("SwitchMode")), QStringLiteral("Global"));
        // the keyboard module's "per application" counts too
        writeFile(user(QStringLiteral("kxkbrc")), "[Layout]\nSwitchMode=WinClass\n");
        KeyboardLayouts again;
        again.reload();
        QVERIFY(again.layoutPerWindow());
    }
};

QTEST_GUILESS_MAIN(TestRegion)
#include "test_region.moc"
