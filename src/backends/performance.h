// SPDX-License-Identifier: GPL-3.0-or-later
// System > Performance options: the visual effects that cost the most on
// a slow PC, one by one, and the four choices Windows offers (let Raye
// choose, best appearance, best performance, custom).
//
//   animations     kdeglobals [KDE] AnimationDurationFactor (0 = off);
//                  everything that moves follows it, window manager too
//   window-open    kwinrc [Plugins] raye_windowsEnabled (Raye's own effect;
//                  the stock scale one is shipped off)
//   minimize       kwinrc [Plugins] squashEnabled, raye_maximizeEnabled
//   menus          kwinrc [Plugins] raye_popupsEnabled, slidingpopupsEnabled
//   desktops       kwinrc [Plugins] slideEnabled
//   transparency   kdeglobals [Raye] TransparencyEffects, kwinrc [Plugins]
//                  blurEnabled, contrastEnabled (the same as Colours)
//   shadows        rayerc [Common] ShadowSize=ShadowNone (window borders
//                  and the menus of the Raye style)
//
// "On" puts the system's own value back (the user's key is removed), so a
// later Raye update can still tune it. The choice made is kept in
// kdeglobals [Raye] VisualEffectsMode; what is shown is worked out from
// the files, so a switch flipped on another page reads as Custom.
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class PerformanceSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "auto", "appearance", "performance" or "custom"
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    // this PC runs without a graphics card the desktop can use, or with
    // little memory: "auto" turns the effects off
    Q_PROPERTY(bool lowEnd READ lowEnd NOTIFY changed)
    Q_PROPERTY(bool animations READ animations NOTIFY changed)
    Q_PROPERTY(bool windowOpen READ windowOpen NOTIFY changed)
    Q_PROPERTY(bool minimize READ minimize NOTIFY changed)
    Q_PROPERTY(bool menus READ menus NOTIFY changed)
    Q_PROPERTY(bool desktops READ desktops NOTIFY changed)
    Q_PROPERTY(bool transparency READ transparency NOTIFY changed)
    Q_PROPERTY(bool shadows READ shadows NOTIFY changed)

public:
    explicit PerformanceSettings(QObject *parent = nullptr);

    // the effects, in the order the page lists them
    static const QStringList effectIds;
    // below this much memory Raye picks speed; a "4 GB" PC reports a
    // little less than 4 GiB, so it counts
    static constexpr qint64 lowMemoryKiB = 4000 * 1024;

    QString mode() const;
    bool lowEnd() const { return m_lowEnd; }
    bool animations() const { return effect(QStringLiteral("animations")); }
    bool windowOpen() const { return effect(QStringLiteral("window-open")); }
    bool minimize() const { return effect(QStringLiteral("minimize")); }
    bool menus() const { return effect(QStringLiteral("menus")); }
    bool desktops() const { return effect(QStringLiteral("desktops")); }
    bool transparency() const { return effect(QStringLiteral("transparency")); }
    bool shadows() const { return effect(QStringLiteral("shadows")); }

    Q_INVOKABLE bool effect(const QString &id) const;
    // one effect on or off; the choice becomes Custom
    Q_INVOKABLE void setEffect(const QString &id, bool on);
    // "auto", "appearance" or "performance" set every effect; "custom"
    // only remembers the choice
    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void reload();

    // --- the parts the tests drive ---
    // the choice to show, from the one kept and how many effects are on
    static QString effectiveMode(const QString &stored, int on, int total, bool lowEnd);
    // KWin's support information and MemTotal from /proc/meminfo
    static bool isLowEnd(const QString &supportInfo, qint64 memoryKiB);
    // Transparency effects, written the same way from every page
    static void writeTransparency(bool on);
    // set the low-end answer (tests; the window asks KWin)
    void setLowEnd(bool lowEnd);

Q_SIGNALS:
    void changed();

private:
    void write(const QString &id, bool on);
    void tell();
    void askCompositor();
    static qint64 memoryKiB();

    KSharedConfigPtr m_globals;
    KSharedConfigPtr m_kwin;
    KSharedConfigPtr m_raye;
    bool m_lowEnd = false;
};
