// SPDX-License-Identifier: GPL-3.0-or-later
#include "region.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <QCollator>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>

namespace {

const QString formatsGroup = QStringLiteral("Formats");
const QString translationsGroup = QStringLiteral("Translations");

// "fr_FR.UTF-8" -> QLocale("fr_FR")
QLocale localeOf(const QString &name)
{
    return QLocale(name.section(QLatin1Char('.'), 0, 0).section(QLatin1Char('@'), 0, 0));
}

void sortByName(QVariantList &list)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(),
                                b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
}

const QString setLocaleAction = QStringLiteral("org.freedesktop.locale1.set-locale");

// polkit's answer, (bba{ss}): is the program allowed?
bool systemAllowed(const QDBusMessage &reply)
{
    if (reply.arguments().isEmpty())
        return false;
    const QDBusArgument result = reply.arguments().constFirst().value<QDBusArgument>();
    bool allowed = false;
    bool challenge = false;
    result.beginStructure();
    result >> allowed >> challenge;
    return allowed;
}

// the session's own environment, as it was when this program started
QString environment(const char *name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

} // namespace

const QStringList RegionSettings::formatKeys = {
    QStringLiteral("LC_NUMERIC"), QStringLiteral("LC_TIME"),  QStringLiteral("LC_MONETARY"),
    QStringLiteral("LC_MEASUREMENT"), QStringLiteral("LC_PAPER"), QStringLiteral("LC_ADDRESS"),
    QStringLiteral("LC_NAME"),    QStringLiteral("LC_TELEPHONE")};

RegionSettings::RegionSettings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("plasma-localerc")))
{
    readInstalled();
    m_startLanguage = language();
    m_startFormat = effectiveFormat();
}

void RegionSettings::readInstalled()
{
    QProcess process;
    process.start(QStringLiteral("locale"), {QStringLiteral("-a")});
    QString output;
    if (process.waitForFinished(3000))
        output = QString::fromLocal8Bit(process.readAllStandardOutput());
    const auto domains = KLocalizedString::availableDomainTranslations("plasmashell");
    setInstalled(parseLocales(output), QStringList(domains.cbegin(), domains.cend()));
}

void RegionSettings::setInstalled(const QStringList &locales, const QStringList &translations)
{
    m_locales = locales;
    const QString territory = localeOf(m_config->group(formatsGroup).readEntry("LANG", environment("LANG"))).name().section(QLatin1Char('_'), 1);
    m_languages = usableLanguages(locales, translations, territory);
    m_startLanguage = language();
    m_startFormat = effectiveFormat();
    Q_EMIT changed();
}

void RegionSettings::reload()
{
    m_config->reparseConfiguration();
    Q_EMIT changed();
}

QStringList RegionSettings::parseLocales(const QString &output)
{
    QStringList list;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression utf8(QStringLiteral("^([a-z]{2,3}(_[A-Z]{2})?(@[a-z]+)?)\\.(utf8|UTF-8)(@[a-z]+)?$"),
                                         QRegularExpression::CaseInsensitiveOption);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const auto m = utf8.match(line);
        if (!m.hasMatch())
            continue;
        const QString base = m.captured(1);
        // "C.utf8" has no language: it is not a choice
        if (base.compare(QLatin1String("C"), Qt::CaseInsensitive) == 0 || !base.contains(QLatin1Char('_')))
            continue;
        list << base + QStringLiteral(".UTF-8") + m.captured(5);
    }
    list.removeDuplicates();
    list.sort();
    return list;
}

QString RegionSettings::nameOf(const QString &locale)
{
    const QLocale l = localeOf(locale);
    // "American English (United States)" says the country twice
    // and "Français canadien (Canada)" too: the language of no country in particular
    QString language = l.language() == QLocale::English
        ? QStringLiteral("English")
        : QLocale(l.language(), l.script(), QLocale::AnyTerritory).nativeLanguageName();
    if (language.isEmpty())
        return locale;
    language[0] = language.at(0).toUpper();
    const QString territory = l.nativeTerritoryName();
    return territory.isEmpty() ? language : QStringLiteral("%1 (%2)").arg(language, territory);
}

QList<RegionSettings::Language> RegionSettings::usableLanguages(const QStringList &locales, const QStringList &translations,
                                                                const QString &preferredTerritory)
{
    QList<Language> list;
    QSet<QString> seen;
    // the code a locale answers to: "pt_BR" when that is translated, else "pt"
    auto add = [&](const QString &code, const QString &locale) {
        if (seen.contains(code))
            return;
        seen.insert(code);
        list << Language{code, locale, nameOf(locale)};
    };
    auto localeFor = [&](const QString &code) -> QString {
        // "pt_BR": that exact locale; "fr": fr_<territory>, else the
        // language's usual country, else any
        if (code.contains(QLatin1Char('_'))) {
            const QString exact = code + QStringLiteral(".UTF-8");
            return locales.contains(exact) ? exact : QString();
        }
        const QString preferred = code + QLatin1Char('_') + preferredTerritory + QStringLiteral(".UTF-8");
        if (!preferredTerritory.isEmpty() && locales.contains(preferred))
            return preferred;
        const QString usual = QLocale(code).name() + QStringLiteral(".UTF-8");
        if (locales.contains(usual))
            return usual;
        for (const QString &l : locales) {
            if (l.startsWith(code + QLatin1Char('_')))
                return l;
        }
        return QString();
    };

    // English is the language everything is written in: always there,
    // with the English of the user's country when it is installed
    QString english = preferredTerritory.isEmpty() ? QString() : localeFor(QStringLiteral("en_") + preferredTerritory);
    if (english.isEmpty())
        english = localeFor(QStringLiteral("en_US"));
    if (english.isEmpty())
        english = QStringLiteral("en_US.UTF-8");
    add(QStringLiteral("en_US"), english);
    for (const QString &code : translations) {
        if (code.startsWith(QLatin1String("en")) || code.contains(QLatin1Char('@')))
            continue;
        const QString locale = localeFor(code);
        if (!locale.isEmpty())
            add(code, locale);
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const Language &a, const Language &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    return list;
}

QVariantList RegionSettings::languages() const
{
    QVariantList list;
    for (const Language &l : m_languages)
        list << QVariantMap{{QStringLiteral("code"), l.code}, {QStringLiteral("locale"), l.locale}, {QStringLiteral("name"), l.name}};
    return list;
}

QString RegionSettings::language() const
{
    QString value = m_config->group(translationsGroup).readEntry("LANGUAGE", QString());
    if (value.isEmpty())
        value = environment("LANGUAGE");
    if (value.isEmpty())
        value = environment("LANG").section(QLatin1Char('.'), 0, 0);
    // "fr:en_US": the first one shows
    QString code = value.section(QLatin1Char(':'), 0, 0).trimmed();
    // the list has one English: Setup writes en_CA, en_GB and so on
    if (code.isEmpty() || code == QLatin1String("C") || code == QLatin1String("POSIX") || code == QLatin1String("en")
        || code.startsWith(QLatin1String("en_")))
        code = QStringLiteral("en_US");
    // a code the list has under its short name ("fr_FR" -> "fr")
    for (const Language &l : m_languages) {
        if (l.code == code)
            return code;
    }
    for (const Language &l : m_languages) {
        if (l.code == code.section(QLatin1Char('_'), 0, 0))
            return l.code;
    }
    return code;
}

QString RegionSettings::languageName() const
{
    const QString code = language();
    for (const Language &l : m_languages) {
        if (l.code == code)
            return l.name;
    }
    return nameOf(code);
}

QVariantList RegionSettings::formats() const
{
    QVariantList list;
    for (const QString &locale : m_locales) {
        if (locale.contains(QLatin1Char('@')))
            continue;
        list << QVariantMap{{QStringLiteral("locale"), locale}, {QStringLiteral("name"), nameOf(locale)}};
    }
    sortByName(list);
    return list;
}

QString RegionSettings::format() const
{
    // the regional format is the time one; the others follow it. The same
    // locale as the language is not a choice, it follows the language.
    const KConfigGroup g = m_config->group(formatsGroup);
    const QString time = g.readEntry("LC_TIME", QString());
    return time == g.readEntry("LANG", QString()) ? QString() : time;
}

QString RegionSettings::effectiveFormat() const
{
    // what the next session gets: its own LC_TIME, else the system's, which
    // wins over a LANG of the file
    QString lang = m_config->group(formatsGroup).readEntry("LC_TIME", QString());
    if (lang.isEmpty())
        lang = environment("LC_TIME");
    if (lang.isEmpty())
        lang = m_config->group(formatsGroup).readEntry("LANG", QString());
    if (lang.isEmpty())
        lang = environment("LANG");
    if (lang.isEmpty() || lang.startsWith(QLatin1String("C")))
        lang = QStringLiteral("en_US.UTF-8");
    return lang;
}

QVariantMap RegionSettings::samples() const
{
    return samplesFor(effectiveFormat(), QDateTime::currentDateTime());
}

QVariantMap RegionSettings::samplesFor(const QString &locale, const QDateTime &when)
{
    const QLocale l = localeOf(locale);
    QString currency = l.toCurrencyString(1234.56);
    return {
        {QStringLiteral("shortDate"), l.toString(when.date(), QLocale::ShortFormat)},
        {QStringLiteral("longDate"), l.toString(when.date(), QLocale::LongFormat)},
        {QStringLiteral("shortTime"), l.toString(when.time(), QLocale::ShortFormat)},
        {QStringLiteral("longTime"), l.toString(when.time(), l.timeFormat(QLocale::ShortFormat).contains(QLatin1Char('a'), Qt::CaseInsensitive)
                                                                 ? QStringLiteral("h:mm:ss AP")
                                                                 : QStringLiteral("HH:mm:ss"))},
        {QStringLiteral("number"), l.toString(1234567.89, 'f', 2)},
        {QStringLiteral("currency"), currency},
        {QStringLiteral("firstDay"), l.dayName(l.firstDayOfWeek(), QLocale::LongFormat)},
    };
}

bool RegionSettings::signOutNeeded() const
{
    return language() != m_startLanguage || effectiveFormat() != m_startFormat;
}

void RegionSettings::write(const QString &group, const QString &key, const QString &value)
{
    KConfigGroup g = m_config->group(group);
    if (value.isEmpty())
        g.deleteEntry(key, KConfig::Notify);
    else
        g.writeEntry(key, value, KConfig::Notify);
}

void RegionSettings::setLanguage(const QString &code)
{
    const auto it = std::find_if(m_languages.cbegin(), m_languages.cend(), [&code](const Language &l) {
        return l.code == code;
    });
    if (it == m_languages.cend() || code == language())
        return;
    // read before LANG moves: a format the user chose stays
    const QString chosen = format();
    // English last, so a word the language lacks still shows
    write(translationsGroup, QStringLiteral("LANGUAGE"),
          code == QLatin1String("en_US") ? code : code + QStringLiteral(":en_US"));
    write(formatsGroup, QStringLiteral("LANG"), it->locale);
    // the system sets every LC_ variable, and a LANG here does not override
    // them: without these the language changes and the clock does not
    if (chosen.isEmpty()) {
        for (const QString &key : formatKeys)
            write(formatsGroup, key, it->locale);
    }
    m_config->sync();
    askSystemToFollow(it->locale, chosen.isEmpty() ? it->locale : chosen);
    Q_EMIT changed();
}

void RegionSettings::askSystemToFollow(const QString &language, const QString &formats)
{
    QStringList entries{QStringLiteral("LANG=") + language};
    for (const QString &key : formatKeys)
        entries << key + QLatin1Char('=') + formats;
    Q_EMIT systemLocaleRequested(entries);

    // The sign-in screen, Firefox's first start and the names the network
    // service gives its connections read this, not the user's file. It takes
    // an administrator; said no to, the user's own session still changes.
    //
    // localed waits only 25 s for polkit, then gives up without a word, and
    // reading the prompt and typing a password often takes longer. So ask
    // polkit here first, as long as the person needs: the action is
    // auth_admin_keep, so the yes is kept for this program and localed's own
    // check passes at once, with no second prompt.
    const QString self = QDBusConnection::systemBus().baseService();
    QDBusMessage check = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PolicyKit1"),
                                                        QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
                                                        QStringLiteral("org.freedesktop.PolicyKit1.Authority"),
                                                        QStringLiteral("CheckAuthorization"));
    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("system-bus-name");
    subject.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QDBusVariant>());
    subject.beginMapEntry();
    subject << QStringLiteral("name") << QDBusVariant(self);
    subject.endMapEntry();
    subject.endMap();
    subject.endStructure();
    QDBusArgument details;
    details.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QString>());
    details.endMap();
    check << QVariant::fromValue(subject) << setLocaleAction << QVariant::fromValue(details)
          << quint32(1) /* the person may be asked */ << QString();

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(check, std::numeric_limits<int>::max()),
                                                QCoreApplication::instance());
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [entries](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        // no polkit to ask: localed asks itself, as before
        if (reply.type() == QDBusMessage::ReplyMessage && !systemAllowed(reply))
            return; // the person said no
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.locale1"),
                                                           QStringLiteral("/org/freedesktop/locale1"),
                                                           QStringLiteral("org.freedesktop.locale1"),
                                                           QStringLiteral("SetLocale"));
        call << entries << true;
        call.setInteractiveAuthorizationAllowed(true);
        QDBusConnection::systemBus().asyncCall(call, std::numeric_limits<int>::max());
    });
}

void RegionSettings::setFormat(const QString &locale)
{
    if (!locale.isEmpty() && !m_locales.contains(locale))
        return;
    if (locale == format())
        return;
    // "" follows the language: its own locale once the file names one, else
    // what the system says
    const QString value = locale.isEmpty() ? m_config->group(formatsGroup).readEntry("LANG", QString()) : locale;
    for (const QString &key : formatKeys)
        write(formatsGroup, key, value);
    m_config->sync();
    Q_EMIT changed();
}

void RegionSettings::openMoreOptions()
{
    // the relay would send this module back to this window;
    // RAYE_SETTINGS_KCMSHELL (set by main) lets it through
    QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_regionandlang")});
}

void RegionSettings::signOut()
{
    // the desktop's own "Sign out?" question, with its countdown
    QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"), QStringLiteral("org.kde.LogoutPrompt"),
        QStringLiteral("promptLogout")));
}

// ---- Typing -----------------------------------------------------------

KeyboardLayouts::KeyboardLayouts(QObject *parent)
    : QObject(parent)
    , m_kxkb(KSharedConfig::openConfig(QStringLiteral("kxkbrc")))
    , m_input(KSharedConfig::openConfig(QStringLiteral("kcminputrc")))
{
}

void KeyboardLayouts::reload()
{
    m_kxkb->reparseConfiguration();
    m_input->reparseConfiguration();
    Q_EMIT changed();
}

QList<QPair<QString, QString>> KeyboardLayouts::layoutsFrom(const KSharedConfigPtr &kxkbrc, const QString &systemKeyboardFile)
{
    QList<QPair<QString, QString>> list;
    const KConfigGroup g = kxkbrc->group(QStringLiteral("Layout"));
    QStringList layouts, variants;
    if (g.readEntry("Use", false)) {
        layouts = g.readEntry("LayoutList", QString()).split(QLatin1Char(','));
        variants = g.readEntry("VariantList", QString()).split(QLatin1Char(','));
    }
    if (layouts.join(QString()).trimmed().isEmpty()) {
        layouts.clear();
        variants.clear();
        QFile file(systemKeyboardFile);
        if (file.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                const QString value = line.section(QLatin1Char('='), 1).trimmed().remove(QLatin1Char('"'));
                if (line.trimmed().startsWith(QLatin1String("XKBLAYOUT=")))
                    layouts = value.split(QLatin1Char(','));
                else if (line.trimmed().startsWith(QLatin1String("XKBVARIANT=")))
                    variants = value.split(QLatin1Char(','));
            }
        }
    }
    for (int i = 0; i < layouts.size(); ++i) {
        const QString layout = layouts.at(i).trimmed();
        if (!layout.isEmpty())
            list << qMakePair(layout, variants.value(i).trimmed());
    }
    if (list.isEmpty())
        list << qMakePair(QStringLiteral("us"), QString());
    return list;
}

QString KeyboardLayouts::layoutName(const QString &layout, const QString &variant, const QString &rulesFile)
{
    QFile file(rulesFile);
    QString name;
    if (file.open(QIODevice::ReadOnly)) {
        // "! layout" then "  fr   French"; "! variant" then "  bepo  fr: French (BEPO)"
        QString section;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('!'))) {
                section = line.mid(1).trimmed();
                continue;
            }
            const QString key = line.section(QLatin1Char(' '), 0, 0);
            const QString rest = line.section(QLatin1Char(' '), 1).trimmed();
            if (variant.isEmpty() && section == QLatin1String("layout") && key == layout) {
                name = rest;
                break;
            }
            if (!variant.isEmpty() && section == QLatin1String("variant") && key == variant
                && rest.startsWith(layout + QLatin1Char(':'))) {
                name = rest.mid(layout.size() + 1).trimmed();
                break;
            }
        }
    }
    if (name.isEmpty())
        return variant.isEmpty() ? layout : QStringLiteral("%1 (%2)").arg(layout, variant);
    return i18nd("xkeyboard-config", name.toUtf8().constData());
}

QVariantList KeyboardLayouts::layouts() const
{
    QVariantList list;
    const auto pairs = layoutsFrom(m_kxkb, QStringLiteral("/etc/default/keyboard"));
    for (const auto &[layout, variant] : pairs) {
        list << QVariantMap{{QStringLiteral("layout"), layout},
                            {QStringLiteral("variant"), variant},
                            {QStringLiteral("name"), layoutName(layout, variant, QStringLiteral("/usr/share/X11/xkb/rules/evdev.lst"))}};
    }
    return list;
}

int KeyboardLayouts::repeatDelay() const
{
    return std::clamp(m_input->group(QStringLiteral("Keyboard")).readEntry("RepeatDelay", 600), minDelay, maxDelay);
}

void KeyboardLayouts::setRepeatDelay(int ms)
{
    ms = std::clamp(ms, minDelay, maxDelay);
    if (ms != repeatDelay())
        writeInput(QStringLiteral("RepeatDelay"), ms);
}

int KeyboardLayouts::repeatRate() const
{
    // stored as a decimal number of repeats a second
    return std::clamp(qRound(m_input->group(QStringLiteral("Keyboard")).readEntry("RepeatRate", 25.0)), minRate, maxRate);
}

void KeyboardLayouts::setRepeatRate(int perSecond)
{
    perSecond = std::clamp(perSecond, minRate, maxRate);
    if (perSecond != repeatRate())
        writeInput(QStringLiteral("RepeatRate"), perSecond);
}

bool KeyboardLayouts::layoutPerWindow() const
{
    const QString mode = m_kxkb->group(QStringLiteral("Layout")).readEntry("SwitchMode", QStringLiteral("Global"));
    return mode == QLatin1String("Window") || mode == QLatin1String("WinClass");
}

void KeyboardLayouts::setLayoutPerWindow(bool on)
{
    if (on == layoutPerWindow())
        return;
    KConfigGroup g = m_kxkb->group(QStringLiteral("Layout"));
    g.writeEntry("SwitchMode", on ? QStringLiteral("Window") : QStringLiteral("Global"), KConfig::Notify);
    m_kxkb->sync();
    // what the keyboard module sends: the window manager reads kxkbrc again
    QDBusConnection::sessionBus().send(
        QDBusMessage::createSignal(QStringLiteral("/Layouts"), QStringLiteral("org.kde.keyboard"), QStringLiteral("reloadConfig")));
    Q_EMIT changed();
}

void KeyboardLayouts::writeInput(const QString &key, int value)
{
    // the window manager watches this file for key repeat
    KConfigGroup g = m_input->group(QStringLiteral("Keyboard"));
    g.writeEntry(key, value, KConfig::Notify);
    m_input->sync();
    Q_EMIT changed();
}

void KeyboardLayouts::openLayouts()
{
    QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_keyboard")});
}
