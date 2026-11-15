// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Date & time: the clock, the time zone list and its
// search, the taskbar clock keys, and the conversation with timedated,
// which here is dbusmock's on a bus of the test's own, so the
// computer's real clock is never touched. Then the three pages of Time &
// language load as the window loads them: no QML warning, and every row
// the search knows is on its page.
#include "datetime.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

namespace {

const QString mockService = QStringLiteral("org.freedesktop.timedate1");
const QString mockPath = QStringLiteral("/org/freedesktop/timedate1");

QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg
        && (qstrcmp(context.category, "qml") == 0 || qstrncmp(context.category, "qt.qml", 6) == 0
            || message.contains(QLatin1String("qrc:")))) {
        qmlWarnings << message;
    }
    fprintf(stderr, "%s\n", qPrintable(message));
}

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

// A bus standing in for the system bus, with dbusmock's timedated on it.
struct PretendClock {
    QProcess bus;
    QProcess mock;
    QString address;
    bool running = false;

    void start()
    {
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/raye-test-bus");
        if (QStandardPaths::findExecutable(QStringLiteral("dbus-daemon")).isEmpty())
            return;
        bus.start(QStringLiteral("dbus-daemon"),
                  {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!bus.waitForStarted() || !bus.waitForReadyRead(5000))
            return;
        address = QString::fromUtf8(bus.readLine()).trimmed();
        if (address.isEmpty())
            return;
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", address.toUtf8());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"), address);
        mock.setProcessEnvironment(env);
        mock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        mock.start(QStringLiteral("python3"), {QStringLiteral("-m"), QStringLiteral("dbusmock"), QStringLiteral("--system"),
                                               QStringLiteral("--template"), QStringLiteral("timedated"),
                                               QStringLiteral("--parameters"),
                                               QStringLiteral("{\"Timezone\": \"Europe/Paris\", \"NTP\": true}")});
        if (!mock.waitForStarted())
            return;
        QDBusConnection connection = QDBusConnection::connectToBus(address, QStringLiteral("raye-test-setup"));
        for (int i = 0; i < 100 && !running; ++i) {
            QTest::qWait(50);
            running = connection.isConnected() && connection.interface()->isServiceRegistered(mockService);
        }
    }

    ~PretendClock()
    {
        mock.kill();
        mock.waitForFinished();
        bus.kill();
        bus.waitForFinished();
    }
};

PretendClock *pretend = nullptr;

QDBusConnection testBus()
{
    return QDBusConnection::connectToBus(pretend->address, QStringLiteral("raye-test-setup"));
}

QVariant mockProperty(const QString &name)
{
    QDBusMessage get = QDBusMessage::createMethodCall(mockService, mockPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << mockService << name;
    const QDBusMessage reply = testBus().call(get, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

} // namespace

class TestDateTime : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_home;
    QString m_system;

    void clean()
    {
        for (const QString &dir : {m_home, m_system}) {
            QDir(dir).removeRecursively();
            QDir().mkpath(dir);
        }
        KSharedConfig::openConfig(QStringLiteral("rayeshellrc"))->reparseConfiguration();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_home = m_dir.filePath(QStringLiteral("config"));
        m_system = m_dir.filePath(QStringLiteral("xdg"));
        qputenv("XDG_CONFIG_HOME", m_home.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_system.toUtf8());
        // no session bus: the shell is not told
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void init() { clean(); }

    void offsets()
    {
        QCOMPARE(DateTimeSettings::offsetText(3600), QStringLiteral("+01:00"));
        QCOMPARE(DateTimeSettings::offsetText(19800), QStringLiteral("+05:30"));
        QCOMPARE(DateTimeSettings::offsetText(-10800), QStringLiteral("-03:00"));
        QCOMPARE(DateTimeSettings::offsetText(0), QStringLiteral("+00:00"));
        QCOMPARE(DateTimeSettings::offsetText(-34200), QStringLiteral("-09:30"));
    }

    void cities()
    {
        QCOMPARE(DateTimeSettings::cityOf(QStringLiteral("Europe/Paris")), QStringLiteral("Paris"));
        QCOMPARE(DateTimeSettings::cityOf(QStringLiteral("America/Argentina/Buenos_Aires")), QStringLiteral("Buenos Aires"));
    }

    // the offset is the one in force at the time asked: summer and winter
    void labels()
    {
        const QDateTime winter(QDate(2026, 1, 15), QTime(12, 0), QTimeZone::UTC);
        const QDateTime summer(QDate(2026, 7, 15), QTime(12, 0), QTimeZone::UTC);
        QCOMPARE(DateTimeSettings::labelFor(QStringLiteral("Europe/Paris"), winter), QStringLiteral("(UTC+01:00) Paris"));
        QCOMPARE(DateTimeSettings::labelFor(QStringLiteral("Europe/Paris"), summer), QStringLiteral("(UTC+02:00) Paris"));
        QCOMPARE(DateTimeSettings::labelFor(QStringLiteral("Asia/Kolkata"), winter), QStringLiteral("(UTC+05:30) Kolkata"));
        QCOMPARE(DateTimeSettings::labelFor(QStringLiteral("America/Toronto"), winter), QStringLiteral("(UTC-05:00) Toronto"));
        QCOMPARE(DateTimeSettings::labelFor(QStringLiteral("UTC"), winter), QStringLiteral("(UTC) Coordinated Universal Time"));
        // an id that is not a zone is shown as it is
        QCOMPARE(DateTimeSettings::labelFor(QStringLiteral("Nowhere/Land"), winter), QStringLiteral("Nowhere/Land"));
    }

    void timeFormats()
    {
        const QDateTime t(QDate(2026, 10, 31), QTime(14, 2, 5));
        const QLocale us(QStringLiteral("en_US"));
        const QLocale fr(QStringLiteral("fr_FR"));
        // automatic: the regional format decides
        QCOMPARE(DateTimeSettings::formatTime(t, fr, 0, false), QStringLiteral("14:02"));
        QVERIFY(DateTimeSettings::formatTime(t, us, 0, false).startsWith(QLatin1String("2:02")));
        QVERIFY(!DateTimeSettings::uses24h(0, us));
        QVERIFY(DateTimeSettings::uses24h(0, fr));
        // the user's choice wins
        QCOMPARE(DateTimeSettings::formatTime(t, us, 1, false), QStringLiteral("14:02"));
        QCOMPARE(DateTimeSettings::formatTime(t, us, 1, true), QStringLiteral("14:02:05"));
        QVERIFY(DateTimeSettings::formatTime(t, fr, 2, false).startsWith(QLatin1String("2:02")));
        QVERIFY(DateTimeSettings::formatTime(t, fr, 2, true).startsWith(QLatin1String("2:02:05")));
    }

    void lastSync()
    {
        const QLocale fr(QStringLiteral("fr_FR"));
        const QDateTime now(QDate(2026, 10, 31), QTime(18, 0));
        QCOMPARE(DateTimeSettings::formatLastSync(QDateTime(QDate(2026, 10, 31), QTime(14, 2)), now, fr, 0),
                 QStringLiteral("today, 14:02"));
        QCOMPARE(DateTimeSettings::formatLastSync(QDateTime(QDate(2026, 10, 30), QTime(9, 15)), now, fr, 0),
                 QStringLiteral("yesterday, 09:15"));
        QCOMPARE(DateTimeSettings::formatLastSync(QDateTime(QDate(2026, 10, 2), QTime(9, 15)), now, fr, 1),
                 QStringLiteral("02/10/2026, 09:15"));
    }

    void zoneList()
    {
        const QStringList ids = TimeZoneModel::zoneIds();
        QVERIFY(ids.contains(QStringLiteral("Europe/Paris")));
        QVERIFY(ids.contains(QStringLiteral("America/Argentina/Buenos_Aires")));
        QVERIFY(ids.contains(QStringLiteral("UTC")));
        // old names and offsets without a place are left out
        QVERIFY(!ids.contains(QStringLiteral("US/Eastern")));
        QVERIFY(!ids.contains(QStringLiteral("Etc/GMT+3")));
        QVERIFY(ids.size() > 300);

        TimeZoneModel model;
        QCOMPARE(model.rowCount(), ids.size());
        // by offset, west first, as Windows lists them
        int last = -24 * 3600;
        for (int row = 0; row < model.rowCount(); ++row) {
            const int offset = model.data(model.index(row, 0), TimeZoneModel::OffsetRole).toInt();
            QVERIFY(offset >= last);
            last = offset;
        }
    }

    void zoneSearch_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("expected");
        QTest::newRow("city") << "paris" << "Europe/Paris";
        QTest::newRow("prefix") << "par" << "Europe/Paris";
        QTest::newRow("accents") << "bogota" << "America/Bogota";
        QTest::newRow("region") << "europe paris" << "Europe/Paris";
        QTest::newRow("two words") << "buenos aires" << "America/Argentina/Buenos_Aires";
        QTest::newRow("utc short") << "utc+5:30" << "Asia/Kolkata";
        QTest::newRow("upper case") << "TOKYO" << "Asia/Tokyo";
    }

    void zoneSearch()
    {
        QFETCH(QString, query);
        QFETCH(QString, expected);
        TimeZoneModel model;
        model.setFilter(query);
        QVERIFY2(model.indexOf(expected) >= 0, qPrintable(query));
        QVERIFY(model.rowCount() < TimeZoneModel::zoneIds().size());
    }

    void zoneSearchMisses()
    {
        TimeZoneModel model;
        QSignalSpy count(&model, &TimeZoneModel::countChanged);
        model.setFilter(QStringLiteral("zzzqqq"));
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(count.count(), 1);
        QCOMPARE(model.indexOf(QStringLiteral("Europe/Paris")), -1);
        // every word must match: Paris is not in America
        model.setFilter(QStringLiteral("america paris"));
        QCOMPARE(model.indexOf(QStringLiteral("Europe/Paris")), -1);
        model.setFilter(QString());
        QCOMPARE(model.rowCount(), TimeZoneModel::zoneIds().size());
    }

    void fold()
    {
        QCOMPARE(TimeZoneModel::fold(QStringLiteral("Bogotá")), QStringLiteral("bogota"));
        QCOMPARE(TimeZoneModel::fold(QStringLiteral("Zürich")), QStringLiteral("zurich"));
    }

    // the clock rows show once the shell ships the keys, and write the
    // shell's file
    void clockKeys()
    {
        DateTimeSettings settings(QDBusConnection(QStringLiteral("none")), nullptr);
        QVERIFY(!settings.showSecondsAvailable());
        QVERIFY(!settings.clockFormatAvailable());
        QVERIFY(!settings.showSeconds());
        QVERIFY(settings.showDate());
        QCOMPARE(settings.clockFormat(), 0);

        writeFile(m_system + QStringLiteral("/rayeshellrc"), "[Clock]\nShowSeconds=false\nShowDate=true\nUse24h=auto\n");
        settings.reload();
        QVERIFY(settings.showSecondsAvailable());
        QVERIFY(settings.showDateAvailable());
        QVERIFY(settings.clockFormatAvailable());

        QSignalSpy changed(&settings, &DateTimeSettings::clockChanged);
        settings.setShowSeconds(true);
        settings.setShowDate(false);
        settings.setClockFormat(1);
        QCOMPARE(changed.count(), 3);
        const QString file = m_home + QStringLiteral("/rayeshellrc");
        QCOMPARE(entry(file, QStringLiteral("Clock"), QStringLiteral("ShowSeconds")), QStringLiteral("true"));
        QCOMPARE(entry(file, QStringLiteral("Clock"), QStringLiteral("ShowDate")), QStringLiteral("false"));
        QCOMPARE(entry(file, QStringLiteral("Clock"), QStringLiteral("Use24h")), QStringLiteral("true"));
        settings.setClockFormat(2);
        QCOMPARE(entry(file, QStringLiteral("Clock"), QStringLiteral("Use24h")), QStringLiteral("false"));
        settings.setClockFormat(0);
        QCOMPARE(entry(file, QStringLiteral("Clock"), QStringLiteral("Use24h")), QStringLiteral("auto"));
        // nonsense is refused
        settings.setClockFormat(7);
        QCOMPARE(entry(file, QStringLiteral("Clock"), QStringLiteral("Use24h")), QStringLiteral("auto"));
    }

    // timedated not there: nothing can change, nothing breaks
    void noService()
    {
        DateTimeSettings settings(QDBusConnection(QStringLiteral("none")), nullptr);
        QTRY_VERIFY(settings.ready());
        QVERIFY(!settings.available());
        QVERIFY(!settings.timeText().isEmpty());
        QVERIFY(!settings.dateText().isEmpty());
        const QVariantMap now = settings.nowParts();
        QVERIFY(now.value(QStringLiteral("year")).toInt() >= 2026);
        QVERIFY(now.value(QStringLiteral("month")).toInt() >= 0 && now.value(QStringLiteral("month")).toInt() <= 11);
        QVERIFY(now.value(QStringLiteral("hour")).toInt() >= 0 && now.value(QStringLiteral("hour")).toInt() <= 23);
    }

    // the dialog starts from the PC's clock in the PC's zone, not this
    // program's: Tokyo's hour, whatever zone the test runs in
    void nowPartsInZone()
    {
        if (!pretend->running)
            QSKIP("dbus-daemon or python3-dbusmock is missing");
        DateTimeSettings settings(testBus(), nullptr);
        QTRY_VERIFY(settings.available());
        settings.setTimeZone(QStringLiteral("Asia/Tokyo"));
        QTRY_COMPARE(settings.timeZone(), QStringLiteral("Asia/Tokyo"));
        const QDateTime tokyo = QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Tokyo"));
        QCOMPARE(settings.nowParts().value(QStringLiteral("hour")).toInt(), tokyo.time().hour());
        settings.setTimeZone(QStringLiteral("Europe/Paris"));
        QTRY_COMPARE(settings.timeZone(), QStringLiteral("Europe/Paris"));
    }

    void readsTimedated()
    {
        if (!pretend->running)
            QSKIP("dbus-daemon or python3-dbusmock is missing");
        DateTimeSettings settings(testBus(), nullptr);
        QTRY_VERIFY(settings.available());
        QVERIFY(settings.automaticTime());
        QVERIFY(settings.canAutomaticTime());
        QCOMPARE(settings.timeZone(), QStringLiteral("Europe/Paris"));
        QVERIFY(settings.timeZoneLabel().endsWith(QLatin1String(") Paris")));
    }

    void changesTimeZone()
    {
        if (!pretend->running)
            QSKIP("dbus-daemon or python3-dbusmock is missing");
        DateTimeSettings settings(testBus(), nullptr);
        QTRY_VERIFY(settings.available());
        QSignalSpy done(&settings, &DateTimeSettings::done);
        settings.setTimeZone(QStringLiteral("Asia/Tokyo"));
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(done.first().at(0).toBool());
        QCOMPARE(mockProperty(QStringLiteral("Timezone")).toString(), QStringLiteral("Asia/Tokyo"));
        QTRY_COMPARE(settings.timeZone(), QStringLiteral("Asia/Tokyo"));
        QVERIFY(!settings.busy());

        // only a zone of the list: no paths, no made-up names
        settings.setTimeZone(QStringLiteral("../../etc/passwd"));
        QCOMPARE(done.count(), 2);
        QVERIFY(!done.last().at(0).toBool());
        QCOMPARE(mockProperty(QStringLiteral("Timezone")).toString(), QStringLiteral("Asia/Tokyo"));
        settings.setTimeZone(QStringLiteral("Europe/Paris"));
        QTRY_COMPARE(settings.timeZone(), QStringLiteral("Europe/Paris"));
    }

    void automaticTimeAndManualTime()
    {
        if (!pretend->running)
            QSKIP("dbus-daemon or python3-dbusmock is missing");
        DateTimeSettings settings(testBus(), nullptr);
        QTRY_VERIFY(settings.available());
        QSignalSpy done(&settings, &DateTimeSettings::done);
        // set automatically: the time cannot be typed in
        settings.setDateTime(QDateTime(QDate(2026, 1, 1), QTime(9, 0)));
        QCOMPARE(done.count(), 1);
        QVERIFY(!done.first().at(0).toBool());

        settings.setAutomaticTime(false);
        QTRY_COMPARE(done.count(), 2);
        QVERIFY(done.last().at(0).toBool());
        QCOMPARE(mockProperty(QStringLiteral("NTP")).toBool(), false);
        QTRY_VERIFY(!settings.automaticTime());

        settings.setDateTime(QDateTime(QDate(2026, 1, 1), QTime(9, 0)));
        QTRY_COMPARE(done.count(), 3);
        QVERIFY(done.last().at(0).toBool());

        settings.setAutomaticTime(true);
        QTRY_COMPARE(done.count(), 4);
        QTRY_VERIFY(settings.automaticTime());
    }

    // "Sync now" turns the time service off and on: it ends on
    void syncNow()
    {
        if (!pretend->running)
            QSKIP("dbus-daemon or python3-dbusmock is missing");
        DateTimeSettings settings(testBus(), nullptr);
        QTRY_VERIFY(settings.available());
        QTRY_VERIFY(settings.automaticTime());
        QSignalSpy done(&settings, &DateTimeSettings::done);
        settings.syncNow();
        QVERIFY(settings.busy());
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(!settings.busy());
        QCOMPARE(mockProperty(QStringLiteral("NTP")).toBool(), true);
    }

    // ---- the pages, loaded as the window loads them -----------------

    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        QTest::newRow("date-time") << "pages/time/DateTimePage.qml" << "time/date-time";
        QTest::newRow("language") << "pages/time/LanguagePage.qml" << "time/language";
        QTest::newRow("typing") << "pages/time/TypingPage.qml" << "time/typing";
    }

    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);
        // every clock row present
        writeFile(m_system + QStringLiteral("/rayeshellrc"), "[Clock]\nShowSeconds=false\nShowDate=true\nUse24h=auto\n");

        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/time.json"));
        QVERIFY(fragment.open(QIODevice::ReadOnly));
        QStringList indexed;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        QVERIFY(!entries.isEmpty());
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
            if (id.startsWith(page + QLatin1Char('#')))
                indexed << id.section(QLatin1Char('#'), 1);
        }
        QVERIFY2(!indexed.isEmpty(), qPrintable(page));

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            QQuickView view(&engine, nullptr);
            view.setResizeMode(QQuickView::SizeRootObjectToView);
            view.resize(1120, 760);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + file));
            QCOMPARE(view.status(), QQuickView::Ready);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(300);

            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            const QStringList drawn = ids.toStringList();
            for (const QString &id : std::as_const(indexed))
                QVERIFY2(drawn.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not on the page")));
            for (const QString &id : drawn)
                QVERIFY2(indexed.contains(id), qPrintable(page + QLatin1Char('#') + id + QStringLiteral(" is not in data/search")));
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

int main(int argc, char *argv[])
{
    // the pretend timedated must be on the bus before anything in here
    // opens a connection to the system bus
    QGuiApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("raye-settings");
    PretendClock clock;
    pretend = &clock;
    clock.start();
    TestDateTime test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "test_datetime.moc"
