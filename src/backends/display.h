// SPDX-License-Identifier: GPL-3.0-or-later
// What System › Display changes: the screens and how they sit next to
// each other, and for the chosen screen its resolution, refresh rate,
// scale, orientation, HDR and adaptive sync. Read and applied through
// libkscreen, the library Plasma's own display settings use.
//
// A change that can leave the screen black (resolution, refresh rate,
// orientation, which screens show the desktop) asks "Keep these display
// settings?" and goes back by itself after 15 seconds, as Windows does.
// Scale and arrangement apply at once.
#pragma once

#include <KScreen/Types>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

// The logic of the page, apart from libkscreen's objects, so the tests
// can check it on plain values.
namespace DisplayLogic
{
struct Resolution {
    QSize size;
    bool recommended = false;
};
// Every resolution of the modes, largest first, the preferred one marked.
QList<Resolution> resolutions(const KScreen::ModeList &modes, const QString &preferredModeId);
// The refresh rates of one resolution, highest first, in Hz.
QList<double> refreshRates(const KScreen::ModeList &modes, const QSize &size);
// "60 Hz", "59.94 Hz", "143.98 Hz"
QString refreshText(double hz);
// "1920 × 1080", with " (Recommended)" for the preferred one
QString resolutionText(const QSize &size, bool recommended);
// The mode for a resolution and a rate: the closest rate when the
// exact one is missing.
QString modeFor(const KScreen::ModeList &modes, const QSize &size, double hz);

// The scale Raye suggests for a screen, in percent: the pixel density
// over 110 dpi (a desk screen seen from arm's length), to the nearest
// 25 %, from 100 to 300. No physical size known: 100.
int recommendedScale(const QSize &pixels, const QSize &millimetres);
// The scales the list offers: 100 to 300 by 25, plus the one in use.
QList<int> scaleSteps(int current);

// KScreen rotation <-> degrees (0, 90, 180, 270)
int degreesOf(int rotation);
int rotationOf(int degrees);

// Where a screen dropped at `dropped` lands next to the others: against
// the nearest free edge of the closest screen, never over one, with the
// whole arrangement moved so it starts at 0,0. Returns every rectangle,
// in the order given.
QList<QRect> placeDropped(const QList<QRect> &screens, int moving, const QRect &dropped);
// Moves all rectangles so the top-left one starts at 0,0.
QList<QRect> normalized(QList<QRect> screens);
}

class DisplayConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the configuration was read
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    // [{ id, number, name, enabled, primary, x, y, width, height, builtIn }]
    // for every connected screen, numbered from 1 as the system sees them
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    // the screen the settings below are about, by output id
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY changed)
    Q_PROPERTY(QVariantList resolutions READ resolutions NOTIFY changed)   // [{ text, width, height, recommended }]
    Q_PROPERTY(int resolutionIndex READ resolutionIndex NOTIFY changed)
    Q_PROPERTY(QVariantList refreshRates READ refreshRates NOTIFY changed) // [{ text, hz }]
    Q_PROPERTY(int refreshIndex READ refreshIndex NOTIFY changed)
    Q_PROPERTY(int scale READ scale NOTIFY changed)                        // percent
    Q_PROPERTY(int recommendedScale READ recommendedScale NOTIFY changed)
    Q_PROPERTY(QVariantList scales READ scales NOTIFY changed)             // [{ text, value }]
    Q_PROPERTY(int orientation READ orientation NOTIFY changed)            // degrees
    Q_PROPERTY(bool hdrCapable READ hdrCapable NOTIFY changed)
    Q_PROPERTY(bool hdrEnabled READ hdrEnabled NOTIFY changed)
    Q_PROPERTY(int sdrBrightness READ sdrBrightness NOTIFY changed)        // nits
    Q_PROPERTY(bool vrrCapable READ vrrCapable NOTIFY changed)
    Q_PROPERTY(bool vrrEnabled READ vrrEnabled NOTIFY changed)
    // "extend", "duplicate", "only" (one screen shows the desktop)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    // a change waits for "Keep changes"; seconds left before it goes back
    Q_PROPERTY(bool confirming READ confirming NOTIFY confirmingChanged)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY confirmingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit DisplayConfig(QObject *parent = nullptr);
    // For the tests: a configuration made by hand, and what "apply" does
    // with it (nothing reaches a real screen).
    // `apply` returns false when the configuration cannot be used.
    DisplayConfig(const KScreen::ConfigPtr &config, std::function<bool(const KScreen::ConfigPtr &)> apply,
                  QObject *parent = nullptr);
    ~DisplayConfig() override;

    bool ready() const { return !m_config.isNull(); }
    QVariantList outputs() const;
    int selected() const { return m_selected; }
    void setSelected(int outputId);
    QVariantList resolutions() const;
    int resolutionIndex() const;
    QVariantList refreshRates() const;
    int refreshIndex() const;
    int scale() const;
    int recommendedScale() const;
    QVariantList scales() const;
    int orientation() const;
    bool hdrCapable() const;
    bool hdrEnabled() const;
    int sdrBrightness() const;
    bool vrrCapable() const;
    bool vrrEnabled() const;
    QString mode() const;
    bool confirming() const { return m_secondsLeft > 0; }
    int secondsLeft() const { return m_secondsLeft; }
    QString error() const { return m_error; }

    // Changes that ask to be kept.
    Q_INVOKABLE void setResolution(int index);
    Q_INVOKABLE void setRefreshRate(int index);
    Q_INVOKABLE void setOrientation(int degrees);
    // "extend", "duplicate", or "only" with the output id that stays on
    Q_INVOKABLE void setMode(const QString &mode, int outputId = -1);
    // Changes that apply at once.
    Q_INVOKABLE void setScale(int percent);
    Q_INVOKABLE void setHdrEnabled(bool on);
    Q_INVOKABLE void setSdrBrightness(int nits);
    Q_INVOKABLE void setVrrEnabled(bool on);
    Q_INVOKABLE void makePrimary(int outputId);
    // A screen dropped in the arrangement at x, y (logical pixels).
    Q_INVOKABLE void moveOutput(int outputId, int x, int y);

    Q_INVOKABLE void keepChanges();
    Q_INVOKABLE void revertChanges();
    // One second of the countdown (the timer calls it; tests too).
    void tick();

    static constexpr int confirmSeconds = 15;

Q_SIGNALS:
    void changed();
    void confirmingChanged();
    void errorChanged();

private:
    void load();
    void apply(bool needsConfirmation);
    void rememberBefore();
    // Puts the output at `where`, snapped to the others; false if it is not on.
    bool place(int outputId, const QPoint &where);
    void setError(const QString &error);
    KScreen::OutputPtr selectedOutput() const;
    QList<KScreen::OutputPtr> connected() const;

    KScreen::ConfigPtr m_config;
    // the configuration to go back to while a change waits to be kept
    KScreen::ConfigPtr m_before;
    std::function<bool(const KScreen::ConfigPtr &)> m_apply;
    int m_selected = -1;
    int m_secondsLeft = 0;
    QTimer m_confirm;
    QString m_error;
};
