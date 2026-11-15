// SPDX-License-Identifier: GPL-3.0-or-later
// System > Notifications and Focus: what the switches read and write in
// the notification server's file (plasmanotifyrc) and in the shell's
// (rayeshellrc), on folders of their own, with the defaults the
// desktop installs under /etc/xdg.
#include "notificationsettings.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestNotificationSettings : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QDateTime m_now = QDateTime(QDate(2026, 10, 31), QTime(14, 0));

    QString home(const QString &file) const { return m_dir.filePath(QStringLiteral("config/") + file); }
    QString xdg(const QString &file) const { return m_dir.filePath(QStringLiteral("xdg/") + file); }

    static void write(const QString &path, const QByteArray &text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }

    void reset()
    {
        for (const QString &f : {QStringLiteral("plasmanotifyrc"), QStringLiteral("rayeshellrc"), QStringLiteral("raye-settingsrc")}) {
            QFile::remove(home(f));
            KSharedConfig::openConfig(f)->reparseConfiguration();
        }
    }

    QString entry(const QString &file, const QStringList &groups, const QString &key) const
    {
        KConfig config(home(file), KConfig::SimpleConfig);
        KConfigGroup g = config.group(groups.first());
        for (int i = 1; i < groups.size(); ++i)
            g = g.group(groups.at(i));
        return g.readEntry(key, QStringLiteral("<unset>"));
    }

    std::unique_ptr<NotificationSettings> make()
    {
        auto s = std::make_unique<NotificationSettings>();
        s->setClock([this]() { return m_now; });
        return s;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        KLocalizedString::setApplicationDomain("raye-settings");
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(QStringLiteral("xdg")).toUtf8());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", m_dir.filePath(QStringLiteral("share")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        // what the desktop installs
        write(xdg(QStringLiteral("plasmanotifyrc")),
              "[Applications][org.kde.spectacle]\nShowPopupsInDndMode=true\n\n"
              "[Applications][vlc]\nShowInHistory=false\n\n"
              "[Applications][@other]\nShowInHistory=false\nShowBadges=false\n");
        write(xdg(QStringLiteral("rayeshellrc")), "[Focus]\nUntil=\n\n[Notifications]\nToastSeconds=5\nLockBadges=true\n");
        const QString apps = m_dir.filePath(QStringLiteral("share/applications/"));
        write(apps + QStringLiteral("org.kde.spectacle.desktop"),
              "[Desktop Entry]\nType=Application\nName=Screenshot tool\nName[fr]=Capture\nIcon=spectacle\nExec=spectacle\n");
        write(apps + QStringLiteral("vlc.desktop"), "[Desktop Entry]\nType=Application\nName=VLC\nIcon=vlc\nExec=vlc\n");
        write(apps + QStringLiteral("org.raye.store.desktop"),
              "[Desktop Entry]\nType=Application\nName=Raye Store\nIcon=raye-store\nExec=raye-store\nX-GNOME-UsesNotifications=true\n");
        write(apps + QStringLiteral("hidden.desktop"),
              "[Desktop Entry]\nType=Application\nName=Hidden helper\nNoDisplay=true\nExec=x\nX-GNOME-UsesNotifications=true\n");
        write(apps + QStringLiteral("plain.desktop"), "[Desktop Entry]\nType=Application\nName=Calculator\nExec=x\n");
        write(m_dir.filePath(QStringLiteral("share/knotifications6/files.notifyrc")),
              "[Global]\nIconName=folder\nComment=Files\nDesktopEntry=org.raye.files\n");
        write(apps + QStringLiteral("org.raye.files.desktop"), "[Desktop Entry]\nType=Application\nName=Files\nIcon=folder\nExec=x\n");
    }

    void init() { reset(); }

    void defaults()
    {
        auto s = make();
        QVERIFY(s->enabled());
        QVERIFY(!s->doNotDisturb());
        QVERIFY(s->whenFullscreen());
        QVERIFY(s->whenScreensMirrored());
        QVERIFY(s->whenScreenSharing());
        QVERIFY(s->sounds());
        QVERIFY(s->lockScreenAvailable());
        QVERIFY(s->lockScreen());
        QVERIFY(!s->focusActive());
        QCOMPARE(s->focusMinutes(), NotificationSettings::defaultFocusMinutes);
        QVERIFY(s->focusDoNotDisturb());
    }

    void doNotDisturb()
    {
        auto s = make();
        QSignalSpy changed(s.get(), &NotificationSettings::changed);
        s->setDoNotDisturb(true);
        QVERIFY(s->doNotDisturb());
        QVERIFY(s->enabled());
        QVERIFY(changed.count() >= 1);
        // "until I turn it off" is a year, as the desktop writes it
        KConfig file(home(QStringLiteral("plasmanotifyrc")), KConfig::SimpleConfig);
        const QDateTime until = file.group(QStringLiteral("DoNotDisturb")).readEntry("Until", QDateTime());
        QCOMPARE(until, m_now.addYears(1));
        QVERIFY(!s->doNotDisturbUntil().isValid()); // shown as no end

        s->setDoNotDisturb(false);
        QVERIFY(!s->doNotDisturb());
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), {QStringLiteral("DoNotDisturb")}, QStringLiteral("Until")),
                 QStringLiteral("<unset>"));
    }

    void doNotDisturbForAWhile()
    {
        write(home(QStringLiteral("plasmanotifyrc")), "[DoNotDisturb]\nUntil=2026,10,31,15,30,0\n");
        auto s = make();
        QVERIFY(s->doNotDisturb());
        QCOMPARE(s->doNotDisturbUntil(), QDateTime(QDate(2026, 10, 31), QTime(15, 30)));
        // past: off
        m_now = QDateTime(QDate(2026, 10, 31), QTime(16, 0));
        QVERIFY(!s->doNotDisturb());
        m_now = QDateTime(QDate(2026, 10, 31), QTime(14, 0));
    }

    void masterSwitch()
    {
        auto s = make();
        s->setEnabled(false);
        QVERIFY(!s->enabled());
        QVERIFY(s->doNotDisturb()); // nothing shows: do not disturb with no end
        KConfig file(home(QStringLiteral("plasmanotifyrc")), KConfig::SimpleConfig);
        QCOMPARE(file.group(QStringLiteral("DoNotDisturb")).readEntry("Until", QDateTime()), NotificationSettings::offMarker());
        // do not disturb cannot be turned off while notifications are
        s->setDoNotDisturb(false);
        QVERIFY(!s->enabled());

        s->setEnabled(true);
        QVERIFY(s->enabled());
        QVERIFY(!s->doNotDisturb());

        // turning do not disturb off elsewhere brings notifications back
        s->setEnabled(false);
        write(home(QStringLiteral("plasmanotifyrc")), "[DoNotDisturb]\nUntil=\n");
        KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"))->reparseConfiguration();
        s->reload();
        QVERIFY(s->enabled());
    }

    void automaticRules()
    {
        auto s = make();
        s->setWhenFullscreen(false);
        s->setWhenScreensMirrored(false);
        s->setWhenScreenSharing(false);
        s->setSounds(false);
        const QStringList g{QStringLiteral("DoNotDisturb")};
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("WhenFullscreen")), QStringLiteral("false"));
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("WhenScreensMirrored")), QStringLiteral("false"));
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("WhenScreenSharing")), QStringLiteral("false"));
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("NotificationSoundsMuted")), QStringLiteral("true"));
        auto again = make();
        QVERIFY(!again->whenFullscreen());
        QVERIFY(!again->sounds());
    }

    void lockScreen()
    {
        auto s = make();
        s->setLockScreen(false);
        QCOMPARE(entry(QStringLiteral("rayeshellrc"), {QStringLiteral("Notifications")}, QStringLiteral("LockBadges")),
                 QStringLiteral("false"));
        QVERIFY(!make()->lockScreen());
    }

    void appList()
    {
        auto s = make();
        QStringList ids;
        QStringList names;
        for (const QVariant &v : s->apps()) {
            ids << v.toMap().value(QStringLiteral("id")).toString();
            names << v.toMap().value(QStringLiteral("name")).toString();
        }
        // known through the server's file, a desktop entry or a notifyrc;
        // not @other, not a hidden entry, not an app that never notifies
        QCOMPARE(names, (QStringList{QStringLiteral("Files"), QStringLiteral("Raye Store"),
                                     QStringLiteral("Screenshot tool"), QStringLiteral("VLC")}));
        QVERIFY(!ids.contains(QStringLiteral("@other")));
        QVERIFY(!ids.contains(QStringLiteral("hidden")));
    }

    void appOptions()
    {
        auto s = make();
        QVariantMap vlc = s->app(QStringLiteral("vlc"));
        QCOMPARE(vlc.value(QStringLiteral("popups")).toBool(), true);
        QCOMPARE(vlc.value(QStringLiteral("history")).toBool(), false); // from /etc/xdg
        QCOMPARE(vlc.value(QStringLiteral("enabled")).toBool(), true);
        QCOMPARE(s->app(QStringLiteral("org.kde.spectacle")).value(QStringLiteral("dnd")).toBool(), true);

        QSignalSpy appsChanged(s.get(), &NotificationSettings::appsChanged);
        s->setAppOption(QStringLiteral("vlc"), QStringLiteral("enabled"), false);
        const QStringList g{QStringLiteral("Applications"), QStringLiteral("vlc")};
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("ShowPopups")), QStringLiteral("false"));
        // ShowInHistory=false is already the default of /etc/xdg: KConfig
        // keeps it out of the user's file, and the cascade still says false
        QVERIFY(!s->app(QStringLiteral("vlc")).value(QStringLiteral("history")).toBool());
        QVERIFY(!s->app(QStringLiteral("vlc")).value(QStringLiteral("enabled")).toBool());
        QVERIFY(appsChanged.count() >= 1);

        s->setAppOption(QStringLiteral("vlc"), QStringLiteral("enabled"), true);
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("ShowPopups")), QStringLiteral("true"));
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("ShowInHistory")), QStringLiteral("true"));

        s->setAppOption(QStringLiteral("vlc"), QStringLiteral("badges"), false);
        s->setAppOption(QStringLiteral("vlc"), QStringLiteral("dnd"), true);
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("ShowBadges")), QStringLiteral("false"));
        QCOMPARE(entry(QStringLiteral("plasmanotifyrc"), g, QStringLiteral("ShowPopupsInDndMode")), QStringLiteral("true"));
        // banners only, still in the centre: on
        s->setAppOption(QStringLiteral("vlc"), QStringLiteral("popups"), false);
        QVERIFY(s->app(QStringLiteral("vlc")).value(QStringLiteral("enabled")).toBool());
    }

    void badIdsAreNotWritten()
    {
        auto s = make();
        s->setAppOption(QStringLiteral("../evil"), QStringLiteral("popups"), false);
        s->setAppOption(QStringLiteral("a]b"), QStringLiteral("popups"), false);
        s->setAppOption(QStringLiteral("vlc"), QStringLiteral("nonsense"), false);
        QVERIFY(!QFile::exists(home(QStringLiteral("plasmanotifyrc"))));
        QVERIFY(NotificationSettings::isAppId(QStringLiteral("org.kde.konsole")));
        QVERIFY(!NotificationSettings::isAppId(QStringLiteral("@other")));
        QVERIFY(!NotificationSettings::isAppId(QStringLiteral("-x")));
        QVERIFY(!NotificationSettings::isAppId(QString()));
    }

    void focus()
    {
        auto s = make();
        s->setFocusMinutes(45);
        s->startFocus();
        QVERIFY(s->focusActive());
        const QDateTime end = m_now.addSecs(45 * 60);
        QCOMPARE(s->focusUntil(), end);
        // the shell reads the end in ISO form
        QCOMPARE(entry(QStringLiteral("rayeshellrc"), {QStringLiteral("Focus")}, QStringLiteral("Until")),
                 end.toString(Qt::ISODate));
        // and do not disturb runs until the same time
        QVERIFY(s->doNotDisturb());
        QCOMPARE(s->doNotDisturbUntil(), end);
        QCOMPARE(entry(QStringLiteral("raye-settingsrc"), {QStringLiteral("Focus")}, QStringLiteral("Minutes")), QStringLiteral("45"));

        s->stopFocus();
        QVERIFY(!s->focusActive());
        QVERIFY(!s->doNotDisturb());
        QCOMPARE(entry(QStringLiteral("rayeshellrc"), {QStringLiteral("Focus")}, QStringLiteral("Until")), QString());
    }

    void focusWithoutDoNotDisturb()
    {
        auto s = make();
        s->setFocusDoNotDisturb(false);
        s->startFocus();
        QVERIFY(s->focusActive());
        QVERIFY(!s->doNotDisturb());
        s->stopFocus();
    }

    void stoppingFocusKeepsOtherDoNotDisturb()
    {
        auto s = make();
        s->setDoNotDisturb(true);
        s->startFocus();
        s->stopFocus();
        // do not disturb was on before the session: it stays on
        QVERIFY(s->doNotDisturb());
    }

    void focusLengthIsBounded()
    {
        auto s = make();
        s->setFocusMinutes(1);
        QCOMPARE(s->focusMinutes(), NotificationSettings::minFocusMinutes);
        s->setFocusMinutes(10000);
        QCOMPARE(s->focusMinutes(), NotificationSettings::maxFocusMinutes);
    }

    void focusEnds()
    {
        write(home(QStringLiteral("rayeshellrc")), "[Focus]\nUntil=2026-10-31T13:00:00\n");
        auto s = make();
        QVERIFY(!s->focusActive()); // ended an hour ago
        write(home(QStringLiteral("rayeshellrc")), "[Focus]\nUntil=garbage\n");
        KSharedConfig::openConfig(QStringLiteral("rayeshellrc"))->reparseConfiguration();
        s->reload();
        QVERIFY(!s->focusActive());
    }
};

QTEST_GUILESS_MAIN(TestNotificationSettings)
#include "test_notificationsettings.moc"
