// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming: Game Mode, Captures and Windows programs, as the window loads
// them. layout.json named them under pages/gaming/ while the only page was
// an old file elsewhere, so all three fell on "This page is on its way",
// and so did the Windows programs tile of the launch screen.
#include "captures.h"
#include "layout.h"
#include "systeminfo.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QStandardPaths>
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

QObject *dialogOf(QObject *root, const QString &primaryText)
{
    const auto children = root->findChildren<QObject *>();
    for (QObject *child : children)
        if (child->property("primaryText").toString() == primaryText)
            return child;
    return nullptr;
}

void allWords(QQuickItem *item, QStringList *words)
{
    for (const char *name : {"text", "title", "message", "description", "iconName", "iconFallback"})
        words->append(item->property(name).toString());
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        allWords(child, words);
}

} // namespace

class TestGamingPages : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    SystemInfo *m_system = nullptr;

    QString path(const QString &name) const { return m_dir.filePath(name); }

    std::unique_ptr<QQuickView> load(const QString &file)
    {
        auto view = std::make_unique<QQuickView>();
        view->engine()->rootContext()->setContextObject(new KLocalizedContext(view->engine()));
        view->engine()->rootContext()->setContextProperty(QStringLiteral("system"), m_system);
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(1120, 760);
        view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + file));
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
        qputenv("XDG_CONFIG_HOME", path(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", path(QStringLiteral("data")).toUtf8());
        qputenv("HOME", path(QStringLiteral("home")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        QDir().mkpath(path(QStringLiteral("home")));
        m_system = new SystemInfo(this);
    }

    void init()
    {
        QDir(path(QStringLiteral("config"))).removeRecursively();
        QDir().mkpath(path(QStringLiteral("config")));
        KSharedConfig::openConfig(QStringLiteral("spectaclerc"))->reparseConfiguration();
    }

    // every page of the category the layout names is really there
    void theLayoutFindsThePages()
    {
        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        int found = 0;
        for (const Category &category : layout.categories) {
            if (category.id != QLatin1String("gaming"))
                continue;
            for (const Page &page : category.pages) {
                if (page.type != QLatin1String("qml"))
                    continue;
                ++found;
                QVERIFY2(QFile::exists(QStringLiteral(":/qt/qml/org/raye/settings/") + page.component), qPrintable(page.component));
            }
        }
        QCOMPARE(found, 4);
        // and the old one is gone: a type is named after its file
        QVERIFY(!QFile::exists(QStringLiteral(":/qt/qml/org/raye/settings/GamingPage.qml")));
    }

    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        QTest::newRow("Game Mode") << QStringLiteral("pages/gaming/GamingPage.qml") << QStringLiteral("gaming/game-mode");
        QTest::newRow("Captures") << QStringLiteral("pages/gaming/CapturesPage.qml") << QStringLiteral("gaming/captures");
        QTest::newRow("Controllers") << QStringLiteral("pages/gaming/ControllersPage.qml") << QString();
        QTest::newRow("Windows programs") << QStringLiteral("pages/gaming/WindowsProgramsPage.qml") << QString();
    }

    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);
        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        QStringList drawn;
        {
            auto view = load(file);
            QVERIFY(view);
            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            drawn = ids.toStringList();
            // a person must never read the name of what runs their programs
            QStringList words;
            allWords(view->rootObject(), &words);
            const QString all = words.join(QLatin1Char(' '));
            QVERIFY2(!all.contains(QLatin1String("wine"), Qt::CaseInsensitive), qPrintable(all));
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
        QStringList indexed;
        if (!page.isEmpty()) {
            QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/gaming.json"));
            QVERIFY(fragment.open(QIODevice::ReadOnly));
            const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
            for (const QJsonValue &v : entries) {
                const QString id = v.toObject().value(QStringLiteral("id")).toString();
                QVERIFY2(!v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty(), qPrintable(id));
                if (id.startsWith(page + QLatin1Char('#')))
                    indexed << id.section(QLatin1Char('#'), 1);
            }
            QVERIFY(!indexed.isEmpty());
        }
        indexed.sort();
        drawn.sort();
        QCOMPARE(drawn, indexed);
    }

    void capturesFollowTheCaptureToolsFile()
    {
        CapturesSettings captures;
        QCOMPARE(captures.screenshots().toLocalFile(),
                 QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Screenshots"));
        QCOMPARE(captures.recordings().toLocalFile(),
                 QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/Screencasts"));
        const QUrl pictures = QUrl::fromLocalFile(path(QStringLiteral("shots")));
        const QUrl videos = QUrl::fromLocalFile(path(QStringLiteral("clips")));
        captures.setScreenshots(pictures);
        captures.setRecordings(videos);
        QCOMPARE(captures.screenshots(), pictures);
        QCOMPARE(captures.recordings(), videos);
        KConfig file(QStringLiteral("spectaclerc"));
        QCOMPARE(file.group(QStringLiteral("ImageSave")).readEntry("imageSaveLocation"), pictures.toString());
        QCOMPARE(file.group(QStringLiteral("VideoSave")).readEntry("videoSaveLocation"), videos.toString());
        // not a folder on this PC: nothing is written
        captures.setScreenshots(QUrl(QStringLiteral("https://example.org/")));
        QCOMPARE(captures.screenshots(), pictures);
    }

    void noProgramsIsSaid()
    {
        QDir(path(QStringLiteral("data"))).removeRecursively();
        QDir(path(QStringLiteral("home"))).removeRecursively();
        QVERIFY(m_system->windowsApps().isEmpty());
        auto view = load(QStringLiteral("pages/gaming/WindowsProgramsPage.qml"));
        QVERIFY(view);
        QQuickItem *empty = withProperty(view->rootObject(), "objectName", QStringLiteral("noPrograms"));
        QVERIFY(empty);
        QVERIFY(empty->isVisible());
    }

    // removing deletes the program: it asks first, and only then it goes
    void removingAsksFirst()
    {
        const QString root = windowsRootForTest();
        QVERIFY(QDir().mkpath(root + QStringLiteral("/Notepad Plus/drive_c")));
        QVERIFY(m_system->windowsApps().contains(QStringLiteral("Notepad Plus")));
        auto view = load(QStringLiteral("pages/gaming/WindowsProgramsPage.qml"));
        QVERIFY(view);
        QQuickItem *row = withProperty(view->rootObject(), "title", QStringLiteral("Notepad Plus"));
        QVERIFY(row);
        QVERIFY(QMetaObject::invokeMethod(row, "buttonClicked"));
        QVERIFY(QDir(root + QStringLiteral("/Notepad Plus")).exists());
        QObject *dialog = dialogOf(view->rootObject(), QStringLiteral("Remove"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!QDir(root + QStringLiteral("/Notepad Plus")).exists());
        QTRY_VERIFY(withProperty(view->rootObject(), "title", QStringLiteral("Notepad Plus")) == nullptr);
    }

private:
    QString windowsRootForTest() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/raye/windows");
    }
};

QTEST_MAIN(TestGamingPages)
#include "test_gaming_pages.moc"
