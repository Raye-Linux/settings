// SPDX-License-Identifier: GPL-3.0-or-later
#include "clipboard.h"

#include <KConfigGroup>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>

namespace {
const QString general = QStringLiteral("General");
}

ClipboardSettings::ClipboardSettings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("klipperrc")))
{
    reload();
}

void ClipboardSettings::reload()
{
    m_config->reparseConfiguration();
    Q_EMIT changed();
}

bool ClipboardSettings::history() const
{
    // the clipboard service keeps 20 by default; 1 is only what is copied now
    return m_config->group(general).readEntry("MaxClipItems", 20) > 1;
}

void ClipboardSettings::setHistory(bool on)
{
    if (on == history())
        return;
    KConfigGroup g = m_config->group(general);
    g.writeEntry("MaxClipItems", on ? historySize : 1, KConfig::Notify);
    // off: nothing copied is kept on disk either
    g.writeEntry("KeepClipboardContents", on, KConfig::Notify);
    m_config->sync();
    callKlipper(QStringLiteral("reloadConfig"), false);
    // as in Windows, turning the history off forgets it
    if (!on)
        callKlipper(QStringLiteral("clearClipboardHistory"), false);
    Q_EMIT changed();
}

void ClipboardSettings::clear()
{
    if (clearing())
        return;
    m_clearFailed = false;
    m_clearing = 2;
    Q_EMIT changed();
    callKlipper(QStringLiteral("clearClipboardHistory"), true);
    callKlipper(QStringLiteral("clearClipboardContents"), true);
}

void ClipboardSettings::callKlipper(const QString &method, bool clearStep)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.klipper"), QStringLiteral("/klipper"),
                                                             QStringLiteral("org.kde.klipper.klipper"), method);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, clearStep](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (!clearStep)
            return;
        m_clearFailed = m_clearFailed || w->isError();
        if (--m_clearing > 0)
            return;
        Q_EMIT changed();
        Q_EMIT cleared(!m_clearFailed);
    });
    // without a session bus the call has already failed, and the
    // watcher would never say so
    if (watcher->isFinished())
        QMetaObject::invokeMethod(watcher, [watcher] { Q_EMIT watcher->finished(watcher); }, Qt::QueuedConnection);
}
