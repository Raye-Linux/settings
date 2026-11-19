// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout.h"
#include "router.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QByteArray smallLayout = R"({
  "version": 2,
  "home": {"aliases": ["kcm_landingpage"]},
  "tiles": [{"target": "system"}, {"target": "devices/touchpad", "summary": "Taps"}],
  "categories": [
    {"id": "system", "name": "System", "name_fr": "Système", "icon": "org.raye.settings-system",
     "icon_fallback": "preferences-system", "summary": "Display and sound",
     "landing": "pages/system/SystemPage.qml",
     "pages": [
       {"id": "system/display", "name": "Display", "name_fr": "Écran", "icon": "i",
        "summary": "Monitors", "summary_fr": "Moniteurs",
        "component": "pages/system/DisplayPage.qml", "aliases": ["kcm_kscreen"]},
       {"id": "system/recovery", "name": "Recovery", "icon": "i",
        "component": "pages/system/RecoveryPage.qml"},
       {"id": "system/about", "name": "About", "icon": "i", "kcm": "kcm_about-distro"}
     ]},
    {"id": "devices", "name": "Devices", "icon": "i",
     "pages": [
       {"id": "devices/touchpad", "name": "Touchpad", "icon": "i",
        "component": "pages/devices/TouchpadPage.qml", "requires": "touchpad"},
       {"id": "devices/mouse", "name": "Mouse", "icon": "i",
        "component": "pages/devices/MousePage.qml", "aliases": ["kcm_mouse"]}
     ]}
  ]
})";

// A firmware boot entry as efivarfs shows it: attributes, load option
// attributes, path length, then the name in UTF-16.
QByteArray bootEntry(const char16_t *name)
{
    QByteArray bytes(10, '\0');
    const QString text = QString::fromUtf16(name);
    bytes.append(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
    bytes.append(2, '\0');
    return bytes;
}

Router *router()
{
    auto *r = new Router;
    r->setLayout(parseLayout(smallLayout, QStringLiteral("en")));
    // the tests say what the computer has, so they do not depend on it
    r->requirements().set(QStringLiteral("touchpad"), true);
    return r;
}

void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(text);
}

// The relays that stand in for kcmshell6 and systemsettings, run as
// shipped, with a stand-in for each program they start. Each program
// writes "<who>|<arg>|<arg>|" on a line of the log.
class Relays
{
public:
    Relays()
    {
        const QString bin = QStringLiteral(BUILD_DIR "/bin/raye-settings");
        m_real = QFileInfo::exists(bin) ? bin : QStringLiteral(BUILD_DIR "/raye-settings");
        QDir root(m_dir.path());
        root.mkpath(QStringLiteral("bin"));
        root.mkpath(QStringLiteral("data/raye-settings"));
        QFile::copy(QStringLiteral(LAYOUT_FILE), root.filePath(QStringLiteral("data/raye-settings/layout.json")));
        script(QStringLiteral("bin/raye-settings"),
               "if [ \"$1\" = --knows ]; then exec \"$RAYE_TEST_BINARY\" \"$@\"; fi\n"
               "printf 'raye|' >> \"$RAYE_TEST_LOG\"\n");
        script(QStringLiteral("kcmshell6.kde"), "printf 'kcmshell|' >> \"$RAYE_TEST_LOG\"\n");
        script(QStringLiteral("systemsettings.kde"), "printf 'systemsettings|' >> \"$RAYE_TEST_LOG\"\n");
    }

    bool ready() const
    {
        return m_dir.isValid() && QFileInfo::exists(m_real)
            && QFileInfo(m_dir.filePath(QStringLiteral("bin/raye-settings"))).isExecutable();
    }

    // runs data/<relay> with these arguments, returns what got started
    QString run(const QString &relay, const QStringList &arguments, const QString &passThrough = {},
                const QString &as = {})
    {
        QFile::remove(m_dir.filePath(QStringLiteral("log")));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), m_dir.filePath(QStringLiteral("bin")) + QStringLiteral(":/usr/bin:/bin"));
        env.insert(QStringLiteral("XDG_DATA_HOME"), m_dir.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("XDG_DATA_DIRS"), m_dir.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("RAYE_SETTINGS_LAYOUT"), m_dir.filePath(QStringLiteral("data/raye-settings/layout.json")));
        env.insert(QStringLiteral("RAYE_TEST_BINARY"), m_real);
        env.insert(QStringLiteral("RAYE_TEST_LOG"), m_dir.filePath(QStringLiteral("log")));
        env.insert(QStringLiteral("RAYE_SETTINGS_KCMSHELL_REAL"), m_dir.filePath(QStringLiteral("kcmshell6.kde")));
        env.insert(QStringLiteral("RAYE_SETTINGS_SYSTEMSETTINGS_REAL"), m_dir.filePath(QStringLiteral("systemsettings.kde")));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        if (!as.isEmpty())
            env.insert(QStringLiteral("RAYE_SETTINGS_AS"), as);
        if (passThrough.isEmpty())
            env.remove(QStringLiteral("RAYE_SETTINGS_KCMSHELL"));
        else
            env.insert(QStringLiteral("RAYE_SETTINGS_KCMSHELL"), passThrough);
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(QStringLiteral("sh"), QStringList{QStringLiteral(SOURCE_DIR "/data/") + relay} + arguments);
        p.waitForFinished(20000);
        QFile log(m_dir.filePath(QStringLiteral("log")));
        if (!log.open(QIODevice::ReadOnly))
            return QStringLiteral("(nothing started)");
        return QString::fromUtf8(log.readAll());
    }

private:
    void script(const QString &name, const QByteArray &firstLines)
    {
        QFile f(m_dir.filePath(name));
        if (!f.open(QIODevice::WriteOnly))
            return; // ready() is false then: the test says so
        f.write("#!/bin/sh\n" + firstLines + "for a in \"$@\"; do printf '%s|' \"$a\" >> \"$RAYE_TEST_LOG\"; done\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    }

    QTemporaryDir m_dir;
    QString m_real;
};

// The maintainer scripts, run as shipped, with stand-ins for every
// program they start: nothing on this computer is moved. Each call is a
// line "<program> <arguments>" of what comes back.
class MaintainerScripts
{
public:
    MaintainerScripts()
    {
        QDir(m_dir.path()).mkpath(QStringLiteral("bin"));
        // what `dpkg-divert --listpackage` answers, from RAYE_TEST_LISTED
        tool(QStringLiteral("dpkg-divert"),
             "[ \"$1\" = --listpackage ] && {\n"
             "    echo \"dpkg-divert $*\" >> \"$RAYE_TEST_LOG\"; printf '%s\\n' \"$RAYE_TEST_LISTED\"; exit 0; }\n");
        // only the version sums are real
        tool(QStringLiteral("dpkg"), "[ \"$1\" = --compare-versions ] && exec /usr/bin/dpkg \"$@\"\n");
        tool(QStringLiteral("mkdir"), "");
        tool(QStringLiteral("rmdir"), "");
    }

    bool ready() const { return m_dir.isValid() && QFileInfo(QStringLiteral("/usr/bin/dpkg")).isExecutable(); }

    QStringList run(const QString &script, const QStringList &arguments, const QString &listed = {})
    {
        QFile::remove(m_dir.filePath(QStringLiteral("log")));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), m_dir.filePath(QStringLiteral("bin")) + QStringLiteral(":/usr/bin:/bin"));
        env.insert(QStringLiteral("RAYE_TEST_LOG"), m_dir.filePath(QStringLiteral("log")));
        env.insert(QStringLiteral("RAYE_TEST_LISTED"), listed);
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(QStringLiteral("sh"), QStringList{QStringLiteral(SOURCE_DIR "/debian/raye-settings.") + script} + arguments);
        p.waitForFinished(20000);
        m_exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
        QFile log(m_dir.filePath(QStringLiteral("log")));
        if (!log.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    int exitCode() const { return m_exitCode; }

private:
    void tool(const QString &name, const QByteArray &firstLines)
    {
        QFile f(m_dir.filePath(QStringLiteral("bin/") + name));
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write("#!/bin/sh\n" + firstLines + "echo \"" + name.toUtf8() + " $*\" >> \"$RAYE_TEST_LOG\"\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    }

    QTemporaryDir m_dir;
    int m_exitCode = -1;
};

const QString addSystemsettings = QStringLiteral(
    "dpkg-divert --package raye-settings --add --rename --divert /usr/bin/systemsettings.kde /usr/bin/systemsettings");
const QString addKcmshell = QStringLiteral(
    "dpkg-divert --package raye-settings --add --rename --divert /usr/bin/kcmshell6.kde /usr/bin/kcmshell6");
const QString addEntry = QStringLiteral(
    "dpkg-divert --package raye-settings --add --rename --divert /usr/share/raye-settings/kde/systemsettings.desktop "
    "/usr/share/applications/systemsettings.desktop");

QString removing(QString add)
{
    return add.replace(QStringLiteral(" --add "), QStringLiteral(" --remove "));
}

} // namespace

class TestRouter : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void startsAtHome()
    {
        std::unique_ptr<Router> r(router());
        QCOMPARE(r->route(), QStringLiteral("home"));
        QVERIFY(r->atHome());
        QVERIFY(!r->canGoBack());
        QVERIFY(!r->canGoForward());
        QVERIFY(r->categoryId().isEmpty());
    }

    void opensAPageById()
    {
        std::unique_ptr<Router> r(router());
        QSignalSpy spy(r.get(), &Router::routeChanged);
        QVERIFY(r->navigate(QStringLiteral("system/display")));
        QCOMPARE(r->route(), QStringLiteral("system/display"));
        QCOMPARE(r->categoryId(), QStringLiteral("system"));
        QVERIFY(!r->atHome());
        QCOMPARE(spy.count(), 1);
    }

    void opensACategory()
    {
        std::unique_ptr<Router> r(router());
        QVERIFY(r->navigate(QStringLiteral("devices")));
        QCOMPARE(r->route(), QStringLiteral("devices"));
        QCOMPARE(r->categoryId(), QStringLiteral("devices"));
    }

    // Plasma's "Configure display settings..." hands us a module id
    void opensAModuleIdThroughItsAlias()
    {
        std::unique_ptr<Router> r(router());
        QVERIFY(r->navigate(QStringLiteral("kcm_kscreen")));
        QCOMPARE(r->route(), QStringLiteral("system/display"));
        QVERIFY(r->navigate(QStringLiteral("kcm_about-distro")));
        QCOMPARE(r->route(), QStringLiteral("system/about"));
        // KRunner hands over the whole file name
        QVERIFY(r->navigate(QStringLiteral("kcm_mouse.desktop")));
        QCOMPARE(r->route(), QStringLiteral("devices/mouse"));
    }

    void readsSettingsUrls()
    {
        std::unique_ptr<Router> r(router());
        QVERIFY(r->navigate(QStringLiteral("settings:system/display")));
        QCOMPARE(r->route(), QStringLiteral("system/display"));
        QVERIFY(r->navigate(QStringLiteral("settings://devices/mouse")));
        QCOMPARE(r->route(), QStringLiteral("devices/mouse"));
        QVERIFY(r->navigate(QStringLiteral("settings:///system/about")));
        QCOMPARE(r->route(), QStringLiteral("system/about"));
        QVERIFY(r->navigate(QStringLiteral("settings:")));
        QCOMPARE(r->route(), QStringLiteral("home"));
    }

    void keepsTheAnchor()
    {
        std::unique_ptr<Router> r(router());
        QSignalSpy anchors(r.get(), &Router::anchorRequested);
        QVERIFY(r->navigate(QStringLiteral("system/recovery#restart-windows")));
        QCOMPARE(r->route(), QStringLiteral("system/recovery"));
        QCOMPARE(r->anchor(), QStringLiteral("restart-windows"));
        QCOMPARE(anchors.count(), 1);
        QCOMPARE(anchors.first().first().toString(), QStringLiteral("restart-windows"));
        // the same link again flashes the row again, without a new step
        QVERIFY(r->navigate(QStringLiteral("system/recovery#restart-windows")));
        QCOMPARE(anchors.count(), 2);
        r->back();
        QCOMPARE(r->route(), QStringLiteral("home"));
        QVERIFY(!r->canGoBack());
    }

    void refusesWhatItDoesNotKnow()
    {
        std::unique_ptr<Router> r(router());
        QVERIFY(r->navigate(QStringLiteral("system/display")));
        QVERIFY(!r->navigate(QStringLiteral("kcm_kwinrules")));
        QVERIFY(!r->navigate(QStringLiteral("system/nothing")));
        // and stays where it was
        QCOMPARE(r->route(), QStringLiteral("system/display"));
    }

    void walksBackAndForward()
    {
        std::unique_ptr<Router> r(router());
        r->navigate(QStringLiteral("system/display"));
        r->navigate(QStringLiteral("devices/mouse"));
        QVERIFY(r->canGoBack());
        QVERIFY(!r->canGoForward());
        r->back();
        QCOMPARE(r->route(), QStringLiteral("system/display"));
        QVERIFY(r->canGoForward());
        r->back();
        QCOMPARE(r->route(), QStringLiteral("home"));
        r->forward();
        QCOMPARE(r->route(), QStringLiteral("system/display"));
        r->forward();
        QCOMPARE(r->route(), QStringLiteral("devices/mouse"));
        r->forward(); // nothing left to do
        QCOMPARE(r->route(), QStringLiteral("devices/mouse"));
    }

    void anewPathDropsWhatWasAhead()
    {
        std::unique_ptr<Router> r(router());
        r->navigate(QStringLiteral("system/display"));
        r->navigate(QStringLiteral("devices/mouse"));
        r->back();
        r->navigate(QStringLiteral("system/about"));
        QVERIFY(!r->canGoForward());
        r->back();
        QCOMPARE(r->route(), QStringLiteral("system/display"));
    }

    void doesNotStackTheSamePageTwice()
    {
        std::unique_ptr<Router> r(router());
        r->navigate(QStringLiteral("system/display"));
        r->navigate(QStringLiteral("system/display"));
        r->back();
        QCOMPARE(r->route(), QStringLiteral("home"));
    }

    // a page for hardware this computer has not got is not shown, and a
    // link to it lands on its category instead of nowhere
    void hidesPagesTheComputerHasNoHardwareFor()
    {
        std::unique_ptr<Router> r(router());
        QVERIFY(r->isAvailable(QStringLiteral("devices/touchpad")));
        r->requirements().set(QStringLiteral("touchpad"), false);
        QVERIFY(!r->isAvailable(QStringLiteral("devices/touchpad")));
        QVERIFY(r->isAvailable(QStringLiteral("devices/mouse")));
        QVERIFY(r->navigate(QStringLiteral("devices/touchpad")));
        QCOMPARE(r->route(), QStringLiteral("devices"));
    }

    void tellsWhichFileToLoad()
    {
        std::unique_ptr<Router> r(router());
        QCOMPARE(r->componentOf(QStringLiteral("system/display")),
                 QStringLiteral("pages/system/DisplayPage.qml"));
        QVERIFY(r->componentOf(QStringLiteral("system/about")).isEmpty()); // a hosted module
        QVERIFY(r->componentOf(QStringLiteral("nothing")).isEmpty());
    }

    // a page named in the layout but not built yet shows the
    // placeholder instead of an empty frame
    void fallsBackToThePlaceholder()
    {
        std::unique_ptr<Router> r(router());
        r->setComponentCheck([](const QString &component) {
            return component == QLatin1String("pages/system/DisplayPage.qml");
        });
        QCOMPARE(r->componentUrlOf(QStringLiteral("system/display")),
                 QStringLiteral("pages/system/DisplayPage.qml"));
        QCOMPARE(r->componentUrlOf(QStringLiteral("system/recovery")),
                 QStringLiteral("PlaceholderPage.qml"));
        // a page that hosts a settings module has no file of its own
        QVERIFY(r->componentUrlOf(QStringLiteral("system/about")).isEmpty());
    }

    void readsTheLayoutOfATile()
    {
        const Layout layout = parseLayout(smallLayout, QStringLiteral("fr"));
        QCOMPARE(layout.tiles.size(), 2);
        QCOMPARE(layout.tiles.at(0).name, QStringLiteral("Système"));
        QCOMPARE(layout.tiles.at(0).icon, QStringLiteral("org.raye.settings-system"));
        QCOMPARE(layout.tiles.at(0).iconFallback, QStringLiteral("preferences-system"));
        // the tile may say its own words
        QCOMPARE(layout.tiles.at(1).name, QStringLiteral("Touchpad"));
        QCOMPARE(layout.tiles.at(1).summary, QStringLiteral("Taps"));
    }

    // the probes read the computer through a root we can build
    void probesALaptop()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        writeFile(root + QStringLiteral("/sys/class/bluetooth/hci0/address"), "00:11\n");
        writeFile(root + QStringLiteral("/sys/class/power_supply/BAT0/type"), "Battery\n");
        writeFile(root + QStringLiteral("/sys/class/power_supply/BAT0/scope"), "System\n");
        writeFile(root + QStringLiteral("/sys/class/input/input5/name"), "SynPS/2 Synaptics TouchPad\n");
        // an Intel and an NVIDIA card, both display controllers
        writeFile(root + QStringLiteral("/sys/class/drm/card0/device/class"), "0x030000\n");
        writeFile(root + QStringLiteral("/sys/class/drm/card1/device/class"), "0x030200\n");
        writeFile(root + QStringLiteral("/sys/firmware/efi/efivars/Boot0001-8be4df61-93ca-11d0-a2fe-00aa00004b98"),
                  bootEntry(u"Windows Boot Manager"));
        writeFile(root + QStringLiteral("/proc/self/mountinfo"),
                  "25 1 0:22 / / rw,relatime shared:1 - btrfs /dev/sda2 rw,subvol=/@\n");
        writeFile(root + QStringLiteral("/usr/share/dbus-1/system-services/net.reactivated.Fprint.service"),
                  "[D-BUS Service]\n");
        writeFile(root + QStringLiteral("/usr/lib/udev/hwdb.d/60-autosuspend-fingerprint-reader.hwdb"),
                  "# Synaptics\nusb:v06CBp00BD*\n ID_AUTOSUSPEND=1\n");
        writeFile(root + QStringLiteral("/sys/bus/usb/devices/1-9/idVendor"), "06cb\n");
        writeFile(root + QStringLiteral("/sys/bus/usb/devices/1-9/idProduct"), "00bd\n");
        writeFile(root + QStringLiteral("/sys/class/net/wlp2s0/wireless/.present"), "");

        Requirements req(root);
        QVERIFY(req.has(QString()));          // no requirement: always there
        QVERIFY(req.has(QStringLiteral("bluetooth")));
        QVERIFY(req.has(QStringLiteral("wifi")));
        QVERIFY(req.has(QStringLiteral("battery")));
        QVERIFY(req.has(QStringLiteral("touchpad")));
        QVERIFY(req.has(QStringLiteral("two-gpus")));
        QVERIFY(req.has(QStringLiteral("windows")));
        QVERIFY(req.has(QStringLiteral("btrfs")));
        QVERIFY(req.has(QStringLiteral("fingerprint")));
        QVERIFY(!req.has(QStringLiteral("tablet")));
        QVERIFY(!req.has(QStringLiteral("something-else"))); // unknown: not shown
    }

    void probesAPlainDesktop()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        writeFile(root + QStringLiteral("/sys/class/input/input1/name"), "AT Translated Set 2 keyboard\n");
        writeFile(root + QStringLiteral("/sys/class/drm/card0/device/class"), "0x030000\n");
        // the firmware framebuffer is a card too, not a second GPU
        writeFile(root + QStringLiteral("/sys/class/drm/card1/dev"), "226:1\n");
        // a boot entry of this system only
        writeFile(root + QStringLiteral("/sys/firmware/efi/efivars/Boot0000-8be4df61-93ca-11d0-a2fe-00aa00004b98"),
                  bootEntry(u"Raye Linux"));
        // fprintd is there, but no reader it knows
        writeFile(root + QStringLiteral("/usr/share/dbus-1/system-services/net.reactivated.Fprint.service"),
                  "[D-BUS Service]\n");
        writeFile(root + QStringLiteral("/usr/lib/udev/hwdb.d/60-autosuspend-fingerprint-reader.hwdb"),
                  "usb:v06CBp00BD*\n ID_AUTOSUSPEND=1\n");
        writeFile(root + QStringLiteral("/sys/bus/usb/devices/1-2/idVendor"), "046d\n");
        writeFile(root + QStringLiteral("/sys/bus/usb/devices/1-2/idProduct"), "c52b\n");
        writeFile(root + QStringLiteral("/sys/class/input/input2/name"), "OpenRGB Virtual Device\n");
        // a wireless mouse reports a battery of its own: not this PC's
        writeFile(root + QStringLiteral("/sys/class/power_supply/hidpp_battery_0/type"), "Battery\n");
        writeFile(root + QStringLiteral("/sys/class/power_supply/hidpp_battery_0/scope"), "Device\n");
        writeFile(root + QStringLiteral("/proc/self/mountinfo"),
                  "25 1 0:22 / / rw,relatime shared:1 - ext4 /dev/sda2 rw\n");
        // a wired card only
        writeFile(root + QStringLiteral("/sys/class/net/enp3s0/address"), "00:11:22:33:44:55\n");
        writeFile(root + QStringLiteral("/sys/class/net/lo/address"), "00:00:00:00:00:00\n");

        Requirements req(root);
        QVERIFY(!req.has(QStringLiteral("bluetooth")));
        QVERIFY(!req.has(QStringLiteral("wifi")));
        QVERIFY(!req.has(QStringLiteral("battery")));
        QVERIFY(!req.has(QStringLiteral("touchpad")));
        QVERIFY(!req.has(QStringLiteral("two-gpus")));
        QVERIFY(!req.has(QStringLiteral("windows")));
        QVERIFY(!req.has(QStringLiteral("btrfs")));
        QVERIFY(!req.has(QStringLiteral("fingerprint")));
        QVERIFY(!req.has(QStringLiteral("tablet")));
    }

    // the NVIDIA Control Panel link: the panel installed and the NVIDIA
    // driver loaded, not one without the other
    void probesTheNvidiaPanel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        QVERIFY(!Requirements(root).has(QStringLiteral("nvidia-panel")));
        writeFile(root + QStringLiteral("/usr/share/applications/nvidia-settings.desktop"), "[Desktop Entry]\n");
        QVERIFY(!Requirements(root).has(QStringLiteral("nvidia-panel")));
        writeFile(root + QStringLiteral("/sys/module/nvidia/version"), "580.95.05\n");
        QVERIFY(Requirements(root).has(QStringLiteral("nvidia-panel")));
        QFile::remove(root + QStringLiteral("/usr/share/applications/nvidia-settings.desktop"));
        QVERIFY(!Requirements(root).has(QStringLiteral("nvidia-panel")));
    }

    void probesTheAccountPreview()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        qunsetenv("RAYE_ACCOUNT_PREVIEW");
        QVERIFY(!Requirements(root).has(QStringLiteral("raye-account")));
        // the switch alone is not enough: the pages need the module
        writeFile(root + QStringLiteral("/etc/raye/account-preview"), "");
        const QByteArray imports = qgetenv("QML_IMPORT_PATH");
        qputenv("QML_IMPORT_PATH", QByteArray(root.toUtf8() + "/qml"));
        QVERIFY(!Requirements(root).has(QStringLiteral("raye-account")));
        writeFile(root + QStringLiteral("/qml/org/raye/account/qmldir"), "module org.raye.account\n");
        QVERIFY(Requirements(root).has(QStringLiteral("raye-account")));
        qputenv("QML_IMPORT_PATH", imports);
    }

    void emailAccountsNeedAProvider()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        QVERIFY(!Requirements(root).has(QStringLiteral("online-accounts")));
        writeFile(root + QStringLiteral("/usr/share/accounts/providers/README"), "");
        QVERIFY(!Requirements(root).has(QStringLiteral("online-accounts")));
        writeFile(root + QStringLiteral("/usr/share/accounts/providers/nextcloud.provider"), "<provider/>");
        QVERIFY(Requirements(root).has(QStringLiteral("online-accounts")));

        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(f.readAll(), QStringLiteral("en"));
        QCOMPARE(layout.page(QStringLiteral("accounts/email"))->requirement, QStringLiteral("online-accounts"));
    }

    void probesATablet()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        writeFile(root + QStringLiteral("/sys/class/input/input9/name"), "Wacom Intuos S Pen\n");
        Requirements req(root);
        QVERIFY(req.has(QStringLiteral("tablet")));
    }

    // a Windows that only the EFI partition knows about still counts
    void findsWindowsOnTheEfiPartition()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.path() + QStringLiteral("/boot/efi/EFI/Microsoft/Boot/bootmgfw.efi"), "MZ");
        Requirements req(dir.path());
        QVERIFY(req.has(QStringLiteral("windows")));
        // and a test can say what the computer has
        req.set(QStringLiteral("windows"), false);
        QVERIFY(!req.has(QStringLiteral("windows")));
        req.forget();
        QVERIFY(req.has(QStringLiteral("windows")));
    }

    // the launch screen answers for KDE's own landing page, and the
    // first Raye pages were linked as raye:<page>
    void readsOldAndKdeNamesForHome()
    {
        std::unique_ptr<Router> r(router());
        QVERIFY(r->navigate(QStringLiteral("system/display")));
        QVERIFY(r->navigate(QStringLiteral("kcm_landingpage")));
        QVERIFY(r->atHome());
        QVERIFY(r->navigate(QStringLiteral("raye:system/recovery")));
        QCOMPARE(r->route(), QStringLiteral("system/recovery"));
        QCOMPARE(r->resolve(QStringLiteral("raye:nothing")), QString());
    }

    // QML gets the router main() built, not one of its own
    void qmlSharesTheRouter()
    {
        std::unique_ptr<Router> r(router());
        QCOMPARE(Router::instance(), r.get());
        QCOMPARE(Router::create(nullptr, nullptr), r.get());
    }

    // no Wi-Fi card: no Wi-Fi page, no known networks, no hotspot
    void wifiPagesNeedAWifiCard()
    {
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Router r;
        r.setLayout(parseLayout(f.readAll(), QStringLiteral("en")));
        r.requirements().set(QStringLiteral("wifi"), false);
        for (const QString &page : {QStringLiteral("network/wifi"), QStringLiteral("network/known-networks"),
                                    QStringLiteral("network/hotspot")}) {
            QVERIFY2(!r.isAvailable(page), qPrintable(page));
            r.navigate(page);
            QCOMPARE(r.route(), QStringLiteral("network"));
        }
        QVERIFY(r.isAvailable(QStringLiteral("network/ethernet")));
        r.requirements().set(QStringLiteral("wifi"), true);
        QVERIFY(r.isAvailable(QStringLiteral("network/wifi")));
    }

    void airplaneModeNeedsARadio()
    {
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Router r;
        r.setLayout(parseLayout(f.readAll(), QStringLiteral("en")));
        r.requirements().set(QStringLiteral("wifi"), false);
        r.requirements().set(QStringLiteral("bluetooth"), false);
        QVERIFY(!r.isAvailable(QStringLiteral("network/airplane-mode")));
        r.navigate(QStringLiteral("network/airplane-mode"));
        QCOMPARE(r.route(), QStringLiteral("network"));
        r.requirements().set(QStringLiteral("bluetooth"), true);
        QVERIFY(r.isAvailable(QStringLiteral("network/airplane-mode")));
        r.requirements().set(QStringLiteral("bluetooth"), false);
        r.requirements().set(QStringLiteral("wifi"), true);
        QVERIFY(r.isAvailable(QStringLiteral("network/airplane-mode")));
    }

    void requirementChangesAreAnnounced()
    {
        Router r;
        r.requirements().set(QStringLiteral("wifi"), false);
        QSignalSpy spy(&r, &Router::requirementsChanged);
        r.setRequirement(QStringLiteral("wifi"), false);
        QCOMPARE(spy.count(), 0);
        r.setRequirement(QStringLiteral("wifi"), true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(r.has(QStringLiteral("wifi")));
        QVERIFY(r.has(QStringLiteral("wifi|bluetooth")));
    }

    // every page of the layout we ship can be reached by its id, and
    // every alias lands somewhere
    void shippedLayoutRoutes()
    {
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Router r;
        const Layout layout = parseLayout(f.readAll(), QStringLiteral("en"));
        r.setLayout(layout);
        for (const QString &name : {QStringLiteral("bluetooth"), QStringLiteral("touchpad"),
                                    QStringLiteral("tablet"), QStringLiteral("two-gpus"),
                                    QStringLiteral("windows"), QStringLiteral("btrfs"),
                                    QStringLiteral("raye-account"), QStringLiteral("wifi"),
                                    QStringLiteral("online-accounts")})
            r.requirements().set(name, true);
        for (const Category &c : layout.categories) {
            QVERIFY2(r.navigate(c.id), qPrintable(c.id));
            QCOMPARE(r.route(), c.id);
            for (const Page &p : c.pages) {
                QVERIFY2(r.navigate(p.id), qPrintable(p.id));
                QCOMPARE(r.route(), p.id);
                for (const QString &alias : p.aliases) {
                    QVERIFY2(r.navigate(alias), qPrintable(alias));
                    QCOMPARE(r.route(), p.id);
                }
            }
        }
        // a module Raye has no page for is left to the real one
        QVERIFY(!r.navigate(QStringLiteral("kcm_kwinrules")));
    }

    // kcmshell6 is ours: a module with a Raye page opens it, any other
    // is handed to KDE's command untouched, as KCMUtils and Plasma call
    // it
    void kcmshellRelay()
    {
        Relays relays;
        QVERIFY(relays.ready());
        const QString kcmshell = QStringLiteral("kcmshell6");
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("kcm_kscreen")}), QStringLiteral("raye|--page|kcm_kscreen|"));
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("kcm_kwinrules")}), QStringLiteral("kcmshell|kcm_kwinrules|"));
        // what the caller wants the module opened on rides as the anchor
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("--args"), QStringLiteral("output"), QStringLiteral("kcm_kscreen")}),
                 QStringLiteral("raye|--page|kcm_kscreen#output|"));
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("--args=output"), QStringLiteral("kcm_pulseaudio")}),
                 QStringLiteral("raye|--page|kcm_pulseaudio#output|"));
        // KCMLauncher.openSystemSettings() falls back to kcmshell6 when
        // systemsettings.desktop is gone (it is, here): module first,
        // then --args with the words joined by a space
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("kcm_kscreen"), QStringLiteral("--args"), QStringLiteral("a b")}),
                 QStringLiteral("raye|--page|kcm_kscreen#a b|"));
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("kcm_kwinrules"), QStringLiteral("--args"), QStringLiteral("uuid")}),
                 QStringLiteral("kcmshell|kcm_kwinrules|--args|uuid|"));
        // KCMLauncher.open() with several modules: the first one decides
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("kcm_kwinrules"), QStringLiteral("kcm_kscreen")}),
                 QStringLiteral("kcmshell|kcm_kwinrules|kcm_kscreen|"));
        // the value of an option is not a module, even when it looks like one
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("--caption"), QStringLiteral("kcm_kscreen"),
                                       QStringLiteral("--icon"), QStringLiteral("video-display"),
                                       QStringLiteral("kcm_kwinrules")}),
                 QStringLiteral("kcmshell|--caption|kcm_kscreen|--icon|video-display|kcm_kwinrules|"));
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("--list")}), QStringLiteral("kcmshell|--list|"));
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("--help")}), QStringLiteral("kcmshell|--help|"));
        // started by Raye Settings itself: never back to the window
        QCOMPARE(relays.run(kcmshell, {QStringLiteral("kcm_kscreen")}, QStringLiteral("pass-through")),
                 QStringLiteral("kcmshell|kcm_kscreen|"));
    }

    // systemsettings is ours too, and KDE's window never shows for a
    // module: one without a Raye page opens on its own
    void systemsettingsRelay()
    {
        Relays relays;
        QVERIFY(relays.ready());
        const QString settings = QStringLiteral("systemsettings");
        QCOMPARE(relays.run(settings, {}), QStringLiteral("raye|"));
        QCOMPARE(relays.run(settings, {QStringLiteral("kcm_kscreen")}), QStringLiteral("raye|--page|kcm_kscreen|"));
        QCOMPARE(relays.run(settings, {QStringLiteral("kcm_mouse.desktop")}),
                 QStringLiteral("raye|--page|kcm_mouse.desktop|"));
        QCOMPARE(relays.run(settings, {QStringLiteral("kcm_kwinrules")}), QStringLiteral("kcmshell|kcm_kwinrules|"));
        QCOMPARE(relays.run(settings, {QStringLiteral("--args"), QStringLiteral("x"), QStringLiteral("kcm_kwinrules")}),
                 QStringLiteral("kcmshell|--args|x|kcm_kwinrules|"));
        QCOMPARE(relays.run(settings, {QStringLiteral("--list")}), QStringLiteral("systemsettings|--list|"));
        // kinfocenter is a link to systemsettings: KDE's Info Center opens
        QCOMPARE(relays.run(settings, {}, {}, QStringLiteral("kinfocenter")), QStringLiteral("systemsettings|"));
        QCOMPARE(relays.run(settings, {QStringLiteral("kcm_memory")}, {}, QStringLiteral("kinfocenter")),
                 QStringLiteral("systemsettings|kcm_memory|"));
    }

    // today's window is handed the module that was asked for by name,
    // not the first module of its page
    void handsTheWindowTheModuleAskedFor()
    {
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Router r;
        r.setLayout(parseLayout(f.readAll(), QStringLiteral("en")));
        const QList<QPair<QString, QString>> asked = {
            // the task switcher's "Configure..." shares a page with the
            // virtual desktops and the screen edges
            {QStringLiteral("kcm_kwintabbox"), QStringLiteral("kcm_kwintabbox")},
            {QStringLiteral("kcm_kwinscreenedges"), QStringLiteral("kcm_kwinscreenedges")},
            {QStringLiteral("kcm_powerdevilactivitiesconfig"), QStringLiteral("kcm_powerdevilactivitiesconfig")},
            {QStringLiteral("kcm_style"), QStringLiteral("kcm_style")},
            {QStringLiteral("kcm_icons"), QStringLiteral("kcm_icons")},
            {QStringLiteral("kcm_fontinst"), QStringLiteral("kcm_fontinst")},
            {QStringLiteral("kcm_filetypes"), QStringLiteral("kcm_filetypes")},
            {QStringLiteral("kcm_formats"), QStringLiteral("kcm_formats")},
            {QStringLiteral("kcm_plasmasearch"), QStringLiteral("kcm_plasmasearch")},
            {QStringLiteral("kcm_recentFiles"), QStringLiteral("kcm_recentFiles")},
            {QStringLiteral("kcm_memory"), QStringLiteral("kcm_memory")},
            {QStringLiteral("kcm_pci"), QStringLiteral("kcm_pci")},
            {QStringLiteral("kcm_firewall"), QStringLiteral("kcm_firewall")},
            // the same, through the forms other apps use
            {QStringLiteral("kcm_kwintabbox.desktop"), QStringLiteral("kcm_kwintabbox")},
            {QStringLiteral("settings:kcm_kwintabbox"), QStringLiteral("kcm_kwintabbox")},
            {QStringLiteral("/kcm_kscreen#output"), QStringLiteral("kcm_kscreen")},
            // a page asked for by its id: the module it hosts
            {QStringLiteral("system/multitasking"), QStringLiteral("kcm_kwin_virtualdesktops")},
            {QStringLiteral("system/display"), QStringLiteral("kcm_kscreen")},
            // only a phone module: that is never what a page shows, so
            // the window lands on the category
            {QStringLiteral("network/wifi"), QStringLiteral("network")},
            {QStringLiteral("settings:system/storage"), QStringLiteral("system")},
            // asked for by name, the phone module is what was wanted
            {QStringLiteral("kcm_mobile_wifi"), QStringLiteral("kcm_mobile_wifi")},
            // Raye's own pages and categories
            {QStringLiteral("raye:update"), QStringLiteral("raye:update")},
            {QStringLiteral("update"), QStringLiteral("raye:update")},
            {QStringLiteral("update/raye-update"), QStringLiteral("raye:update")},
            {QStringLiteral("privacy/device-security"), QStringLiteral("raye:security")},
            {QStringLiteral("network"), QStringLiteral("network")},
            // home is the window as it opens; the unknown goes on as it came
            {QString(), QString()},
            {QStringLiteral("kcm_landingpage"), QString()},
            {QStringLiteral("kcm_kwinrules"), QStringLiteral("kcm_kwinrules")},
        };
        for (const auto &[target, module] : asked)
            QVERIFY2(r.moduleFor(target) == module,
                     qPrintable(target + QStringLiteral(" gave ") + r.moduleFor(target)));
    }

    // a second raye-settings, or the command line: the route moves and
    // the window is told what to open
    void handsOffALink()
    {
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Router r;
        r.setLayout(parseLayout(f.readAll(), QStringLiteral("en")));
        QSignalSpy anchors(&r, &Router::anchorRequested);
        QCOMPARE(r.handOff(QStringLiteral("kcm_kwintabbox")), QStringLiteral("kcm_kwintabbox"));
        QCOMPARE(r.route(), QStringLiteral("system/multitasking"));
        QCOMPARE(r.handOff(QStringLiteral("settings:system/sound#output")), QStringLiteral("kcm_pulseaudio"));
        QCOMPARE(r.route(), QStringLiteral("system/sound"));
        QCOMPARE(anchors.size(), 1);
        QCOMPARE(anchors.first().first().toString(), QStringLiteral("output"));
        QVERIFY(r.canGoBack());
        // the unknown leaves the route alone
        QCOMPARE(r.handOff(QStringLiteral("kcm_kwinrules")), QStringLiteral("kcm_kwinrules"));
        QCOMPARE(r.route(), QStringLiteral("system/sound"));
        QCOMPARE(r.handOff(QString()), QString());
        QVERIFY(r.atHome());
    }

    // what a browser or a shell may put in a settings: address
    void readsEncodedSettingsAddresses()
    {
        std::unique_ptr<Router> r(router());
        QCOMPARE(Router::idOf(QStringLiteral("settings:system%2Fdisplay?from=start#night%20light")),
                 QStringLiteral("system/display"));
        QCOMPARE(Router::anchorOf(QStringLiteral("settings:system%2Fdisplay?from=start#night%20light")),
                 QStringLiteral("night light"));
        QCOMPARE(Router::idOf(QStringLiteral("settings:///kcm_kscreen.desktop")), QStringLiteral("kcm_kscreen"));
        QCOMPARE(Router::anchorOf(QStringLiteral("system/display")), QString());
        QVERIFY(r->navigate(QStringLiteral("settings://system/display?x=1#a%23b")));
        QCOMPARE(r->route(), QStringLiteral("system/display"));
        QCOMPARE(r->anchor(), QStringLiteral("a#b"));
    }

    // the package moves KDE's commands and menu entry aside, and puts
    // them back on every way out
    void maintainerScriptsMoveKdeAside()
    {
        MaintainerScripts scripts;
        if (!scripts.ready())
            QSKIP("dpkg is not here to compare versions");
        const QString listKinfocenter = QStringLiteral(
            "dpkg-divert --listpackage /usr/share/applications/org.kde.kinfocenter.desktop");
        const QString listKmenuedit = QStringLiteral(
            "dpkg-divert --listpackage /usr/share/applications/org.kde.kmenuedit.desktop");
        const QStringList install = {addSystemsettings, addKcmshell,
                                     QStringLiteral("mkdir -p /usr/share/raye-settings/kde"), addEntry,
                                     listKinfocenter, listKmenuedit};
        QCOMPARE(scripts.run(QStringLiteral("preinst"), {QStringLiteral("install")}), install);
        QCOMPARE(scripts.exitCode(), 0);
        QCOMPARE(scripts.run(QStringLiteral("preinst"), {QStringLiteral("upgrade"), QStringLiteral("0.1~dev5")}),
                 install);
        // a test build that moved KDE's two menu entries: they come back
        const QStringList restored = scripts.run(QStringLiteral("preinst"), {QStringLiteral("upgrade"),
                                                                              QStringLiteral("0.1~dev5")},
                                                 QStringLiteral("raye-settings"));
        QVERIFY(restored.contains(QStringLiteral(
            "dpkg-divert --package raye-settings --remove --rename --divert "
            "/usr/share/raye-settings/kde/org.kde.kinfocenter.desktop /usr/share/applications/org.kde.kinfocenter.desktop")));
        QVERIFY(restored.contains(QStringLiteral(
            "dpkg-divert --package raye-settings --remove --rename --divert "
            "/usr/share/raye-settings/kde/org.kde.kmenuedit.desktop /usr/share/applications/org.kde.kmenuedit.desktop")));
        QVERIFY(scripts.run(QStringLiteral("preinst"), {QStringLiteral("abort-upgrade")}).isEmpty());

        const QStringList all = {removing(addSystemsettings), removing(addKcmshell), removing(addEntry),
                                 QStringLiteral("rmdir /usr/share/raye-settings/kde")};
        for (const QString &why : {QStringLiteral("remove"), QStringLiteral("abort-install"), QStringLiteral("disappear")})
            QCOMPARE(scripts.run(QStringLiteral("postrm"), {why}), all);
        QVERIFY(scripts.run(QStringLiteral("postrm"), {QStringLiteral("purge")}).isEmpty());
        QVERIFY(scripts.run(QStringLiteral("postrm"), {QStringLiteral("upgrade"), QStringLiteral("0.1~dev7")}).isEmpty());
        // an upgrade stopped half way: only what the old version had no
        // relay for goes back
        QVERIFY(scripts.run(QStringLiteral("postrm"), {QStringLiteral("abort-upgrade"), QStringLiteral("0.1~dev6")}).isEmpty());
        QCOMPARE(scripts.run(QStringLiteral("postrm"), {QStringLiteral("abort-upgrade"), QStringLiteral("0.1~dev5")}),
                 QStringList{removing(addKcmshell)});
        QCOMPARE(scripts.run(QStringLiteral("postrm"), {QStringLiteral("abort-upgrade"), QStringLiteral("0.1~dev4")}),
                 (QStringList{removing(addKcmshell), removing(addSystemsettings), removing(addEntry),
                              QStringLiteral("rmdir /usr/share/raye-settings/kde")}));
        QCOMPARE(scripts.exitCode(), 0);
    }

    // every page of the layout shows something: its own file once it is
    // built, the placeholder until then
    void everyPageHasAFileToShow()
    {
        QVERIFY(QFile::exists(QStringLiteral(":/qt/qml/org/raye/settings/PlaceholderPage.qml")));
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Router r;
        r.setLayout(parseLayout(f.readAll(), QStringLiteral("en")));
        int built = 0;
        for (const Category &c : r.layout().categories) {
            for (const Page &p : c.pages) {
                const QString url = r.componentUrlOf(p.id);
                if (p.type == QLatin1String("kcm")) {
                    QVERIFY2(url.isEmpty(), qPrintable(p.id));
                    continue;
                }
                const bool inSources = QFile::exists(QStringLiteral(SOURCE_DIR "/src/qml/") + p.component);
                QCOMPARE(url, inSources ? p.component : QStringLiteral("PlaceholderPage.qml"));
                if (inSources) {
                    ++built;
                    QVERIFY(QFile::exists(QStringLiteral(":/qt/qml/org/raye/settings/") + p.component));
                }
            }
        }
        qInfo("%d pages built, the others show the placeholder", built);
    }
};

QTEST_GUILESS_MAIN(TestRouter)
#include "test_router.moc"
