// SPDX-License-Identifier: GPL-3.0-or-later
// The Privacy pages' backends:
// - app permissions against a stand-in for the desktop's permission
//   store, on a D-Bus daemon of the test's own;
// - "File system" against Flatpak installations made of a few folders;
// - the firewall's settings file and its helper, with a fake ufw;
// - the file index, the activity history and the usage statistics
//   settings, in the test's own config folder.
#include "firewall.h"
#include "indexing.h"
#include "permissions.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using PermissionTable = QMap<QString, QStringList>;

// What xdg-permission-store answers, kept in memory.
class FakeStore : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.PermissionStore")

public:
    QHash<QString, PermissionTable> entries; // "table/id" → app → values
    int writes = 0;

public Q_SLOTS:
    // on the store's own thread: its connection and its object
    bool attach(const QString &address)
    {
        QDBusConnection bus = QDBusConnection::connectToBus(address, QStringLiteral("store"));
        return bus.isConnected()
            && bus.registerObject(QStringLiteral("/org/freedesktop/impl/portal/PermissionStore"), this,
                                  QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
            && bus.registerService(QStringLiteral("org.freedesktop.impl.portal.PermissionStore"));
    }

    // the store goes away, and comes back
    bool setRegistered(bool on)
    {
        QDBusConnection bus(QStringLiteral("store"));
        const QString name = QStringLiteral("org.freedesktop.impl.portal.PermissionStore");
        return on ? bus.registerService(name) : bus.unregisterService(name);
    }

    // changes a value from outside (another app's prompt) and says so
    void change(const QString &table, const QString &id, const QString &app, const QStringList &value)
    {
        entries[table + QLatin1Char('/') + id][app] = value;
        Q_EMIT Changed(table, id, false, entries.value(table + QLatin1Char('/') + id), QDBusVariant(QVariant(0)));
    }

public Q_SLOTS:
    PermissionTable Lookup(const QString &table, const QString &id, QDBusVariant &data)
    {
        data = QDBusVariant(QVariant(0));
        const QString key = table + QLatin1Char('/') + id;
        if (!entries.contains(key)) {
            sendErrorReply(QStringLiteral("org.freedesktop.portal.Error.NotFound"), QStringLiteral("No entry"));
            return {};
        }
        return entries.value(key);
    }

    void SetPermission(const QString &table, bool create, const QString &id, const QString &app,
                       const QStringList &permissions)
    {
        const QString key = table + QLatin1Char('/') + id;
        if (!create && !entries.contains(key)) {
            sendErrorReply(QStringLiteral("org.freedesktop.portal.Error.NotFound"), QStringLiteral("No entry"));
            return;
        }
        ++writes;
        entries[key][app] = permissions;
        Q_EMIT Changed(table, id, false, entries.value(key), QDBusVariant(QVariant(0)));
    }

Q_SIGNALS:
    void Changed(const QString &table, const QString &id, bool deleted, const PermissionTable &permissions,
                 const QDBusVariant &data);
};

class TestPermissions : public QObject
{
    Q_OBJECT

    QProcess m_daemon;
    QString m_address;
    // answers on a thread of its own, as the real store answers from its
    // own process: the backend's calls wait for the answer
    QThread m_storeThread;
    // deleted on its own thread, where it lives
    FakeStore *m_store = new FakeStore;
    QTemporaryDir m_dir;

    void storeChange(const QString &table, const QString &id, const QString &app, const QStringList &value)
    {
        QMetaObject::invokeMethod(m_store, [&] { m_store->change(table, id, app, value); }, Qt::BlockingQueuedConnection);
    }

    bool storeRegistered(bool on)
    {
        bool ok = false;
        QMetaObject::invokeMethod(m_store, "setRegistered", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, ok),
                                  Q_ARG(bool, on));
        return ok;
    }

    QDBusConnection clientBus() const { return QDBusConnection(QStringLiteral("client")); }

    void writeFile(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        f.write(data);
    }

    QByteArray readFile(const QString &path) const
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // an app installed in a fake Flatpak installation
    void installApp(const QString &installation, const QString &id, const QByteArray &filesystems)
    {
        writeFile(m_dir.filePath(installation + QStringLiteral("/app/") + id + QStringLiteral("/current/active/metadata")),
                  "[Application]\nname=" + id.toUtf8() + "\nruntime=org.kde.Platform/x86_64/6.8\n\n[Context]\nshared=network;ipc;\n"
                      + (filesystems.isEmpty() ? QByteArray() : "filesystems=" + filesystems + "\n"));
    }

    // waits for the model to have read the store
    bool load(AppPermissions &model, const QString &kind)
    {
        QSignalSpy loaded(&model, &AppPermissions::loaded);
        model.setBus(clientBus());
        model.setKind(kind);
        return loaded.count() > 0 || loaded.wait(5000);
    }

    QStringList ids(const AppPermissions &model) const
    {
        QStringList list;
        for (int row = 0; row < model.rowCount(); ++row)
            list.append(model.data(model.index(row), AppPermissions::AppIdRole).toString());
        return list;
    }

    void resetPrivacyConfig()
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"));
        config->deleteGroup(QStringLiteral("Privacy"));
        config->sync();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
        qDBusRegisterMetaType<PermissionTable>();

        // a bus of our own: the session's may have the real store on it;
        // and no service files, or the real one would start on demand
        const QString busConfig = m_dir.filePath(QStringLiteral("bus.conf"));
        writeFile(busConfig, QStringLiteral("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                                            " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                                            "<busconfig><type>session</type><listen>unix:dir=%1</listen>\n"
                                            "<policy context=\"default\"><allow send_destination=\"*\"/><allow receive_sender=\"*\"/><allow own=\"*\"/></policy>\n"
                                            "</busconfig>\n")
                                 .arg(m_dir.path())
                                 .toUtf8());
        m_daemon.start(QStringLiteral("dbus-daemon"), {QStringLiteral("--config-file=") + busConfig,
                                                       QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        QVERIFY2(m_daemon.waitForStarted(5000), "dbus-daemon is needed for this test");
        QVERIFY(m_daemon.waitForReadyRead(5000));
        m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
        QVERIFY(!m_address.isEmpty());

        m_store->moveToThread(&m_storeThread);
        m_storeThread.start();
        bool attached = false;
        QMetaObject::invokeMethod(m_store, "attach", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, attached),
                                  Q_ARG(QString, m_address));
        QVERIFY(attached);
        QVERIFY(QDBusConnection::connectToBus(m_address, QStringLiteral("client")).isConnected());

        // what the apps call themselves
        const QString apps = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        writeFile(apps + QStringLiteral("/org.raye.Paint.desktop"),
                  "[Desktop Entry]\nType=Application\nName=Paint\nIcon=org.raye.Paint\nExec=paint\n");
        writeFile(apps + QStringLiteral("/com.example.Chat.desktop"),
                  "[Desktop Entry]\nType=Application\nName=Chat\nIcon=chat\nExec=chat\n");

        qputenv("FLATPAK_USER_DIR", QFile::encodeName(m_dir.filePath(QStringLiteral("user"))));
        qputenv("FLATPAK_SYSTEM_DIR", QFile::encodeName(m_dir.filePath(QStringLiteral("system"))));
    }

    void cleanupTestCase()
    {
        QMetaObject::invokeMethod(m_store, [] {
            QDBusConnection(QStringLiteral("store")).unregisterObject(QStringLiteral("/org/freedesktop/impl/portal/PermissionStore"));
            QDBusConnection::disconnectFromBus(QStringLiteral("store"));
        }, Qt::BlockingQueuedConnection);
        m_store->deleteLater();
        m_store = nullptr;
        m_storeThread.quit();
        m_storeThread.wait();
        QDBusConnection::disconnectFromBus(QStringLiteral("client"));
        m_daemon.kill();
        m_daemon.waitForFinished(3000);
    }

    void init()
    {
        m_store->entries.clear();
        m_store->writes = 0;
        resetPrivacyConfig();
    }

    // --- the store's values ---------------------------------------------

    void storeValues()
    {
        QCOMPARE(AppPermissions::storeValue(QStringLiteral("camera"), true, {}), QStringList{QStringLiteral("yes")});
        QCOMPARE(AppPermissions::storeValue(QStringLiteral("background"), false, {QStringLiteral("yes")}),
                 QStringList{QStringLiteral("no")});
        // location keeps when it was last used
        QCOMPARE(AppPermissions::storeValue(QStringLiteral("location"), false,
                                            {QStringLiteral("EXACT"), QStringLiteral("1761900000")}),
                 (QStringList{QStringLiteral("NONE"), QStringLiteral("1761900000")}));
        QCOMPARE(AppPermissions::storeValue(QStringLiteral("location"), true, {}),
                 (QStringList{QStringLiteral("EXACT"), QStringLiteral("0")}));

        QVERIFY(AppPermissions::storeAllows(QStringLiteral("camera"), {QStringLiteral("yes")}));
        QVERIFY(!AppPermissions::storeAllows(QStringLiteral("camera"), {QStringLiteral("no")}));
        QVERIFY(!AppPermissions::storeAllows(QStringLiteral("background"), {QStringLiteral("ask")}));
        QVERIFY(!AppPermissions::storeAllows(QStringLiteral("camera"), {}));
        QVERIFY(AppPermissions::storeAllows(QStringLiteral("location"), {QStringLiteral("CITY"), QStringLiteral("0")}));
        QVERIFY(!AppPermissions::storeAllows(QStringLiteral("location"), {QStringLiteral("NONE"), QStringLiteral("0")}));
    }

    // --- one permission, read and written through the store --------------

    void listsTheAppsThatAsked()
    {
        m_store->entries[QStringLiteral("devices/camera")] = {
            {QStringLiteral("org.raye.Paint"), {QStringLiteral("yes")}},
            {QStringLiteral("com.example.Chat"), {QStringLiteral("no")}},
            {QStringLiteral("org.unknown.App"), {QStringLiteral("yes")}},
            // an app outside the sandbox: not limited, not listed
            {QString(), {QStringLiteral("yes")}},
        };
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("camera")));
        QVERIFY(model.available());
        QCOMPARE(model.count(), 3);
        // sorted by the name people see; no .desktop file: the id
        QCOMPARE(ids(model), (QStringList{QStringLiteral("com.example.Chat"), QStringLiteral("org.unknown.App"),
                                          QStringLiteral("org.raye.Paint")}));
        QCOMPARE(model.data(model.index(0), AppPermissions::NameRole).toString(), QStringLiteral("Chat"));
        QCOMPARE(model.data(model.index(1), AppPermissions::NameRole).toString(), QStringLiteral("org.unknown.App"));
        QCOMPARE(model.data(model.index(2), AppPermissions::NameRole).toString(), QStringLiteral("Paint"));
        QCOMPARE(model.data(model.index(2), AppPermissions::IconRole).toString(), QStringLiteral("org.raye.Paint"));
        QVERIFY(!model.isAllowed(QStringLiteral("com.example.Chat")));
        QVERIFY(model.isAllowed(QStringLiteral("org.raye.Paint")));
    }

    void writesOneApp()
    {
        m_store->entries[QStringLiteral("devices/microphone")] = {{QStringLiteral("com.example.Chat"), {QStringLiteral("no")}}};
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("microphone")));
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QVERIFY(model.setAllowed(QStringLiteral("com.example.Chat"), true));
        QCOMPARE(m_store->entries.value(QStringLiteral("devices/microphone")).value(QStringLiteral("com.example.Chat")),
                 QStringList{QStringLiteral("yes")});
        QVERIFY(model.isAllowed(QStringLiteral("com.example.Chat")));
        QVERIFY(!changed.isEmpty());
        // an app the page does not show is not written
        QVERIFY(!model.setAllowed(QStringLiteral("org.not.Listed"), true));
        QVERIFY(!model.setAllowed(QStringLiteral("../../evil"), true));
        QCOMPARE(m_store->writes, 1);
    }

    void locationKeepsItsTimestamp()
    {
        m_store->entries[QStringLiteral("location/location")] = {
            {QStringLiteral("org.raye.Paint"), {QStringLiteral("EXACT"), QStringLiteral("1761900000")}}};
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("location")));
        QVERIFY(model.isAllowed(QStringLiteral("org.raye.Paint")));
        QVERIFY(model.setAllowed(QStringLiteral("org.raye.Paint"), false));
        QCOMPARE(m_store->entries.value(QStringLiteral("location/location")).value(QStringLiteral("org.raye.Paint")),
                 (QStringList{QStringLiteral("NONE"), QStringLiteral("1761900000")}));
    }

    void screenshotAndBackgroundTables()
    {
        m_store->entries[QStringLiteral("screenshot/screenshot")] = {{QStringLiteral("org.raye.Paint"), {QStringLiteral("yes")}}};
        m_store->entries[QStringLiteral("background/background")] = {{QStringLiteral("com.example.Chat"), {QStringLiteral("yes")}}};
        AppPermissions shots;
        QVERIFY(load(shots, QStringLiteral("screenshot")));
        QCOMPARE(ids(shots), QStringList{QStringLiteral("org.raye.Paint")});
        AppPermissions background;
        QVERIFY(load(background, QStringLiteral("background")));
        QCOMPARE(ids(background), QStringList{QStringLiteral("com.example.Chat")});
    }

    // nobody asked yet: the store has no entry, which is not an error
    void nothingAskedYet()
    {
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("camera")));
        QVERIFY(model.available());
        QCOMPARE(model.count(), 0);
    }

    void storeMissing()
    {
        QVERIFY(storeRegistered(false));
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("camera")));
        QVERIFY(!model.available());
        QCOMPARE(model.count(), 0);
        QVERIFY(storeRegistered(true));
    }

    // an app's prompt answered elsewhere: the page follows
    void followsTheStore()
    {
        m_store->entries[QStringLiteral("devices/camera")] = {{QStringLiteral("org.raye.Paint"), {QStringLiteral("no")}}};
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("camera")));
        QVERIFY(!model.isAllowed(QStringLiteral("org.raye.Paint")));
        QSignalSpy loaded(&model, &AppPermissions::loaded);
        storeChange(QStringLiteral("devices"), QStringLiteral("camera"), QStringLiteral("com.example.Chat"),
                    {QStringLiteral("yes")});
        QVERIFY(loaded.wait(5000));
        QCOMPARE(model.count(), 2);
        QVERIFY(model.isAllowed(QStringLiteral("com.example.Chat")));
        // another table changing is not this page's business
        loaded.clear();
        storeChange(QStringLiteral("devices"), QStringLiteral("microphone"), QStringLiteral("org.raye.Paint"),
                    {QStringLiteral("yes")});
        QVERIFY(!loaded.wait(500));
    }

    // "Let apps use your camera": off takes it from every app, on gives it
    // back to the ones that had it
    void masterSwitch()
    {
        m_store->entries[QStringLiteral("devices/camera")] = {
            {QStringLiteral("org.raye.Paint"), {QStringLiteral("yes")}},
            {QStringLiteral("com.example.Chat"), {QStringLiteral("no")}},
        };
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("camera")));
        QVERIFY(model.masterEnabled());
        QSignalSpy master(&model, &AppPermissions::masterEnabledChanged);
        model.setMasterEnabled(false);
        QCOMPARE(master.count(), 1);
        QVERIFY(!model.masterEnabled());
        const PermissionTable after = m_store->entries.value(QStringLiteral("devices/camera"));
        QCOMPARE(after.value(QStringLiteral("org.raye.Paint")), QStringList{QStringLiteral("no")});
        QCOMPARE(after.value(QStringLiteral("com.example.Chat")), QStringList{QStringLiteral("no")});
        // remembered for other pages and the permission prompt
        const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("raye-settingsrc")), QStringLiteral("Privacy"));
        QCOMPARE(group.readEntry("CameraAccess", true), false);
        // one by one, nothing comes back while it is off
        QVERIFY(!model.setAllowed(QStringLiteral("org.raye.Paint"), true));

        model.setMasterEnabled(true);
        QVERIFY(model.masterEnabled());
        const PermissionTable restored = m_store->entries.value(QStringLiteral("devices/camera"));
        QCOMPARE(restored.value(QStringLiteral("org.raye.Paint")), QStringList{QStringLiteral("yes")});
        QCOMPARE(restored.value(QStringLiteral("com.example.Chat")), QStringList{QStringLiteral("no")});
        QVERIFY(!group.hasKey("CameraRestore"));
    }

    // each permission has its own switch
    void masterSwitchesAreSeparate()
    {
        AppPermissions camera;
        QVERIFY(load(camera, QStringLiteral("camera")));
        AppPermissions microphone;
        QVERIFY(load(microphone, QStringLiteral("microphone")));
        camera.setMasterEnabled(false);
        QVERIFY(!camera.masterEnabled());
        QVERIFY(microphone.masterEnabled());
    }

    // --- File system: Flatpak apps with the whole home or computer ---------

    void broadEntries()
    {
        QCOMPARE(AppPermissions::broadFilesystems({QStringLiteral("xdg-download"), QStringLiteral("home:ro")}),
                 QStringList{QStringLiteral("home")});
        QCOMPARE(AppPermissions::broadFilesystems({QStringLiteral("host"), QStringLiteral("home"), QStringLiteral("host:rw")}),
                 (QStringList{QStringLiteral("host"), QStringLiteral("home")}));
        QCOMPARE(AppPermissions::broadFilesystems({QStringLiteral("host-os"), QStringLiteral("host-etc"),
                                                   QStringLiteral("~/Games"), QStringLiteral("!home")}),
                 QStringList());
        // taking it away keeps the rest of the override
        QCOMPARE(AppPermissions::overrideFilesystems({QStringLiteral("xdg-music"), QStringLiteral("home")},
                                                     {QStringLiteral("home")}, false),
                 (QStringList{QStringLiteral("xdg-music"), QStringLiteral("!home")}));
        QCOMPARE(AppPermissions::overrideFilesystems({QStringLiteral("xdg-music"), QStringLiteral("!home"),
                                                      QStringLiteral("!host")},
                                                     {QStringLiteral("home"), QStringLiteral("host")}, true),
                 QStringList{QStringLiteral("xdg-music")});
    }

    void filesListsBroadApps()
    {
        installApp(QStringLiteral("system"), QStringLiteral("org.raye.Paint"), "host;xdg-download;");
        installApp(QStringLiteral("system"), QStringLiteral("org.game.Small"), "xdg-download;");
        installApp(QStringLiteral("system"), QStringLiteral("org.empty.App"), QByteArray());
        installApp(QStringLiteral("user"), QStringLiteral("com.example.Chat"), "home:ro;");
        // an app whose access a system override took away already
        installApp(QStringLiteral("system"), QStringLiteral("org.locked.Down"), "home;");
        writeFile(m_dir.filePath(QStringLiteral("system/overrides/org.locked.Down")), "[Context]\nfilesystems=!home;\n");

        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("files")));
        QVERIFY(model.available());
        QCOMPARE(ids(model), (QStringList{QStringLiteral("com.example.Chat"), QStringLiteral("org.locked.Down"),
                                          QStringLiteral("org.raye.Paint")}));
        QVERIFY(model.isAllowed(QStringLiteral("org.raye.Paint")));
        QVERIFY(model.isAllowed(QStringLiteral("com.example.Chat")));
        QVERIFY(!model.isAllowed(QStringLiteral("org.locked.Down")));
    }

    void filesWritesUserOverride()
    {
        installApp(QStringLiteral("system"), QStringLiteral("org.raye.Paint"), "host;xdg-download;");
        const QString override = m_dir.filePath(QStringLiteral("user/overrides/org.raye.Paint"));
        // what the user set with flatpak override stays
        writeFile(override, "# set by hand\n[Context]\nfilesystems=xdg-music;\n\n[Environment]\nFOO=1\n");
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("files")));
        QVERIFY(model.setAllowed(QStringLiteral("org.raye.Paint"), false));
        QByteArray text = readFile(override);
        QVERIFY2(text.contains("filesystems=xdg-music;!host;"), text.constData());
        QVERIFY(text.contains("FOO=1"));
        QVERIFY(text.contains("# set by hand"));

        // read back from the disk, it says the same
        AppPermissions again;
        QVERIFY(load(again, QStringLiteral("files")));
        QVERIFY(!again.isAllowed(QStringLiteral("org.raye.Paint")));

        QVERIFY(again.setAllowed(QStringLiteral("org.raye.Paint"), true));
        text = readFile(override);
        QVERIFY2(text.contains("filesystems=xdg-music;"), text.constData());
        QVERIFY(!text.contains("!host"));
        QVERIFY(again.isAllowed(QStringLiteral("org.raye.Paint")));
        QFile::remove(override);
    }

    // giving it back over a system-wide "no" needs the entry itself
    void filesAllowsOverSystemOverride()
    {
        installApp(QStringLiteral("system"), QStringLiteral("org.locked.Down"), "home;");
        writeFile(m_dir.filePath(QStringLiteral("system/overrides/org.locked.Down")), "[Context]\nfilesystems=!home;\n");
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("files")));
        QVERIFY(model.setAllowed(QStringLiteral("org.locked.Down"), true));
        const QByteArray text = readFile(m_dir.filePath(QStringLiteral("user/overrides/org.locked.Down")));
        QVERIFY2(text.contains("filesystems=home;"), text.constData());
        AppPermissions again;
        QVERIFY(load(again, QStringLiteral("files")));
        QVERIFY(again.isAllowed(QStringLiteral("org.locked.Down")));
    }

    void filesMasterSwitch()
    {
        installApp(QStringLiteral("system"), QStringLiteral("org.raye.Paint"), "host;");
        installApp(QStringLiteral("user"), QStringLiteral("com.example.Chat"), "home;");
        QFile::remove(m_dir.filePath(QStringLiteral("user/overrides/org.raye.Paint")));
        QFile::remove(m_dir.filePath(QStringLiteral("user/overrides/com.example.Chat")));
        AppPermissions model;
        QVERIFY(load(model, QStringLiteral("files")));
        model.setMasterEnabled(false);
        QVERIFY(!model.isAllowed(QStringLiteral("org.raye.Paint")));
        QVERIFY(!model.isAllowed(QStringLiteral("com.example.Chat")));
        model.setMasterEnabled(true);
        QVERIFY(model.isAllowed(QStringLiteral("org.raye.Paint")));
        QVERIFY(model.isAllowed(QStringLiteral("com.example.Chat")));
    }

    // --- firewall ---------------------------------------------------------

    void firewallConf()
    {
        QCOMPARE(Firewall::parseConf(QStringLiteral("# /etc/ufw/ufw.conf\nENABLED=yes\nLOGLEVEL=low\n")), 1);
        QCOMPARE(Firewall::parseConf(QStringLiteral("ENABLED=no\n")), 0);
        QCOMPARE(Firewall::parseConf(QStringLiteral("ENABLED=\"yes\"\n")), 1);
        QCOMPARE(Firewall::parseConf(QStringLiteral("#ENABLED=yes\nENABLED=no\n")), 0);
        QCOMPARE(Firewall::parseConf(QStringLiteral("ENABLED_OTHER=yes\n")), 0);
        QCOMPARE(Firewall::parseConf(QString()), 0);
    }

    void firewallState()
    {
        Firewall firewall;
        firewall.setConfPath(m_dir.filePath(QStringLiteral("no-ufw.conf")));
        firewall.refresh();
        QVERIFY(!firewall.installed());
        QVERIFY(!firewall.setEnabled(true));

        writeFile(m_dir.filePath(QStringLiteral("ufw.conf")), "ENABLED=no\n");
        firewall.setConfPath(m_dir.filePath(QStringLiteral("ufw.conf")));
        firewall.refresh();
        QVERIFY(firewall.installed());
        QVERIFY(!firewall.enabled());
    }

    // the helper runs Settings' own script with a fake ufw on the PATH
    // (pkexec stands aside: the test runs the script directly)
    void firewallHelper()
    {
        const QString bin = m_dir.filePath(QStringLiteral("bin"));
        const QString conf = m_dir.filePath(QStringLiteral("ufw.conf"));
        writeFile(conf, "ENABLED=no\n");
        writeFile(bin + QStringLiteral("/ufw"),
                  QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > '%1/ufw-args'\n"
                                 "case \"$*\" in '--force enable') echo ENABLED=yes > '%2' ;; disable) echo ENABLED=no > '%2' ;; esac\n")
                      .arg(m_dir.path(), conf)
                      .toUtf8());
        QFile::setPermissions(bin + QStringLiteral("/ufw"), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QByteArray(QFile::encodeName(bin) + ':' + oldPath));

        Firewall firewall;
        firewall.setConfPath(conf);
        firewall.setHelperCommand({QStringLiteral("sh"), QStringLiteral(SOURCE_DIR "/data/libexec/firewall")});
        firewall.refresh();
        QSignalSpy finished(&firewall, &Firewall::finished);
        QVERIFY(firewall.setEnabled(true));
        QVERIFY(firewall.busy());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toBool(), true);
        QCOMPARE(readFile(m_dir.filePath(QStringLiteral("ufw-args"))), QByteArray("--force\nenable\n"));
        QVERIFY(firewall.enabled());
        QVERIFY(!firewall.failed());

        finished.clear();
        QVERIFY(firewall.setEnabled(false));
        QVERIFY(finished.wait(5000));
        QCOMPARE(readFile(m_dir.filePath(QStringLiteral("ufw-args"))), QByteArray("disable\n"));
        QVERIFY(!firewall.enabled());

        // anything else is refused by the helper, ufw is not run
        QFile::remove(m_dir.filePath(QStringLiteral("ufw-args")));
        QProcess helper;
        helper.start(QStringLiteral("sh"), {QStringLiteral(SOURCE_DIR "/data/libexec/firewall"), QStringLiteral("reset")});
        QVERIFY(helper.waitForFinished(5000));
        QCOMPARE(helper.exitCode(), 2);
        QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("ufw-args"))));
        qputenv("PATH", oldPath);
    }

    void firewallHelperFails()
    {
        writeFile(m_dir.filePath(QStringLiteral("ufw.conf")), "ENABLED=no\n");
        Firewall firewall;
        firewall.setConfPath(m_dir.filePath(QStringLiteral("ufw.conf")));
        firewall.refresh();
        // the password prompt closed: not a failure to report
        firewall.setHelperCommand({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("exit 126"), QStringLiteral("x")});
        QSignalSpy finished(&firewall, &Firewall::finished);
        QVERIFY(firewall.setEnabled(true));
        QVERIFY(finished.wait(5000));
        QVERIFY(!firewall.failed());
        QVERIFY(!firewall.enabled());
        firewall.setHelperCommand({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("exit 1"), QStringLiteral("x")});
        finished.clear();
        QVERIFY(firewall.setEnabled(true));
        QVERIFY(finished.wait(5000));
        QVERIFY(firewall.failed());
    }

    // --- file index, activity history, usage statistics --------------------

    void fileIndexing()
    {
        FileIndexing indexing;
        indexing.setControlCommand({});
        QVERIFY(indexing.enabled());
        QSignalSpy changed(&indexing, &FileIndexing::changed);
        indexing.setEnabled(false);
        QVERIFY(!indexing.enabled());
        QCOMPARE(changed.count(), 1);
        KSharedConfig::Ptr file = KSharedConfig::openConfig(QStringLiteral("baloofilerc"), KConfig::NoGlobals);
        file->reparseConfiguration();
        QCOMPARE(file->group(QStringLiteral("Basic Settings")).readEntry("Indexing-Enabled", true), false);
        indexing.setEnabled(true);
        QVERIFY(indexing.enabled());

        const QString privateFolder = QDir::homePath() + QStringLiteral("/Private");
        QVERIFY(indexing.addExcludedFolder(privateFolder + QStringLiteral("/")));
        QVERIFY(!indexing.addExcludedFolder(privateFolder));
        QVERIFY(!indexing.addExcludedFolder(QStringLiteral("relative/folder")));
        QVERIFY(!indexing.addExcludedFolder(QString()));
        QCOMPARE(indexing.excludedFolders(), QStringList{privateFolder});
        QCOMPARE(indexing.displayPath(privateFolder), QStringLiteral("~/Private"));
        QCOMPARE(indexing.displayPath(QStringLiteral("/srv/data")), QStringLiteral("/srv/data"));
        file->reparseConfiguration();
        // the indexer reads the list with $HOME in it
        QVERIFY(file->group(QStringLiteral("General")).readPathEntry("exclude folders", QStringList()).contains(privateFolder));
        QVERIFY(indexing.removeExcludedFolder(privateFolder));
        QVERIFY(!indexing.removeExcludedFolder(privateFolder));
        QVERIFY(indexing.excludedFolders().isEmpty());
    }

    void activityHistory()
    {
        ActivityHistory history;
        QVERIFY(history.remember());
        history.setRemember(false);
        QVERIFY(!history.remember());
        KSharedConfig::Ptr activities =
            KSharedConfig::openConfig(QStringLiteral("kactivitymanagerd-pluginsrc"), KConfig::NoGlobals);
        activities->reparseConfiguration();
        QCOMPARE(activities->group(QStringLiteral("Plugin-org.kde.ActivityManager.Resources.Scoring"))
                     .readEntry("what-to-remember", 0),
                 2);
        KSharedConfig::Ptr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        globals->reparseConfiguration();
        QCOMPARE(globals->group(QStringLiteral("RecentDocuments")).readEntry("UseRecent", true), false);

        history.setRemember(true);
        QVERIFY(history.remember());
        activities->reparseConfiguration();
        globals->reparseConfiguration();
        QCOMPARE(activities->group(QStringLiteral("Plugin-org.kde.ActivityManager.Resources.Scoring"))
                     .readEntry("what-to-remember", 2),
                 0);
        QCOMPARE(globals->group(QStringLiteral("RecentDocuments")).readEntry("UseRecent", false), true);

        QSignalSpy cleared(&history, &ActivityHistory::cleared);
        history.clear();
        QCOMPARE(cleared.count(), 1);
    }

    void usageStatistics()
    {
        UsageStatistics statistics;
        // off unless turned on
        QVERIFY(!statistics.enabled());
        statistics.setEnabled(true);
        QVERIFY(statistics.enabled());
        KSharedConfig::Ptr file = KSharedConfig::openConfig(QStringLiteral("PlasmaUserFeedback"), KConfig::NoGlobals);
        file->reparseConfiguration();
        QCOMPARE(file->group(QStringLiteral("Global")).readEntry("FeedbackLevel", 0), UsageStatistics::levelBasicUsage);
        statistics.setEnabled(false);
        QVERIFY(!statistics.enabled());
        file->reparseConfiguration();
        QCOMPARE(file->group(QStringLiteral("Global")).readEntry("FeedbackLevel", -1), 0);
    }
};

QTEST_GUILESS_MAIN(TestPermissions)
#include "test_permissions.moc"
