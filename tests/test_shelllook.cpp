// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Desktop style: rayeshellrc [Appearance] Look, which the
// taskbar, Start and the window frames follow at once. The key is read
// with the words the shell accepts, written with KConfig::Notify (checked
// on a session bus of the test's own), and the two pages show it: the
// choice on Personalization, and Taskbar alignment locked to the left in
// the Windows 10 style.
#include "shelllook.h"
#include "taskbar.h"

#include <KConfig>
#include <KConfigGroup>
#include <KConfigWatcher>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QJSEngine>
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

#include <memory>

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

QQuickItem *withProperty(QQuickItem *item, const char *property, const QString &value)
{
    if (!item)
        return nullptr;
    if (item->property(property).toString() == value)
        return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        if (QQuickItem *found = withProperty(child, property, value))
            return found;
    return nullptr;
}

QByteArray readFile(const QString &name)
{
    QFile f(QStringLiteral(SOURCE_DIR "/tests/taskbar/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot read tests/taskbar/%s", qPrintable(name));
    return f.readAll();
}

// Raye's own taskbar, as the shell's scripting hands it out
const QByteArray rayePanels = R"({"panels": [{"id": 3, "screen": 0, "location": "bottom", "hiding": "none", "widgets": [
    {"id": 5, "plugin": "org.raye.start", "general": {}, "root": {}},
    {"id": 6, "plugin": "org.raye.taskbar", "general": {}, "root": {}},
    {"id": 8, "plugin": "org.kde.plasma.systemtray", "general": {}, "root": {}},
    {"id": 20, "plugin": "org.raye.clock", "general": {}, "root": {}}]}]})";

// runs the scripts the backend sends on a copy of those panels
TaskbarSettings::Shell rayeShell()
{
    auto store = std::make_shared<QByteArray>(rayePanels);
    const QByteArray fake = readFile(QStringLiteral("shell.js"));
    return [store, fake](const QString &script, const std::function<void(bool, const QString &)> &done) {
        QJSEngine engine;
        engine.globalObject().setProperty(QStringLiteral("store"),
                                          engine.evaluate(QStringLiteral("(") + QString::fromUtf8(*store) + QStringLiteral(")")));
        engine.evaluate(QString::fromUtf8(fake));
        engine.evaluate(script);
        *store = engine.evaluate(QStringLiteral("JSON.stringify(store)")).toString().toUtf8();
        done(true, engine.evaluate(QStringLiteral("printed.join('\\n')")).toString());
    };
}

QPair<QString, QString> describe(const QString &id)
{
    return {id, QString()};
}

} // namespace

class TestShellLook : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QProcess m_bus;
    bool m_hasBus = false;

    QString config() const { return m_dir.filePath(QStringLiteral("config")); }

    QString written(const char *group, const char *key) const
    {
        return KConfig(config() + QStringLiteral("/rayeshellrc"), KConfig::SimpleConfig)
            .group(QString::fromLatin1(group))
            .readEntry(key, QStringLiteral("<absent>"));
    }

    void writeLook(const QString &value)
    {
        KSharedConfigPtr shellrc = KSharedConfig::openConfig(QStringLiteral("rayeshellrc"));
        shellrc->group(QStringLiteral("Appearance")).writeEntry("Look", value);
        shellrc->sync();
    }

    std::unique_ptr<QQuickView> load(QQmlEngine &engine, const QString &file)
    {
        auto view = std::make_unique<QQuickView>(&engine, nullptr);
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(1120, 760);
        view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/personalization/") + file));
        if (view->status() != QQuickView::Ready)
            return nullptr;
        view->show();
        if (!QTest::qWaitForWindowExposed(view.get()))
            return nullptr;
        QTest::qWait(100);
        return view;
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", config().toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(QStringLiteral("xdg")).toUtf8());
        qputenv("HOME", m_dir.filePath(QStringLiteral("home")).toUtf8());
        // a session bus of our own, so KConfig::Notify can be heard
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        if (QStandardPaths::findExecutable(QStringLiteral("dbus-daemon")).isEmpty())
            return;
        m_bus.start(QStringLiteral("dbus-daemon"),
                    {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!m_bus.waitForStarted() || !m_bus.waitForReadyRead(5000))
            return;
        const QByteArray address = m_bus.readLine().trimmed();
        if (address.isEmpty())
            return;
        qputenv("DBUS_SESSION_BUS_ADDRESS", address);
        m_hasBus = QDBusConnection::sessionBus().isConnected();
    }

    void cleanupTestCase()
    {
        m_bus.kill();
        m_bus.waitForFinished();
    }

    void init()
    {
        QDir(config()).removeRecursively();
        QDir().mkpath(config());
        KSharedConfig::openConfig(QStringLiteral("rayeshellrc"))->reparseConfiguration();
    }

    void normalize_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("look");
        QTest::newRow("empty") << QString() << QStringLiteral("windows11");
        QTest::newRow("windows11") << QStringLiteral("windows11") << QStringLiteral("windows11");
        QTest::newRow("windows10") << QStringLiteral("windows10") << QStringLiteral("windows10");
        QTest::newRow("case and spaces") << QStringLiteral(" Windows10 ") << QStringLiteral("windows10");
        QTest::newRow("10") << QStringLiteral("10") << QStringLiteral("windows10");
        QTest::newRow("win10") << QStringLiteral("win10") << QStringLiteral("windows10");
        QTest::newRow("11") << QStringLiteral("11") << QStringLiteral("windows11");
        QTest::newRow("win11") << QStringLiteral("Win11") << QStringLiteral("windows11");
        QTest::newRow("unknown") << QStringLiteral("vista") << QStringLiteral("windows11");
        QTest::newRow("windows 10 with a space") << QStringLiteral("windows 10") << QStringLiteral("windows11");
    }

    // the same words the shell's key table takes, so Settings never shows
    // a look the desktop is not drawing
    void normalize()
    {
        QFETCH(QString, value);
        QFETCH(QString, look);
        QCOMPARE(ShellLook::normalize(value), look);
    }

    void defaultIsWindows11()
    {
        ShellLook look;
        QCOMPARE(look.look(), QStringLiteral("windows11"));
        QVERIFY(!look.windows10());
        QCOMPARE(written("Appearance", "Look"), QStringLiteral("<absent>"));
    }

    void readsTheKey()
    {
        writeLook(QStringLiteral("win10"));
        ShellLook look;
        QCOMPARE(look.look(), QStringLiteral("windows10"));
        QVERIFY(look.windows10());
        writeLook(QStringLiteral("something else"));
        KSharedConfig::openConfig(QStringLiteral("rayeshellrc"))->reparseConfiguration();
        QCOMPARE(look.look(), QStringLiteral("windows11"));
    }

    void writesAndIgnoresBadValues()
    {
        ShellLook look;
        QSignalSpy changed(&look, &ShellLook::changed);
        look.setLook(QStringLiteral("windows10"));
        QCOMPARE(written("Appearance", "Look"), QStringLiteral("windows10"));
        QVERIFY(look.windows10());
        QCOMPARE(changed.count(), 1);

        look.setLook(QStringLiteral("vista"));
        look.setLook(QString());
        look.setLook(QStringLiteral("windows10"));
        QCOMPARE(written("Appearance", "Look"), QStringLiteral("windows10"));
        QCOMPARE(changed.count(), 1);

        // an alias is written in the shell's own word
        look.setLook(QStringLiteral("11"));
        QCOMPARE(written("Appearance", "Look"), QStringLiteral("windows11"));
        QCOMPARE(changed.count(), 2);

        // the rest of the file is left alone
        QCOMPARE(written("Taskbar", "Alignment"), QStringLiteral("<absent>"));
    }

    void writeNotifies()
    {
        if (!m_hasBus)
            QSKIP("no dbus-daemon to hear KConfig::Notify on");
        KConfigWatcher::Ptr watcher = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")));
        QSignalSpy heard(watcher.data(), &KConfigWatcher::configChanged);
        ShellLook look;
        look.setLook(QStringLiteral("windows10"));
        QVERIFY(heard.wait(3000));
        const auto group = heard.first().at(0).value<KConfigGroup>();
        const auto names = heard.first().at(1).value<QByteArrayList>();
        QCOMPARE(group.name(), QStringLiteral("Appearance"));
        QVERIFY(names.contains("Look"));
    }

    // Windows 10 has no centred taskbar: the alignment key keeps the
    // Windows 11 choice, the page shows left and does not change it
    void taskbarAlignmentFollowsTheLook()
    {
        TaskbarSettings taskbar(rayeShell(), describe);
        QVERIFY(taskbar.available());
        QCOMPARE(taskbar.alignment(), QStringLiteral("center"));
        QVERIFY(!taskbar.alignmentFixed());

        writeLook(QStringLiteral("windows10"));
        QVERIFY(taskbar.alignmentFixed());
        QCOMPARE(taskbar.alignment(), QStringLiteral("left"));
        taskbar.setAlignment(QStringLiteral("left"));
        taskbar.setAlignment(QStringLiteral("center"));
        QCOMPARE(written("Taskbar", "Alignment"), QStringLiteral("<absent>"));

        writeLook(QStringLiteral("windows11"));
        QVERIFY(!taskbar.alignmentFixed());
        QCOMPARE(taskbar.alignment(), QStringLiteral("center"));
    }

    // the stock panel does not read the look, its alignment stays free
    void stockTaskbarIgnoresTheLook()
    {
        writeLook(QStringLiteral("windows10"));
        const QByteArray stock = readFile(QStringLiteral("desktop.json"));
        auto store = std::make_shared<QByteArray>(stock);
        const QByteArray fake = readFile(QStringLiteral("shell.js"));
        TaskbarSettings taskbar(
            [store, fake](const QString &script, const std::function<void(bool, const QString &)> &done) {
                QJSEngine engine;
                engine.globalObject().setProperty(QStringLiteral("store"),
                                                  engine.evaluate(QStringLiteral("(") + QString::fromUtf8(*store) + QStringLiteral(")")));
                engine.evaluate(QString::fromUtf8(fake));
                engine.evaluate(script);
                done(true, engine.evaluate(QStringLiteral("printed.join('\\n')")).toString());
            },
            describe);
        QVERIFY(taskbar.available());
        QVERIFY(!taskbar.rayeTasks());
        QVERIFY(!taskbar.alignmentFixed());
    }

    void personalizationPage()
    {
        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/personalization.json"));
        QVERIFY(fragment.open(QIODevice::ReadOnly));
        bool indexed = false;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &v : entries) {
            if (v.toObject().value(QStringLiteral("id")).toString() == QLatin1String("personalization#desktop-style")) {
                indexed = !v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty();
            }
        }
        QVERIFY2(indexed, "personalization#desktop-style is not in data/search with its French label");

        writeLook(QStringLiteral("windows10"));
        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            auto view = load(engine, QStringLiteral("PersonalizationPage.qml"));
            QVERIFY(view);
            QQuickItem *card = withProperty(view->rootObject(), "searchId", QStringLiteral("desktop-style"));
            QVERIFY2(card, "the desktop style choice is not on the page");
            QCOMPARE(card->property("currentValue").toString(), QStringLiteral("windows10"));
            auto *combo = card->property("comboBox").value<QObject *>();
            QVERIFY(combo);
            QCOMPARE(combo->property("count").toInt(), 2);

            // choosing in the list writes the key
            QVERIFY(QMetaObject::invokeMethod(card, "activated", Q_ARG(int, 0)));
            QCOMPARE(written("Appearance", "Look"), QStringLiteral("windows11"));
            QTRY_COMPARE(card->property("currentValue").toString(), QStringLiteral("windows11"));
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }

    void taskbarPageLocksAlignment()
    {
        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            auto view = load(engine, QStringLiteral("TaskbarPage.qml"));
            QVERIFY(view);
            auto *taskbar = view->rootObject()->findChild<TaskbarSettings *>();
            QVERIFY(taskbar);
            taskbar->useShell(rayeShell(), describe);
            QTRY_VERIFY(taskbar->available());

            QQuickItem *row = withProperty(view->rootObject(), "searchId", QStringLiteral("taskbar-alignment"));
            QVERIFY(row);
            QVERIFY(row->isEnabled());
            QCOMPARE(row->property("description").toString(), QString());

            writeLook(QStringLiteral("windows10"));
            taskbar->reload();
            QTRY_VERIFY(!row->isEnabled());
            QCOMPARE(row->property("description").toString(), QStringLiteral("Always on the left in the Windows 10 style"));
            QQuickItem *combo = withProperty(row, "objectName", QStringLiteral("alignmentChoice"));
            QVERIFY(combo);
            QCOMPARE(combo->property("currentIndex").toInt(), 1);

            writeLook(QStringLiteral("windows11"));
            taskbar->reload();
            QTRY_VERIFY(row->isEnabled());
            QCOMPARE(combo->property("currentIndex").toInt(), 0);
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestShellLook)
#include "test_shelllook.moc"
