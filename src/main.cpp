// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Settings: one window for the whole system. It opens on a page
// id ("system/display"), a module id that has a Raye page
// ("kcm_kscreen"), a settings: address, or --page <id> (the shell's way).
// A second raye-settings hands its page to the window already open.

#include "categorymodel.h"
#include "modulehost.h"
#include "network.h"
#include "router.h"
#include "systeminfo.h"
#include "updatestatus.h"
#include "windowsimport.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <QDirIterator>
#include <QFile>
#include <QIcon>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QWindow>

#include <memory>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include <cstring>

namespace {

// The layout the package installs. A copy left in a home folder must not
// stand in for it (the relay reads it before every module); the tests
// point at their own with RAYE_SETTINGS_LAYOUT.
QString layoutPath()
{
    const QString test = qEnvironmentVariable("RAYE_SETTINGS_LAYOUT");
    return test.isEmpty() ? QStringLiteral(RAYE_SETTINGS_LAYOUT_FILE) : test;
}

// Does Raye Settings have a page for this id? The kcmshell6 relay asks
// before every call, so the answer must come without a display and
// without a window.
bool knows(const QString &target)
{
    QFile file(layoutPath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    Router router;
    router.setLayout(parseLayout(file.readAll(), QStringLiteral("en")));
    const QString route = router.resolve(target);
    return !route.isEmpty() && route != QLatin1String("home");
}

// The language of the names in the layout: the first one the session
// asks for (LANGUAGE=fr:en), else the locale.
QString layoutLanguage()
{
    const QString wanted = qEnvironmentVariable("LANGUAGE").section(QLatin1Char(':'), 0, 0);
    const QString name = wanted.isEmpty() ? QLocale().name() : wanted;
    return name.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('-'), 0, 0).section(QLatin1Char('.'), 0, 0);
}

// Every QML file of the module must load; the build test runs this.
bool optionalModuleMissing(const QList<QQmlError> &errors)
{
    return std::all_of(errors.cbegin(), errors.cend(), [](const QQmlError &e) {
        return e.description().contains(QLatin1String("module \"org.raye.account\" is not installed"));
    });
}

int checkQml()
{
    QQmlEngine engine;
    int broken = 0;
    QDirIterator it(QStringLiteral(":/qt/qml/org/raye/settings"), {QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc") + path));
        // the Raye account pages need a module that ships on its own;
        // without it they stay hidden, and are checked where it is
        if (component.isError() && optionalModuleMissing(component.errors())) {
            qInfo("%s: org.raye.account is not installed, not checked", qPrintable(path));
            continue;
        }
        if (component.isError()) {
            ++broken;
            for (const auto &error : component.errors())
                qCritical("%s", qPrintable(error.toString()));
        }
    }
    QQmlComponent main(&engine);
    main.loadFromModule("org.raye.settings", "Main");
    for (const auto &error : main.errors())
        qCritical("%s", qPrintable(error.toString()));
    return broken == 0 && !main.isError() ? 0 : 1;
}

// The page asked for on the command line: --page, a page id, a module
// id, or a settings: address from another app.
QString wantedPage(const QCommandLineParser &parser)
{
    const QString page = parser.value(QStringLiteral("page"));
    return page.isEmpty() ? parser.positionalArguments().value(0) : page;
}

void raise(QQmlApplicationEngine &engine)
{
    const auto objects = engine.rootObjects();
    for (QObject *object : objects) {
        if (auto *window = qobject_cast<QWindow *>(object)) {
            window->show();
            window->raise();
            window->requestActivate();
            return;
        }
    }
}

// The window of today has no router yet: hand it the module the way
// its own links do.
void open(QQmlApplicationEngine &engine, const QString &target)
{
    if (target.isEmpty())
        return;
    const auto objects = engine.rootObjects();
    for (QObject *object : objects) {
        if (object->metaObject()->indexOfMethod("openModule(QVariant)") >= 0) {
            QMetaObject::invokeMethod(object, "openModule", Q_ARG(QVariant, target));
            return;
        }
    }
}

// One page in a window of the default size, on the window colour.
int showPreview(QQmlApplicationEngine &engine, Router &router, const QString &pageId)
{
    QString component = router.componentUrlOf(router.resolve(pageId));
    if (component.isEmpty())
        component = pageId.endsWith(QLatin1String(".qml")) ? pageId : QStringLiteral("PlaceholderPage.qml");
    const Page *page = router.layout().page(router.resolve(pageId));
    QVariantMap properties;
    if (component == QLatin1String("PlaceholderPage.qml") && page) {
        properties.insert(QStringLiteral("pageName"), page->name);
        properties.insert(QStringLiteral("summary"), page->summary);
        if (!page->iconFallback.isEmpty())
            properties.insert(QStringLiteral("iconName"), page->iconFallback);
    }
    if (page)
        router.navigate(page->id);

    auto *view = new QQuickView(&engine, nullptr);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->setColor(QGuiApplication::palette().color(QPalette::Window));
    view->setTitle(page ? page->name : pageId);
    view->setInitialProperties(properties);
    view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + component));
    if (view->status() == QQuickView::Error) {
        for (const auto &error : view->errors())
            qCritical("%s", qPrintable(error.toString()));
        return 1;
    }
    view->resize(1120, 760);
    view->show();
    return QGuiApplication::exec();
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef __GLIBC__
    // the backends' threads share two heaps instead of keeping one each,
    // and pictures and icons go back to the system when freed
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
#endif
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--knows") == 0) {
            QCoreApplication ask(argc, argv);
            return knows(QString::fromLocal8Bit(argv[i + 1])) ? 0 : 1;
        }
    }

    // after the first sign-in: Windows is on this PC, bring its files over?
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--offer-windows-import") == 0) {
            QCoreApplication offer(argc, argv);
            KLocalizedString::setApplicationDomain("raye-settings");
            return WindowsImport::offerAtSignIn();
        }
    }

    QApplication app(argc, argv);

    // used by the build tests: load the interface, report, quit
    if (app.arguments().contains(QStringLiteral("--check-qml")))
        return checkQml();
    KLocalizedString::setApplicationDomain("raye-settings");
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-system")));

    // /usr/bin/kcmshell6 is ours, and sends the module ids that have a
    // Raye page back to this window. Anything we start ourselves must
    // reach the real one instead, or it would come straight back.
    qputenv("RAYE_SETTINGS_KCMSHELL", "pass-through");

    KAboutData about(QStringLiteral("raye-settings"), i18n("Settings"), QStringLiteral(RAYE_SETTINGS_VERSION),
                     i18n("Settings for Raye Linux"), KAboutLicense::GPL_V3);
    about.setDesktopFileName(QStringLiteral("org.raye.settings"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addOption({QStringLiteral("page"), i18n("Settings page to open"), i18n("page")});
    parser.addPositionalArgument(QStringLiteral("page"),
                                 i18n("Settings page to open, as a page name or a settings: address"));
    // for the people building pages: one page alone in a window, as it
    // will sit in Settings (the placeholder when it is not built yet)
    QCommandLineOption previewOption(QStringLiteral("preview"), QStringLiteral("Show one page alone"),
                                           QStringLiteral("page"));
    previewOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(previewOption);
    parser.process(app);
    about.processCommandLine(&parser);

    // One window: a second raye-settings hands its page to this one and
    // quits here. A preview is a window of its own.
    const QString preview = parser.value(previewOption);
    std::unique_ptr<KDBusService> service;
    if (preview.isEmpty())
        service = std::make_unique<KDBusService>(KDBusService::Unique);

    const QString path = layoutPath();
    QFile file(path);
    CategoryModel categories;
    const QString lang = layoutLanguage();
    if (!file.open(QIODevice::ReadOnly) || !categories.load(file.readAll(), lang)) {
        qCritical("raye-settings: cannot read the layout at %s: %s", qPrintable(path),
                  qPrintable(categories.lastError()));
        return 1;
    }

    Router router;
    router.setLayout(categories.layout());
    categories.follow(&router);
    // QML reaches it as the Router singleton of org.raye.settings

    WifiHardware wifi;
    if (wifi.known())
        router.setRequirement(QStringLiteral("wifi"), wifi.present());
    QObject::connect(&wifi, &WifiHardware::changed, &router, [&router](bool present) {
        router.setRequirement(QStringLiteral("wifi"), present);
    });

    // built before the engine, so they outlive the bindings that read them
    UpdateStatus updates(QStringLiteral("/var/lib/raye-update/state.json"));
    SystemInfo system;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    ModuleHost host(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);
    engine.rootContext()->setContextProperty(QStringLiteral("system"), &system);
    engine.rootContext()->setContextProperty(QStringLiteral("categories"), &categories);
    engine.rootContext()->setContextProperty(QStringLiteral("moduleHost"), &host);
    engine.rootContext()->setContextProperty(QStringLiteral("router"), &router);

    if (!preview.isEmpty())
        return showPreview(engine, router, preview);

    const QString start = wantedPage(parser);
    engine.rootContext()->setContextProperty(QStringLiteral("startModule"), router.handOff(start));
    engine.loadFromModule("org.raye.settings", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject::connect(service.get(), &KDBusService::activateRequested, &app,
                     [&](const QStringList &arguments, const QString &) {
                         QCommandLineParser again;
                         about.setupCommandLine(&again);
                         again.addOption({QStringLiteral("page"), i18n("Settings page to open"),
                                          i18n("page")});
                         again.addPositionalArgument(QStringLiteral("page"), i18n("Settings page to open"));
                         again.parse(arguments);
                         const QString page = wantedPage(again);
                         if (!page.isEmpty())
                             open(engine, router.handOff(page));
                         raise(engine);
                     });

    return app.exec();
}
