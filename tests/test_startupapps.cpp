// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Startup: which autostart entries are listed, and what turning
// one off or on writes, in folders of the test's own.
#include "startupapps.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

QByteArray entry(const QByteArray &name, const QByteArray &extra = {})
{
    return "[Desktop Entry]\nType=Application\nName=" + name + "\nExec=/bin/true\nIcon=app\n" + extra;
}

const StartupEntry *named(const QList<StartupEntry> &list, const QString &name)
{
    for (const StartupEntry &e : list) {
        if (e.name == name)
            return &e;
    }
    return nullptr;
}

} // namespace

class TestStartupApps : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_user;
    QString m_system;
    QString m_system2;

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_user = m_dir.filePath(QStringLiteral("config/autostart"));
        m_system = m_dir.filePath(QStringLiteral("xdg/autostart"));
        m_system2 = m_dir.filePath(QStringLiteral("xdg2/autostart"));
        for (const QString &d : {m_user, m_system, m_system2}) {
            QDir(d).removeRecursively();
            QDir().mkpath(d);
        }
    }

    void listsAppsNotDesktopPieces()
    {
        writeFile(m_system + QLatin1String("/chat.desktop"), entry("Chat"));
        // the shell itself: not something to turn off
        writeFile(m_system + QLatin1String("/shell.desktop"), entry("Shell", "NoDisplay=true\nOnlyShowIn=KDE;\n"));
        writeFile(m_system + QLatin1String("/gnome.desktop"), entry("Other desktop", "OnlyShowIn=GNOME;\n"));
        writeFile(m_system + QLatin1String("/notkde.desktop"), entry("Not here", "NotShowIn=KDE;\n"));
        writeFile(m_system + QLatin1String("/missing.desktop"), entry("Missing", "TryExec=/nonexistent/program\n"));
        writeFile(m_user + QLatin1String("/mine.desktop"), entry("Mine"));
        writeFile(m_user + QLatin1String("/link.desktop"), "[Desktop Entry]\nType=Link\nName=Link\nURL=https://example.org\n");

        const QList<StartupEntry> list = StartupApps::scan(m_user, {m_system});
        QStringList names;
        for (const StartupEntry &e : list)
            names << e.name;
        QCOMPARE(names, (QStringList{QStringLiteral("Chat"), QStringLiteral("Mine")}));
        QVERIFY(list.at(0).enabled && list.at(1).enabled);
    }

    void firstSystemFolderAndUserWin()
    {
        writeFile(m_system + QLatin1String("/a.desktop"), entry("First"));
        writeFile(m_system2 + QLatin1String("/a.desktop"), entry("Second"));
        QList<StartupEntry> list = StartupApps::scan(m_user, {m_system, m_system2});
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().name, QStringLiteral("First"));
        QCOMPARE(list.first().systemPath, m_system + QLatin1String("/a.desktop"));

        writeFile(m_user + QLatin1String("/a.desktop"), entry("Mine", "Hidden=true\n"));
        list = StartupApps::scan(m_user, {m_system, m_system2});
        QCOMPARE(list.first().name, QStringLiteral("Mine"));
        QVERIFY(!list.first().enabled);
    }

    void gnomeSwitchCounts()
    {
        writeFile(m_system + QLatin1String("/off.desktop"), entry("Off", "X-GNOME-Autostart-enabled=false\n"));
        const QList<StartupEntry> list = StartupApps::scan(m_user, {m_system});
        QCOMPARE(list.size(), 1);
        QVERIFY(!list.first().enabled);
    }

    void turnSystemEntryOffAndOn()
    {
        writeFile(m_system + QLatin1String("/chat.desktop"), entry("Chat"));
        StartupEntry e = *named(StartupApps::scan(m_user, {m_system}), QStringLiteral("Chat"));

        QVERIFY(StartupApps::writeEnabled(e, m_user, false));
        const QString copy = m_user + QLatin1String("/chat.desktop");
        QVERIFY(QFile::exists(copy));
        // the system file is never touched
        QCOMPARE(KConfig(e.systemPath, KConfig::SimpleConfig).group(QStringLiteral("Desktop Entry")).readEntry("Hidden", false), false);
        e = *named(StartupApps::scan(m_user, {m_system}), QStringLiteral("Chat"));
        QVERIFY(!e.enabled);
        QCOMPARE(e.path, copy);

        QVERIFY(StartupApps::writeEnabled(e, m_user, true));
        e = *named(StartupApps::scan(m_user, {m_system}), QStringLiteral("Chat"));
        QVERIFY(e.enabled);
    }

    void hidingStubGoesAway()
    {
        // another tool hid the system entry with a file that says only that
        writeFile(m_system + QLatin1String("/chat.desktop"), entry("Chat"));
        writeFile(m_user + QLatin1String("/chat.desktop"), "[Desktop Entry]\nHidden=true\n");
        StartupEntry e = *named(StartupApps::scan(m_user, {m_system}), QStringLiteral("Chat"));
        QVERIFY(!e.enabled);
        QVERIFY(StartupApps::writeEnabled(e, m_user, true));
        QVERIFY(!QFile::exists(m_user + QLatin1String("/chat.desktop")));
        QVERIFY(named(StartupApps::scan(m_user, {m_system}), QStringLiteral("Chat"))->enabled);
    }

    void turnedOffDesktopPieceStaysListed()
    {
        // someone turned a desktop piece off: it is listed so it can come back
        writeFile(m_system + QLatin1String("/files.desktop"), entry("File indexing", "NoDisplay=true\n"));
        QVERIFY(StartupApps::scan(m_user, {m_system}).isEmpty());
        writeFile(m_user + QLatin1String("/files.desktop"), entry("File indexing", "NoDisplay=true\nHidden=true\n"));
        const QList<StartupEntry> list = StartupApps::scan(m_user, {m_system});
        QCOMPARE(list.size(), 1);
        QVERIFY(!list.first().enabled);
    }

    void refusesOddNames()
    {
        StartupEntry e;
        e.fileName = QStringLiteral("../evil.desktop");
        e.systemPath = m_system + QLatin1String("/x.desktop");
        QVERIFY(!StartupApps::writeEnabled(e, m_user, false));
        QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("config/evil.desktop"))));
    }

    void modelAddsAndSorts()
    {
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(QStringLiteral("xdg")).toUtf8());
        writeFile(m_system + QLatin1String("/b.desktop"), entry("Bravo"));
        writeFile(m_system + QLatin1String("/a.desktop"), entry("Alpha"));
        const QString app = m_dir.filePath(QStringLiteral("share/applications/charlie.desktop"));
        writeFile(app, entry("Charlie"));

        StartupApps model;
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.addApp(app));
        QCOMPARE(model.rowCount(), 3);
        QVERIFY(QFile::exists(m_user + QLatin1String("/charlie.desktop")));
        QVERIFY(!model.addApp(QStringLiteral("relative.desktop")));

        QVERIFY(model.setEnabled(QStringLiteral("a.desktop"), false));
        model.setSortOrder(QStringLiteral("status"));
        QCOMPARE(model.data(model.index(0), StartupApps::NameRole).toString(), QStringLiteral("Bravo"));
        QCOMPARE(model.data(model.index(2), StartupApps::NameRole).toString(), QStringLiteral("Alpha"));
        QCOMPARE(model.data(model.index(2), StartupApps::EnabledRole).toBool(), false);
        model.setSortOrder(QStringLiteral("name"));
        QCOMPARE(model.data(model.index(0), StartupApps::NameRole).toString(), QStringLiteral("Alpha"));
    }
};

QTEST_GUILESS_MAIN(TestStartupApps)
#include "test_startupapps.moc"
