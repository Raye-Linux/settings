// SPDX-License-Identifier: GPL-3.0-or-later
// The blurred backdrop of the Settings window: the wallpaper shows
// through, blurred, behind a see-through Raye background. When the
// compositor has no blur, or the user turned transparency effects off,
// the window is opaque instead (`translucent` says which). The setting
// is kdeglobals [Raye] TransparencyEffects, followed while the window is
// open.
//
//   WindowEffects {
//       id: effects
//       window: root               // the ApplicationWindow
//       blurBehind: true           // this window wants blur at all
//   }
//   color: effects.translucent ? Theme.backdrop : Theme.backdropSolid

#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QRect>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class WindowEffects : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    // the window to blur behind
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    // what the user wants: blur on (Transparency effects)
    Q_PROPERTY(bool blurBehind READ blurBehind WRITE setBlurBehind NOTIFY blurBehindChanged)
    // the part of the window to blur, the whole window when empty
    Q_PROPERTY(QRect region READ region WRITE setRegion NOTIFY regionChanged)
    // Transparency effects is on (Settings > Personalization > Colors)
    Q_PROPERTY(bool transparencyEffects READ transparencyEffects NOTIFY translucentChanged)
    // the compositor can blur
    Q_PROPERTY(bool blurAvailable READ blurAvailable NOTIFY translucentChanged)
    // blur is on: paint the see-through backdrop, else the opaque one
    Q_PROPERTY(bool translucent READ translucent NOTIFY translucentChanged)

public:
    explicit WindowEffects(QObject *parent = nullptr);
    ~WindowEffects() override;

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    bool blurBehind() const { return m_blurBehind; }
    void setBlurBehind(bool blur);
    QRect region() const { return m_region; }
    void setRegion(const QRect &region);
    bool blurAvailable() const { return m_available; }
    bool transparencyEffects() const;
    bool translucent() const { return m_translucent; }

    // Look again (the compositor may have turned blur on or off).
    Q_INVOKABLE void refresh();

    void classBegin() override { }
    void componentComplete() override;

    // How the compositor is asked; the tests hand in their own.
    struct Backend {
        virtual ~Backend() = default;
        virtual bool available() const = 0;
        virtual void setBlur(QWindow *window, bool enable, const QRect &region) = 0;
    };
    // Takes ownership; nullptr goes back to KWindowEffects.
    void setBackend(Backend *backend);

Q_SIGNALS:
    void windowChanged();
    void blurBehindChanged();
    void regionChanged();
    void translucentChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void apply();
    void settingChanged();

    QPointer<QWindow> m_window;
    Backend *m_backend = nullptr;
    QRect m_region;
    KSharedConfigPtr m_globals;
    KConfigWatcher::Ptr m_watcher;
    bool m_blurBehind = true;
    bool m_available = false;
    bool m_translucent = false;
    bool m_complete = false;
    // blur was asked for on this window, so turning it off must be said
    bool m_applied = false;
};
