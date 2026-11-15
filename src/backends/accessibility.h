// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility: the settings of other programs these pages change, with
// the key names of the desktop's own modules (Plasma 6.6), so the two
// agree. Every change is written at once (KConfig's Notify flag), and the
// program that reads it is told.
//   kdeglobals [General] font, menuFont, toolBarFont, smallestReadableFont,
//              fixed; [WM] activeFont          text size, scaled together
//   kdeglobals [Raye] TextSize                 the size picked (100-225 %)
//   kdeglobals [Raye] TransparencyEffects, kwinrc [Plugins] blurEnabled,
//              contrastEnabled                 transparency (as Colours)
//   kdeglobals [KDE] AnimationDurationFactor   animations (0 = off)
//   rayeshellrc [Notifications] ToastSeconds   notifications stay this long
//   kcminputrc [Mouse] cursorTheme, cursorSize pointer
//   kwinrc [Plugins] shakecursorEnabled, zoomEnabled, magnifierEnabled,
//              colorblindnesscorrectionEnabled
//   kwinrc [Effect-zoom] ZoomFactor, MouseTracking, EnableFocusTracking,
//              EnableTextCaretTracking
//   kwinrc [Effect-colorblindnesscorrection] Mode, Intensity
//   kaccessrc [ScreenReader] Enabled; [Bell] VisibleBell;
//              [Keyboard] StickyKeys, StickyKeysLatch, StickyKeysAutoOff,
//              StickyKeysBeep, Gestures, SlowKeys, BounceKeys,
//              ToggleKeysBeep; [Mouse] MouseKeys, MKMaxSpeed, MKTimeToMax
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Accessibility > Text size: every font of the desktop at one size, the
// way the fonts module's "Adjust all fonts" does it.
class TextSizeSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the size in use, in percent
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    // the point size of the main font at 100 %, for the preview
    Q_PROPERTY(qreal basePointSize READ basePointSize NOTIFY changed)
    Q_PROPERTY(QString family READ family NOTIFY changed)

public:
    static constexpr int minPercent = 100;
    static constexpr int maxPercent = 225;

    explicit TextSizeSettings(QObject *parent = nullptr);

    int percent() const;
    qreal basePointSize() const;
    QString family() const;

    Q_INVOKABLE void reload();
    // scale every font to `percent` (rounded to 5 %, 100 to 225)
    Q_INVOKABLE void apply(int percent);

    // a font as kdeglobals writes it, at the size it would have at `to`
    // percent when it is at `from` percent now; sizes keep one decimal
    static QString scaledFont(const QString &font, int from, int to);
    static int clampPercent(int percent);
    // the font keys, as "group/key"
    static const QStringList fontKeys;

Q_SIGNALS:
    void changed();

private:
    KSharedConfigPtr m_globals;
};

// Accessibility > Visual effects.
class VisualEffectsSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool transparency READ transparency WRITE setTransparency NOTIFY changed)
    Q_PROPERTY(bool animations READ animations WRITE setAnimations NOTIFY changed)
    Q_PROPERTY(int toastSeconds READ toastSeconds WRITE setToastSeconds NOTIFY changed)
    // the shell reads ToastSeconds (its key is in the system defaults)
    Q_PROPERTY(bool toastAvailable READ toastAvailable NOTIFY changed)

public:
    // what "Dismiss notifications after" offers, as Windows does
    static const QList<int> toastChoices;

    explicit VisualEffectsSettings(QObject *parent = nullptr);

    bool transparency() const;
    void setTransparency(bool on);
    bool animations() const;
    void setAnimations(bool on);
    int toastSeconds() const;
    void setToastSeconds(int seconds);
    bool toastAvailable() const;

    Q_INVOKABLE void reload();
    // the place of `seconds` in toastChoices (the nearest one)
    Q_INVOKABLE static int toastIndex(int seconds);

Q_SIGNALS:
    void changed();

private:
    KSharedConfigPtr m_globals;
    KSharedConfigPtr m_kwin;
    KSharedConfigPtr m_shell;
};

// Accessibility > Mouse pointer and touch.
class PointerSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{ id: "Raye", name, preview: file URL of its arrow or "" }], Raye's first
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themesChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(int size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(bool shakeToFind READ shakeToFind WRITE setShakeToFind NOTIFY changed)

public:
    static constexpr int minSize = 24;
    static constexpr int maxSize = 96;
    static constexpr int defaultSize = 24;

    struct Theme {
        QString id;
        QString name;
        QString preview;
    };

    explicit PointerSettings(QObject *parent = nullptr);

    QVariantList themes() const;
    QString theme() const;
    void setTheme(const QString &id);
    int size() const;
    void setSize(int size);
    bool shakeToFind() const;
    void setShakeToFind(bool on);

    Q_INVOKABLE void reload();

    // the cursor themes in the icon folders (a "cursors" folder each),
    // Raye's first, then by name; the user's own copy wins
    static QList<Theme> findThemes();

Q_SIGNALS:
    void changed();
    void themesChanged();

private:
    void apply(const QString &theme, int size);

    KSharedConfigPtr m_input;
    KSharedConfigPtr m_kwin;
    QList<Theme> m_themes;
};

// Accessibility > Magnifier: the window manager's zoom (full screen) or
// magnifier (lens) effect; Windows key + Plus zooms in.
class MagnifierSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // 0 full screen, 1 lens
    Q_PROPERTY(int view READ view WRITE setView NOTIFY changed)
    // the place of the zoom step in zoomSteps
    Q_PROPERTY(int zoomStep READ zoomStep WRITE setZoomStep NOTIFY changed)
    // 0 proportional, 1 centred, 2 within the edges (the effect's numbers)
    Q_PROPERTY(int tracking READ tracking WRITE setTracking NOTIFY changed)
    Q_PROPERTY(bool followPointer READ followPointer WRITE setFollowPointer NOTIFY changed)
    Q_PROPERTY(bool followFocus READ followFocus WRITE setFollowFocus NOTIFY changed)
    Q_PROPERTY(bool followTextCursor READ followTextCursor WRITE setFollowTextCursor NOTIFY changed)

public:
    // each press of Windows key + Plus multiplies the zoom by one of these
    static const QList<double> zoomSteps;

    explicit MagnifierSettings(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool on);
    int view() const;
    void setView(int view);
    int zoomStep() const;
    void setZoomStep(int index);
    int tracking() const;
    void setTracking(int tracking);
    bool followPointer() const;
    void setFollowPointer(bool on);
    bool followFocus() const;
    void setFollowFocus(bool on);
    bool followTextCursor() const;
    void setFollowTextCursor(bool on);

    Q_INVOKABLE void reload();
    static int stepIndex(double factor);

Q_SIGNALS:
    void changed();

private:
    KSharedConfigPtr m_kwin;
    // the way the pointer was followed before "Follow" was turned off
    int m_lastTracking = 1;
};

// Accessibility > Colour filters.
class ColorFilterSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // 0 red-green (protanopia), 1 red-green (deuteranopia),
    // 2 blue-yellow (tritanopia), 3 greyscale
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    // 0 to 100
    Q_PROPERTY(int intensity READ intensity WRITE setIntensity NOTIFY changed)

public:
    static constexpr int modeCount = 4;

    explicit ColorFilterSettings(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool on);
    int mode() const;
    void setMode(int mode);
    int intensity() const;
    void setIntensity(int percent);

    Q_INVOKABLE void reload();

Q_SIGNALS:
    void changed();

private:
    KSharedConfigPtr m_kwin;
};

// Accessibility > Screen reader, Audio, Keyboard and Mouse: the
// accessibility service (kaccess and the window manager's keyboard
// helpers) and the screen reader.
class AccessSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool screenReader READ screenReader WRITE setScreenReader NOTIFY changed)
    Q_PROPERTY(bool screenReaderInstalled READ screenReaderInstalled CONSTANT)
    Q_PROPERTY(bool visibleBell READ visibleBell WRITE setVisibleBell NOTIFY changed)
    Q_PROPERTY(bool stickyKeys READ stickyKeys WRITE setStickyKeys NOTIFY changed)
    Q_PROPERTY(bool stickyKeysLatch READ stickyKeysLatch WRITE setStickyKeysLatch NOTIFY changed)
    Q_PROPERTY(bool stickyKeysAutoOff READ stickyKeysAutoOff WRITE setStickyKeysAutoOff NOTIFY changed)
    Q_PROPERTY(bool stickyKeysBeep READ stickyKeysBeep WRITE setStickyKeysBeep NOTIFY changed)
    // Shift five times turns sticky keys on (and the other key gestures)
    Q_PROPERTY(bool gestures READ gestures WRITE setGestures NOTIFY changed)
    // filter keys = slow keys and/or bounce keys
    Q_PROPERTY(bool filterKeys READ filterKeys WRITE setFilterKeys NOTIFY changed)
    Q_PROPERTY(bool slowKeys READ slowKeys WRITE setSlowKeys NOTIFY changed)
    Q_PROPERTY(bool bounceKeys READ bounceKeys WRITE setBounceKeys NOTIFY changed)
    Q_PROPERTY(bool toggleKeys READ toggleKeys WRITE setToggleKeys NOTIFY changed)
    Q_PROPERTY(bool mouseKeys READ mouseKeys WRITE setMouseKeys NOTIFY changed)
    // 1 to 10
    Q_PROPERTY(int mouseKeysSpeed READ mouseKeysSpeed WRITE setMouseKeysSpeed NOTIFY changed)
    Q_PROPERTY(int mouseKeysAcceleration READ mouseKeysAcceleration WRITE setMouseKeysAcceleration NOTIFY changed)
    // the on-screen keyboard (the window manager's virtual keyboard)
    Q_PROPERTY(bool onScreenKeyboardAvailable READ onScreenKeyboardAvailable NOTIFY keyboardChanged)
    Q_PROPERTY(bool onScreenKeyboard READ onScreenKeyboard WRITE setOnScreenKeyboard NOTIFY keyboardChanged)

public:
    explicit AccessSettings(QObject *parent = nullptr);

    bool screenReader() const;
    void setScreenReader(bool on);
    bool screenReaderInstalled() const;
    bool visibleBell() const;
    void setVisibleBell(bool on);
    bool stickyKeys() const;
    void setStickyKeys(bool on);
    bool stickyKeysLatch() const;
    void setStickyKeysLatch(bool on);
    bool stickyKeysAutoOff() const;
    void setStickyKeysAutoOff(bool on);
    bool stickyKeysBeep() const;
    void setStickyKeysBeep(bool on);
    bool gestures() const;
    void setGestures(bool on);
    bool filterKeys() const;
    void setFilterKeys(bool on);
    bool slowKeys() const;
    void setSlowKeys(bool on);
    bool bounceKeys() const;
    void setBounceKeys(bool on);
    bool toggleKeys() const;
    void setToggleKeys(bool on);
    bool mouseKeys() const;
    void setMouseKeys(bool on);
    int mouseKeysSpeed() const;
    void setMouseKeysSpeed(int level);
    int mouseKeysAcceleration() const;
    void setMouseKeysAcceleration(int level);
    bool onScreenKeyboardAvailable() const { return m_oskAvailable; }
    bool onScreenKeyboard() const { return m_oskEnabled; }
    void setOnScreenKeyboard(bool on);

    Q_INVOKABLE void reload();
    // the screen reader's own settings window
    Q_INVOKABLE void openScreenReaderSettings();

    // mouse keys: level 1-10 <-> the service's numbers
    static int speedToMaxSpeed(int level);
    static int maxSpeedToSpeed(int maxSpeed);
    static int accelerationToTime(int level);
    static int timeToAcceleration(int time);

Q_SIGNALS:
    void changed();
    void keyboardChanged();

private:
    bool readBool(const QString &group, const QString &key, bool fallback) const;
    void write(const QString &group, const QString &key, const QVariant &value);
    void readKeyboard();

    KSharedConfigPtr m_access;
    bool m_oskAvailable = false;
    bool m_oskEnabled = false;
};
