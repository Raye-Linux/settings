// SPDX-License-Identifier: GPL-3.0-or-later
#include "accessibility.h"

#include "multitasking.h"
#include "performance.h"

#include <KConfig>
#include <KConfigGroup>
#include <QCollator>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

// A call that failed before it left (no bus) is finished before anyone
// listens: say so once the caller is connected.
void finishLate(QDBusPendingCallWatcher *watcher)
{
    if (watcher->isFinished())
        QMetaObject::invokeMethod(watcher, [watcher] { Q_EMIT watcher->finished(watcher); }, Qt::QueuedConnection);
}

const QString plugins = QStringLiteral("Plugins");
const QString zoomGroup = QStringLiteral("Effect-zoom");
const QString filterGroup = QStringLiteral("Effect-colorblindnesscorrection");

// Is a desktop session there to be told? (Not in the tests, which have
// no session bus: then the files are all there is.)
bool sessionRunning()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected();
}

void writeNotify(const KSharedConfigPtr &config, const QString &group, const QString &key, const QVariant &value)
{
    KConfigGroup g = config->group(group);
    g.writeEntry(key, value, KConfig::Notify);
    config->sync();
}

void deleteNotify(const KSharedConfigPtr &config, const QString &group, const QString &key)
{
    KConfigGroup g = config->group(group);
    g.deleteEntry(key, KConfig::Notify);
    config->sync();
}

// The window manager reads its file again on this signal, which also
// loads and unloads the effects named in [Plugins]; an effect already
// running reads its own group again when told by name.
void tellKwin(const QString &effect = QString())
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.send(QDBusMessage::createSignal(QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reloadConfig")));
    if (!effect.isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                                                           QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("reconfigureEffect"));
        call << effect;
        bus.send(call);
    }
}

double roundOne(double value)
{
    return std::round(value * 10.0) / 10.0;
}

} // namespace

// ---- Text size --------------------------------------------------------

const QStringList TextSizeSettings::fontKeys = {
    QStringLiteral("General/font"),        QStringLiteral("General/menuFont"),
    QStringLiteral("General/toolBarFont"), QStringLiteral("General/smallestReadableFont"),
    QStringLiteral("General/fixed"),       QStringLiteral("WM/activeFont")};

TextSizeSettings::TextSizeSettings(QObject *parent)
    : QObject(parent)
    , m_globals(KSharedConfig::openConfig(QStringLiteral("kdeglobals")))
{
}

void TextSizeSettings::reload()
{
    m_globals->reparseConfiguration();
    Q_EMIT changed();
}

int TextSizeSettings::clampPercent(int percent)
{
    const int rounded = qRound(percent / 5.0) * 5;
    return std::clamp(rounded, minPercent, maxPercent);
}

int TextSizeSettings::percent() const
{
    return clampPercent(m_globals->group(QStringLiteral("Raye")).readEntry("TextSize", 100));
}

QString TextSizeSettings::family() const
{
    QFont f;
    const QString value = m_globals->group(QStringLiteral("General")).readEntry("font", QString());
    if (value.isEmpty() || !f.fromString(value))
        return QGuiApplication::font().family();
    return f.family();
}

qreal TextSizeSettings::basePointSize() const
{
    QFont f;
    const QString value = m_globals->group(QStringLiteral("General")).readEntry("font", QString());
    if (value.isEmpty() || !f.fromString(value) || f.pointSizeF() <= 0)
        return 10;
    return roundOne(f.pointSizeF() * 100.0 / percent());
}

QString TextSizeSettings::scaledFont(const QString &font, int from, int to)
{
    QFont f;
    if (font.isEmpty() || !f.fromString(font) || f.pointSizeF() <= 0 || from <= 0 || to <= 0)
        return font;
    // through the size at 100 %, so going up and down again ends where it began
    const double base = roundOne(f.pointSizeF() * 100.0 / from);
    f.setPointSizeF(roundOne(base * to / 100.0));
    return f.toString();
}

void TextSizeSettings::apply(int requested)
{
    const int to = clampPercent(requested);
    const int from = percent();
    if (to == from)
        return;
    for (const QString &entry : fontKeys) {
        const QString group = entry.section(QLatin1Char('/'), 0, 0);
        const QString key = entry.section(QLatin1Char('/'), 1);
        KConfigGroup g = m_globals->group(group);
        const QString font = g.readEntry(key, QString());
        // a font nobody set is the toolkit's: nothing to scale
        if (font.isEmpty())
            continue;
        g.writeEntry(key, scaledFont(font, from, to), KConfig::Notify);
    }
    KConfigGroup raye = m_globals->group(QStringLiteral("Raye"));
    raye.writeEntry("TextSize", to, KConfig::Notify);
    m_globals->sync();
    Q_EMIT changed();
}

// ---- Visual effects ---------------------------------------------------

const QList<int> VisualEffectsSettings::toastChoices = {5, 7, 15, 30, 60, 300};

VisualEffectsSettings::VisualEffectsSettings(QObject *parent)
    : QObject(parent)
    , m_globals(KSharedConfig::openConfig(QStringLiteral("kdeglobals")))
    , m_kwin(KSharedConfig::openConfig(QStringLiteral("kwinrc")))
    , m_shell(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
{
}

void VisualEffectsSettings::reload()
{
    m_globals->reparseConfiguration();
    m_kwin->reparseConfiguration();
    m_shell->reparseConfiguration();
    Q_EMIT changed();
}

bool VisualEffectsSettings::transparency() const
{
    return m_globals->group(QStringLiteral("Raye")).readEntry("TransparencyEffects", true);
}

void VisualEffectsSettings::setTransparency(bool on)
{
    if (on == transparency())
        return;
    PerformanceSettings::writeTransparency(on);
    tellKwin();
    Q_EMIT changed();
}

bool VisualEffectsSettings::animations() const
{
    return m_globals->group(QStringLiteral("KDE")).readEntry("AnimationDurationFactor", 1.0) > 0;
}

void VisualEffectsSettings::setAnimations(bool on)
{
    if (on == animations())
        return;
    writeNotify(m_globals, QStringLiteral("KDE"), QStringLiteral("AnimationDurationFactor"), on ? 1.0 : 0.0);
    Q_EMIT changed();
}

int VisualEffectsSettings::toastSeconds() const
{
    return toastChoices.at(toastIndex(m_shell->group(QStringLiteral("Notifications")).readEntry("ToastSeconds", 5)));
}

void VisualEffectsSettings::setToastSeconds(int seconds)
{
    if (!toastChoices.contains(seconds) || seconds == toastSeconds())
        return;
    writeNotify(m_shell, QStringLiteral("Notifications"), QStringLiteral("ToastSeconds"), seconds);
    Q_EMIT changed();
}

bool VisualEffectsSettings::toastAvailable() const
{
    return MultitaskingSettings::systemDefines(QStringLiteral("rayeshellrc"), QStringLiteral("Notifications"),
                                               QStringLiteral("ToastSeconds"));
}

int VisualEffectsSettings::toastIndex(int seconds)
{
    int best = 0;
    for (int i = 1; i < toastChoices.size(); ++i) {
        if (std::abs(toastChoices.at(i) - seconds) < std::abs(toastChoices.at(best) - seconds))
            best = i;
    }
    return best;
}

// ---- Mouse pointer ----------------------------------------------------

PointerSettings::PointerSettings(QObject *parent)
    : QObject(parent)
    , m_input(KSharedConfig::openConfig(QStringLiteral("kcminputrc")))
    , m_kwin(KSharedConfig::openConfig(QStringLiteral("kwinrc")))
    , m_themes(findThemes())
{
}

void PointerSettings::reload()
{
    m_input->reparseConfiguration();
    m_kwin->reparseConfiguration();
    m_themes = findThemes();
    Q_EMIT themesChanged();
    Q_EMIT changed();
}

QList<PointerSettings::Theme> PointerSettings::findThemes()
{
    QStringList dirs;
    dirs << QDir::homePath() + QStringLiteral("/.icons");
    const QStringList data = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &d : data)
        dirs << d + QStringLiteral("/icons");
    dirs << QStringLiteral("/usr/share/pixmaps");

    QList<Theme> list;
    QSet<QString> seen;
    for (const QString &dir : std::as_const(dirs)) {
        const QStringList names = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : names) {
            // "default" only points at another theme; a name starting with
            // "-" would be read as an option by the tool that applies it
            if (seen.contains(id) || id == QLatin1String("default") || id.startsWith(QLatin1Char('-')))
                continue;
            const QString path = dir + QLatin1Char('/') + id;
            if (!QFileInfo(path + QStringLiteral("/cursors")).isDir())
                continue;
            seen.insert(id);
            Theme t;
            t.id = id;
            KConfig index(path + QStringLiteral("/index.theme"), KConfig::SimpleConfig);
            t.name = index.group(QStringLiteral("Icon Theme")).readEntry("Name", id);
            for (const char *arrow : {"/cursors_scalable/default/default.svg", "/cursors_scalable/left_ptr/left_ptr.svg"}) {
                const QString file = path + QString::fromLatin1(arrow);
                if (QFileInfo::exists(file)) {
                    t.preview = QUrl::fromLocalFile(file).toString();
                    break;
                }
            }
            list << t;
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const Theme &a, const Theme &b) {
        const bool rayeA = a.id.startsWith(QLatin1String("Raye"));
        const bool rayeB = b.id.startsWith(QLatin1String("Raye"));
        if (rayeA != rayeB)
            return rayeA;
        return collator.compare(a.name, b.name) < 0;
    });
    return list;
}

QVariantList PointerSettings::themes() const
{
    QVariantList list;
    for (const Theme &t : m_themes)
        list << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("name"), t.name}, {QStringLiteral("preview"), t.preview}};
    return list;
}

QString PointerSettings::theme() const
{
    return m_input->group(QStringLiteral("Mouse")).readEntry("cursorTheme", QStringLiteral("breeze_cursors"));
}

void PointerSettings::setTheme(const QString &id)
{
    const bool known = std::any_of(m_themes.cbegin(), m_themes.cend(), [&id](const Theme &t) {
        return t.id == id;
    });
    if (!known || id == theme())
        return;
    apply(id, size());
}

int PointerSettings::size() const
{
    return std::clamp(m_input->group(QStringLiteral("Mouse")).readEntry("cursorSize", defaultSize), minSize, maxSize);
}

void PointerSettings::setSize(int size)
{
    size = std::clamp(size, minSize, maxSize);
    if (size != this->size())
        apply(theme(), size);
}

void PointerSettings::apply(const QString &theme, int size)
{
    // the desktop's own tool tells the window manager and running apps;
    // without a session the file is all there is
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("plasma-apply-cursortheme"));
    if (sessionRunning() && !tool.isEmpty()) {
        QProcess process;
        process.start(tool, {theme, QStringLiteral("--size"), QString::number(size)});
        process.waitForFinished(5000);
        m_input->reparseConfiguration();
    }
    if (this->theme() != theme || this->size() != size) {
        KConfigGroup g = m_input->group(QStringLiteral("Mouse"));
        g.writeEntry("cursorTheme", theme, KConfig::Notify);
        g.writeEntry("cursorSize", size, KConfig::Notify);
        m_input->sync();
    }
    Q_EMIT changed();
}

bool PointerSettings::shakeToFind() const
{
    return m_kwin->group(plugins).readEntry("shakecursorEnabled", true);
}

void PointerSettings::setShakeToFind(bool on)
{
    if (on == shakeToFind())
        return;
    writeNotify(m_kwin, plugins, QStringLiteral("shakecursorEnabled"), on);
    tellKwin();
    Q_EMIT changed();
}

bool PointerSettings::ctrlToFind() const
{
    return m_kwin->group(plugins).readEntry("trackmouseEnabled", false);
}

void PointerSettings::setCtrlToFind(bool on)
{
    if (on == ctrlToFind())
        return;
    if (on) {
        KConfigGroup g = m_kwin->group(QStringLiteral("Effect-trackmouse"));
        g.writeEntry("Control", true, KConfig::Notify);
        for (const char *key : {"Meta", "Alt", "Shift"})
            g.writeEntry(key, false, KConfig::Notify);
    }
    writeNotify(m_kwin, plugins, QStringLiteral("trackmouseEnabled"), on);
    tellKwin(QStringLiteral("trackmouse"));
    Q_EMIT changed();
}

bool PointerSettings::touchIndicator() const
{
    return m_kwin->group(plugins).readEntry("touchpointsEnabled", false);
}

void PointerSettings::setTouchIndicator(bool on)
{
    if (on == touchIndicator())
        return;
    writeNotify(m_kwin, plugins, QStringLiteral("touchpointsEnabled"), on);
    tellKwin();
    Q_EMIT changed();
}

// ---- Magnifier --------------------------------------------------------

const QList<double> MagnifierSettings::zoomSteps = {1.1, 1.2, 1.25, 1.5, 2.0};

MagnifierSettings::MagnifierSettings(QObject *parent)
    : QObject(parent)
    , m_kwin(KSharedConfig::openConfig(QStringLiteral("kwinrc")))
{
}

void MagnifierSettings::reload()
{
    m_kwin->reparseConfiguration();
    Q_EMIT changed();
}

bool MagnifierSettings::enabled() const
{
    const KConfigGroup g = m_kwin->group(plugins);
    // the window manager has zoom on by default
    return g.readEntry("zoomEnabled", true) || g.readEntry("magnifierEnabled", false);
}

void MagnifierSettings::setEnabled(bool on)
{
    if (on == enabled())
        return;
    if (on) {
        // back to the view it had, full screen if none
        writeNotify(m_kwin, plugins, QStringLiteral("zoomEnabled"), view() == 0);
        writeNotify(m_kwin, plugins, QStringLiteral("magnifierEnabled"), view() == 1);
    } else {
        writeNotify(m_kwin, plugins, QStringLiteral("zoomEnabled"), false);
        writeNotify(m_kwin, plugins, QStringLiteral("magnifierEnabled"), false);
    }
    tellKwin();
    Q_EMIT changed();
}

int MagnifierSettings::view() const
{
    const KConfigGroup g = m_kwin->group(plugins);
    return g.readEntry("magnifierEnabled", false) && !g.readEntry("zoomEnabled", true) ? 1 : 0;
}

void MagnifierSettings::setView(int view)
{
    if (view < 0 || view > 1 || (view == this->view() && enabled()))
        return;
    // the two share Windows key + Plus: only one at a time
    writeNotify(m_kwin, plugins, QStringLiteral("zoomEnabled"), view == 0);
    writeNotify(m_kwin, plugins, QStringLiteral("magnifierEnabled"), view == 1);
    tellKwin();
    Q_EMIT changed();
}

int MagnifierSettings::stepIndex(double factor)
{
    int best = 0;
    for (int i = 1; i < zoomSteps.size(); ++i) {
        if (std::abs(zoomSteps.at(i) - factor) < std::abs(zoomSteps.at(best) - factor))
            best = i;
    }
    return best;
}

int MagnifierSettings::zoomStep() const
{
    return stepIndex(m_kwin->group(zoomGroup).readEntry("ZoomFactor", 1.2));
}

void MagnifierSettings::setZoomStep(int index)
{
    if (index < 0 || index >= zoomSteps.size() || index == zoomStep())
        return;
    writeNotify(m_kwin, zoomGroup, QStringLiteral("ZoomFactor"), zoomSteps.at(index));
    tellKwin(QStringLiteral("zoom"));
    Q_EMIT changed();
}

int MagnifierSettings::tracking() const
{
    const int value = m_kwin->group(zoomGroup).readEntry("MouseTracking", 0);
    return value >= 0 && value <= 2 ? value : m_lastTracking;
}

void MagnifierSettings::setTracking(int tracking)
{
    if (tracking < 0 || tracking > 2 || (tracking == this->tracking() && followPointer()))
        return;
    m_lastTracking = tracking;
    writeNotify(m_kwin, zoomGroup, QStringLiteral("MouseTracking"), tracking);
    tellKwin(QStringLiteral("zoom"));
    Q_EMIT changed();
}

bool MagnifierSettings::followPointer() const
{
    // 3 is "the view does not move with the pointer"
    return m_kwin->group(zoomGroup).readEntry("MouseTracking", 0) != 3;
}

void MagnifierSettings::setFollowPointer(bool on)
{
    if (on == followPointer())
        return;
    if (!on)
        m_lastTracking = tracking();
    writeNotify(m_kwin, zoomGroup, QStringLiteral("MouseTracking"), on ? m_lastTracking : 3);
    tellKwin(QStringLiteral("zoom"));
    Q_EMIT changed();
}

bool MagnifierSettings::followFocus() const
{
    return m_kwin->group(zoomGroup).readEntry("EnableFocusTracking", false);
}

void MagnifierSettings::setFollowFocus(bool on)
{
    if (on == followFocus())
        return;
    writeNotify(m_kwin, zoomGroup, QStringLiteral("EnableFocusTracking"), on);
    tellKwin(QStringLiteral("zoom"));
    Q_EMIT changed();
}

bool MagnifierSettings::followTextCursor() const
{
    return m_kwin->group(zoomGroup).readEntry("EnableTextCaretTracking", false);
}

void MagnifierSettings::setFollowTextCursor(bool on)
{
    if (on == followTextCursor())
        return;
    writeNotify(m_kwin, zoomGroup, QStringLiteral("EnableTextCaretTracking"), on);
    tellKwin(QStringLiteral("zoom"));
    Q_EMIT changed();
}

// ---- Colour filters ---------------------------------------------------

ColorFilterSettings::ColorFilterSettings(QObject *parent)
    : QObject(parent)
    , m_kwin(KSharedConfig::openConfig(QStringLiteral("kwinrc")))
{
}

void ColorFilterSettings::reload()
{
    m_kwin->reparseConfiguration();
    Q_EMIT changed();
}

bool ColorFilterSettings::enabled() const
{
    return m_kwin->group(plugins).readEntry("colorblindnesscorrectionEnabled", false);
}

void ColorFilterSettings::setEnabled(bool on)
{
    if (on == enabled())
        return;
    writeNotify(m_kwin, plugins, QStringLiteral("colorblindnesscorrectionEnabled"), on);
    tellKwin();
    Q_EMIT changed();
}

int ColorFilterSettings::mode() const
{
    const int value = m_kwin->group(filterGroup).readEntry("Mode", 0);
    return value >= 0 && value < modeCount ? value : 0;
}

void ColorFilterSettings::setMode(int mode)
{
    if (mode < 0 || mode >= modeCount || mode == this->mode())
        return;
    writeNotify(m_kwin, filterGroup, QStringLiteral("Mode"), mode);
    tellKwin(QStringLiteral("colorblindnesscorrection"));
    Q_EMIT changed();
}

int ColorFilterSettings::intensity() const
{
    const double value = m_kwin->group(filterGroup).readEntry("Intensity", 1.0);
    return std::clamp(qRound(value * 100), 0, 100);
}

void ColorFilterSettings::setIntensity(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == intensity())
        return;
    writeNotify(m_kwin, filterGroup, QStringLiteral("Intensity"), percent / 100.0);
    tellKwin(QStringLiteral("colorblindnesscorrection"));
    Q_EMIT changed();
}

// ---- Screen reader, audio, keyboard, mouse ----------------------------

AccessSettings::AccessSettings(QObject *parent)
    : QObject(parent)
    , m_access(KSharedConfig::openConfig(QStringLiteral("kaccessrc")))
{
    readKeyboard();
}

void AccessSettings::reload()
{
    m_access->reparseConfiguration();
    readKeyboard();
    Q_EMIT changed();
}

bool AccessSettings::readBool(const QString &group, const QString &key, bool fallback) const
{
    return m_access->group(group).readEntry(key, fallback);
}

void AccessSettings::write(const QString &group, const QString &key, const QVariant &value)
{
    writeNotify(m_access, group, key, value);
    if (sessionRunning()) {
        // the accessibility service is a single instance: starting it
        // again makes the running one read its settings (and start or
        // stop the screen reader); the window manager's keyboard helpers
        // read theirs on its signal
        QProcess::startDetached(QStringLiteral("kaccess"), {});
        tellKwin();
    }
    Q_EMIT changed();
}

bool AccessSettings::screenReader() const
{
    return readBool(QStringLiteral("ScreenReader"), QStringLiteral("Enabled"), false);
}

void AccessSettings::setScreenReader(bool on)
{
    if (on != screenReader())
        write(QStringLiteral("ScreenReader"), QStringLiteral("Enabled"), on);
}

bool AccessSettings::screenReaderInstalled() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("orca")).isEmpty();
}

void AccessSettings::openScreenReaderSettings()
{
    QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("--setup")});
}

bool AccessSettings::visibleBell() const
{
    return readBool(QStringLiteral("Bell"), QStringLiteral("VisibleBell"), false);
}

void AccessSettings::setVisibleBell(bool on)
{
    if (on != visibleBell())
        write(QStringLiteral("Bell"), QStringLiteral("VisibleBell"), on);
}

#define RAYE_KEYBOARD_BOOL(getter, setter, key, fallback)                         \
    bool AccessSettings::getter() const                                           \
    {                                                                             \
        return readBool(QStringLiteral("Keyboard"), QStringLiteral(key), fallback); \
    }                                                                             \
    void AccessSettings::setter(bool on)                                          \
    {                                                                             \
        if (on != getter())                                                       \
            write(QStringLiteral("Keyboard"), QStringLiteral(key), on);           \
    }

RAYE_KEYBOARD_BOOL(stickyKeys, setStickyKeys, "StickyKeys", false)
RAYE_KEYBOARD_BOOL(stickyKeysLatch, setStickyKeysLatch, "StickyKeysLatch", true)
RAYE_KEYBOARD_BOOL(stickyKeysAutoOff, setStickyKeysAutoOff, "StickyKeysAutoOff", false)
RAYE_KEYBOARD_BOOL(stickyKeysBeep, setStickyKeysBeep, "StickyKeysBeep", false)
RAYE_KEYBOARD_BOOL(gestures, setGestures, "Gestures", false)
RAYE_KEYBOARD_BOOL(gestureConfirmation, setGestureConfirmation, "GestureConfirmation", false)
RAYE_KEYBOARD_BOOL(gestureBeep, setGestureBeep, "AccessXBeep", true)
RAYE_KEYBOARD_BOOL(slowKeys, setSlowKeys, "SlowKeys", false)
RAYE_KEYBOARD_BOOL(bounceKeys, setBounceKeys, "BounceKeys", false)
RAYE_KEYBOARD_BOOL(toggleKeys, setToggleKeys, "ToggleKeysBeep", false)

#undef RAYE_KEYBOARD_BOOL

bool AccessSettings::underlineAccessKeys() const
{
    return KSharedConfig::openConfig(QStringLiteral("rayerc"))->group(QStringLiteral("Style")).readEntry("MnemonicsMode", QString())
        == QLatin1String("MN_ALWAYS");
}

void AccessSettings::setUnderlineAccessKeys(bool on)
{
    if (on == underlineAccessKeys())
        return;
    writeNotify(KSharedConfig::openConfig(QStringLiteral("rayerc")), QStringLiteral("Style"), QStringLiteral("MnemonicsMode"),
                on ? QStringLiteral("MN_ALWAYS") : QStringLiteral("MN_AUTO"));
    // every app with the Raye style reads it again on this signal
    QDBusConnection::sessionBus().send(QDBusMessage::createSignal(QStringLiteral("/RayeStyle"), QStringLiteral("org.raye.Style"),
                                                                  QStringLiteral("reparseConfiguration")));
    Q_EMIT changed();
}

bool AccessSettings::filterKeys() const
{
    return slowKeys() || bounceKeys();
}

void AccessSettings::setFilterKeys(bool on)
{
    if (on == filterKeys())
        return;
    if (on) {
        // as Windows: ignore repeated keystrokes to begin with
        write(QStringLiteral("Keyboard"), QStringLiteral("BounceKeys"), true);
    } else {
        KConfigGroup g = m_access->group(QStringLiteral("Keyboard"));
        g.writeEntry("SlowKeys", false, KConfig::Notify);
        write(QStringLiteral("Keyboard"), QStringLiteral("BounceKeys"), false);
    }
}

bool AccessSettings::mouseKeys() const
{
    return readBool(QStringLiteral("Mouse"), QStringLiteral("MouseKeys"), false);
}

void AccessSettings::setMouseKeys(bool on)
{
    if (on != mouseKeys())
        write(QStringLiteral("Mouse"), QStringLiteral("MouseKeys"), on);
}

int AccessSettings::speedToMaxSpeed(int level)
{
    return std::clamp(level, 1, 10) * 10;
}

int AccessSettings::maxSpeedToSpeed(int maxSpeed)
{
    return std::clamp(qRound(maxSpeed / 10.0), 1, 10);
}

int AccessSettings::accelerationToTime(int level)
{
    // faster acceleration = less time to reach the top speed
    return (11 - std::clamp(level, 1, 10)) * 10;
}

int AccessSettings::timeToAcceleration(int time)
{
    return std::clamp(11 - qRound(time / 10.0), 1, 10);
}

int AccessSettings::mouseKeysSpeed() const
{
    return maxSpeedToSpeed(m_access->group(QStringLiteral("Mouse")).readEntry("MKMaxSpeed", 30));
}

void AccessSettings::setMouseKeysSpeed(int level)
{
    level = std::clamp(level, 1, 10);
    if (level != mouseKeysSpeed())
        write(QStringLiteral("Mouse"), QStringLiteral("MKMaxSpeed"), speedToMaxSpeed(level));
}

int AccessSettings::mouseKeysAcceleration() const
{
    return timeToAcceleration(m_access->group(QStringLiteral("Mouse")).readEntry("MKTimeToMax", 30));
}

void AccessSettings::setMouseKeysAcceleration(int level)
{
    level = std::clamp(level, 1, 10);
    if (level != mouseKeysAcceleration())
        write(QStringLiteral("Mouse"), QStringLiteral("MKTimeToMax"), accelerationToTime(level));
}

void AccessSettings::readKeyboard()
{
    if (!sessionRunning())
        return;
    QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualKeyboard"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    get << QStringLiteral("org.kde.kwin.VirtualKeyboard");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(get, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError())
            return;
        m_oskAvailable = reply.value().value(QStringLiteral("available")).toBool();
        m_oskEnabled = reply.value().value(QStringLiteral("enabled")).toBool();
        Q_EMIT keyboardChanged();
    });
    finishLate(watcher);
}

void AccessSettings::setOnScreenKeyboard(bool on)
{
    if (!m_oskAvailable || on == m_oskEnabled)
        return;
    QDBusMessage set = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualKeyboard"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    set << QStringLiteral("org.kde.kwin.VirtualKeyboard") << QStringLiteral("enabled") << QVariant::fromValue(QDBusVariant(on));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(set, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        readKeyboard();
    });
    finishLate(watcher);
    m_oskEnabled = on;
    Q_EMIT keyboardChanged();
}
