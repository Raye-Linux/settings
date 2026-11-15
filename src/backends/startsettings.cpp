// SPDX-License-Identifier: GPL-3.0-or-later
#include "startsettings.h"

#include <KConfigGroup>
#include <QDirIterator>
#include <QFile>

namespace {
const QStringList layouts = {QStringLiteral("default"), QStringLiteral("pins"), QStringLiteral("recommendations")};
const QString installedMenu = QStringLiteral("/usr/share/plasma/plasmoids/org.raye.start/contents/ui");
}

StartSettings::StartSettings(QObject *parent)
    : StartSettings(installedMenu, parent)
{
}

StartSettings::StartSettings(const QString &menuFolder, QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("rayeshellrc")))
    , m_read(namedIn(menuFolder, {QStringLiteral("startRecentApps"), QStringLiteral("startMostUsed"), QStringLiteral("startFolders")}))
{
    m_watcher = KConfigWatcher::create(m_config);
    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this] {
        Q_EMIT changed();
    });
}

QStringList StartSettings::folderIds()
{
    return {QStringLiteral("settings"), QStringLiteral("files"), QStringLiteral("documents"),
            QStringLiteral("downloads"), QStringLiteral("music"), QStringLiteral("pictures"),
            QStringLiteral("videos"), QStringLiteral("network"), QStringLiteral("personal")};
}

QStringList StartSettings::namedIn(const QString &folder, const QStringList &names)
{
    QStringList found;
    QDirIterator it(folder, {QStringLiteral("*.qml"), QStringLiteral("*.js")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && found.size() < names.size()) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray text = file.readAll();
        for (const QString &name : names) {
            if (!found.contains(name) && text.contains(name.toLatin1()))
                found.append(name);
        }
    }
    return found;
}

QString StartSettings::layout() const
{
    const QString value = m_config->group(QStringLiteral("Start")).readEntry("Layout", QStringLiteral("default")).toLower();
    // the words an older Settings wrote; the shell takes them too
    if (value == QLatin1String("morepins"))
        return QStringLiteral("pins");
    if (value == QLatin1String("morerecommended"))
        return QStringLiteral("recommendations");
    return layouts.contains(value) ? value : QStringLiteral("default");
}

void StartSettings::setLayout(const QString &layout)
{
    if (layouts.contains(layout) && layout != this->layout())
        write("Layout", layout);
}

bool StartSettings::readBool(const char *key, bool fallback) const
{
    return m_config->group(QStringLiteral("Start")).readEntry(key, fallback);
}

void StartSettings::write(const char *key, const QVariant &value)
{
    KConfigGroup group = m_config->group(QStringLiteral("Start"));
    group.writeEntry(key, value, KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}

bool StartSettings::recommended() const { return readBool("Recommended", true); }
void StartSettings::setRecommended(bool on) { if (on != recommended()) write("Recommended", on); }
bool StartSettings::recentApps() const { return readBool("RecentApps", true); }
void StartSettings::setRecentApps(bool on) { if (on != recentApps()) write("RecentApps", on); }
bool StartSettings::mostUsed() const { return readBool("MostUsed", false); }
void StartSettings::setMostUsed(bool on) { if (on != mostUsed()) write("MostUsed", on); }

QStringList StartSettings::folders() const
{
    const QStringList stored = m_config->group(QStringLiteral("Start"))
                                   .readEntry("Folders", QStringList{QStringLiteral("settings"), QStringLiteral("files")});
    QStringList known;
    for (const QString &id : folderIds()) {
        if (stored.contains(id))
            known.append(id);
    }
    return known;
}

void StartSettings::setFolder(const QString &id, bool on)
{
    if (!folderIds().contains(id) || folders().contains(id) == on)
        return;
    QStringList next;
    const QStringList current = folders();
    for (const QString &known : folderIds()) {
        if (known == id ? on : current.contains(known))
            next.append(known);
    }
    write("Folders", next);
}
