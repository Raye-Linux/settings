// SPDX-License-Identifier: GPL-3.0-or-later
#include "captures.h"

#include <KConfigGroup>
#include <QDesktopServices>
#include <QDir>
#include <QStandardPaths>

namespace {

QUrl stored(const KSharedConfigPtr &config, const char *group, const char *key, QStandardPaths::StandardLocation base, const char *folder)
{
    const QString value = config->group(QString::fromLatin1(group)).readEntry(key, QString());
    if (!value.isEmpty())
        return value.startsWith(QLatin1Char('/')) ? QUrl::fromLocalFile(value) : QUrl(value);
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(base) + QLatin1Char('/') + QString::fromLatin1(folder));
}

}

CapturesSettings::CapturesSettings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("spectaclerc")))
{
}

QUrl CapturesSettings::screenshots() const
{
    return stored(m_config, "ImageSave", "imageSaveLocation", QStandardPaths::PicturesLocation, "Screenshots");
}

QUrl CapturesSettings::recordings() const
{
    return stored(m_config, "VideoSave", "videoSaveLocation", QStandardPaths::MoviesLocation, "Screencasts");
}

void CapturesSettings::write(const char *group, const char *key, const QUrl &folder)
{
    if (!folder.isLocalFile())
        return;
    KConfigGroup g = m_config->group(QString::fromLatin1(group));
    g.writeEntry(key, folder.toString(), KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}

void CapturesSettings::setScreenshots(const QUrl &folder) { write("ImageSave", "imageSaveLocation", folder); }
void CapturesSettings::setRecordings(const QUrl &folder) { write("VideoSave", "videoSaveLocation", folder); }

QString CapturesSettings::pathOf(const QUrl &folder) const
{
    return folder.isLocalFile() ? QDir::toNativeSeparators(folder.toLocalFile()) : folder.toString();
}

bool CapturesSettings::open(const QUrl &folder) const
{
    if (!folder.isLocalFile() || !QDir().mkpath(folder.toLocalFile()))
        return false;
    return QDesktopServices::openUrl(folder);
}
