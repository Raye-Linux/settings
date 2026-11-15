// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Start, Folders, Lock screen and Text input: what each
// setting reads and writes, and the pages as the window loads them. Before
// these pages existed, all four fell on "This page is on its way".
//
// Nothing here touches the session: the config files are in a folder of the
// test's own, and the sign-in helper is a small script in the same place.
#include "captures.h"
#include "lockscreen.h"
#include "startsettings.h"
#include "textinput.h"

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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

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

QString readConfig(const QString &file, const QStringList &groups, const char *key)
{
    KConfig config(file);
    KConfigGroup group = config.group(groups.first());
    for (const QString &name : groups.mid(1))
        group = group.group(name);
    return group.readEntry(key, QStringLiteral("<absent>"));
}

} // namespace

class TestStartLockPages : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString path(const QString &name) const { return m_dir.filePath(name); }

    void writeFile(const QString &name, const QByteArray &contents, bool executable = false)
    {
        QDir().mkpath(QFileInfo(path(name)).absolutePath());
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(contents);
        f.close();
        if (executable)
            QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }

    // a stand-in for pkexec and the helper: writes down its arguments and
    // what it was given, and exits with the code the test picks
    void writeHelper(int code)
    {
        writeFile(QStringLiteral("helper"),
                  "#!/bin/sh\necho \"$@\" >> '" + path(QStringLiteral("calls")).toUtf8() + "'\n"
                  "case \"$1\" in --set) cat > '" + path(QStringLiteral("given")).toUtf8() + "' ;; esac\n"
                  "exit " + QByteArray::number(code) + "\n", true);
    }

    QStringList calls() const
    {
        QFile f(path(QStringLiteral("calls")));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    std::unique_ptr<QQuickView> load(const QString &file)
    {
        auto view = std::make_unique<QQuickView>();
        view->engine()->rootContext()->setContextObject(new KLocalizedContext(view->engine()));
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

    QStringList indexedFor(const QString &page)
    {
        QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/personalization.json"));
        if (!fragment.open(QIODevice::ReadOnly))
            return {};
        QStringList rows;
        const QJsonArray entries = QJsonDocument::fromJson(fragment.readAll()).object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &v : entries) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (id.startsWith(page + QLatin1Char('#')))
                rows << id.section(QLatin1Char('#'), 1);
        }
        return rows;
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", path(QStringLiteral("config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", path(QStringLiteral("xdg")).toUtf8());
        qputenv("HOME", path(QStringLiteral("home")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void init()
    {
        QDir(path(QStringLiteral("config"))).removeRecursively();
        QDir().mkpath(path(QStringLiteral("config")));
        QFile::remove(path(QStringLiteral("calls")));
        QFile::remove(path(QStringLiteral("given")));
        for (const char *name : {"rayeshellrc", "kscreenlockerrc", "raye-settingsrc", "kwinrc"})
            KSharedConfig::openConfig(QString::fromLatin1(name))->reparseConfiguration();
    }

    // --- Start -----------------------------------------------------------
    void startLayoutIsWrittenAndBadOnesAreNot()
    {
        StartSettings start(path(QStringLiteral("no-menu")));
        QCOMPARE(start.layout(), QStringLiteral("default"));
        start.setLayout(QStringLiteral("pins"));
        QCOMPARE(readConfig(QStringLiteral("rayeshellrc"), {QStringLiteral("Start")}, "Layout"), QStringLiteral("pins"));
        start.setLayout(QStringLiteral("nonsense"));
        QCOMPARE(start.layout(), QStringLiteral("pins"));
        start.setLayout(QStringLiteral("recommendations"));
        QCOMPARE(readConfig(QStringLiteral("rayeshellrc"), {QStringLiteral("Start")}, "Layout"), QStringLiteral("recommendations"));
    }

    void startReadsTheWordsTheShellAccepts()
    {
        KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("rayeshellrc"));
        config->group(QStringLiteral("Start")).writeEntry("Layout", "morePins");
        config->sync();
        StartSettings start(path(QStringLiteral("no-menu")));
        QCOMPARE(start.layout(), QStringLiteral("pins"));
    }

    void foldersKeepTheirOrder()
    {
        StartSettings start(path(QStringLiteral("no-menu")));
        QCOMPARE(start.folders(), QStringList({QStringLiteral("settings"), QStringLiteral("files")}));
        start.setFolder(QStringLiteral("personal"), true);
        start.setFolder(QStringLiteral("documents"), true);
        start.setFolder(QStringLiteral("settings"), false);
        start.setFolder(QStringLiteral("not-a-folder"), true);
        const QStringList want = {QStringLiteral("files"), QStringLiteral("documents"), QStringLiteral("personal")};
        QCOMPARE(start.folders(), want);
        QCOMPARE(readConfig(QStringLiteral("rayeshellrc"), {QStringLiteral("Start")}, "Folders"), want.join(QLatin1Char(',')));
    }

    // these words are the ones the shell's key table accepts: a word it does
    // not know would fall back to the default, and the switch would lie
    void foldersAreTheOnesTheShellKnows()
    {
        QCOMPARE(StartSettings::folderIds().join(QLatin1Char(',')),
                 QStringLiteral("settings,files,documents,downloads,music,pictures,videos,network,personal"));
    }

    // a row is only offered for what the menu really reads
    void followedMeansNamedInTheMenu()
    {
        writeFile(QStringLiteral("menu/ui/start/View.qml"), "readonly property var f: ShellSettings.startFolders\n");
        StartSettings reads(path(QStringLiteral("menu")));
        QVERIFY(reads.foldersFollowed());
        QVERIFY(!reads.recentAppsFollowed());
        QVERIFY(!reads.mostUsedFollowed());
        StartSettings noMenu(path(QStringLiteral("no-menu")));
        QVERIFY(!noMenu.foldersFollowed());
    }

    // --- lock screen -----------------------------------------------------
    void lockPictureGoesWhereTheLockScreenReadsIt()
    {
        writeFile(QStringLiteral("pics/a.png"), "x");
        LockScreenSettings lock;
        lock.setThemeConfigPath(path(QStringLiteral("no-theme-file")));
        lock.setHelperCommand({path(QStringLiteral("helper"))});
        writeHelper(0);
        QCOMPARE(lock.image(), QUrl());
        const QUrl picture = QUrl::fromLocalFile(path(QStringLiteral("pics/a.png")));
        lock.setPicture(picture);
        QCOMPARE(readConfig(QStringLiteral("kscreenlockerrc"),
                            {QStringLiteral("Greeter"), QStringLiteral("Wallpaper"), QStringLiteral("org.kde.image"), QStringLiteral("General")},
                            "Image"),
                 picture.toString());
        QCOMPARE(lock.image(), picture);
        QCOMPARE(lock.recentImages().size(), 1);
        // the sign-in screen was not asked to follow, so no password was asked for
        QVERIFY(calls().isEmpty());

        lock.setPicture(QUrl::fromLocalFile(path(QStringLiteral("pics/not-there.png"))));
        QCOMPARE(lock.image(), picture);
        lock.setPicture(QUrl(QStringLiteral("https://example.org/a.png")));
        QCOMPARE(lock.image(), picture);

        lock.useRayePicture();
        QCOMPARE(lock.image(), QUrl());
        QCOMPARE(readConfig(QStringLiteral("kscreenlockerrc"),
                            {QStringLiteral("Greeter"), QStringLiteral("Wallpaper"), QStringLiteral("org.kde.image"), QStringLiteral("General")},
                            "Image"),
                 QStringLiteral("<absent>"));
    }

    void recentPicturesAreFiveNewestFirst()
    {
        LockScreenSettings lock;
        lock.setThemeConfigPath(path(QStringLiteral("no-theme-file")));
        QList<QUrl> urls;
        for (int i = 0; i < 7; ++i) {
            const QString name = QStringLiteral("pics/%1.png").arg(i);
            writeFile(name, "x");
            urls << QUrl::fromLocalFile(path(name));
            lock.setPicture(urls.last());
        }
        lock.setPicture(urls.at(3));
        const QVariantList recent = lock.recentImages();
        QCOMPARE(recent.size(), 5);
        QCOMPARE(recent.first().toUrl(), urls.at(3));
        QCOMPARE(recent.at(1).toUrl(), urls.at(6));
    }

    void signInGetsTheBytesNotAPath()
    {
        writeFile(QStringLiteral("pics/a.png"), "the picture");
        writeHelper(0);
        LockScreenSettings lock;
        lock.setThemeConfigPath(path(QStringLiteral("theme.conf.user")));
        lock.setHelperCommand({path(QStringLiteral("helper"))});
        lock.setPicture(QUrl::fromLocalFile(path(QStringLiteral("pics/a.png"))));
        QVERIFY(!lock.onSignIn());

        QSignalSpy busy(&lock, &LockScreenSettings::busyChanged);
        lock.setOnSignIn(true);
        QVERIFY(lock.busy());
        QTRY_VERIFY(!lock.busy());
        QCOMPARE(calls(), QStringList{QStringLiteral("--set")});
        QFile given(path(QStringLiteral("given")));
        QVERIFY(given.open(QIODevice::ReadOnly));
        QCOMPARE(given.readAll(), QByteArray("the picture"));
    }

    void signInFollowsAChangeOnceItIsOn()
    {
        writeFile(QStringLiteral("pics/a.png"), "x");
        writeFile(QStringLiteral("theme.conf.user"), "[General]\nbackground=/var/lib/raye-settings/signin/picture.png\n");
        writeHelper(0);
        LockScreenSettings lock;
        lock.setThemeConfigPath(path(QStringLiteral("theme.conf.user")));
        lock.setHelperCommand({path(QStringLiteral("helper"))});
        QVERIFY(lock.onSignIn());
        lock.setPicture(QUrl::fromLocalFile(path(QStringLiteral("pics/a.png"))));
        QTRY_VERIFY(!lock.busy());
        lock.useRayePicture();
        QTRY_VERIFY(!lock.busy());
        QCOMPARE(calls(), QStringList({QStringLiteral("--set"), QStringLiteral("--clear")}));
    }

    // the Raye picture is on the sign-in screen already: no password for nothing
    void noPictureNoHelper()
    {
        writeHelper(0);
        LockScreenSettings lock;
        lock.setThemeConfigPath(path(QStringLiteral("theme.conf.user")));
        lock.setHelperCommand({path(QStringLiteral("helper"))});
        lock.setOnSignIn(true);
        QVERIFY(!lock.busy());
        QVERIFY(calls().isEmpty());
    }

    void signInFailuresAreNamed_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("reason");
        QTest::newRow("password not given") << 126 << QStringLiteral("cancelled");
        QTest::newRow("not allowed") << 127 << QStringLiteral("cancelled");
        QTest::newRow("not a picture") << 3 << QStringLiteral("picture");
        QTest::newRow("no theme") << 4 << QStringLiteral("failed");
    }

    void signInFailuresAreNamed()
    {
        QFETCH(int, code);
        QFETCH(QString, reason);
        writeFile(QStringLiteral("pics/a.png"), "x");
        writeHelper(code);
        LockScreenSettings lock;
        lock.setThemeConfigPath(path(QStringLiteral("no-theme-file")));
        lock.setHelperCommand({path(QStringLiteral("helper"))});
        lock.setPicture(QUrl::fromLocalFile(path(QStringLiteral("pics/a.png"))));
        QSignalSpy failed(&lock, &LockScreenSettings::signInFailed);
        lock.setOnSignIn(true);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), reason);
        QVERIFY(!lock.busy());
    }

    void aMissingHelperIsAFailureNotAHang()
    {
        writeFile(QStringLiteral("pics/a.png"), "x");
        LockScreenSettings lock;
        lock.setThemeConfigPath(path(QStringLiteral("no-theme-file")));
        lock.setHelperCommand({path(QStringLiteral("no-such-program"))});
        lock.setPicture(QUrl::fromLocalFile(path(QStringLiteral("pics/a.png"))));
        QSignalSpy failed(&lock, &LockScreenSettings::signInFailed);
        lock.setOnSignIn(true);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!lock.busy());
    }

    void readsTheThemeFile()
    {
        QCOMPARE(LockScreenSettings::backgroundIn("[General]\nbackground=/a/b.png\n"), QStringLiteral("/a/b.png"));
        QCOMPARE(LockScreenSettings::backgroundIn("[General]\nbackground=\n"), QString());
        QCOMPARE(LockScreenSettings::backgroundIn(""), QString());
    }

    // --- text input ------------------------------------------------------
    void keyboardNotInstalledIsSaidAndNothingIsWritten()
    {
        TextInputSettings input(path(QStringLiteral("no-keyboard.desktop")));
        QVERIFY(!input.available());
        input.setOnScreenKeyboard(true);
        QVERIFY(!input.onScreenKeyboard());
        QCOMPARE(readConfig(QStringLiteral("kwinrc"), {QStringLiteral("Wayland")}, "InputMethod"), QStringLiteral("<absent>"));
    }

    void keyboardGoesInTheCompositorsFile()
    {
        writeFile(QStringLiteral("keyboard.desktop"), "[Desktop Entry]\n");
        TextInputSettings input(path(QStringLiteral("keyboard.desktop")));
        QVERIFY(input.available());
        QVERIFY(!input.onScreenKeyboard());
        input.setOnScreenKeyboard(true);
        QVERIFY(input.onScreenKeyboard());
        QCOMPARE(readConfig(QStringLiteral("kwinrc"), {QStringLiteral("Wayland")}, "InputMethod"), path(QStringLiteral("keyboard.desktop")));
        input.setOnScreenKeyboard(false);
        QVERIFY(!input.onScreenKeyboard());
        QCOMPARE(readConfig(QStringLiteral("kwinrc"), {QStringLiteral("Wayland")}, "InputMethod"), QString());
    }

    // --- the pages -------------------------------------------------------
    void pages_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        QTest::newRow("Start") << QStringLiteral("StartPage.qml") << QStringLiteral("personalization/start");
        QTest::newRow("Folders") << QStringLiteral("StartFoldersPage.qml") << QStringLiteral("personalization/start-folders");
        QTest::newRow("Lock screen") << QStringLiteral("LockScreenPage.qml") << QStringLiteral("personalization/lock-screen");
        QTest::newRow("Text input") << QStringLiteral("TextInputPage.qml") << QStringLiteral("personalization/text-input");
    }

    // the page is really there (not the placeholder), loads without a word,
    // and draws what the search sends people to
    void pages()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);
        QVERIFY2(QFile::exists(QStringLiteral(":/qt/qml/org/raye/settings/pages/personalization/") + file), qPrintable(file));
        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        QStringList drawn;
        {
            auto view = load(file);
            QVERIFY(view);
            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            drawn = ids.toStringList();
        }
        qInstallMessageHandler(previous);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
        QStringList indexed = indexedFor(page);
        QVERIFY(!indexed.isEmpty());
        indexed.sort();
        drawn.sort();
        QCOMPARE(drawn, indexed);
    }

    void clickingALayoutWritesIt()
    {
        auto view = load(QStringLiteral("StartPage.qml"));
        QVERIFY(view);
        QQuickItem *card = withProperty(view->rootObject(), "title", QStringLiteral("More pins"));
        QVERIFY(card);
        QVERIFY(QMetaObject::invokeMethod(card, "click"));
        QTRY_COMPARE(readConfig(QStringLiteral("rayeshellrc"), {QStringLiteral("Start")}, "Layout"), QStringLiteral("pins"));
    }

    void clickingAFolderWritesIt()
    {
        auto view = load(QStringLiteral("StartFoldersPage.qml"));
        QVERIFY(view);
        QQuickItem *card = withProperty(view->rootObject(), "title", QStringLiteral("Documents"));
        QVERIFY(card);
        QVERIFY(QMetaObject::invokeMethod(card, "click"));
        QTRY_COMPARE(readConfig(QStringLiteral("rayeshellrc"), {QStringLiteral("Start")}, "Folders"),
                     QStringLiteral("settings,files,documents"));
    }

    // the app is called Files everywhere else: the page uses the same name
    void foldersPageNamesFilesAsStartDoes()
    {
        auto view = load(QStringLiteral("StartFoldersPage.qml"));
        QVERIFY(view);
        QVERIFY(withProperty(view->rootObject(), "title", QStringLiteral("Files")));
        QVERIFY(!withProperty(view->rootObject(), "title", QStringLiteral("File Explorer")));
    }

    // a menu that does not draw the folders: the page says so, and offers no
    // switch for the two Start keys nobody reads
    void pagesAdmitWhatStartDoesNotDoYet()
    {
        auto folders = load(QStringLiteral("StartFoldersPage.qml"));
        QVERIFY(folders);
        QQuickItem *bar = withProperty(folders->rootObject(), "objectName", QStringLiteral("notShownYet"));
        QVERIFY(bar);
        QVERIFY(bar->isVisible());

        auto start = load(QStringLiteral("StartPage.qml"));
        QVERIFY(start);
        QQuickItem *recent = withProperty(start->rootObject(), "title", QStringLiteral("Show recently added apps"));
        QVERIFY(recent);
        QVERIFY(!recent->isVisible());
        QQuickItem *most = withProperty(start->rootObject(), "title", QStringLiteral("Show most used apps"));
        QVERIFY(most);
        QVERIFY(!most->isVisible());
    }

    // a wallpaper of the system is a folder of sizes: choosing it must still
    // put a picture on the lock screen, not do nothing
    void clickingAPictureOfTheSystemSetsIt()
    {
        writeFile(QStringLiteral("data/wallpapers/Test/metadata.json"), "{}");
        writeFile(QStringLiteral("data/wallpapers/Test/contents/images/1920x1080.png"), "x");
        qputenv("XDG_DATA_HOME", path(QStringLiteral("data")).toUtf8());
        auto view = load(QStringLiteral("LockScreenPage.qml"));
        QVERIFY(view);
        std::function<QQuickItem *(QQuickItem *)> tile = [&tile](QQuickItem *item) -> QQuickItem * {
            if (item->property("label").isValid() && item->property("selected").isValid())
                return item;
            const auto children = item->childItems();
            for (QQuickItem *child : children)
                if (QQuickItem *found = tile(child))
                    return found;
            return nullptr;
        };
        QTRY_VERIFY(tile(view->rootObject()) != nullptr);
        QVERIFY(QMetaObject::invokeMethod(tile(view->rootObject()), "click"));
        const QStringList groups = {QStringLiteral("Greeter"), QStringLiteral("Wallpaper"), QStringLiteral("org.kde.image"), QStringLiteral("General")};
        QTRY_VERIFY(readConfig(QStringLiteral("kscreenlockerrc"), groups, "Image") != QStringLiteral("<absent>"));
        const QUrl chosen(readConfig(QStringLiteral("kscreenlockerrc"), groups, "Image"));
        QVERIFY2(QFileInfo(chosen.toLocalFile()).isFile(), qPrintable(chosen.toString()));
    }

    void keyboardRowSaysItIsNotInstalled()
    {
        auto view = load(QStringLiteral("TextInputPage.qml"));
        QVERIFY(view);
        QQuickItem *row = withProperty(view->rootObject(), "objectName", QStringLiteral("onScreenKeyboard"));
        QVERIFY(row);
        // the agents' chroot has no on-screen keyboard, nor has the image yet
        if (QFile::exists(QStringLiteral("/usr/share/applications/org.kde.plasma.keyboard.desktop")))
            QSKIP("the on-screen keyboard is installed here");
        QVERIFY(!row->isEnabled());
        QCOMPARE(row->property("description").toString(), QStringLiteral("Not installed on this PC"));
    }
};

QTEST_MAIN(TestStartLockPages)
#include "test_start_lock_pages.moc"
