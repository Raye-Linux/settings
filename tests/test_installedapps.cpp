// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Installed apps: what dpkg, raye-windows and the uninstallers
// say, read the way the page needs it; the list sorted and filtered;
// and the model on an empty Flatpak installation of the test's own.
#include "installedapps.h"

#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

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
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

InstalledApp make(const QString &name, const QString &source, qint64 size, const QDateTime &when)
{
    InstalledApp a;
    a.id = source + QLatin1Char(':') + name;
    a.name = name;
    a.source = source;
    a.size = size;
    a.installed = when;
    return a;
}

QStringList names(const QList<InstalledApp> &list)
{
    QStringList n;
    for (const InstalledApp &a : list)
        n << a.name;
    return n;
}

} // namespace

class TestInstalledApps : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", m_dir.filePath(QStringLiteral("share")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
        // an empty Flatpak installation, no system one, no tools
        qputenv("FLATPAK_USER_DIR", m_dir.filePath(QStringLiteral("flatpak")).toUtf8());
        qputenv("FLATPAK_SYSTEM_DIR", m_dir.filePath(QStringLiteral("flatpak-system")).toUtf8());
        qputenv("PATH", "/nonexistent");
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void packageOwners()
    {
        const QByteArray out = "kate: /usr/share/applications/org.kde.kate.desktop\n"
                               "libfoo:amd64, foo-common: /usr/share/applications/foo.desktop\n"
                               "diversion by raye-settings from: /usr/share/applications/systemsettings.desktop\n"
                               "dpkg-query: no path found matching pattern /x\n";
        const auto owners = InstalledApps::parsePackageOwners(out);
        QCOMPARE(owners.size(), 2);
        QCOMPARE(owners.value(QStringLiteral("/usr/share/applications/org.kde.kate.desktop")), QStringLiteral("kate"));
        QCOMPARE(owners.value(QStringLiteral("/usr/share/applications/foo.desktop")), QStringLiteral("libfoo"));
    }

    void packageInfo()
    {
        const auto info = InstalledApps::parsePackageInfo("kate\t4:25.12.3-0\t12345\nlibx:amd64\t1.0\t\nbroken\n");
        QCOMPARE(info.value(QStringLiteral("kate")).first, QStringLiteral("4:25.12.3-0"));
        QCOMPARE(info.value(QStringLiteral("kate")).second, qint64(12345) * 1024);
        QCOMPARE(info.value(QStringLiteral("libx")).second, qint64(-1));
        QVERIFY(!info.contains(QStringLiteral("broken")));
    }

    void versions()
    {
        QCOMPARE(InstalledApps::displayVersion(QStringLiteral("4:25.12.3-0build1")), QStringLiteral("25.12.3"));
        QCOMPARE(InstalledApps::displayVersion(QStringLiteral("25.12.3+raye1")), QStringLiteral("25.12.3"));
        QCOMPARE(InstalledApps::displayVersion(QStringLiteral("8:7.1.2.18+dfsg1-1")), QStringLiteral("7.1.2.18"));
        QCOMPARE(InstalledApps::displayVersion(QStringLiteral("1.0~rc1-2")), QStringLiteral("1.0"));
        QCOMPARE(InstalledApps::displayVersion(QStringLiteral("2024.1")), QStringLiteral("2024.1"));
        QCOMPARE(InstalledApps::displayVersion(QString()), QString());
    }

    void depends()
    {
        const QStringList d = InstalledApps::parseDepends(
            QStringLiteral("plasma-desktop (>= 4:6.6), raye-settings, kate | kwrite, foo:any, , python3 [amd64]\nbar"));
        QCOMPARE(d, (QStringList{QStringLiteral("plasma-desktop"), QStringLiteral("raye-settings"), QStringLiteral("kate"),
                                 QStringLiteral("kwrite"), QStringLiteral("foo"), QStringLiteral("python3"), QStringLiteral("bar")}));
    }

    void windowsList()
    {
        const QByteArray json = R"([
            {"space": "notepad", "key": "Notepad++", "name": "Notepad++", "publisher": "Notepad++ Team",
             "version": "8.7.9", "size": 4096, "installDate": "2026-10-01", "icon": "/home/u/.cache/i.png",
             "removable": true, "desktopFiles": ["/home/u/.local/share/applications/raye-win-notepad-notepad.desktop"]},
            {"space": "x", "key": "", "name": "no key"},
            {"space": "game", "key": "{GUID}", "name": "Game", "size": null, "icon": "relative.png", "removable": false}
        ])";
        const QList<InstalledApp> list = InstalledApps::parseWindowsList(json);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).id, QStringLiteral("windows:notepad/Notepad++"));
        QCOMPARE(list.at(0).source, QStringLiteral("windows"));
        QCOMPARE(list.at(0).size, qint64(4096));
        QCOMPARE(list.at(0).installed.date(), QDate(2026, 10, 1));
        QCOMPARE(list.at(0).iconSource, QStringLiteral("/home/u/.cache/i.png"));
        QVERIFY(list.at(0).removable);
        QCOMPARE(list.at(1).size, qint64(-1));
        QVERIFY(list.at(1).iconSource.isEmpty());
        QVERIFY(!list.at(1).removable);
        QVERIFY(InstalledApps::parseWindowsList("not json").isEmpty());
    }

    void windowsProfiles()
    {
        QList<InstalledApp> list = InstalledApps::parseWindowsList(R"([
            {"space": "winrar", "key": "WinRAR", "name": "WinRAR"},
            {"space": "fl-studio", "key": "FL", "name": "FL Studio"},
            {"space": "tool", "key": "T", "name": "Tool"}])");
        InstalledApps::applyWindowsProfiles(list, R"({"winrar": {"profile": "winrar", "optimized": true},
            "fl-studio": {"profile": "fl-studio", "optimized": false}})");
        QCOMPARE(list.at(0).profile, QStringLiteral("optimized"));
        QCOMPARE(list.at(1).profile, QStringLiteral("default"));
        QCOMPARE(list.at(2).profile, QStringLiteral("default"));
        // no answer at all: never a claim
        InstalledApps::applyWindowsProfiles(list, "");
        QCOMPARE(list.at(0).profile, QStringLiteral("default"));
    }

    void systemAppsNeedAPackage()
    {
        const QString folder = m_dir.filePath(QStringLiteral("apps"));
        writeFile(folder + QLatin1String("/org.kde.kate.desktop"), "[Desktop Entry]\nType=Application\nName=Kate\nExec=kate\nIcon=kate\n");
        writeFile(folder + QLatin1String("/hidden.desktop"), "[Desktop Entry]\nType=Application\nName=H\nExec=h\nNoDisplay=true\n");
        writeFile(folder + QLatin1String("/loose.desktop"), "[Desktop Entry]\nType=Application\nName=Loose\nExec=l\n");
        const QHash<QString, QString> owners{{folder + QLatin1String("/org.kde.kate.desktop"), QStringLiteral("kate")},
                                             {folder + QLatin1String("/hidden.desktop"), QStringLiteral("h")}};
        const QList<InstalledApp> list = InstalledApps::systemApps({folder}, owners);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().id, QStringLiteral("desktop:org.kde.kate.desktop"));
        QCOMPARE(list.first().appId, QStringLiteral("org.kde.kate"));
        QCOMPARE(list.first().package, QStringLiteral("kate"));
        QCOMPARE(list.first().source, QStringLiteral("system"));
    }

    void arrange()
    {
        const QDateTime day(QDate(2026, 10, 1), QTime(10, 0));
        const QList<InstalledApp> all{make(QStringLiteral("beta"), QStringLiteral("store"), 300, day.addDays(-3)),
                                      make(QStringLiteral("Alpha"), QStringLiteral("system"), -1, day),
                                      make(QStringLiteral("gamma"), QStringLiteral("windows"), 900, QDateTime()),
                                      make(QStringLiteral("Delta"), QStringLiteral("system"), 500, day.addDays(-1))};
        QCOMPARE(names(InstalledApps::arrange(all, QStringLiteral("all"), {}, QStringLiteral("name"))),
                 (QStringList{QStringLiteral("Alpha"), QStringLiteral("beta"), QStringLiteral("Delta"), QStringLiteral("gamma")}));
        QCOMPARE(names(InstalledApps::arrange(all, QStringLiteral("all"), {}, QStringLiteral("size"))),
                 (QStringList{QStringLiteral("gamma"), QStringLiteral("Delta"), QStringLiteral("beta"), QStringLiteral("Alpha")}));
        // newest first, unknown dates last
        QCOMPARE(names(InstalledApps::arrange(all, QStringLiteral("all"), {}, QStringLiteral("date"))),
                 (QStringList{QStringLiteral("Alpha"), QStringLiteral("Delta"), QStringLiteral("beta"), QStringLiteral("gamma")}));
        QCOMPARE(names(InstalledApps::arrange(all, QStringLiteral("system"), {}, QStringLiteral("name"))),
                 (QStringList{QStringLiteral("Alpha"), QStringLiteral("Delta")}));
        QCOMPARE(names(InstalledApps::arrange(all, QStringLiteral("all"), QStringLiteral("ELT"), QStringLiteral("name"))),
                 QStringList{QStringLiteral("Delta")});
    }

    void appDataFolder()
    {
        QCOMPARE(InstalledApps::appDataFolder(QStringLiteral("/home/u"), QStringLiteral("org.mozilla.firefox")),
                 QStringLiteral("/home/u/.var/app/org.mozilla.firefox"));
        for (const char *bad : {"..", "../..", "org/../../etc", "firefox", "org.mozilla.firefox/..", ".hidden.app", "a..b", ""})
            QVERIFY2(InstalledApps::appDataFolder(QStringLiteral("/home/u"), QString::fromLatin1(bad)).isEmpty(), bad);
        QVERIFY(InstalledApps::appDataFolder(QStringLiteral("relative"), QStringLiteral("org.a.b")).isEmpty());
    }

    void progress()
    {
        const QVariantMap step = InstalledApps::parseProgress(R"({"state":"removing","percent":40,"message":"Removing Kate"})");
        QCOMPARE(step.value(QStringLiteral("state")).toString(), QStringLiteral("removing"));
        QCOMPARE(step.value(QStringLiteral("percent")).toInt(), 40);
        QCOMPARE(step.value(QStringLiteral("message")).toString(), QStringLiteral("Removing Kate"));
        QVERIFY(InstalledApps::parseProgress("Removing...").isEmpty());
    }

    void modelLoads()
    {
        InstalledApps model;
        QSignalSpy loaded(&model, &InstalledApps::loadingChanged);
        QVERIFY(model.loading());
        QVERIFY(loaded.wait(20000));
        QVERIFY(!model.loading());
        // nothing to show in an empty world, and nothing breaks
        QCOMPARE(model.total(), 0);
        QVERIFY(model.app(QStringLiteral("store:nothing")).isEmpty());
        QSignalSpy finished(&model, &InstalledApps::uninstallFinished);
        QVERIFY(!model.uninstall(QStringLiteral("store:nothing")));
        QVERIFY(!model.resetData(QStringLiteral("store:nothing")));
        model.setFilter(QStringLiteral("nonsense"));
        QCOMPARE(model.filter(), QStringLiteral("all"));
        model.setSortOrder(QStringLiteral("size"));
        QCOMPARE(model.sortOrder(), QStringLiteral("size"));
    }

    // the four pages of Apps load as Settings shows them: no QML warning,
    // and every row the search knows is on its page
    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        QTest::newRow("installed") << QStringLiteral("pages/apps/InstalledAppsPage.qml") << QStringLiteral("apps/installed");
        QTest::newRow("default") << QStringLiteral("pages/apps/DefaultAppsPage.qml") << QStringLiteral("apps/default");
        QTest::newRow("startup") << QStringLiteral("pages/apps/StartupAppsPage.qml") << QStringLiteral("apps/startup");
        QTest::newRow("features") << QStringLiteral("pages/apps/FeaturesPage.qml") << QStringLiteral("apps/features");
    }

    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);

        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/apps.json"));
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
            QTest::qWait(500);

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

QTEST_MAIN(TestInstalledApps)
#include "test_installedapps.moc"
