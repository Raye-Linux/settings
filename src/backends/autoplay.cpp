// SPDX-License-Identifier: GPL-3.0-or-later
#include "autoplay.h"

#include <KConfigGroup>

AutoPlaySettings::AutoPlaySettings(QObject *parent)
    : AutoPlaySettings(QStringLiteral("rayeshellrc"), parent)
{
}

AutoPlaySettings::AutoPlaySettings(const QString &configFile, QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(configFile, KConfig::SimpleConfig))
{
}

QString AutoPlaySettings::normalized(const QString &choice)
{
    if (choice == QLatin1String("open") || choice == QLatin1String("nothing"))
        return choice;
    return QStringLiteral("ask");
}

bool AutoPlaySettings::enabled() const
{
    return m_config->group(QStringLiteral("AutoPlay")).readEntry("Enabled", true);
}

void AutoPlaySettings::setEnabled(bool on)
{
    if (on == enabled())
        return;
    KConfigGroup group = m_config->group(QStringLiteral("AutoPlay"));
    // on is the default: leave no key behind for it
    if (on)
        group.deleteEntry("Enabled", KConfig::Notify);
    else
        group.writeEntry("Enabled", false, KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}

QString AutoPlaySettings::removableDrive() const
{
    return normalized(m_config->group(QStringLiteral("AutoPlay")).readEntry("RemovableDrive", QString()));
}

void AutoPlaySettings::setRemovableDrive(const QString &choice)
{
    write("RemovableDrive", normalized(choice));
}

QString AutoPlaySettings::memoryCard() const
{
    const QString card = m_config->group(QStringLiteral("AutoPlay")).readEntry("MemoryCard", QString());
    return card.isEmpty() ? removableDrive() : normalized(card);
}

void AutoPlaySettings::setMemoryCard(const QString &choice)
{
    write("MemoryCard", normalized(choice));
}

void AutoPlaySettings::write(const char *key, const QString &value)
{
    KConfigGroup group = m_config->group(QStringLiteral("AutoPlay"));
    if (group.readEntry(key, QString()) == value)
        return;
    group.writeEntry(key, value, KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}
