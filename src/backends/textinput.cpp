// SPDX-License-Identifier: GPL-3.0-or-later
#include "textinput.h"

#include <KConfigGroup>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>

TextInputSettings::TextInputSettings(QObject *parent)
    : TextInputSettings(QStringLiteral("/usr/share/applications/org.kde.plasma.keyboard.desktop"), parent)
{
}

TextInputSettings::TextInputSettings(const QString &keyboardFile, QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("kwinrc")))
    , m_keyboardFile(keyboardFile)
{
}

bool TextInputSettings::available() const
{
    return QFileInfo(m_keyboardFile).isFile();
}

bool TextInputSettings::onScreenKeyboard() const
{
    return available() && m_config->group(QStringLiteral("Wayland")).readEntry("InputMethod", QString()) == m_keyboardFile;
}

void TextInputSettings::setOnScreenKeyboard(bool on)
{
    if (!available() || on == onScreenKeyboard())
        return;
    KConfigGroup group = m_config->group(QStringLiteral("Wayland"));
    group.writeEntry("InputMethod", on ? m_keyboardFile : QString(), KConfig::Notify);
    m_config->sync();
    // KWin looks at the setting again when told to
    QDBusConnection::sessionBus().send(QDBusMessage::createSignal(QStringLiteral("/VirtualKeyboard"),
                                                                  QStringLiteral("org.kde.kwin.VirtualKeyboard"),
                                                                  QStringLiteral("reconfigure")));
    Q_EMIT changed();
}
