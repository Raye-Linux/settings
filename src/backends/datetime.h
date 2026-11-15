// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Date & time. The clock of the PC is systemd's
// timedated (org.freedesktop.timedate1 on the system bus): set the time
// automatically (NTP), the time zone and the time itself; polkit asks for
// the administrator password when it has to. How the taskbar clock looks
// is the shell's (rayeshellrc [Clock], shell.md 3.21):
//   ShowSeconds, ShowDate (true/false), Use24h (auto, true, false)
// A clock row shows only once the shell ships its key in its defaults.
#pragma once

#include <KSharedConfig>
#include <QAbstractListModel>
#include <QDBusConnection>
#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

class DateTimeSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // timedated answered: without it nothing here can change
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // timedated was asked at least once (so "not available" is known, not a guess)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool automaticTime READ automaticTime NOTIFY changed)
    // a time service is installed (else "Set time automatically" is off for good)
    Q_PROPERTY(bool canAutomaticTime READ canAutomaticTime NOTIFY changed)
    Q_PROPERTY(bool synchronized READ synchronized NOTIFY changed)
    // "Europe/Paris"
    Q_PROPERTY(QString timeZone READ timeZone NOTIFY changed)
    // "(UTC+01:00) Paris"
    Q_PROPERTY(QString timeZoneLabel READ timeZoneLabel NOTIFY changed)
    // the big clock of the page, in the PC's time zone, every second
    Q_PROPERTY(QString timeText READ timeText NOTIFY tick)
    Q_PROPERTY(QString dateText READ dateText NOTIFY tick)
    // the last time the clock was set from the internet, "" when never
    Q_PROPERTY(QString lastSyncText READ lastSyncText NOTIFY changed)
    // a change is on its way (the password dialog may be open)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    Q_PROPERTY(bool showSeconds READ showSeconds WRITE setShowSeconds NOTIFY clockChanged)
    Q_PROPERTY(bool showDate READ showDate WRITE setShowDate NOTIFY clockChanged)
    // 0 automatic (the regional format decides), 1 24-hour, 2 12-hour
    Q_PROPERTY(int clockFormat READ clockFormat WRITE setClockFormat NOTIFY clockChanged)
    Q_PROPERTY(bool showSecondsAvailable READ showSecondsAvailable NOTIFY clockChanged)
    Q_PROPERTY(bool showDateAvailable READ showDateAvailable NOTIFY clockChanged)
    Q_PROPERTY(bool clockFormatAvailable READ clockFormatAvailable NOTIFY clockChanged)

public:
    explicit DateTimeSettings(QObject *parent = nullptr);
    // `bus` is for the tests (a private bus with a pretend timedated)
    DateTimeSettings(const QDBusConnection &bus, QObject *parent);

    bool available() const { return m_available; }
    bool ready() const { return m_ready; }
    bool automaticTime() const { return m_ntp; }
    bool canAutomaticTime() const { return m_canNtp; }
    bool synchronized() const { return m_synchronized; }
    QString timeZone() const { return m_timeZone; }
    QString timeZoneLabel() const;
    QString timeText() const;
    QString dateText() const;
    QString lastSyncText() const;
    bool busy() const { return m_busy > 0; }

    bool showSeconds() const;
    void setShowSeconds(bool on);
    bool showDate() const;
    void setShowDate(bool on);
    int clockFormat() const;
    void setClockFormat(int format);
    bool showSecondsAvailable() const;
    bool showDateAvailable() const;
    bool clockFormatAvailable() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void setAutomaticTime(bool on);
    // an id of the time zone list ("Europe/Paris"); anything else is refused
    Q_INVOKABLE void setTimeZone(const QString &id);
    // the date and time the user typed, in the PC's time zone; refused
    // while the time is set automatically
    Q_INVOKABLE void setDateTime(const QDateTime &local);
    // set the clock from the internet now: the time service starts again,
    // which makes it ask at once
    Q_INVOKABLE void syncNow();
    // the time on the PC's clock now, to start the "Change" dialog with:
    // { year, month (0-11), day, hour, minute } (a date handed to QML would
    // be shown in this program's own time zone, which may be an old one)
    Q_INVOKABLE QVariantMap nowParts() const;

    // "(UTC+05:30) Kolkata"; the offset is the one in force at `when`
    static QString labelFor(const QString &id, const QDateTime &when);
    // "+05:30", "-03:00", "+00:00"
    static QString offsetText(int seconds);
    // the city part of a zone id: "America/Argentina/Buenos_Aires" -> "Buenos Aires"
    static QString cityOf(const QString &id);
    // a 24-hour clock? 1/2 say so, 0 asks the locale
    static bool uses24h(int clockFormat, const QLocale &locale);
    // "14:02" or "2:02 PM" (with seconds when asked)
    static QString formatTime(const QDateTime &time, const QLocale &locale, int clockFormat, bool seconds);
    // "today, 14:02", "yesterday, 9:15 AM" or "31/10/2026, 14:02"
    static QString formatLastSync(const QDateTime &when, const QDateTime &now, const QLocale &locale, int clockFormat);
    // where the time service leaves a mark each time it set the clock
    static QDateTime lastSyncTime();

Q_SIGNALS:
    void changed();
    void clockChanged();
    void tick();
    void busyChanged();
    // after a change: ok, or why not ("denied" when the password was not
    // given, "failed" otherwise)
    void done(bool ok, const QString &error);

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void call(const QString &method, const QVariantList &arguments, std::function<void(bool)> then = {});
    void writeClock(const QString &key, const QString &value);
    QDateTime zoneNow() const;

    QDBusConnection m_bus;
    KSharedConfigPtr m_shell;
    QTimer m_ticker;
    bool m_available = false;
    bool m_ready = false;
    bool m_ntp = false;
    bool m_canNtp = false;
    bool m_synchronized = false;
    QString m_timeZone;
    int m_busy = 0;
};

// Every time zone, by offset then city, as Windows lists them, and the
// words a search may find each one by: the city, the region, the zone's
// name in the user's language, its abbreviation and "UTC+1".
class TimeZoneModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    // what the user typed in the search box
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, LabelRole, DescriptionRole, OffsetRole };

    struct Zone {
        QString id;
        QString label;       // "(UTC+01:00) Paris"
        QString description; // "Central European Time"
        int offset = 0;      // seconds east of UTC, now
        QString words;       // folded words a search looks in
    };

    explicit TimeZoneModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    // the row of a zone in the filtered list, -1 when hidden
    Q_INVOKABLE int indexOf(const QString &id) const;

    // every zone id worth listing: Region/City, no "Etc/", no old names
    static QStringList zoneIds();
    static Zone zoneFor(const QString &id, const QDateTime &when, const QLocale &locale);
    // lower case, accents gone ("Bogotá" -> "bogota")
    static QString fold(const QString &text);
    // every word of `query` starts a word of `zone` (or is its "utc+1")
    static bool matches(const Zone &zone, const QString &query);

Q_SIGNALS:
    void filterChanged();
    void countChanged();

private:
    void refilter();

    QList<Zone> m_all;
    QList<int> m_shown;
    QString m_filter;
};
