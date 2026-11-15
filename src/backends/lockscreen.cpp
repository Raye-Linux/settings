// SPDX-License-Identifier: GPL-3.0-or-later
#include "lockscreen.h"

#include <KConfigGroup>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace {

KConfigGroup pictureGroup(const KSharedConfigPtr &config)
{
    return config->group(QStringLiteral("Greeter"))
        .group(QStringLiteral("Wallpaper"))
        .group(QStringLiteral("org.kde.image"))
        .group(QStringLiteral("General"));
}

constexpr int MaxRecent = 5;
constexpr int NotAPicture = 3;
// what pkexec exits with when no password was given
constexpr int Dismissed = 126;
constexpr int NotAuthorized = 127;

}

LockScreenSettings::LockScreenSettings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc")))
    , m_settings(KSharedConfig::openConfig(QStringLiteral("raye-settingsrc")))
    , m_command({QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-settings/signin-background")})
    , m_themeConfig(QStringLiteral("/usr/share/sddm/themes/raye/theme.conf.user"))
{
}

QUrl LockScreenSettings::image() const
{
    const QString value = pictureGroup(m_config).readEntry("Image", QString());
    if (value.isEmpty())
        return {};
    return value.startsWith(QLatin1Char('/')) ? QUrl::fromLocalFile(value) : QUrl(value);
}

QVariantList LockScreenSettings::recentImages() const
{
    QVariantList images;
    const QStringList stored = m_settings->group(QStringLiteral("LockScreen")).readEntry("Recent", QStringList());
    for (const QString &value : stored)
        images.append(QUrl(value));
    return images;
}

QString LockScreenSettings::backgroundIn(const QByteArray &themeConfig)
{
    QString background;
    const QList<QByteArray> lines = themeConfig.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("background="))
            background = QString::fromUtf8(line.mid(11)).trimmed();
    }
    return background;
}

bool LockScreenSettings::onSignIn() const
{
    QFile file(m_themeConfig);
    return file.open(QIODevice::ReadOnly) && !backgroundIn(file.readAll()).isEmpty();
}

void LockScreenSettings::remember(const QUrl &image)
{
    KConfigGroup group = m_settings->group(QStringLiteral("LockScreen"));
    QStringList recent = group.readEntry("Recent", QStringList());
    recent.removeAll(image.toString());
    recent.prepend(image.toString());
    group.writeEntry("Recent", recent.mid(0, MaxRecent));
    m_settings->sync();
}

void LockScreenSettings::setPicture(const QUrl &image)
{
    if (!image.isLocalFile() || !QFileInfo(image.toLocalFile()).isFile())
        return;
    KConfigGroup group = pictureGroup(m_config);
    group.writeEntry("Image", image.toString(), KConfig::Notify);
    m_config->sync();
    remember(image);
    Q_EMIT changed();
    // the sign-in screen keeps following it
    if (onSignIn())
        runHelper({QStringLiteral("--set")}, image.toLocalFile());
}

void LockScreenSettings::useRayePicture()
{
    KConfigGroup group = pictureGroup(m_config);
    group.deleteEntry("Image", KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
    if (onSignIn())
        runHelper({QStringLiteral("--clear")}, QString());
}

void LockScreenSettings::setOnSignIn(bool on)
{
    if (on == onSignIn())
        return;
    if (!on) {
        runHelper({QStringLiteral("--clear")}, QString());
        return;
    }
    const QUrl picture = image();
    // the Raye picture is on the sign-in screen already
    if (picture.isLocalFile())
        runHelper({QStringLiteral("--set")}, picture.toLocalFile());
}

void LockScreenSettings::runHelper(const QStringList &arguments, const QString &input)
{
    if (m_helper) {
        Q_EMIT signInFailed(QStringLiteral("failed"));
        return;
    }
    m_helper = new QProcess(this);
    if (!input.isEmpty())
        m_helper->setStandardInputFile(input);
    connect(m_helper, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_helper)
            return;
        m_helper->deleteLater();
        m_helper = nullptr;
        Q_EMIT busyChanged();
        Q_EMIT signInFailed(QStringLiteral("failed"));
    });
    connect(m_helper, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!m_helper)
            return;
        m_helper->deleteLater();
        m_helper = nullptr;
        Q_EMIT busyChanged();
        Q_EMIT changed();
        if (status == QProcess::NormalExit && code == 0)
            return;
        if (status == QProcess::NormalExit && (code == Dismissed || code == NotAuthorized))
            Q_EMIT signInFailed(QStringLiteral("cancelled"));
        else if (status == QProcess::NormalExit && code == NotAPicture)
            Q_EMIT signInFailed(QStringLiteral("picture"));
        else
            Q_EMIT signInFailed(QStringLiteral("failed"));
    });
    Q_EMIT busyChanged();
    m_helper->start(m_command.first(), m_command.mid(1) + arguments);
}
