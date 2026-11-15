// SPDX-License-Identifier: GPL-3.0-or-later
#include "soundtheme.h"

#include <KConfigGroup>
#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMediaPlayer>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

namespace {

const QString group = QStringLiteral("Sounds");

QStringList soundDirs()
{
    QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("sounds"),
                                                 QStandardPaths::LocateDirectory);
    return dirs;
}

QString themeFile(const QStringList &dirs, const QString &theme)
{
    for (const QString &dir : dirs) {
        const QString file = dir + QLatin1Char('/') + theme + QStringLiteral("/index.theme");
        if (QFileInfo::exists(file))
            return file;
    }
    return {};
}

QString localized(const QSettings &ini, const QString &key, const QString &lang)
{
    const QString own = ini.value(key + QLatin1Char('[') + lang + QLatin1Char(']')).toString();
    return own.isEmpty() ? ini.value(key).toString() : own;
}

} // namespace

SoundTheme::SoundTheme(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("kdeglobals")))
    , m_shell(KSharedConfig::openConfig(QStringLiteral("rayeshellrc"), KConfig::SimpleConfig))
{
}

QVariantList SoundTheme::themesIn(const QStringList &dirs, const QString &lang)
{
    QVariantList themes;
    QSet<QString> seen;
    for (const QString &dir : dirs) {
        const QStringList entries = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &id : entries) {
            if (seen.contains(id))
                continue; // a theme in the user's folder hides the system one
            const QString file = dir + QLatin1Char('/') + id + QStringLiteral("/index.theme");
            if (!QFileInfo::exists(file))
                continue;
            seen.insert(id);
            QSettings ini(file, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Sound Theme"));
            if (ini.value(QStringLiteral("Hidden")).toString() == QLatin1String("true"))
                continue;
            QString name = localized(ini, QStringLiteral("Name"), lang);
            if (name.isEmpty())
                name = id;
            themes.append(QVariantMap{{QStringLiteral("id"), id},
                                      {QStringLiteral("name"), name},
                                      {QStringLiteral("comment"), localized(ini, QStringLiteral("Comment"), lang)}});
        }
    }
    std::sort(themes.begin(), themes.end(), [](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(a.toMap().value(QStringLiteral("name")).toString(),
                                           b.toMap().value(QStringLiteral("name")).toString())
            < 0;
    });
    return themes;
}

QString SoundTheme::soundFile(const QStringList &dirs, const QString &theme, const QString &name)
{
    QStringList queue{theme};
    QSet<QString> done;
    while (!queue.isEmpty()) {
        const QString current = queue.takeFirst();
        if (current.isEmpty() || done.contains(current))
            continue;
        done.insert(current);
        const QString file = themeFile(dirs, current);
        if (file.isEmpty())
            continue;
        QSettings ini(file, QSettings::IniFormat);
        const QString folders = ini.value(QStringLiteral("Sound Theme/Directories")).toStringList().join(QLatin1Char(','));
        const QString root = QFileInfo(file).absolutePath();
        for (QString folder : folders.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            folder = folder.trimmed();
            for (const char *ext : {".oga", ".ogg", ".wav"}) {
                const QString path = root + QLatin1Char('/') + folder + QLatin1Char('/') + name + QLatin1String(ext);
                if (QFileInfo::exists(path))
                    return path;
            }
        }
        const QStringList parents = ini.value(QStringLiteral("Sound Theme/Inherits")).toStringList();
        for (const QString &parent : parents)
            queue.append(parent.trimmed());
    }
    return {};
}

QString SoundTheme::exampleOf(const QStringList &dirs, const QString &theme)
{
    const QString file = themeFile(dirs, theme);
    QString example;
    if (!file.isEmpty())
        example = QSettings(file, QSettings::IniFormat).value(QStringLiteral("Sound Theme/Example")).toString();
    QString found = example.isEmpty() ? QString() : soundFile(dirs, theme, example);
    for (const QString &name : {QStringLiteral("dialog-information"), QStringLiteral("bell")}) {
        if (!found.isEmpty())
            break;
        found = soundFile(dirs, theme, name);
    }
    return found;
}

QVariantList SoundTheme::themes() const
{
    return themesIn(soundDirs(), QLocale().name().section(QLatin1Char('_'), 0, 0));
}

QString SoundTheme::theme() const
{
    // the desktop's own default when nothing is set
    return m_config->group(group).readEntry("Theme", QStringLiteral("ocean"));
}

void SoundTheme::setTheme(const QString &id)
{
    if (id.isEmpty() || id == theme())
        return;
    m_config->group(group).writeEntry("Theme", id, KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}

bool SoundTheme::enabled() const
{
    return m_config->group(group).readEntry("Enable", true);
}

void SoundTheme::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_config->group(group).writeEntry("Enable", on, KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}

bool SoundTheme::play(const QString &id)
{
    const QString file = exampleOf(soundDirs(), id.isEmpty() ? theme() : id);
    return playFile(file);
}

bool SoundTheme::playFile(const QString &file)
{
    if (file.isEmpty())
        return false;
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_output = new QAudioOutput(this);
        m_player->setAudioOutput(m_output);
    }
    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(file));
    m_player->play();
    return true;
}

bool SoundTheme::volumeChime() const
{
    return m_shell->group(QStringLiteral("Sound")).readEntry("VolumeChime", true);
}

void SoundTheme::setVolumeChime(bool on)
{
    if (on == volumeChime())
        return;
    m_shell->group(QStringLiteral("Sound")).writeEntry("VolumeChime", on, KConfig::Notify);
    m_shell->sync();
    Q_EMIT changed();
}

bool SoundTheme::playVolumeChime()
{
    if (!volumeChime() || !enabled())
        return false;
    return playFile(soundFile(soundDirs(), theme(), QStringLiteral("audio-volume-change")));
}
