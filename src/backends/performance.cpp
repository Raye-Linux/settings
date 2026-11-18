// SPDX-License-Identifier: GPL-3.0-or-later
#include "performance.h"

#include <KConfigGroup>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QRegularExpression>

namespace {

const QString plugins = QStringLiteral("Plugins");

// the window manager effects behind each switch
QStringList kwinEffects(const QString &id)
{
    if (id == QLatin1String("window-open"))
        return {QStringLiteral("scaleEnabled")};
    if (id == QLatin1String("minimize"))
        return {QStringLiteral("squashEnabled"), QStringLiteral("raye_maximizeEnabled")};
    if (id == QLatin1String("menus"))
        return {QStringLiteral("raye_popupsEnabled"), QStringLiteral("slidingpopupsEnabled")};
    if (id == QLatin1String("desktops"))
        return {QStringLiteral("slideEnabled")};
    if (id == QLatin1String("transparency"))
        return {QStringLiteral("blurEnabled"), QStringLiteral("contrastEnabled")};
    return {};
}

void writePlugins(const KSharedConfigPtr &kwin, const QStringList &keys, bool on)
{
    KConfigGroup g = kwin->group(plugins);
    for (const QString &key : keys) {
        // on: back to the system's default (on in Raye)
        if (on)
            g.deleteEntry(key, KConfig::Notify);
        else
            g.writeEntry(key, false, KConfig::Notify);
    }
    kwin->sync();
}

} // namespace

const QStringList PerformanceSettings::effectIds = {
    QStringLiteral("animations"), QStringLiteral("window-open"), QStringLiteral("minimize"), QStringLiteral("menus"),
    QStringLiteral("desktops"),   QStringLiteral("transparency"), QStringLiteral("shadows"),
};

PerformanceSettings::PerformanceSettings(QObject *parent)
    : QObject(parent)
    , m_globals(KSharedConfig::openConfig(QStringLiteral("kdeglobals")))
    , m_kwin(KSharedConfig::openConfig(QStringLiteral("kwinrc")))
    , m_raye(KSharedConfig::openConfig(QStringLiteral("rayerc")))
{
    m_lowEnd = isLowEnd(QString(), memoryKiB());
    askCompositor();
}

void PerformanceSettings::reload()
{
    m_globals->reparseConfiguration();
    m_kwin->reparseConfiguration();
    m_raye->reparseConfiguration();
    Q_EMIT changed();
}

void PerformanceSettings::setLowEnd(bool lowEnd)
{
    if (m_lowEnd == lowEnd)
        return;
    m_lowEnd = lowEnd;
    Q_EMIT changed();
}

bool PerformanceSettings::effect(const QString &id) const
{
    if (id == QLatin1String("animations"))
        return m_globals->group(QStringLiteral("KDE")).readEntry("AnimationDurationFactor", 1.0) > 0;
    if (id == QLatin1String("transparency")) {
        if (!m_globals->group(QStringLiteral("Raye")).readEntry("TransparencyEffects", true))
            return false;
    } else if (id == QLatin1String("shadows")) {
        // the window border writes the name, an older file may hold the number
        const QString size = m_raye->group(QStringLiteral("Common")).readEntry("ShadowSize", QString());
        return size != QLatin1String("ShadowNone") && size != QLatin1String("0");
    }
    const QStringList keys = kwinEffects(id);
    if (keys.isEmpty())
        return false;
    const KConfigGroup g = m_kwin->group(plugins);
    for (const QString &key : keys) {
        if (!g.readEntry(key, true))
            return false;
    }
    return true;
}

void PerformanceSettings::write(const QString &id, bool on)
{
    if (id == QLatin1String("animations")) {
        KConfigGroup g = m_globals->group(QStringLiteral("KDE"));
        g.writeEntry("AnimationDurationFactor", on ? 1.0 : 0.0, KConfig::Notify);
        m_globals->sync();
    } else if (id == QLatin1String("transparency")) {
        writeTransparency(on);
    } else if (id == QLatin1String("shadows")) {
        KConfigGroup g = m_raye->group(QStringLiteral("Common"));
        if (on)
            g.deleteEntry("ShadowSize", KConfig::Notify);
        else
            g.writeEntry("ShadowSize", QStringLiteral("ShadowNone"), KConfig::Notify);
        m_raye->sync();
    } else {
        writePlugins(m_kwin, kwinEffects(id), on);
    }
}

void PerformanceSettings::writeTransparency(bool on)
{
    KSharedConfigPtr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    KConfigGroup raye = globals->group(QStringLiteral("Raye"));
    raye.writeEntry("TransparencyEffects", on, KConfig::Notify);
    globals->sync();
    // Raye's surfaces follow the key; without the compositor's blur and
    // contrast Plasma's panels and menus are drawn opaque too
    writePlugins(KSharedConfig::openConfig(QStringLiteral("kwinrc")), kwinEffects(QStringLiteral("transparency")), on);
}

void PerformanceSettings::setEffect(const QString &id, bool on)
{
    if (!effectIds.contains(id) || effect(id) == on)
        return;
    write(id, on);
    KConfigGroup g = m_globals->group(QStringLiteral("Raye"));
    g.writeEntry("VisualEffectsMode", QStringLiteral("custom"), KConfig::Notify);
    m_globals->sync();
    tell();
    Q_EMIT changed();
}

void PerformanceSettings::setMode(const QString &mode)
{
    static const QStringList modes = {QStringLiteral("auto"), QStringLiteral("appearance"), QStringLiteral("performance"),
                                      QStringLiteral("custom")};
    if (!modes.contains(mode))
        return;
    if (mode != QLatin1String("custom")) {
        const bool on = mode == QLatin1String("appearance") || (mode == QLatin1String("auto") && !m_lowEnd);
        for (const QString &id : effectIds) {
            if (effect(id) != on)
                write(id, on);
        }
    }
    KConfigGroup g = m_globals->group(QStringLiteral("Raye"));
    g.writeEntry("VisualEffectsMode", mode, KConfig::Notify);
    m_globals->sync();
    tell();
    Q_EMIT changed();
}

QString PerformanceSettings::mode() const
{
    int on = 0;
    for (const QString &id : effectIds)
        on += effect(id) ? 1 : 0;
    return effectiveMode(m_globals->group(QStringLiteral("Raye")).readEntry("VisualEffectsMode", QString()), on,
                         effectIds.size(), m_lowEnd);
}

QString PerformanceSettings::effectiveMode(const QString &stored, int on, int total, bool lowEnd)
{
    const bool allOn = on == total;
    const bool allOff = on == 0;
    const bool autoState = lowEnd ? allOff : allOn;
    if (stored == QLatin1String("custom"))
        return stored;
    if (stored == QLatin1String("appearance") && allOn)
        return stored;
    if (stored == QLatin1String("performance") && allOff)
        return stored;
    if (stored == QLatin1String("auto"))
        return autoState ? stored : QStringLiteral("custom");
    if (stored == QLatin1String("appearance") || stored == QLatin1String("performance"))
        return QStringLiteral("custom");
    // nothing chosen yet (or a value this version does not know)
    if (autoState)
        return QStringLiteral("auto");
    if (allOn)
        return QStringLiteral("appearance");
    if (allOff)
        return QStringLiteral("performance");
    return QStringLiteral("custom");
}

bool PerformanceSettings::isLowEnd(const QString &supportInfo, qint64 memoryKiB)
{
    if (memoryKiB > 0 && memoryKiB < lowMemoryKiB)
        return true;
    static const QRegularExpression software(QStringLiteral("llvmpipe|softpipe|swrast|software rasterizer"),
                                             QRegularExpression::CaseInsensitiveOption);
    const QStringList lines = supportInfo.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("OpenGL renderer string:")) && line.contains(software))
            return true;
        // no OpenGL at all: every effect is drawn by the processor
        if (line.startsWith(QLatin1String("Compositing Type:")) && line.contains(QLatin1String("QPainter")))
            return true;
    }
    return false;
}

qint64 PerformanceSettings::memoryKiB()
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        if (line.startsWith("MemTotal:"))
            return line.mid(9).trimmed().split(' ').value(0).toLongLong();
    }
    return 0;
}

// KWin knows what draws the desktop; it answers once the window is up
void PerformanceSettings::askCompositor()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                                       QStringLiteral("org.kde.KWin"), QStringLiteral("supportInformation"));
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString> reply = *w;
        w->deleteLater();
        if (!reply.isError())
            setLowEnd(isLowEnd(reply.value(), memoryKiB()));
    });
}

// the window manager loads and unloads effects and reads its file again;
// the window borders and the Raye style reload on the same signal
void PerformanceSettings::tell()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    bus.send(QDBusMessage::createSignal(QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reloadConfig")));
    bus.asyncCall(QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"),
                                                 QStringLiteral("reconfigure")));
}
