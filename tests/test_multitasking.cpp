// SPDX-License-Identifier: GPL-3.0-or-later
// System > Multitasking, Advanced and Remote Desktop: what each setting
// reads and writes, in config folders of the test's own (the user's in
// XDG_CONFIG_HOME, the system's defaults in XDG_CONFIG_DIRS). Then the
// five pages of this package are loaded as Settings shows them: no QML
// warning, and every row the search knows is on its page.
#include "multitasking.h"

#include <KConfig>
#include <KConfigGroup>
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

QString entry(const QString &path, const QString &group, const QString &key)
{
    KConfig config(path, KConfig::SimpleConfig);
    return config.group(group).readEntry(key, QStringLiteral("<unset>"));
}

} // namespace

class TestMultitasking : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_home;   // XDG_CONFIG_HOME
    QString m_system; // XDG_CONFIG_DIRS
    QString m_data;   // XDG_DATA_HOME
    QString m_sysData;

    QString user(const QString &file) const { return m_home + QLatin1Char('/') + file; }

    void clean()
    {
        for (const QString &dir : {m_home, m_system, m_data, m_sysData}) {
            QDir(dir).removeRecursively();
            QDir().mkpath(dir);
        }
        for (const char *name : {"kwinrc", "rayeshellrc", "dolphinrc", "kdeglobals", "krdpserverrc"})
            KSharedConfig::openConfig(QString::fromLatin1(name))->reparseConfiguration();
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_home = m_dir.filePath(QStringLiteral("config"));
        m_system = m_dir.filePath(QStringLiteral("xdg"));
        m_data = m_dir.filePath(QStringLiteral("data"));
        m_sysData = m_dir.filePath(QStringLiteral("share"));
        qputenv("XDG_CONFIG_HOME", m_home.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_system.toUtf8());
        qputenv("XDG_DATA_HOME", m_data.toUtf8());
        qputenv("XDG_DATA_DIRS", m_sysData.toUtf8());
        // no session bus: the window manager and the shell are not told
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("KDE_HOME_READONLY", "");
    }

    void init() { clean(); }

    // with nothing written, what the window manager does by default
    void multitaskingDefaults()
    {
        MultitaskingSettings m;
        QVERIFY(m.snapWindows());
        QVERIFY(m.snapLayoutsOnHover());
        QVERIFY(m.snapBar());
        QVERIFY(m.snapAssist());
        QVERIFY(!m.altTabAllDesktops());
        QVERIFY(!m.altTabShowsDesktop());
        QVERIFY(!m.taskbarAllDesktops());
        QVERIFY(!m.taskbarAllDesktopsAvailable());
    }

    // Raye's defaults in /etc/xdg are read, the user's copy wins
    void multitaskingSystemDefaults()
    {
        writeFile(m_system + QStringLiteral("/kwinrc"),
                  "[TabBox]\nDesktopMode=0\nShowDesktopMode=1\n[Script-org.raye.snap]\nSnapBar=false\n");
        writeFile(user(QStringLiteral("kwinrc")), "[Script-org.raye.snap]\nSnapBar=true\n");
        MultitaskingSettings m;
        QVERIFY(m.altTabAllDesktops());
        QVERIFY(m.altTabShowsDesktop());
        QVERIFY(m.snapBar());
    }

    void multitaskingWrites()
    {
        MultitaskingSettings m;
        QSignalSpy changed(&m, &MultitaskingSettings::changed);
        m.setSnapWindows(false);
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Windows"), QStringLiteral("ElectricBorderTiling")), QStringLiteral("false"));
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("Windows"), QStringLiteral("ElectricBorderMaximize")), QStringLiteral("false"));
        QVERIFY(!m.snapWindows());
        QCOMPARE(changed.count(), 1);
        m.setSnapWindows(false); // nothing new: no signal
        QCOMPARE(changed.count(), 1);

        m.setSnapLayoutsOnHover(false);
        m.setSnapBar(false);
        m.setSnapAssist(false);
        const QString snap = QStringLiteral("Script-org.raye.snap");
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), snap, QStringLiteral("SnapLayoutsOnHover")), QStringLiteral("false"));
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), snap, QStringLiteral("SnapBar")), QStringLiteral("false"));
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), snap, QStringLiteral("SnapAssist")), QStringLiteral("false"));

        m.setAltTabAllDesktops(true);
        m.setAltTabShowsDesktop(true);
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("TabBox"), QStringLiteral("DesktopMode")), QStringLiteral("0"));
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("TabBox"), QStringLiteral("ShowDesktopMode")), QStringLiteral("1"));
        m.setAltTabAllDesktops(false);
        QCOMPARE(entry(user(QStringLiteral("kwinrc")), QStringLiteral("TabBox"), QStringLiteral("DesktopMode")), QStringLiteral("1"));

        m.setTaskbarAllDesktops(true);
        QCOMPARE(entry(user(QStringLiteral("rayeshellrc")), QStringLiteral("Taskbar"), QStringLiteral("AllDesktops")), QStringLiteral("true"));

        // a fresh look at the files says the same
        MultitaskingSettings again;
        QVERIFY(!again.snapWindows());
        QVERIFY(!again.snapAssist());
        QVERIFY(again.altTabShowsDesktop());
        QVERIFY(again.taskbarAllDesktops());
    }

    // the taskbar row shows only once the shell ships the key in its
    // system defaults; a user's own copy does not count
    void taskbarRowNeedsTheShell()
    {
        writeFile(user(QStringLiteral("rayeshellrc")), "[Taskbar]\nAllDesktops=true\n");
        QVERIFY(!MultitaskingSettings::systemDefines(QStringLiteral("rayeshellrc"), QStringLiteral("Taskbar"), QStringLiteral("AllDesktops")));
        QVERIFY(!MultitaskingSettings().taskbarAllDesktopsAvailable());
        writeFile(m_system + QStringLiteral("/rayeshellrc"), "[Taskbar]\nAlignment=center\nAllDesktops=false\n");
        QVERIFY(MultitaskingSettings::systemDefines(QStringLiteral("rayeshellrc"), QStringLiteral("Taskbar"), QStringLiteral("AllDesktops")));
        QVERIFY(!MultitaskingSettings::systemDefines(QStringLiteral("rayeshellrc"), QStringLiteral("Taskbar"), QStringLiteral("Nope")));
        MultitaskingSettings m;
        QVERIFY(m.taskbarAllDesktopsAvailable());
        QVERIFY(m.taskbarAllDesktops());
    }

    void advancedDefaultsAndWrites()
    {
        AdvancedSettings a;
        QVERIFY(!a.showExtensions());
        QVERIFY(!a.showHidden());
        QVERIFY(!a.endTask());
        QSignalSpy changed(&a, &AdvancedSettings::changed);
        a.setShowExtensions(true);
        a.setShowHidden(true);
        a.setEndTask(true);
        QCOMPARE(changed.count(), 3);
        QCOMPARE(entry(user(QStringLiteral("dolphinrc")), QStringLiteral("Raye"), QStringLiteral("ShowExtensions")), QStringLiteral("true"));
        QCOMPARE(entry(user(QStringLiteral("dolphinrc")), QStringLiteral("Raye"), QStringLiteral("ShowHidden")), QStringLiteral("true"));
        QCOMPARE(entry(user(QStringLiteral("rayeshellrc")), QStringLiteral("Taskbar"), QStringLiteral("EndTask")), QStringLiteral("true"));
        AdvancedSettings again;
        QVERIFY(again.showExtensions());
        QVERIFY(again.endTask());
    }

    void commandOfExec()
    {
        QCOMPARE(AdvancedSettings::commandOf(QStringLiteral("konsole %u")), QStringLiteral("konsole"));
        QCOMPARE(AdvancedSettings::commandOf(QStringLiteral("xterm")), QStringLiteral("xterm"));
        QCOMPARE(AdvancedSettings::commandOf(QStringLiteral("/opt/term/run --new %F %i")), QStringLiteral("/opt/term/run --new"));
        QCOMPARE(AdvancedSettings::commandOf(QStringLiteral("app %%x %U")), QStringLiteral("app %x"));
        QCOMPARE(AdvancedSettings::commandOf(QString()), QString());
    }

    void terminals()
    {
        const QByteArray konsole = "[Desktop Entry]\nType=Application\nName=Konsole\nName[fr]=Konsole\nExec=konsole\n"
                                   "Icon=utilities-terminal\nCategories=Qt;KDE;System;TerminalEmulator;\n";
        writeFile(m_sysData + QStringLiteral("/applications/org.kde.konsole.desktop"), konsole);
        writeFile(m_sysData + QStringLiteral("/applications/aaa-term.desktop"),
                  "[Desktop Entry]\nType=Application\nName=Alacritty\nExec=alacritty %u\nCategories=System;TerminalEmulator;\n");
        writeFile(m_sysData + QStringLiteral("/applications/hidden-term.desktop"),
                  "[Desktop Entry]\nType=Application\nName=Hidden\nExec=hidden\nNoDisplay=true\nCategories=TerminalEmulator;\n");
        writeFile(m_sysData + QStringLiteral("/applications/editor.desktop"),
                  "[Desktop Entry]\nType=Application\nName=Editor\nExec=editor\nCategories=Utility;TextEditor;\n");
        // the user hid this one with an entry of their own
        writeFile(m_sysData + QStringLiteral("/applications/gone-term.desktop"),
                  "[Desktop Entry]\nType=Application\nName=Gone\nExec=gone\nCategories=TerminalEmulator;\n");
        writeFile(m_data + QStringLiteral("/applications/gone-term.desktop"), "[Desktop Entry]\nHidden=true\n");

        const auto found = AdvancedSettings::findTerminals();
        QStringList ids;
        for (const auto &t : found)
            ids << t.id;
        QCOMPARE(ids, (QStringList{QStringLiteral("aaa-term.desktop"), QStringLiteral("org.kde.konsole.desktop")}));
        QCOMPARE(found.first().name, QStringLiteral("Alacritty"));
        QCOMPARE(found.last().icon, QStringLiteral("utilities-terminal"));

        AdvancedSettings a;
        QCOMPARE(a.terminals().size(), 2);
        // nothing chosen: the one Raye ships
        QCOMPARE(a.terminal(), QStringLiteral("org.kde.konsole.desktop"));
        a.setTerminal(QStringLiteral("aaa-term.desktop"));
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("General"), QStringLiteral("TerminalService")), QStringLiteral("aaa-term.desktop"));
        QCOMPARE(entry(user(QStringLiteral("kdeglobals")), QStringLiteral("General"), QStringLiteral("TerminalApplication")), QStringLiteral("alacritty"));
        QCOMPARE(a.terminal(), QStringLiteral("aaa-term.desktop"));
        // only an installed terminal can be picked
        a.setTerminal(QStringLiteral("../../evil.desktop"));
        QCOMPARE(a.terminal(), QStringLiteral("aaa-term.desktop"));
        a.setTerminal(QStringLiteral("editor.desktop"));
        QCOMPARE(a.terminal(), QStringLiteral("aaa-term.desktop"));
    }

    void remoteDesktopConfig()
    {
        RemoteDesktop r;
        QCOMPARE(r.port(), 3389);
        QVERIFY(!r.hasUsers());
        writeFile(user(QStringLiteral("krdpserverrc")), "[General]\nListenPort=3390\nUsers=alex\n");
        r.refresh();
        QCOMPARE(r.port(), 3390);
        QVERIFY(r.hasUsers());
        writeFile(user(QStringLiteral("krdpserverrc")), "[General]\nSystemUserEnabled=true\n");
        r.refresh();
        QVERIFY(r.hasUsers());
        QCOMPARE(r.port(), 3389);
        // a port that makes no sense falls back to the usual one
        writeFile(user(QStringLiteral("krdpserverrc")), "[General]\nListenPort=99999\n");
        r.refresh();
        QCOMPARE(r.port(), 3389);
    }

    void remoteDesktopAddresses()
    {
        QCOMPARE(RemoteDesktop::formatAddresses({QStringLiteral("192.168.1.20"), QStringLiteral("2001:db8::5")}, 3389),
                 (QStringList{QStringLiteral("192.168.1.20:3389"), QStringLiteral("[2001:db8::5]:3389")}));
        QVERIFY(RemoteDesktop::formatAddresses({}, 3389).isEmpty());
        // what this machine has: never the loopback or a link-local one
        const QStringList mine = RemoteDesktop::localAddresses();
        for (const QString &ip : mine) {
            QVERIFY2(!ip.startsWith(QLatin1String("127.")) && ip != QLatin1String("::1"), qPrintable(ip));
            QVERIFY2(!ip.startsWith(QLatin1String("169.254.")) && !ip.startsWith(QLatin1String("fe80")), qPrintable(ip));
        }
    }

    // ---- the pages, loaded as the window loads them -----------------

    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        for (const char *name : {"Multitasking", "Clipboard", "RemoteDesktop", "Advanced", "Troubleshoot"}) {
            const QString n = QString::fromLatin1(name);
            QString id = n.toLower();
            if (n == QLatin1String("RemoteDesktop"))
                id = QStringLiteral("remote-desktop");
            QTest::newRow(name) << QStringLiteral("pages/system/%1Page.qml").arg(n) << QStringLiteral("system/") + id;
        }
    }

    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);

        // the rows the search sends people to, for this page
        // (any fragment: the developer rows of Advanced have a file of their own)
        QStringList indexed;
        const QDir search(QStringLiteral(SOURCE_DIR "/data/search"));
        for (const QString &name : search.entryList({QStringLiteral("system*.json")}, QDir::Files)) {
            QFile fragment(search.filePath(name));
            QVERIFY(fragment.open(QIODevice::ReadOnly));
            const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
            QVERIFY2(!entries.isEmpty(), qPrintable(name));
            for (const QJsonValue &v : entries) {
                const QString id = v.toObject().value(QStringLiteral("id")).toString();
                QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
                if (id.startsWith(page + QLatin1Char('#')))
                    indexed << id.section(QLatin1Char('#'), 1);
            }
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

QTEST_MAIN(TestMultitasking)
#include "test_multitasking.moc"
