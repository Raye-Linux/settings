// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Default apps: reading defaults in the order the XDG spec gives,
// writing a choice where it wins, roles and the type search; all in
// folders of the test's own.
#include "defaultapps.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
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
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

QByteArray app(const QByteArray &name, const QByteArray &mime, const QByteArray &extra = {})
{
    return "[Desktop Entry]\nType=Application\nName=" + name + "\nExec=" + name.toLower() + " %U\nIcon=" + name.toLower()
        + "\nMimeType=" + mime + "\n" + extra;
}

QStringList ids(const QVariantList &rows, const char *key = "id")
{
    QStringList list;
    for (const QVariant &v : rows)
        list << v.toMap().value(QString::fromLatin1(key)).toString();
    return list;
}

} // namespace

class TestDefaultApps : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_config;
    QString m_xdg;
    QString m_data;
    QString m_share;

    QString apps() const { return m_share + QLatin1String("/applications/"); }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_config = m_dir.filePath(QStringLiteral("config"));
        m_xdg = m_dir.filePath(QStringLiteral("xdg"));
        m_data = m_dir.filePath(QStringLiteral("data"));
        m_share = m_dir.filePath(QStringLiteral("share"));
        qputenv("XDG_CONFIG_HOME", m_config.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_xdg.toUtf8());
        qputenv("XDG_DATA_HOME", m_data.toUtf8());
        qputenv("XDG_DATA_DIRS", m_share.toUtf8());
        qputenv("XDG_CURRENT_DESKTOP", "KDE");
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        // no service cache to rebuild here
        qputenv("PATH", "/nonexistent");
    }

    void init()
    {
        for (const QString &d : {m_config, m_xdg, m_data, m_share}) {
            QDir(d).removeRecursively();
            QDir().mkpath(d);
        }
        // the file types come from the system's database, through the data dirs
        QVERIFY(QFile::link(QStringLiteral("/usr/share/mime"), m_share + QLatin1String("/mime")));
        KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->reparseConfiguration();
        writeFile(apps() + QLatin1String("firefox.desktop"), app("Firefox", "text/html;x-scheme-handler/http;x-scheme-handler/https;"));
        writeFile(apps() + QLatin1String("browser2.desktop"), app("Browser2", "text/html;x-scheme-handler/http;"));
        writeFile(apps() + QLatin1String("kate.desktop"), app("Kate", "text/plain;"));
        writeFile(apps() + QLatin1String("notes.desktop"), app("Notes", "text/plain;", "X-KDE-InitialPreference=9\n"));
        writeFile(apps() + QLatin1String("viewer.desktop"), app("Viewer", "image/png;image/jpeg;"));
        writeFile(apps() + QLatin1String("hidden.desktop"), app("Hidden", "text/plain;", "Hidden=true\n"));
        writeFile(apps() + QLatin1String("helper.desktop"), app("Helper", "text/plain;", "NoDisplay=true\n"));
        writeFile(apps() + QLatin1String("konsole.desktop"), app("Konsole", "", "Categories=System;TerminalEmulator;\n"));
        writeFile(apps() + QLatin1String("xterm.desktop"), app("XTerm", "", "Categories=TerminalEmulator;\n"));
        writeFile(apps() + QLatin1String("kde/sub.desktop"), app("Sub", "application/pdf;"));
    }

    void scanFindsEntries()
    {
        const auto all = AppEntries::scan(AppEntries::folders());
        QVERIFY(all.contains(QStringLiteral("firefox.desktop")));
        QVERIFY(!all.contains(QStringLiteral("hidden.desktop")));
        // a sub folder makes the id with a dash
        QVERIFY(all.contains(QStringLiteral("kde-sub.desktop")));
        QVERIFY(all.value(QStringLiteral("helper.desktop")).noDisplay);
    }

    void precedence()
    {
        const auto all = AppEntries::scan(AppEntries::folders());
        const QStringList files = DefaultApps::mimeappsFiles();
        QCOMPARE(files.first(), m_config + QLatin1String("/kde-mimeapps.list"));
        QCOMPARE(files.at(1), m_config + QLatin1String("/mimeapps.list"));

        writeFile(m_share + QLatin1String("/applications/mimeapps.list"), "[Default Applications]\ntext/plain=kate.desktop;\n");
        QCOMPARE(DefaultApps::defaultIn(files, QStringLiteral("text/plain"), all), QStringLiteral("kate.desktop"));
        writeFile(m_xdg + QLatin1String("/kde-mimeapps.list"), "[Default Applications]\ntext/plain=notes.desktop;\n");
        QCOMPARE(DefaultApps::defaultIn(files, QStringLiteral("text/plain"), all), QStringLiteral("notes.desktop"));
        // an app that is not installed is skipped
        writeFile(m_config + QLatin1String("/mimeapps.list"), "[Default Applications]\ntext/plain=gone.desktop;kate.desktop;\n");
        QCOMPARE(DefaultApps::defaultIn(files, QStringLiteral("text/plain"), all), QStringLiteral("kate.desktop"));
    }

    void writeWinsOverDesktopList()
    {
        writeFile(m_config + QLatin1String("/kde-mimeapps.list"), "[Default Applications]\ntext/plain=kate.desktop;\nimage/png=viewer.desktop;\n");
        QVERIFY(DefaultApps::writeDefaults(m_config + QLatin1String("/mimeapps.list"), m_config + QLatin1String("/kde-mimeapps.list"),
                                           {QStringLiteral("text/plain")}, QStringLiteral("notes.desktop")));
        KConfig user(m_config + QLatin1String("/mimeapps.list"), KConfig::SimpleConfig);
        QCOMPARE(user.group(QStringLiteral("Default Applications")).readXdgListEntry("text/plain"), QStringList{QStringLiteral("notes.desktop")});
        QCOMPARE(user.group(QStringLiteral("Added Associations")).readXdgListEntry("text/plain").value(0), QStringLiteral("notes.desktop"));
        KConfig own(m_config + QLatin1String("/kde-mimeapps.list"), KConfig::SimpleConfig);
        QVERIFY(!own.group(QStringLiteral("Default Applications")).hasKey("text/plain"));
        // other types of that file are left alone
        QVERIFY(own.group(QStringLiteral("Default Applications")).hasKey("image/png"));

        DefaultApps d;
        QCOMPARE(d.defaultFor(QStringLiteral("text/plain")), QStringLiteral("notes.desktop"));
    }

    void fallbacks()
    {
        DefaultApps d;
        // nobody named one: the highest preference among the apps that say they open it
        QCOMPARE(d.defaultFor(QStringLiteral("text/plain")), QStringLiteral("notes.desktop"));
        // a child type falls back to its parent ("text/x-csrc" is a text/plain)
        QCOMPARE(d.defaultFor(QStringLiteral("text/x-csrc")), QStringLiteral("notes.desktop"));
        QCOMPARE(d.defaultFor(QStringLiteral("application/x-nothing-opens-this")), QString());
    }

    void roles()
    {
        DefaultApps d;
        const QVariantList roles = d.roles();
        QCOMPARE(ids(roles).size(), 10);
        QCOMPARE(ids(roles).first(), QStringLiteral("browser"));
        QVERIFY(!roles.first().toMap().value(QStringLiteral("title")).toString().isEmpty());

        // the browser role is found by the apps that open web links
        QCOMPARE(ids(d.candidates(QStringLiteral("browser"))), (QStringList{QStringLiteral("browser2.desktop"), QStringLiteral("firefox.desktop")}));
        // a helper that hides from menus is not offered
        QVERIFY(!ids(d.candidates(QStringLiteral("text"))).contains(QStringLiteral("helper.desktop")));
        QCOMPARE(ids(d.candidates(QStringLiteral("terminal"))), (QStringList{QStringLiteral("konsole.desktop"), QStringLiteral("xterm.desktop")}));

        QSignalSpy changed(&d, &DefaultApps::changed);
        QVERIFY(d.setRoleDefault(QStringLiteral("browser"), QStringLiteral("firefox.desktop")));
        QCOMPARE(changed.count(), 1);
        KConfig user(m_config + QLatin1String("/mimeapps.list"), KConfig::SimpleConfig);
        const KConfigGroup g = user.group(QStringLiteral("Default Applications"));
        for (const char *type : {"x-scheme-handler/http", "x-scheme-handler/https", "text/html", "application/xhtml+xml"})
            QCOMPARE(g.readXdgListEntry(QString::fromLatin1(type)), QStringList{QStringLiteral("firefox.desktop")});
        KConfig globals(m_config + QLatin1String("/kdeglobals"), KConfig::SimpleConfig);
        QCOMPARE(globals.group(QStringLiteral("General")).readEntry("BrowserApplication"), QStringLiteral("firefox.desktop"));
        QCOMPARE(roles.size(), d.roles().size());
        QCOMPARE(d.roles().first().toMap().value(QStringLiteral("appId")).toString(), QStringLiteral("firefox.desktop"));

        // an app that is not installed is refused
        QVERIFY(!d.setRoleDefault(QStringLiteral("browser"), QStringLiteral("gone.desktop")));
        QVERIFY(!d.setRoleDefault(QStringLiteral("nothing"), QStringLiteral("firefox.desktop")));
    }

    void terminalRole()
    {
        DefaultApps d;
        QVERIFY(d.setRoleDefault(QStringLiteral("terminal"), QStringLiteral("xterm.desktop")));
        KConfig globals(m_config + QLatin1String("/kdeglobals"), KConfig::SimpleConfig);
        const KConfigGroup g = globals.group(QStringLiteral("General"));
        QCOMPARE(g.readEntry("TerminalApplication"), QStringLiteral("xterm"));
        QCOMPARE(g.readEntry("TerminalService"), QStringLiteral("xterm.desktop"));
        QCOMPARE(d.roles().at(6).toMap().value(QStringLiteral("appId")).toString(), QStringLiteral("xterm.desktop"));
    }

    void commandOf()
    {
        QCOMPARE(DefaultApps::commandOf(QStringLiteral("konsole --separate %u")), QStringLiteral("konsole --separate"));
        QCOMPARE(DefaultApps::commandOf(QStringLiteral("app %F %i %c")), QStringLiteral("app"));
    }

    void typeSearch()
    {
        DefaultApps d;
        QVariantList hits = d.searchTypes(QStringLiteral(".png"));
        QVERIFY(!hits.isEmpty());
        QCOMPARE(hits.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral(".png"));
        QCOMPARE(hits.first().toMap().value(QStringLiteral("type")).toString(), QStringLiteral("image/png"));
        QCOMPARE(hits.first().toMap().value(QStringLiteral("appId")).toString(), QStringLiteral("viewer.desktop"));
        // links are found by their name
        hits = d.searchTypes(QStringLiteral("https"));
        QVERIFY(ids(hits, "type").contains(QStringLiteral("x-scheme-handler/https")));
        QVERIFY(d.searchTypes(QStringLiteral("  ")).isEmpty());
        QVERIFY(d.searchTypes(QStringLiteral("p"), 5).size() <= 5);

        QCOMPARE(ids(d.candidatesForType(QStringLiteral("image/png"))), QStringList{QStringLiteral("viewer.desktop")});
        QVERIFY(d.setTypeDefault(QStringLiteral("text/plain"), QStringLiteral("kate.desktop")));
        QCOMPARE(d.defaultFor(QStringLiteral("text/plain")), QStringLiteral("kate.desktop"));
        const QVariantList candidates = d.candidatesForType(QStringLiteral("text/plain"));
        for (const QVariant &c : candidates)
            QCOMPARE(c.toMap().value(QStringLiteral("current")).toBool(), c.toMap().value(QStringLiteral("id")).toString() == QLatin1String("kate.desktop"));
        QVERIFY(!d.setTypeDefault(QStringLiteral("notatype"), QStringLiteral("kate.desktop")));
    }
};

QTEST_GUILESS_MAIN(TestDefaultApps)
#include "test_defaultapps.moc"
