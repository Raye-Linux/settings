// SPDX-License-Identifier: GPL-3.0-or-later
#include "windoweffects.h"

#include <KWindowEffects>
#include <QEvent>
#include <QRegion>

namespace {

struct SystemBackend : WindowEffects::Backend {
    bool available() const override
    {
        return KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
    }
    void setBlur(QWindow *window, bool enable, const QRect &region) override
    {
        KWindowEffects::enableBlurBehind(window, enable, region.isEmpty() ? QRegion() : QRegion(region));
    }
};

} // namespace

WindowEffects::WindowEffects(QObject *parent)
    : QObject(parent)
    , m_backend(new SystemBackend)
{
}

WindowEffects::~WindowEffects()
{
    delete m_backend;
}

void WindowEffects::setBackend(Backend *backend)
{
    delete m_backend;
    m_backend = backend ? backend : new SystemBackend;
    apply();
}

void WindowEffects::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    if (m_window) {
        m_window->removeEventFilter(this);
        if (m_applied)
            m_backend->setBlur(m_window, false, QRect());
        m_applied = false;
    }
    m_window = window;
    // a compositor may only answer for a window on screen: ask again
    // when it shows, if blur did not take before
    if (m_window)
        m_window->installEventFilter(this);
    Q_EMIT windowChanged();
    apply();
}

void WindowEffects::setBlurBehind(bool blur)
{
    if (m_blurBehind == blur)
        return;
    m_blurBehind = blur;
    Q_EMIT blurBehindChanged();
    apply();
}

void WindowEffects::setRegion(const QRect &region)
{
    if (m_region == region)
        return;
    m_region = region;
    Q_EMIT regionChanged();
    apply();
}

void WindowEffects::refresh()
{
    apply();
}

void WindowEffects::componentComplete()
{
    m_complete = true;
    apply();
}

bool WindowEffects::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && (event->type() == QEvent::Expose || event->type() == QEvent::Show)) {
        if (m_window->isExposed() && !m_translucent && m_blurBehind)
            apply();
    }
    return QObject::eventFilter(watched, event);
}

void WindowEffects::apply()
{
    if (!m_complete)
        return;
    const bool available = m_backend->available();
    const bool want = m_window && m_blurBehind && available;
    if (m_window && (want || m_applied)) {
        m_backend->setBlur(m_window, want, m_region);
        m_applied = want;
    }
    const bool translucent = want;
    if (available != m_available || translucent != m_translucent) {
        m_available = available;
        m_translucent = translucent;
        Q_EMIT translucentChanged();
    }
}
