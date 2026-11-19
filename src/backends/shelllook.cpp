// SPDX-License-Identifier: GPL-3.0-or-later
#include "shelllook.h"

#include <KConfigGroup>

namespace {
const QString appearanceGroup = QStringLiteral("Appearance");
const QString lookWindows11 = QStringLiteral("windows11");
const QString lookWindows10 = QStringLiteral("windows10");

// the word a value stands for, or "" for one the shell does not know
QString word(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == lookWindows10 || v == QLatin1String("10") || v == QLatin1String("win10"))
        return lookWindows10;
    if (v == lookWindows11 || v == QLatin1String("11") || v == QLatin1String("win11"))
        return lookWindows11;
    return QString();
}

QString readLook(const KSharedConfigPtr &config)
{
    return ShellLook::normalize(config->group(appearanceGroup).readEntry("Look", lookWindows11));
}
}

ShellLook::ShellLook(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
{
    m_watcher = KConfigWatcher::create(m_config);
    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &g) {
        if (g.name() == appearanceGroup)
            Q_EMIT changed();
    });
}

// the same table as the shell's (ShellSettings, [Appearance] Look)
QString ShellLook::normalize(const QString &value)
{
    const QString w = word(value);
    return w.isEmpty() ? lookWindows11 : w;
}

bool ShellLook::isWindows10(const KSharedConfigPtr &shellrc)
{
    return readLook(shellrc) == lookWindows10;
}

QString ShellLook::look() const
{
    return readLook(m_config);
}

void ShellLook::setLook(const QString &look)
{
    const QString value = word(look);
    if (value.isEmpty())
        return;
    if (value == this->look() && m_config->group(appearanceGroup).hasKey("Look"))
        return;
    KConfigGroup g = m_config->group(appearanceGroup);
    g.writeEntry("Look", value, KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}
