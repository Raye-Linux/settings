// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > the theme, Colours and Themes: Raye Night or Raye
// Day, the accent colour, transparency and the accent on window borders.
//
//  - A theme is a look-and-feel package (plasma/look-and-feel/<id>),
//    applied with `plasma-apply-lookandfeel -a <id>`: colours, style,
//    icons, cursor, window borders. Without --resetLayout, so the taskbar
//    and the desktop stay as the user left them. A theme writes its own
//    accent colour: a colour the user chose is put back right after.
//  - "Choose your mode": Dark is Raye Night, Light is Raye Day.
//  - Accent: raye-accent (from raye-theme) writes Raye Night or Raye Day
//    again with every accent shade taken from the theme's rule, and no
//    [General] AccentColor, which would make Plasma blend the accent
//    over the scheme. Outside Raye's schemes it falls back to
//    `plasma-apply-colorscheme --accent-color`. The accent shown is
//    AccentColor when Plasma set one, the selection colour otherwise.
//    [General] accentColorFromWallpaper is "Automatic".
//  - Transparency effects: kdeglobals [Raye] TransparencyEffects (Raye's
//    own windows follow it) and the compositor's blur and contrast
//    effects (kwinrc [Plugins]), which also makes the shell draw opaque.
//  - Accent on title bars and window borders: the Raye window border's
//    rayerc [Windeco] AccentBorder.
#pragma once

#include <KConfigGroup>
#include <QColor>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppearanceSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{ id, name, description, preview, current, raye }], Raye's own first
    Q_PROPERTY(QVariantList themes READ themes NOTIFY changed)
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY changed)
    // "dark" or "light"
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY changed)
    Q_PROPERTY(bool accentFromWallpaper READ accentFromWallpaper WRITE setAccentFromWallpaper NOTIFY changed)
    Q_PROPERTY(bool transparency READ transparency WRITE setTransparency NOTIFY changed)
    Q_PROPERTY(bool accentOnBorders READ accentOnBorders WRITE setAccentOnBorders NOTIFY changed)
    // the window borders are Raye's (the accent border is theirs)
    Q_PROPERTY(bool rayeBorders READ rayeBorders NOTIFY changed)
    // a theme is being applied
    Q_PROPERTY(bool applying READ applying NOTIFY changed)
    // the 24 accent colours offered, Raye violet first
    Q_PROPERTY(QVariantList swatches READ swatches CONSTANT)

public:
    explicit AppearanceSettings(QObject *parent = nullptr);

    QVariantList themes() const { return m_themes; }
    QString currentTheme() const;
    QString mode() const;
    QColor accentColor() const;
    bool accentFromWallpaper() const;
    void setAccentFromWallpaper(bool automatic);
    bool transparency() const;
    void setTransparency(bool on);
    bool accentOnBorders() const;
    void setAccentOnBorders(bool on);
    bool rayeBorders() const;
    bool applying() const { return m_applying; }
    QVariantList swatches() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool applyTheme(const QString &id);
    Q_INVOKABLE bool setMode(const QString &mode);
    Q_INVOKABLE void setAccentColor(const QColor &color);
    // the swatch nearest to a colour: the one to mark as chosen, -1 when none is close
    Q_INVOKABLE int swatchIndex(const QColor &color) const;

    // --- the parts the tests drive ---
    static QStringList swatchColors();
    static QVariantList scanThemes(const QStringList &folders, const QString &current);
    // "dark" or "light" for a colour scheme name and its window colour
    static QString modeOf(const QString &scheme, const QColor &window);
    static QString themeForMode(const QString &mode);
    // what the accent is for a kdeglobals: AccentColor, else the selection
    static QColor accentOf(const KConfigGroup &general, const KConfigGroup &selection);

Q_SIGNALS:
    void changed();

private:
    void applyAccent(const QColor &color);
    void reconfigureCompositor();

    QVariantList m_themes;
    bool m_applying = false;
};
