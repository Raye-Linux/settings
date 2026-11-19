// SPDX-License-Identifier: GPL-3.0-or-later
#include "router.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QSet>
#include <QJSEngine>
#include <QQmlEngine>
#include <QUrl>

#include <algorithm>

namespace {

Router *s_instance = nullptr;

// Reads the first line of a sysfs file, trimmed.
QString firstLine(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

// All the names the input devices report, lower case.
QStringList inputNames(const QString &sys)
{
    QStringList names;
    const QDir dir(sys + QStringLiteral("/class/input"));
    const QStringList entries = dir.entryList(QStringList{QStringLiteral("input*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QString name = firstLine(dir.filePath(entry + QStringLiteral("/name")));
        if (!name.isEmpty())
            names.append(name.toLower());
    }
    return names;
}

// Whole words only: "pen" is in "Wacom Pen", not in "OpenRGB".
bool anyNameHas(const QStringList &names, const QString &words)
{
    const QRegularExpression word(QStringLiteral("\\b(%1)\\b").arg(words));
    for (const QString &name : names) {
        if (word.match(name).hasMatch())
            return true;
    }
    return false;
}

// The firmware's boot entries (Boot0000 ... in efivarfs) are how the
// boot menu finds Windows. The description is UTF-16 after a 4-byte
// attribute word, a 4-byte load option attribute and a 2-byte length;
// the path to Windows' loader comes after it. Reading the bytes as
// UTF-16 and looking for either is enough.
bool firmwareKnowsWindows(const QString &root)
{
    static const QRegularExpression bootEntry(
        QStringLiteral("^Boot[0-9A-Fa-f]{4}-8be4df61-93ca-11d0-a2fe-00aa00004b98$"));
    const QDir dir(root + QStringLiteral("/sys/firmware/efi/efivars"));
    const QStringList entries = dir.entryList(QDir::Files);
    for (const QString &entry : entries) {
        if (!bootEntry.match(entry).hasMatch())
            continue;
        QFile f(dir.filePath(entry));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QByteArray bytes = f.read(4096);
        // the text starts on an even byte (offset 10); keep it aligned
        if (bytes.size() > 4)
            bytes.remove(0, 4);
        if (bytes.size() % 2)
            bytes.chop(1);
        const QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()),
                                                bytes.size() / 2);
        if (text.contains(QStringLiteral("Windows Boot Manager"))
            || text.contains(QStringLiteral("bootmgfw.efi"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// USB ids of the readers libfprint drives, from the hardware database
// it installs ("usb:v138Ap0011*").
QSet<QString> fingerprintReaderIds(const QString &root)
{
    QSet<QString> ids;
    static const QRegularExpression line(QStringLiteral("^usb:v([0-9A-Fa-f]{4})p([0-9A-Fa-f]{4})"));
    for (const QString &folder : {QStringLiteral("/usr/lib/udev/hwdb.d"), QStringLiteral("/lib/udev/hwdb.d")}) {
        const QDir dir(root + folder);
        const QStringList files = dir.entryList({QStringLiteral("*fingerprint*.hwdb")}, QDir::Files);
        for (const QString &name : files) {
            QFile f(dir.filePath(name));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            while (!f.atEnd()) {
                const QRegularExpressionMatch m = line.match(QString::fromLatin1(f.readLine()).trimmed());
                if (m.hasMatch())
                    ids.insert((m.captured(1) + QLatin1Char(':') + m.captured(2)).toLower());
            }
        }
    }
    return ids;
}

// Where the QML module puts the page files once they are built.
bool builtIn(const QString &component)
{
    return QFile::exists(QStringLiteral(":/qt/qml/org/raye/settings/") + component);
}

} // namespace

Requirements::Requirements(const QString &root)
    : m_root(root == QLatin1String("/") ? QString() : root)
{
}

void Requirements::set(const QString &name, bool present)
{
    m_answers.insert(name, present);
}

void Requirements::forget()
{
    m_answers.clear();
}

bool Requirements::has(const QString &name) const
{
    if (name.isEmpty())
        return true;
    if (name.contains(QLatin1Char('|'))) {
        const QStringList either = name.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        return std::any_of(either.cbegin(), either.cend(), [this](const QString &one) { return has(one); });
    }
    const auto answer = m_answers.constFind(name);
    if (answer != m_answers.cend())
        return *answer;
    const bool present = probe(name);
    m_answers.insert(name, present);
    return present;
}

bool Requirements::probe(const QString &name) const
{
    const QString sys = m_root + QStringLiteral("/sys");

    if (name == QLatin1String("bluetooth"))
        return !QDir(sys + QStringLiteral("/class/bluetooth"))
                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
                    .isEmpty();

    if (name == QLatin1String("battery")) {
        const QDir dir(sys + QStringLiteral("/class/power_supply"));
        const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            if (firstLine(dir.filePath(entry + QStringLiteral("/type"))) != QLatin1String("Battery"))
                continue;
            // a mouse or a headset carries its own battery; only the
            // one that powers the computer counts
            const QString scope = firstLine(dir.filePath(entry + QStringLiteral("/scope")));
            if (scope.isEmpty() || scope == QLatin1String("System"))
                return true;
        }
        return false;
    }

    if (name == QLatin1String("wifi")) {
        // a wireless interface has a "wireless" folder or a phy80211 link;
        // in the app WifiHardware replaces this with what NetworkManager sees
        const QDir net(sys + QStringLiteral("/class/net"));
        const QStringList entries = net.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            if (QFileInfo::exists(net.filePath(entry + QStringLiteral("/wireless")))
                || QFileInfo::exists(net.filePath(entry + QStringLiteral("/phy80211"))))
                return true;
        }
        return false;
    }

    if (name == QLatin1String("online-accounts")) {
        // the services Email & accounts can add, one file each
        const QDir providers(m_root + QStringLiteral("/usr/share/accounts/providers"));
        return !providers.entryList({QStringLiteral("*.provider")}, QDir::Files).isEmpty();
    }

    if (name == QLatin1String("touchpad"))
        return anyNameHas(inputNames(sys), QStringLiteral("touchpad|trackpad|glidepoint"));

    if (name == QLatin1String("tablet"))
        return anyNameHas(inputNames(sys), QStringLiteral("tablet|pen|stylus|wacom"));

    if (name == QLatin1String("two-gpus")) {
        // display controllers only (PCI class 0x03....): the firmware's
        // framebuffer and a USB display adapter also show up as cards
        static const QRegularExpression card(QStringLiteral("^card[0-9]+$"));
        int cards = 0;
        const QDir drm(sys + QStringLiteral("/class/drm"));
        const QStringList entries = drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            if (!card.match(entry).hasMatch())
                continue;
            if (firstLine(drm.filePath(entry + QStringLiteral("/device/class"))).startsWith(QLatin1String("0x03")))
                ++cards;
        }
        return cards >= 2;
    }

    if (name == QLatin1String("nvidia-panel")) {
        // the panel alone is useless without the driver (nouveau, or a card
        // taken out), and the driver alone has nothing to open
        return QFileInfo::exists(sys + QStringLiteral("/module/nvidia"))
            && QFileInfo::exists(m_root + QStringLiteral("/usr/share/applications/nvidia-settings.desktop"));
    }

    if (name == QLatin1String("windows")) {
        if (firmwareKnowsWindows(m_root))
            return true;
        // the boot menu can still start it from the EFI partition
        return QFileInfo::exists(m_root + QStringLiteral("/boot/efi/EFI/Microsoft/Boot/bootmgfw.efi"));
    }

    if (name == QLatin1String("btrfs")) {
        QFile mounts(m_root + QStringLiteral("/proc/self/mountinfo"));
        if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        while (!mounts.atEnd()) {
            // ... <mount point> ... - <fs type> <source> <options>
            const QStringList fields = QString::fromUtf8(mounts.readLine()).split(QLatin1Char(' '));
            const int separator = fields.indexOf(QStringLiteral("-"));
            if (fields.size() < 5 || separator < 0 || separator + 1 >= fields.size())
                continue;
            if (fields.at(4) == QLatin1String("/") && fields.at(separator + 1) == QLatin1String("btrfs"))
                return true;
        }
        return false;
    }

    if (name == QLatin1String("raye-account")) {
        // the account preview switch, and the QML module the pages need
        const bool preview = qEnvironmentVariable("RAYE_ACCOUNT_PREVIEW") == QLatin1String("1")
            || QFileInfo::exists(m_root + QStringLiteral("/etc/raye/account-preview"));
        if (!preview)
            return false;
        QStringList imports = qEnvironmentVariable("QML_IMPORT_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        imports << QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
        for (const QString &dir : std::as_const(imports)) {
            if (QFileInfo::exists(dir + QStringLiteral("/org/raye/account/qmldir")))
                return true;
        }
        return false;
    }

    if (name == QLatin1String("fingerprint")) {
        // a reader libfprint knows, and fprintd to enrol a finger with
        if (!QFileInfo::exists(m_root + QStringLiteral("/usr/share/dbus-1/system-services/net.reactivated.Fprint.service")))
            return false;
        const QSet<QString> known = fingerprintReaderIds(m_root);
        if (known.isEmpty())
            return false;
        const QDir usb(sys + QStringLiteral("/bus/usb/devices"));
        const QStringList devices = usb.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &device : devices) {
            const QString vendor = firstLine(usb.filePath(device + QStringLiteral("/idVendor")));
            const QString product = firstLine(usb.filePath(device + QStringLiteral("/idProduct")));
            if (!vendor.isEmpty() && known.contains((vendor + QLatin1Char(':') + product).toLower()))
                return true;
        }
        return false;
    }

    return false; // a word we do not know hides the page rather than promising it
}

Router::Router(QObject *parent)
    : QObject(parent)
    , m_hasComponent(builtIn)
{
    if (!s_instance)
        s_instance = this;
    m_history.append({QStringLiteral("home"), QString()});
    m_position = 0;
}

Router::~Router()
{
    // the tests build and drop routers one after another; whoever asks
    // for the one QML uses must not be handed a dead object
    if (s_instance == this)
        s_instance = nullptr;
}

Router *Router::instance()
{
    return s_instance;
}

Router *Router::create(QQmlEngine *qmlEngine, QJSEngine *)
{
    if (s_instance) {
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }
    return new Router(qmlEngine);
}

void Router::setLayout(const Layout &layout)
{
    m_layout = layout;
}

QString Router::categoryId() const
{
    if (atHome())
        return {};
    const int row = m_layout.categoryIndexOf(m_route);
    return row < 0 ? QString() : m_layout.categories.at(row).id;
}

namespace {

// "settings:" and the slashes a scheme handler puts after it, then the
// query a browser may add; the anchor is left on.
QString withoutScheme(const QString &target, bool *wasUrl = nullptr)
{
    QString id = target.trimmed();
    const bool url = id.startsWith(QLatin1String("settings:"), Qt::CaseInsensitive);
    if (wasUrl)
        *wasUrl = url;
    if (!url)
        return id;
    id = id.mid(9);
    const int hash = id.indexOf(QLatin1Char('#'));
    const QString anchor = hash < 0 ? QString() : id.mid(hash);
    id = (hash < 0 ? id : id.left(hash)).section(QLatin1Char('?'), 0, 0) + anchor;
    return QUrl::fromPercentEncoding(id.toUtf8());
}

} // namespace

QString Router::idOf(const QString &target)
{
    QString id = withoutScheme(target);
    // settings:page, settings://page and settings:///page all come
    while (id.startsWith(QLatin1Char('/')))
        id = id.mid(1);
    id = id.section(QLatin1Char('#'), 0, 0);
    // a desktop file name is a module id
    if (id.endsWith(QLatin1String(".desktop")))
        id.chop(8);
    return id;
}

QString Router::anchorOf(const QString &target)
{
    return withoutScheme(target).section(QLatin1Char('#'), 1);
}

QString Router::resolve(const QString &target) const
{
    const QString id = idOf(target);
    if (id.isEmpty() || id == QLatin1String("home") || m_layout.homeAliases.contains(id))
        return QStringLiteral("home");
    if (m_layout.page(id))
        return id;
    if (m_layout.category(id))
        return id;
    if (const Page *page = m_layout.pageForModule(id))
        return page->id;
    // the first Raye pages were opened as raye:<page>
    if (id.startsWith(QLatin1String("raye:"))) {
        const QString rest = id.mid(5);
        if (m_layout.page(rest) || m_layout.category(rest))
            return rest;
    }
    return {};
}

QString Router::moduleFor(const QString &target) const
{
    const QString route = resolve(target);
    if (route.isEmpty())
        return target;
    if (route == QLatin1String("home"))
        return {};
    const QString id = idOf(target);
    if (const Page *page = m_layout.page(route)) {
        // asked for by its module id: that module, not the page's first
        // one (the task switcher's "Configure..." is kcm_kwintabbox,
        // which shares a page with the virtual desktops)
        if (id == page->kcm || page->aliases.contains(id))
            return id;
        if (page->opensInWindow())
            return page->target();
        // nothing to open yet: the window shows the page's category
        return route.section(QLatin1Char('/'), 0, 0);
    }
    // a category: the window shows its list, unless the category opens
    // straight on a Raye page (Raye Update)
    const Category *category = m_layout.category(route);
    if (category && !category->pages.isEmpty() && category->pages.first().kcm.startsWith(QLatin1String("raye:")))
        return category->pages.first().target();
    return route;
}

QString Router::handOff(const QString &target)
{
    navigate(target);
    return moduleFor(target);
}

void Router::setRequirement(const QString &name, bool present)
{
    if (m_requirements.has(name) == present)
        return;
    m_requirements.set(name, present);
    Q_EMIT requirementsChanged();
}

bool Router::isAvailable(const QString &pageId) const
{
    const Page *page = m_layout.page(pageId);
    return page ? m_requirements.has(page->requirement) : false;
}

QString Router::componentOf(const QString &pageId) const
{
    const Page *page = m_layout.page(pageId);
    return page ? page->component : QString();
}

QString Router::componentUrlOf(const QString &pageId) const
{
    const QString component = componentOf(pageId);
    if (component.isEmpty())
        return {};
    if (m_hasComponent && m_hasComponent(component))
        return component;
    return QStringLiteral("PlaceholderPage.qml");
}

void Router::setComponentCheck(std::function<bool(const QString &)> check)
{
    m_hasComponent = std::move(check);
}

bool Router::navigate(const QString &target)
{
    QString route = resolve(target);
    if (route.isEmpty())
        return false;

    // a link to a page this computer has no hardware for lands on the
    // category, which is where the user was heading anyway
    QString anchor = anchorOf(target);
    if (const Page *page = m_layout.page(route)) {
        if (!m_requirements.has(page->requirement)) {
            const QString category = route.section(QLatin1Char('/'), 0, 0);
            // a page id always starts with its category (test_layout
            // holds the layout to it); fall back home if one ever did not
            route = m_layout.category(category) ? category : QStringLiteral("home");
            anchor.clear();
        }
    }

    go({route, anchor});
    return true;
}

void Router::go(const Step &step)
{
    const bool sameSpot = step.route == m_route && step.anchor == m_anchor;
    if (!sameSpot) {
        m_history.remove(m_position + 1, m_history.size() - m_position - 1);
        m_history.append(step);
        ++m_position;
        m_route = step.route;
        m_anchor = step.anchor;
        Q_EMIT routeChanged();
    }
    // the same link twice still flashes the row it points at
    if (!step.anchor.isEmpty())
        Q_EMIT anchorRequested(step.anchor);
}

// Back and forward put the window where it was, anchor included, but
// do not flash the row again: the user is retracing a step, not
// following a link.
void Router::back()
{
    if (!canGoBack())
        return;
    --m_position;
    const Step &step = m_history.at(m_position);
    m_route = step.route;
    m_anchor = step.anchor;
    Q_EMIT routeChanged();
}

void Router::forward()
{
    if (!canGoForward())
        return;
    ++m_position;
    const Step &step = m_history.at(m_position);
    m_route = step.route;
    m_anchor = step.anchor;
    Q_EMIT routeChanged();
}
