// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Desktop environment: which desktop the session is,
// which ones are installed, what the next sign-in opens, what is installed
// first, a switch right now (the apps written down for the other desktop,
// one sign-in armed, the session left, taken back when an app stays open),
// what is never removed by itself, and the page as the window loads it:
// two cards, the save-your-work note and the open apps on the page itself,
// no dialog, the transition, and sign out now or later.
//
// Nothing here touches the PC: the session files are in a folder of the
// test's own, the helper is a small script that writes down its calls and
// packages "install" by dropping a session file there.
#include "desktopenvironment.h"

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
#include <QTimer>

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

QQuickItem *named(QQuickItem *item, const QString &name)
{
    if (!item)
        return nullptr;
    if (item->objectName() == name)
        return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        if (QQuickItem *found = named(child, name))
            return found;
    return nullptr;
}

} // namespace

class TestDesktopEnvironment : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    // what the fake package manager was asked: "install raye-hyprland"
    QStringList m_packageCalls;
    bool m_packagesWork = true;
    // what the fake app list says is open, and the sessions left
    QStringList m_openIds{QStringLiteral("org.kde.kate"), QStringLiteral("org.raye.settings"), QStringLiteral("bad/id")};
    QStringList m_left;

    QString path(const QString &name) const { return m_dir.filePath(name); }
    QString sessions() const { return path(QStringLiteral("sessions")); }

    void touch(const QString &file)
    {
        QDir().mkpath(sessions());
        QFile f(sessions() + QLatin1Char('/') + file);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }

    void writeHelper(int code)
    {
        QFile f(path(QStringLiteral("helper")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\necho \"$@\" >> '" + path(QStringLiteral("calls")).toUtf8() + "'\nexit " + QByteArray::number(code) + "\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }

    QStringList calls() const
    {
        QFile f(path(QStringLiteral("calls")));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    DesktopEnvironment::PackageRunner fakePackages()
    {
        return [this](const QString &operation, const QStringList &names, std::function<void(int)> progress,
                      std::function<void(bool, const QString &)> done) {
            m_packageCalls << operation + QLatin1Char(' ') + names.join(QLatin1Char(' '));
            progress(40);
            if (m_packagesWork) {
                for (const QString &name : names) {
                    const QString file = name == QLatin1String("raye-hyprland") ? QStringLiteral("raye-hyprland.desktop")
                                                                                : QStringLiteral("plasma.desktop");
                    if (operation == QLatin1String("install"))
                        touch(file);
                    else
                        QFile::remove(sessions() + QLatin1Char('/') + file);
                }
            }
            QTimer::singleShot(0, [done, this] { done(m_packagesWork, m_packagesWork ? QString() : QStringLiteral("no network")); });
        };
    }

    std::unique_ptr<DesktopEnvironment> make(const QString &xdgCurrentDesktop, const QString &session = QStringLiteral("2"))
    {
        return std::make_unique<DesktopEnvironment>(
            sessions(), QStringList{path(QStringLiteral("helper"))}, xdgCurrentDesktop, session, QStringLiteral("raye-settingsrc"),
            fakePackages(),
            [this](const QString &, std::function<void(const QStringList &)> done) {
                QTimer::singleShot(0, [this, done] { done(m_openIds); });
            },
            [this](const QString &desktop) { m_left << desktop; });
    }

    QByteArray restoreWritten() const
    {
        QFile f(DesktopEnvironment::restoreFile());
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", path(QStringLiteral("config")).toUtf8());
        qputenv("HOME", path(QStringLiteral("home")).toUtf8());
        qputenv("XDG_STATE_HOME", path(QStringLiteral("state")).toUtf8());
        qputenv("XDG_DATA_DIRS", path(QStringLiteral("share")).toUtf8());
        qputenv("XDG_DATA_HOME", path(QStringLiteral("share-home")).toUtf8());
        QDir().mkpath(path(QStringLiteral("share/applications")));
        QFile kate(path(QStringLiteral("share/applications/org.kde.kate.desktop")));
        QVERIFY(kate.open(QIODevice::WriteOnly));
        kate.write("[Desktop Entry]\nType=Application\nName=Kate\nName[fr]=Kate\nExec=kate\n");
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void init()
    {
        QDir(path(QStringLiteral("config"))).removeRecursively();
        QDir(sessions()).removeRecursively();
        QDir().mkpath(path(QStringLiteral("config")));
        QFile::remove(path(QStringLiteral("calls")));
        KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"))->reparseConfiguration();
        m_packageCalls.clear();
        m_packagesWork = true;
        m_left.clear();
        QFile::remove(DesktopEnvironment::restoreFile());
        writeHelper(0);
    }

    void theSessionsDesktopIsReadFromItsNames()
    {
        QCOMPARE(DesktopEnvironment::desktopOf(QStringLiteral("KDE")), QStringLiteral("plasma"));
        QCOMPARE(DesktopEnvironment::desktopOf(QStringLiteral("Hyprland")), QStringLiteral("hyprland"));
        QCOMPARE(DesktopEnvironment::desktopOf(QStringLiteral("ubuntu:GNOME")), QString());
        QCOMPARE(DesktopEnvironment::desktopOf(QString()), QString());
        // the session file's DesktopNames, as the theme ships it
        QFile session(QStringLiteral(SOURCE_DIR "/../theme/hyprland/session/raye-hyprland.desktop"));
        if (session.open(QIODevice::ReadOnly))
            QVERIFY(session.readAll().contains("DesktopNames=Hyprland"));
    }

    // Settings opened again by the desktop switch runs as a systemd unit,
    // without XDG_SESSION_ID: "Switch to KDE Plasma now" did nothing on
    // Hyprland (VM 130, 1.0 Beta Sundog). logind's graphical session of
    // the person is asked then.
    void theSessionIsAskedOfLogindWhenTheEnvironmentHasNone()
    {
        bool asked = false;
        const auto display = [&asked](const QString &answer) {
            return [&asked, answer] {
                asked = true;
                return answer;
            };
        };
        QCOMPARE(DesktopEnvironment::sessionOrDisplay(QStringLiteral("5"), display(QStringLiteral("9\n"))), QStringLiteral("5"));
        QVERIFY(!asked);
        QCOMPARE(DesktopEnvironment::sessionOrDisplay(QString(), display(QStringLiteral("17\n"))), QStringLiteral("17"));
        QVERIFY(asked);
        QCOMPARE(DesktopEnvironment::sessionOrDisplay(QString(), display(QString())), QString());
        QCOMPARE(DesktopEnvironment::sessionOrDisplay(QString(), display(QStringLiteral("3 --now"))), QString());
    }

    void installedIsWhatTheSessionFilesSay()
    {
        touch(QStringLiteral("plasma.desktop"));
        auto d = make(QStringLiteral("KDE"));
        QVERIFY(d->plasmaInstalled());
        QVERIFY(!d->hyprlandInstalled());
        QCOMPARE(d->current(), QStringLiteral("plasma"));
        QCOMPARE(d->next(), QStringLiteral("plasma"));
        QVERIFY(!d->signOutNeeded());
        // only Hyprland can be removed, and only when both are there
        QVERIFY(!d->canRemoveHyprland());
    }

    void choosingAnInstalledDesktopRunsTheHelperOnly()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        auto d = make(QStringLiteral("KDE"));
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        QVERIFY(d->choose(QStringLiteral("hyprland")));
        QCOMPARE(d->busy(), QStringLiteral("switching"));
        QVERIFY(done.wait());
        QCOMPARE(done.first().at(0).toString(), QStringLiteral("switch"));
        QVERIFY(done.first().at(2).toBool());
        QCOMPARE(calls(), QStringList{QStringLiteral("set hyprland")});
        QVERIFY(m_packageCalls.isEmpty());
        QCOMPARE(d->next(), QStringLiteral("hyprland"));
        QVERIFY(d->signOutNeeded());
        QVERIFY(!d->canRemoveHyprland());
        // the same again does nothing
        QVERIFY(!d->choose(QStringLiteral("hyprland")));
        // and nothing but the two desktops
        QVERIFY(!d->choose(QStringLiteral("gnome")));
    }

    void aMissingDesktopIsInstalledFirst()
    {
        touch(QStringLiteral("plasma.desktop"));
        auto d = make(QStringLiteral("KDE"));
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        // choosing the next sign-in never installs by itself
        QVERIFY(!d->choose(QStringLiteral("hyprland")));
        QVERIFY(d->install(QStringLiteral("hyprland"), false));
        QCOMPARE(d->busy(), QStringLiteral("installing"));
        QCOMPARE(d->busyPercent(), 40);
        QVERIFY(done.wait());
        QCOMPARE(done.first().at(0).toString(), QStringLiteral("install"));
        QVERIFY(done.wait());
        QCOMPARE(m_packageCalls, QStringList{QStringLiteral("install raye-hyprland")});
        QCOMPARE(calls(), QStringList{QStringLiteral("set hyprland")});
        QVERIFY(d->hyprlandInstalled());
        QCOMPARE(d->next(), QStringLiteral("hyprland"));
        QVERIFY(d->plasmaInstalled());
    }

    void aFailedInstallSwitchesNothing()
    {
        touch(QStringLiteral("plasma.desktop"));
        m_packagesWork = false;
        auto d = make(QStringLiteral("KDE"));
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        QVERIFY(d->install(QStringLiteral("hyprland"), true));
        QVERIFY(done.wait());
        QCOMPARE(done.first().at(0).toString(), QStringLiteral("install"));
        QVERIFY(!done.first().at(2).toBool());
        QVERIFY(m_left.isEmpty());
        QVERIFY(calls().isEmpty());
        QCOMPARE(d->next(), QStringLiteral("plasma"));
        QCOMPARE(d->busy(), QString());
    }

    void aClosedPasswordBoxIsNotAnError()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        writeHelper(126);
        auto d = make(QStringLiteral("KDE"));
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        QVERIFY(d->choose(QStringLiteral("hyprland")));
        QVERIFY(done.wait());
        QVERIFY(!done.first().at(2).toBool());
        QVERIFY(done.first().at(3).toBool());
        QCOMPARE(d->next(), QStringLiteral("plasma"));
    }

    // what was chosen here counts for this session; the next session is
    // its own answer (the sign-in screen may have picked another)
    void aChoiceIsForgottenInTheNextSession()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        {
            auto d = make(QStringLiteral("KDE"), QStringLiteral("2"));
            QSignalSpy done(d.get(), &DesktopEnvironment::finished);
            d->choose(QStringLiteral("hyprland"));
            QVERIFY(done.wait());
        }
        KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"))->reparseConfiguration();
        auto same = make(QStringLiteral("KDE"), QStringLiteral("2"));
        QCOMPARE(same->next(), QStringLiteral("hyprland"));
        auto later = make(QStringLiteral("KDE"), QStringLiteral("3"));
        QCOMPARE(later->next(), QStringLiteral("plasma"));
        QVERIFY(!later->signOutNeeded());
    }

    void hyprlandIsRemovedOnlyWhenItIsNotInUse()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        auto inHyprland = make(QStringLiteral("Hyprland"));
        QVERIFY(!inHyprland->canRemoveHyprland());
        QVERIFY(!inHyprland->removeHyprland());

        auto d = make(QStringLiteral("KDE"));
        QVERIFY(d->canRemoveHyprland());
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        QVERIFY(d->removeHyprland());
        QCOMPARE(d->busy(), QStringLiteral("removing"));
        QVERIFY(done.wait());
        QCOMPARE(m_packageCalls, QStringList{QStringLiteral("remove raye-hyprland")});
        QVERIFY(done.first().at(2).toBool());
        QVERIFY(!d->hyprlandInstalled());
        QVERIFY(d->plasmaInstalled());
    }

    void signingOutFollowsTheDesktop()
    {
        QCOMPARE(DesktopEnvironment::signOutCommand(QStringLiteral("plasma"), QStringLiteral("2")).first(),
                 QStringLiteral("dbus:org.kde.Shutdown"));
        QCOMPARE(DesktopEnvironment::signOutCommand(QStringLiteral("hyprland"), QStringLiteral("2")),
                 QStringList({QStringLiteral("hyprctl"), QStringLiteral("dispatch"), QStringLiteral("exit")}));
        QCOMPARE(DesktopEnvironment::signOutCommand(QString(), QStringLiteral("7")),
                 QStringList({QStringLiteral("loginctl"), QStringLiteral("terminate-session"), QStringLiteral("7")}));
        QVERIFY(DesktopEnvironment::signOutCommand(QString(), QString()).isEmpty());
    }

    void thePageIsReachedFromPersonalizationAndSearch()
    {
        QFile layout(QStringLiteral(LAYOUT_FILE));
        QVERIFY(layout.open(QIODevice::ReadOnly));
        QVERIFY(layout.readAll().contains("\"component\": \"pages/personalization/DesktopEnvironmentPage.qml\""));
        QFile landing(QStringLiteral(SOURCE_DIR "/src/qml/pages/personalization/PersonalizationPage.qml"));
        QVERIFY(landing.open(QIODevice::ReadOnly));
        QVERIFY(landing.readAll().contains("target: \"personalization/desktop\""));
        QFile search(QStringLiteral(SOURCE_DIR "/data/search/personalization.json"));
        QVERIFY(search.open(QIODevice::ReadOnly));
        QStringList ids;
        const QJsonArray entries = QJsonDocument::fromJson(search.readAll()).object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &v : entries)
            ids << v.toObject().value(QStringLiteral("id")).toString();
        for (const char *id : {"personalization/desktop#plasma", "personalization/desktop#hyprland", "personalization/desktop#remove-hyprland"})
            QVERIFY2(ids.contains(QLatin1String(id)), id);
        // the page itself is in the index from the layout; its words are synonyms
        QVERIFY(!ids.contains(QLatin1String("personalization/desktop")));
        QFile synonyms(QStringLiteral(SOURCE_DIR "/data/search/synonyms.json"));
        QVERIFY(synonyms.open(QIODevice::ReadOnly));
        QString terms;
        const QJsonArray pages = QJsonDocument::fromJson(synonyms.readAll()).object().value(QStringLiteral("targets")).toArray();
        for (const QJsonValue &v : pages)
            if (v.toObject().value(QStringLiteral("id")).toString() == QLatin1String("personalization/desktop"))
                terms = QString::fromUtf8(QJsonDocument(v.toObject()).toJson());
        for (const char *word : {"hyprland", "kde", "session", "bureau", "changer de bureau"})
            QVERIFY2(terms.contains(QString::fromUtf8(word)), word);
    }

    // --- a switch right now ------------------------------------------------
    void switchingNowWritesTheAppsArmsOneSignInAndLeaves()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        auto d = make(QStringLiteral("KDE"), QStringLiteral("5"));
        QVERIFY(d->canSwitchNow());
        QSignalSpy ready(d.get(), &DesktopEnvironment::readyToLeave);
        QVERIFY(d->switchNow(QStringLiteral("hyprland")));
        QCOMPARE(d->busy(), QStringLiteral("preparing"));
        QVERIFY(ready.wait());
        // the helper armed one sign-in for this session
        QCOMPARE(calls(), QStringList{QStringLiteral("switch hyprland 5")});
        // the apps, for Hyprland, read once by desktop-restore
        const QByteArray list = restoreWritten();
        QVERIFY(list.contains("\nto=hyprland\n"));
        QVERIFY(list.contains("\napp=org.kde.kate\n"));
        QVERIFY(!list.contains("bad/id"));
        // Settings once, last: it opens again on the Desktop environment page
        QCOMPARE(list.count("app=org.raye.settings"), 1);
        QVERIFY(list.endsWith("app=org.raye.settings\n"));
        QVERIFY(m_left.isEmpty());
        // the page plays its transition, then leaves
        d->leave();
        QCOMPARE(m_left, QStringList{QStringLiteral("plasma")});
        QCOMPARE(d->busy(), QStringLiteral("leaving"));
        QCOMPARE(d->next(), QStringLiteral("hyprland"));
    }

    // the list desktop-restore reads, in the words it reads
    void theRestoreListIsWhatTheRestoreHelperReads()
    {
        const QByteArray list = DesktopEnvironment::restoreList(QStringLiteral("plasma"), 1700000000,
                                                                {QStringLiteral("org.kde.dolphin"), QStringLiteral("org.kde.dolphin"),
                                                                 QStringLiteral("../x"), QStringLiteral("firefox")});
        QCOMPARE(list.count("\napp="), 3);
        QVERIFY(list.contains("\nto=plasma\nat=1700000000\n"));
        QFile helper(QStringLiteral(SOURCE_DIR "/data/libexec/desktop-restore"));
        QVERIFY(helper.open(QIODevice::ReadOnly));
        const QByteArray script = helper.readAll();
        for (const char *word : {"value to", "value at", "s/^app=//p", "/raye/desktop-switch"})
            QVERIFY2(script.contains(word), word);
        QVERIFY(DesktopEnvironment::restoreFile().endsWith(QLatin1String("/raye/desktop-switch")));
    }

    void anAppThatStaysOpenTakesTheSwitchBack()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        auto d = make(QStringLiteral("KDE"), QStringLiteral("5"));
        QSignalSpy ready(d.get(), &DesktopEnvironment::readyToLeave);
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        QVERIFY(d->switchNow(QStringLiteral("hyprland")));
        QVERIFY(ready.wait());
        d->leave();
        // the session is still here (an app asked to save): taken back
        QVERIFY(done.wait(25000));
        QCOMPARE(done.last().at(0).toString(), QStringLiteral("switch-now"));
        QVERIFY(!done.last().at(2).toBool());
        QCOMPARE(done.last().at(4).toString(), QStringLiteral("still-open"));
        QTRY_COMPARE(calls(), QStringList({QStringLiteral("switch hyprland 5"), QStringLiteral("cancel")}));
        QVERIFY(!QFile::exists(DesktopEnvironment::restoreFile()));
        QCOMPARE(d->busy(), QString());
    }

    void aPCThatCannotSwitchRightAwaySetsTheNextSignIn()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        writeHelper(6);
        auto d = make(QStringLiteral("KDE"), QStringLiteral("5"));
        QSignalSpy unavailable(d.get(), &DesktopEnvironment::switchNowUnavailable);
        QSignalSpy ready(d.get(), &DesktopEnvironment::readyToLeave);
        QVERIFY(d->switchNow(QStringLiteral("hyprland")));
        QVERIFY(unavailable.wait());
        QVERIFY(ready.isEmpty());
        QVERIFY(m_left.isEmpty());
        QVERIFY(!QFile::exists(DesktopEnvironment::restoreFile()));
        // then the next sign-in, as Settings did before (set)
        QTRY_COMPARE(calls(), QStringList({QStringLiteral("switch hyprland 5"), QStringLiteral("set hyprland")}));
    }

    void aClosedPasswordBoxOrNoSessionSwitchesNothingNow()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        auto noSession = make(QStringLiteral("KDE"), QString());
        QVERIFY(!noSession->switchNow(QStringLiteral("hyprland")));
        auto other = make(QStringLiteral("GNOME"), QStringLiteral("5"));
        QVERIFY(!other->canSwitchNow());
        auto d = make(QStringLiteral("KDE"), QStringLiteral("5"));
        // the desktop in use is not a switch
        QVERIFY(!d->switchNow(QStringLiteral("plasma")));
        writeHelper(126);
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        QVERIFY(d->switchNow(QStringLiteral("hyprland")));
        QVERIFY(done.wait());
        QVERIFY(done.first().at(3).toBool());
        QVERIFY(m_left.isEmpty());
        QVERIFY(!QFile::exists(DesktopEnvironment::restoreFile()));
    }

    void installingCanSwitchRightAfter()
    {
        touch(QStringLiteral("plasma.desktop"));
        auto d = make(QStringLiteral("KDE"), QStringLiteral("5"));
        QSignalSpy ready(d.get(), &DesktopEnvironment::readyToLeave);
        QVERIFY(d->install(QStringLiteral("hyprland"), true));
        QVERIFY(ready.wait());
        QCOMPARE(m_packageCalls, QStringList{QStringLiteral("install raye-hyprland")});
        QCOMPARE(calls(), QStringList{QStringLiteral("switch hyprland 5")});
    }

    void hyprlandWindowsAreNamedByTheirApps()
    {
        const QList<QPair<QString, QString>> apps{{QStringLiteral("org.kde.dolphin"), QString()},
                                                  {QStringLiteral("firefox_firefox"), QStringLiteral("firefox")},
                                                  {QStringLiteral("org.kde.kate"), QStringLiteral("kate")},
                                                  {QStringLiteral("code"), QString()}};
        QCOMPARE(DesktopEnvironment::desktopIdForClass(QStringLiteral("org.kde.dolphin"), apps), QStringLiteral("org.kde.dolphin"));
        QCOMPARE(DesktopEnvironment::desktopIdForClass(QStringLiteral("firefox"), apps), QStringLiteral("firefox_firefox"));
        QCOMPARE(DesktopEnvironment::desktopIdForClass(QStringLiteral("Code"), apps), QStringLiteral("code"));
        QCOMPARE(DesktopEnvironment::desktopIdForClass(QStringLiteral("dolphin"), apps), QStringLiteral("org.kde.dolphin"));
        QCOMPARE(DesktopEnvironment::desktopIdForClass(QStringLiteral("nothing"), apps), QString());
        QCOMPARE(DesktopEnvironment::desktopIdForClass(QString(), apps), QString());
    }

    void kwinsScriptHandsTheAppsToSettings()
    {
        const QByteArray script = DesktopEnvironment::kwinListScript();
        for (const char *word : {"workspace.windowList()", "desktopFileName", "skipTaskbar", "normalWindow",
                                 "callDBus(\"org.raye.settings.DesktopSwitch\", \"/DesktopSwitch\", \"org.raye.settings.DesktopSwitch\", \"apps\""})
            QVERIFY2(script.contains(word), word);
        touch(QStringLiteral("plasma.desktop"));
        auto d = make(QStringLiteral("KDE"));
        // what the lister gives: Settings itself left out, names shown
        d->refreshOpenApps();
        QTRY_COMPARE(d->openApps(), QStringList{QStringLiteral("Kate")});
    }

    void thePageSaysWhatSwitchingDoesAndSwitchesWithoutADialog()
    {
        touch(QStringLiteral("plasma.desktop"));
        auto d = make(QStringLiteral("KDE"), QStringLiteral("5"));
        qmlWarnings.clear();
        qInstallMessageHandler(collect);
        auto view = std::make_unique<QQuickView>();
        view->engine()->rootContext()->setContextObject(new KLocalizedContext(view->engine()));
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(1120, 900);
        view->setInitialProperties({{QStringLiteral("desktops"), QVariant::fromValue(d.get())}});
        view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/personalization/DesktopEnvironmentPage.qml")));
        QCOMPARE(view->status(), QQuickView::Ready);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        QTest::qWait(100);
        QQuickItem *root = view->rootObject();
        QQuickItem *plasma = named(root, QStringLiteral("plasmaCard"));
        QQuickItem *hyprland = named(root, QStringLiteral("hyprlandCard"));
        QVERIFY(plasma && hyprland);
        QVERIFY(plasma->property("checked").toBool());
        QCOMPARE(plasma->property("stateText").toString(), QStringLiteral("In use"));
        QVERIFY(hyprland->property("stateText").toString().startsWith(QLatin1String("Not installed")));
        QVERIFY(!named(root, QStringLiteral("switchPanel"))->isVisible());
        QVERIFY(!named(root, QStringLiteral("removeHyprland"))->isVisible());

        // picking Hyprland only shows, on the page, what a switch does
        QMetaObject::invokeMethod(hyprland, "click");
        QTest::qWait(100);
        QVERIFY(hyprland->property("checked").toBool());
        QVERIFY(named(root, QStringLiteral("switchPanel"))->isVisible());
        QCOMPARE(named(root, QStringLiteral("saveNote"))->property("text").toString(),
                 QStringLiteral("Open apps close and open again on Hyprland. Save your work first."));
        QTRY_COMPARE(named(root, QStringLiteral("openAppsLine"))->property("text").toString(), QStringLiteral("1 app is open: Kate"));
        QVERIFY(named(root, QStringLiteral("downloadLine"))->isVisible());
        QQuickItem *now = named(root, QStringLiteral("switchNowButton"));
        QCOMPARE(now->property("text").toString(), QStringLiteral("Install and switch now"));
        QVERIFY(calls().isEmpty());
        QVERIFY(m_packageCalls.isEmpty());

        // one press: installed, armed, the transition plays, the session is left
        QSignalSpy ready(d.get(), &DesktopEnvironment::readyToLeave);
        QMetaObject::invokeMethod(now, "click");
        QVERIFY(ready.wait());
        QObject *transition = root->findChild<QObject *>(QStringLiteral("switchTransition"));
        QVERIFY(transition);
        QTRY_VERIFY(transition->property("visible").toBool());
        QCOMPARE(transition->property("title").toString(), QStringLiteral("Switching to Hyprland…"));
        QTRY_COMPARE_WITH_TIMEOUT(m_left, QStringList{QStringLiteral("plasma")}, 5000);
        QCOMPARE(calls(), QStringList{QStringLiteral("switch hyprland 5")});
        QVERIFY(root->findChildren<QObject *>(QStringLiteral("installDialog")).isEmpty());
        qInstallMessageHandler(nullptr);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }

    void laterSetsTheNextSignInAndOffersToSignOut()
    {
        touch(QStringLiteral("plasma.desktop"));
        touch(QStringLiteral("raye-hyprland.desktop"));
        auto d = make(QStringLiteral("KDE"), QStringLiteral("5"));
        qmlWarnings.clear();
        qInstallMessageHandler(collect);
        auto view = std::make_unique<QQuickView>();
        view->engine()->rootContext()->setContextObject(new KLocalizedContext(view->engine()));
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(1120, 900);
        view->setInitialProperties({{QStringLiteral("desktops"), QVariant::fromValue(d.get())}});
        view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/personalization/DesktopEnvironmentPage.qml")));
        QCOMPARE(view->status(), QQuickView::Ready);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        QQuickItem *root = view->rootObject();
        QQuickItem *plasma = named(root, QStringLiteral("plasmaCard"));
        QQuickItem *hyprland = named(root, QStringLiteral("hyprlandCard"));
        QVERIFY(named(root, QStringLiteral("removeHyprland"))->isVisible());
        QVERIFY(named(root, QStringLiteral("removeHyprland"))->isEnabled());
        QMetaObject::invokeMethod(hyprland, "click");
        QTest::qWait(50);
        QCOMPARE(named(root, QStringLiteral("switchNowButton"))->property("text").toString(), QStringLiteral("Switch to Hyprland now"));
        QSignalSpy done(d.get(), &DesktopEnvironment::finished);
        QMetaObject::invokeMethod(named(root, QStringLiteral("switchLaterButton")), "click");
        QVERIFY(done.wait());
        QTest::qWait(50);
        QCOMPARE(hyprland->property("stateText").toString(), QStringLiteral("Opens the next time you sign in"));
        QCOMPARE(plasma->property("stateText").toString(), QStringLiteral("In use until you sign out"));
        QQuickItem *bar = named(root, QStringLiteral("signOutBar"));
        QVERIFY(bar->property("open").toBool());
        QVERIFY(bar->property("message").toString().contains(QLatin1String("Hyprland")));
        QVERIFY(!named(root, QStringLiteral("removeHyprland"))->isEnabled());
        QVERIFY(m_left.isEmpty());
        QMetaObject::invokeMethod(named(root, QStringLiteral("signOutLater")), "click");
        QTest::qWait(50);
        QVERIFY(!bar->property("open").toBool());
        // back to Plasma for the next sign-in: no sign-out needed
        QMetaObject::invokeMethod(plasma, "click");
        QTest::qWait(50);
        QVERIFY(named(root, QStringLiteral("switchPanel"))->isVisible());
        QVERIFY(!named(root, QStringLiteral("switchNowButton"))->isVisible());
        QQuickItem *keep = named(root, QStringLiteral("switchLaterButton"));
        QCOMPARE(keep->property("text").toString(), QStringLiteral("Keep KDE Plasma"));
        QMetaObject::invokeMethod(keep, "click");
        QVERIFY(done.wait());
        QTest::qWait(50);
        QCOMPARE(plasma->property("stateText").toString(), QStringLiteral("In use"));
        QVERIFY(!named(root, QStringLiteral("switchPanel"))->isVisible());
        QVERIFY(named(root, QStringLiteral("removeHyprland"))->isEnabled());
        QCOMPARE(calls(), QStringList({QStringLiteral("set hyprland"), QStringLiteral("set plasma")}));
        qInstallMessageHandler(nullptr);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestDesktopEnvironment)
#include "test_desktopenvironment.moc"
