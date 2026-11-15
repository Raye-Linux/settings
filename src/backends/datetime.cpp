// SPDX-License-Identifier: GPL-3.0-or-later
#include "datetime.h"

#include "multitasking.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

#include <algorithm>

namespace {

// A call that failed before it left (no bus) is finished before anyone
// listens: say so once the caller is connected.
void finishLate(QDBusPendingCallWatcher *watcher)
{
    if (watcher->isFinished())
        QMetaObject::invokeMethod(watcher, [watcher] { Q_EMIT watcher->finished(watcher); }, Qt::QueuedConnection);
}

const QString service = QStringLiteral("org.freedesktop.timedate1");
const QString path = QStringLiteral("/org/freedesktop/timedate1");
const QString interface = QStringLiteral("org.freedesktop.timedate1");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString clockGroup = QStringLiteral("Clock");
// the password dialog can stay open a long time
constexpr int interactiveTimeout = 10 * 60 * 1000;

bool denied(const QString &errorName)
{
    return errorName.contains(QLatin1String("AccessDenied")) || errorName.contains(QLatin1String("NotAuthorized"))
        || errorName.contains(QLatin1String("InteractiveAuthorizationRequired"));
}

// the regional format's way to write the time, made 12- or 24-hour
QString timePattern(const QLocale &locale, bool h24, bool seconds)
{
    QString pattern = locale.timeFormat(seconds ? QLocale::LongFormat : QLocale::ShortFormat);
    // the long format carries the zone name: not on a clock
    pattern.remove(QRegularExpression(QStringLiteral("\\s*t+")));
    const bool localeIs24 = !pattern.contains(QLatin1Char('a'), Qt::CaseInsensitive);
    if (h24 == localeIs24)
        return pattern.trimmed();
    if (h24)
        return seconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm");
    return seconds ? QStringLiteral("h:mm:ss AP") : QStringLiteral("h:mm AP");
}

} // namespace

// ---- Date & time ------------------------------------------------------

DateTimeSettings::DateTimeSettings(QObject *parent)
    : DateTimeSettings(QDBusConnection::systemBus(), parent)
{
}

DateTimeSettings::DateTimeSettings(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_shell(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
{
    m_timeZone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    m_bus.connect(service, path, properties, QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    // the clock of the page turns over on the second
    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, &DateTimeSettings::tick);
    m_ticker.start();
    reload();
}

void DateTimeSettings::reload()
{
    m_shell->reparseConfiguration();
    Q_EMIT clockChanged();

    QDBusMessage get = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("GetAll"));
    get << interface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            if (m_available || !m_ready) {
                m_available = false;
                m_ready = true;
                Q_EMIT changed();
            }
            return;
        }
        m_ready = true;
        m_available = true;
        onPropertiesChanged(interface, reply.value(), {});
    });
    finishLate(watcher);
}

void DateTimeSettings::onPropertiesChanged(const QString &iface, const QVariantMap &values, const QStringList &invalidated)
{
    if (iface != interface)
        return;
    if (!invalidated.isEmpty()) {
        reload();
        return;
    }
    if (values.contains(QStringLiteral("NTP")))
        m_ntp = values.value(QStringLiteral("NTP")).toBool();
    if (values.contains(QStringLiteral("CanNTP")))
        m_canNtp = values.value(QStringLiteral("CanNTP")).toBool();
    if (values.contains(QStringLiteral("NTPSynchronized")))
        m_synchronized = values.value(QStringLiteral("NTPSynchronized")).toBool();
    const QString zone = values.value(QStringLiteral("Timezone")).toString();
    if (!zone.isEmpty())
        m_timeZone = zone;
    Q_EMIT changed();
    Q_EMIT tick();
}

QDateTime DateTimeSettings::zoneNow() const
{
    const QTimeZone zone(m_timeZone.toUtf8());
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    return zone.isValid() ? utc.toTimeZone(zone) : utc.toLocalTime();
}

QVariantMap DateTimeSettings::nowParts() const
{
    const QDateTime t = zoneNow();
    return {{QStringLiteral("year"), t.date().year()},
            {QStringLiteral("month"), t.date().month() - 1},
            {QStringLiteral("day"), t.date().day()},
            {QStringLiteral("hour"), t.time().hour()},
            {QStringLiteral("minute"), t.time().minute()}};
}

QString DateTimeSettings::timeZoneLabel() const
{
    return labelFor(m_timeZone, QDateTime::currentDateTimeUtc());
}

QString DateTimeSettings::timeText() const
{
    return formatTime(zoneNow(), QLocale(), clockFormat(), false);
}

QString DateTimeSettings::dateText() const
{
    return QLocale().toString(zoneNow().date(), QLocale::LongFormat);
}

QString DateTimeSettings::lastSyncText() const
{
    const QDateTime last = lastSyncTime();
    if (!last.isValid())
        return QString();
    const QTimeZone zone(m_timeZone.toUtf8());
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    return formatLastSync(zone.isValid() ? last.toTimeZone(zone) : last, zone.isValid() ? utc.toTimeZone(zone) : utc,
                          QLocale(), clockFormat());
}

QDateTime DateTimeSettings::lastSyncTime()
{
    // touched by the time service at each successful synchronisation
    for (const char *file : {"/run/systemd/timesync/synchronized", "/var/lib/systemd/timesync/clock"}) {
        const QFileInfo info(QString::fromLatin1(file));
        if (info.exists())
            return info.lastModified(QTimeZone::UTC);
    }
    return QDateTime();
}

void DateTimeSettings::call(const QString &method, const QVariantList &arguments, std::function<void(bool)> then)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    ++m_busy;
    Q_EMIT busyChanged();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, interactiveTimeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, then](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        const bool ok = !reply.isError();
        if (then && ok) {
            // the next step keeps the page busy
            then(true);
        }
        --m_busy;
        Q_EMIT busyChanged();
        if (!ok || !then) {
            Q_EMIT done(ok, ok ? QString() : denied(reply.error().name()) ? QStringLiteral("denied") : QStringLiteral("failed"));
            reload();
        }
    });
    finishLate(watcher);
}

void DateTimeSettings::setAutomaticTime(bool on)
{
    if (on == m_ntp && m_available)
        return;
    call(QStringLiteral("SetNTP"), {on, true});
}

void DateTimeSettings::setTimeZone(const QString &id)
{
    if (id == m_timeZone || !TimeZoneModel::zoneIds().contains(id)) {
        if (id != m_timeZone)
            Q_EMIT done(false, QStringLiteral("failed"));
        return;
    }
    call(QStringLiteral("SetTimezone"), {id, true});
}

void DateTimeSettings::setDateTime(const QDateTime &local)
{
    if (m_ntp || !local.isValid()) {
        Q_EMIT done(false, QStringLiteral("failed"));
        return;
    }
    // what the user typed is a time on the PC's clock, not on Settings'
    const QTimeZone zone(m_timeZone.toUtf8());
    const QDateTime when = zone.isValid() ? QDateTime(local.date(), local.time(), zone) : local;
    const qint64 usec = when.toMSecsSinceEpoch() * 1000;
    call(QStringLiteral("SetTime"), {QVariant::fromValue<qlonglong>(usec), false, true});
}

void DateTimeSettings::syncNow()
{
    if (!m_ntp)
        return;
    // off and on again: the time service starts over and asks at once
    call(QStringLiteral("SetNTP"), {false, true}, [this](bool) {
        call(QStringLiteral("SetNTP"), {true, true});
    });
}

bool DateTimeSettings::showSeconds() const
{
    return m_shell->group(clockGroup).readEntry("ShowSeconds", false);
}

void DateTimeSettings::setShowSeconds(bool on)
{
    if (on != showSeconds())
        writeClock(QStringLiteral("ShowSeconds"), on ? QStringLiteral("true") : QStringLiteral("false"));
}

bool DateTimeSettings::showDate() const
{
    return m_shell->group(clockGroup).readEntry("ShowDate", true);
}

void DateTimeSettings::setShowDate(bool on)
{
    if (on != showDate())
        writeClock(QStringLiteral("ShowDate"), on ? QStringLiteral("true") : QStringLiteral("false"));
}

int DateTimeSettings::clockFormat() const
{
    const QString value = m_shell->group(clockGroup).readEntry("Use24h", QStringLiteral("auto")).trimmed().toLower();
    if (value == QLatin1String("true"))
        return 1;
    if (value == QLatin1String("false"))
        return 2;
    return 0;
}

void DateTimeSettings::setClockFormat(int format)
{
    if (format < 0 || format > 2 || format == clockFormat())
        return;
    static const QString values[] = {QStringLiteral("auto"), QStringLiteral("true"), QStringLiteral("false")};
    writeClock(QStringLiteral("Use24h"), values[format]);
    Q_EMIT tick();
}

bool DateTimeSettings::showSecondsAvailable() const
{
    return MultitaskingSettings::systemDefines(QStringLiteral("rayeshellrc"), clockGroup, QStringLiteral("ShowSeconds"));
}

bool DateTimeSettings::showDateAvailable() const
{
    return MultitaskingSettings::systemDefines(QStringLiteral("rayeshellrc"), clockGroup, QStringLiteral("ShowDate"));
}

bool DateTimeSettings::clockFormatAvailable() const
{
    return MultitaskingSettings::systemDefines(QStringLiteral("rayeshellrc"), clockGroup, QStringLiteral("Use24h"));
}

void DateTimeSettings::writeClock(const QString &key, const QString &value)
{
    // the shell watches the file and follows at once
    KConfigGroup g = m_shell->group(clockGroup);
    g.writeEntry(key, value, KConfig::Notify);
    m_shell->sync();
    Q_EMIT clockChanged();
}

QString DateTimeSettings::offsetText(int seconds)
{
    const QChar sign = seconds < 0 ? QLatin1Char('-') : QLatin1Char('+');
    const int minutes = std::abs(seconds) / 60;
    return QStringLiteral("%1%2:%3").arg(sign).arg(minutes / 60, 2, 10, QLatin1Char('0')).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString DateTimeSettings::cityOf(const QString &id)
{
    QString city = id.section(QLatin1Char('/'), -1);
    city.replace(QLatin1Char('_'), QLatin1Char(' '));
    return city;
}

QString DateTimeSettings::labelFor(const QString &id, const QDateTime &when)
{
    const QTimeZone zone(id.toUtf8());
    if (!zone.isValid())
        return id;
    const int offset = zone.offsetFromUtc(when);
    if (id == QLatin1String("UTC") || id == QLatin1String("Etc/UTC"))
        return i18nc("@item time zone", "(UTC) Coordinated Universal Time");
    return QStringLiteral("(UTC%1) %2").arg(offsetText(offset), cityOf(id));
}

bool DateTimeSettings::uses24h(int clockFormat, const QLocale &locale)
{
    if (clockFormat == 1)
        return true;
    if (clockFormat == 2)
        return false;
    return !locale.timeFormat(QLocale::ShortFormat).contains(QLatin1Char('a'), Qt::CaseInsensitive);
}

QString DateTimeSettings::formatTime(const QDateTime &time, const QLocale &locale, int clockFormat, bool seconds)
{
    return locale.toString(time.time(), timePattern(locale, uses24h(clockFormat, locale), seconds));
}

QString DateTimeSettings::formatLastSync(const QDateTime &when, const QDateTime &now, const QLocale &locale, int clockFormat)
{
    const QString time = formatTime(when, locale, clockFormat, false);
    const qint64 days = when.date().daysTo(now.date());
    if (days == 0)
        return i18nc("@info last time the clock was set, %1 a time", "today, %1", time);
    if (days == 1)
        return i18nc("@info last time the clock was set, %1 a time", "yesterday, %1", time);
    return i18nc("@info last time the clock was set, %1 a date, %2 a time", "%1, %2",
                 locale.toString(when.date(), QLocale::ShortFormat), time);
}

// ---- Time zones -------------------------------------------------------

TimeZoneModel::TimeZoneModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QLocale locale;
    const QStringList ids = zoneIds();
    m_all.reserve(ids.size());
    for (const QString &id : ids)
        m_all << zoneFor(id, now, locale);
    std::sort(m_all.begin(), m_all.end(), [](const Zone &a, const Zone &b) {
        if (a.offset != b.offset)
            return a.offset < b.offset;
        return QString::localeAwareCompare(a.label, b.label) < 0;
    });
    refilter();
}

QStringList TimeZoneModel::zoneIds()
{
    static const QStringList ids = [] {
        static const QSet<QString> regions = {
            QStringLiteral("Africa"),   QStringLiteral("America"), QStringLiteral("Antarctica"),
            QStringLiteral("Arctic"),   QStringLiteral("Asia"),    QStringLiteral("Atlantic"),
            QStringLiteral("Australia"), QStringLiteral("Europe"), QStringLiteral("Indian"),
            QStringLiteral("Pacific")};
        QStringList list;
        const auto all = QTimeZone::availableTimeZoneIds();
        for (const QByteArray &raw : all) {
            const QString id = QString::fromUtf8(raw);
            // "Europe/Paris", "America/Argentina/Salta"; not "US/Eastern" or "Etc/GMT+3"
            if (regions.contains(id.section(QLatin1Char('/'), 0, 0)) && id.contains(QLatin1Char('/')))
                list << id;
        }
        list << QStringLiteral("UTC");
        list.removeDuplicates();
        return list;
    }();
    return ids;
}

TimeZoneModel::Zone TimeZoneModel::zoneFor(const QString &id, const QDateTime &when, const QLocale &locale)
{
    Zone z;
    z.id = id;
    const QTimeZone zone(id.toUtf8());
    z.offset = zone.isValid() ? zone.offsetFromUtc(when) : 0;
    z.label = DateTimeSettings::labelFor(id, when);
    z.description = zone.isValid() ? zone.displayName(QTimeZone::GenericTime, QTimeZone::LongName, locale) : QString();
    if (z.description == id)
        z.description.clear();
    const QString english = zone.isValid() ? zone.displayName(QTimeZone::GenericTime, QTimeZone::LongName, QLocale::c()) : QString();
    const QString abbreviation = zone.isValid() ? zone.abbreviation(when) : QString();
    QString territory;
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    if (zone.isValid() && zone.territory() != QLocale::AnyTerritory)
        territory = QLocale::territoryToString(zone.territory());
#endif
    // "utc+1" and "utc+01:00" both find Paris; "utc+5:30" finds Kolkata
    const int minutes = std::abs(z.offset) / 60;
    const QString sign = z.offset < 0 ? QStringLiteral("-") : QStringLiteral("+");
    QString utcShort = QStringLiteral("utc") + sign + QString::number(minutes / 60);
    if (minutes % 60)
        utcShort += QLatin1Char(':') + QString::number(minutes % 60);
    const QString utcLong = QStringLiteral("utc") + DateTimeSettings::offsetText(z.offset);
    QString path = id;
    path.replace(QLatin1Char('/'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    z.words = fold(QStringList{path, z.description, english, abbreviation, territory, utcShort, utcLong}.join(QLatin1Char(' ')));
    return z;
}

QString TimeZoneModel::fold(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_SpacingCombining
            || c.category() == QChar::Mark_Enclosing)
            continue;
        out += c;
    }
    return out;
}

bool TimeZoneModel::matches(const Zone &zone, const QString &query)
{
    const QStringList words = fold(query).split(QRegularExpression(QStringLiteral("[\\s,()]+")), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return true;
    static const QRegularExpression separators(QStringLiteral("[\\s,()'’\\-]+"));
    const QStringList haystack = zone.words.split(separators, Qt::SkipEmptyParts);
    for (const QString &word : words) {
        const bool found = std::any_of(haystack.cbegin(), haystack.cend(), [&word](const QString &w) {
            return w.startsWith(word);
        });
        if (!found)
            return false;
    }
    return true;
}

void TimeZoneModel::setFilter(const QString &filter)
{
    if (filter == m_filter)
        return;
    m_filter = filter;
    Q_EMIT filterChanged();
    refilter();
}

void TimeZoneModel::refilter()
{
    beginResetModel();
    m_shown.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (matches(m_all.at(i), m_filter))
            m_shown << i;
    }
    endResetModel();
    Q_EMIT countChanged();
}

int TimeZoneModel::indexOf(const QString &id) const
{
    for (int row = 0; row < m_shown.size(); ++row) {
        if (m_all.at(m_shown.at(row)).id == id)
            return row;
    }
    return -1;
}

int TimeZoneModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_shown.size();
}

QVariant TimeZoneModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_shown.size())
        return {};
    const Zone &z = m_all.at(m_shown.at(index.row()));
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return z.label;
    case IdRole:
        return z.id;
    case DescriptionRole:
        return z.description;
    case OffsetRole:
        return z.offset;
    }
    return {};
}

QHash<int, QByteArray> TimeZoneModel::roleNames() const
{
    return {{IdRole, "zoneId"}, {LabelRole, "label"}, {DescriptionRole, "description"}, {OffsetRole, "offset"}};
}
