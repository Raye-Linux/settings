// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallpaper.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCollator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace {

const QString imagePlugin = QStringLiteral("org.kde.image");
const QString colorPlugin = QStringLiteral("org.kde.color");
const QString slideshowPlugin = QStringLiteral("org.kde.slideshow");
const QString animatedPlugin = QStringLiteral("org.raye.animated");
const QStringList animationSuffixes{QStringLiteral("webm"), QStringLiteral("mp4"), QStringLiteral("gif")};
// what a wallpaper video is cut down to
constexpr double lightFps = 30;
constexpr int lightHeight = 1080;
constexpr int recentCount = 5;

const QStringList pictureSuffixes{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                  QStringLiteral("webp"), QStringLiteral("avif"), QStringLiteral("jxl")};

// the same lines for every desktop: switch the plugin, then write its keys
QString forEveryDesktop(const QString &plugin, const QString &writes)
{
    return QStringLiteral("const all = desktops();\n"
                          "for (let i = 0; i < all.length; ++i) {\n"
                          "    const d = all[i];\n"
                          "    d.wallpaperPlugin = %1;\n"
                          "    d.currentConfigGroup = [\"Wallpaper\", %1, \"General\"];\n"
                          "%2"
                          "}\n")
        .arg(BackgroundSettings::quoted(plugin), writes);
}

QString localName(const QJsonObject &plugin)
{
    const QString language = QLocale().name().section(QLatin1Char('_'), 0, 0);
    const QString translated = plugin.value(QStringLiteral("Name[%1]").arg(language)).toString();
    return translated.isEmpty() ? plugin.value(QStringLiteral("Name")).toString() : translated;
}

QString previewOf(const QString &package)
{
    const QDir images(package + QLatin1String("/contents/images"));
    for (const QString &preferred : {QStringLiteral("1920x1080"), QStringLiteral("2560x1440"), QStringLiteral("3840x2160")}) {
        for (const QString &suffix : pictureSuffixes) {
            const QString path = images.filePath(preferred + QLatin1Char('.') + suffix);
            if (QFileInfo::exists(path))
                return path;
        }
    }
    const QString screenshot = package + QLatin1String("/contents/screenshot.png");
    if (QFileInfo::exists(screenshot))
        return screenshot;
    const QStringList all = images.entryList(QDir::Files, QDir::Name);
    return all.isEmpty() ? QString() : images.filePath(all.first());
}

} // namespace

BackgroundSettings::BackgroundSettings(QObject *parent)
    : QObject(parent)
{
    reload();
}

int BackgroundSettings::fillModeNumber(const QString &mode)
{
    if (mode == QLatin1String("fit"))
        return 1;
    if (mode == QLatin1String("stretch"))
        return 0;
    if (mode == QLatin1String("tile"))
        return 3;
    if (mode == QLatin1String("centre"))
        return 6;
    return 2; // fill
}

QString BackgroundSettings::fillModeName(int number)
{
    switch (number) {
    case 0:
        return QStringLiteral("stretch");
    case 1:
        return QStringLiteral("fit");
    case 3:
    case 4:
    case 5:
        return QStringLiteral("tile");
    case 6:
        return QStringLiteral("centre");
    }
    return QStringLiteral("fill");
}

QString BackgroundSettings::quoted(const QString &text)
{
    // a one-string JSON array is the string literal with its brackets
    const QByteArray json = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    QString literal = QString::fromUtf8(json.mid(1, json.size() - 2));
    // JSON leaves these two alone; older scripts end a line on them
    literal.replace(QChar(0x2028), QLatin1String("\\u2028"));
    literal.replace(QChar(0x2029), QLatin1String("\\u2029"));
    return literal;
}

QString BackgroundSettings::pictureScript(const QUrl &image, int fillMode)
{
    return forEveryDesktop(imagePlugin,
                           QStringLiteral("    d.writeConfig(\"Image\", %1);\n"
                                          "    d.writeConfig(\"FillMode\", %2);\n")
                               // one pass: a "%2" in the name must stay as it is
                               .arg(quoted(image.toString()), QString::number(fillMode)));
}

QString BackgroundSettings::colorScript(const QColor &color)
{
    return forEveryDesktop(colorPlugin,
                           QStringLiteral("    d.writeConfig(\"Color\", %1);\n")
                               .arg(quoted(QStringLiteral("%1,%2,%3").arg(color.red()).arg(color.green()).arg(color.blue()))));
}

QString BackgroundSettings::slideshowScript(const QString &folder, int intervalSeconds, bool shuffle)
{
    // the shell's orders: 0 random, 1 by name
    return forEveryDesktop(slideshowPlugin,
                           QStringLiteral("    d.writeConfig(\"SlidePaths\", [%1]);\n"
                                          "    d.writeConfig(\"SlideInterval\", %2);\n"
                                          "    d.writeConfig(\"SlideshowMode\", %3);\n")
                               .arg(quoted(folder), QString::number(qBound(10, intervalSeconds, 7 * 24 * 3600)),
                                    QString::number(shuffle ? 0 : 1)));
}

QString BackgroundSettings::animatedScript(const QUrl &source, const QUrl &still, bool pauseCovered, bool pauseOnBattery)
{
    const auto flag = [](bool on) { return on ? QStringLiteral("true") : QStringLiteral("false"); };
    return forEveryDesktop(animatedPlugin,
                           QStringLiteral("    d.writeConfig(\"Source\", %1);\n"
                                          "    d.writeConfig(\"Still\", %2);\n"
                                          "    d.writeConfig(\"PauseCovered\", %3);\n"
                                          "    d.writeConfig(\"PauseOnBattery\", %4);\n")
                               .arg(quoted(source.toString()), quoted(still.toString()), flag(pauseCovered), flag(pauseOnBattery)));
}

double BackgroundSettings::parseRate(const QString &rate)
{
    const QStringList parts = rate.trimmed().split(QLatin1Char('/'));
    bool ok = false;
    const double num = parts.value(0).toDouble(&ok);
    if (!ok)
        return 0;
    if (parts.size() < 2)
        return num;
    const double den = parts.value(1).toDouble(&ok);
    return ok && den > 0 ? num / den : 0;
}

bool BackgroundSettings::needsLighterCopy(double fps, int height)
{
    return fps > lightFps + 0.5 || height > lightHeight;
}

QStringList BackgroundSettings::lighterArguments(const QString &in, const QString &out)
{
    // fps only ever goes down: a 24 fps film stays at 24
    return {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"), in, QStringLiteral("-an"),
            QStringLiteral("-vf"), QStringLiteral("scale=-2:'min(%1,ih)',fps=fps='min(%2,source_fps)'").arg(lightHeight).arg(lightFps),
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("veryfast"),
            QStringLiteral("-crf"), QStringLiteral("23"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"), out};
}

QStringList BackgroundSettings::stillArguments(const QString &in, const QString &out)
{
    return {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"), in,
            QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-q:v"), QStringLiteral("3"), out};
}

QVariantList BackgroundSettings::scanAnimations(const QStringList &folders)
{
    QVariantList list;
    QStringList seen;
    const QString language = QLocale().name().section(QLatin1Char('_'), 0, 0);
    for (const QString &folder : folders) {
        const QDir dir(folder);
        QFile meta(dir.filePath(QStringLiteral("metadata.json")));
        QJsonObject names;
        if (meta.open(QIODevice::ReadOnly))
            names = QJsonDocument::fromJson(meta.readAll()).object();
        for (const QFileInfo &info : dir.entryInfoList(QDir::Files, QDir::Name)) {
            if (!animationSuffixes.contains(info.suffix().toLower()) || seen.contains(info.completeBaseName()))
                continue;
            const QString jpg = dir.filePath(info.completeBaseName() + QLatin1String(".jpg"));
            const QString preview = QFileInfo::exists(jpg) ? jpg : info.suffix().toLower() == QLatin1String("gif") ? info.filePath() : QString();
            const QJsonObject n = names.value(info.completeBaseName()).toObject();
            QString name = n.value(QStringLiteral("Name[%1]").arg(language)).toString();
            if (name.isEmpty())
                name = n.value(QStringLiteral("Name")).toString();
            if (name.isEmpty())
                name = info.completeBaseName();
            list << QVariantMap{{QStringLiteral("url"), QUrl::fromLocalFile(info.filePath()).toString()},
                                {QStringLiteral("name"), name},
                                {QStringLiteral("preview"), preview.isEmpty() ? QUrl() : QUrl::fromLocalFile(preview)}};
            seen << info.completeBaseName();
        }
    }
    return list;
}

BackgroundSettings::Current BackgroundSettings::readCurrent(const QString &appletsrc)
{
    Current current;
    KConfig file(appletsrc, KConfig::SimpleConfig);
    const KConfigGroup containments = file.group(QStringLiteral("Containments"));
    QStringList ids = containments.groupList();
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
    KConfigGroup desktop;
    for (const QString &id : std::as_const(ids)) {
        const KConfigGroup g = containments.group(id);
        if (!g.hasKey("wallpaperplugin") || g.readEntry("formfactor", 0) != 0)
            continue;
        // the desktop of the first screen, else the first desktop
        if (!desktop.isValid() || g.readEntry("lastScreen", -1) == 0)
            desktop = g;
        if (g.readEntry("lastScreen", -1) == 0)
            break;
    }
    if (!desktop.isValid())
        return current;
    const QString plugin = desktop.readEntry("wallpaperplugin", imagePlugin);
    const KConfigGroup general = desktop.group(QStringLiteral("Wallpaper")).group(plugin).group(QStringLiteral("General"));
    if (plugin == colorPlugin) {
        current.mode = QStringLiteral("color");
        current.color = general.readEntry("Color", current.color);
    } else if (plugin == animatedPlugin) {
        current.mode = QStringLiteral("animated");
        const QString source = general.readEntry("Source");
        current.animation = source.isEmpty() ? QUrl() : QUrl::fromUserInput(source, {}, QUrl::AssumeLocalFile);
        current.pauseCovered = general.readEntry("PauseCovered", true);
        current.pauseOnBattery = general.readEntry("PauseOnBattery", true);
    } else if (plugin == slideshowPlugin) {
        current.mode = QStringLiteral("slideshow");
        current.folder = general.readEntry("SlidePaths", QStringList()).value(0);
        current.interval = general.readEntry("SlideInterval", 900);
        current.shuffle = general.readEntry("SlideshowMode", 0) == 0;
    } else {
        const QString image = general.readEntry("Image");
        current.image = image.isEmpty() ? QUrl() : QUrl::fromUserInput(image, {}, QUrl::AssumeLocalFile);
        current.fillMode = general.readEntry("FillMode", 2);
    }
    return current;
}

QVariantList BackgroundSettings::scanWallpapers(const QStringList &folders)
{
    struct Item {
        QString url, name, preview;
        bool raye;
        bool first = false;
    };
    QList<Item> items;
    QStringList seen;
    for (const QString &folder : folders) {
        const QDir dir(folder);
        for (const QFileInfo &info : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            if (seen.contains(info.fileName()))
                continue;
            if (info.isDir()) {
                const QString metadata = info.filePath() + QLatin1String("/metadata.json");
                QFile f(metadata);
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                const QJsonObject plugin = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("KPlugin")).toObject();
                const QString preview = previewOf(info.filePath());
                if (preview.isEmpty())
                    continue;
                QString name = localName(plugin);
                if (name.isEmpty())
                    name = info.fileName();
                items << Item{QUrl::fromLocalFile(info.filePath()).toString(), name, preview,
                              info.fileName().startsWith(QLatin1String("Raye")), info.fileName() == QLatin1String("Raye")};
            } else if (pictureSuffixes.contains(info.suffix().toLower())) {
                items << Item{QUrl::fromLocalFile(info.filePath()).toString(), info.completeBaseName(), info.filePath(),
                              info.fileName().startsWith(QLatin1String("raye"), Qt::CaseInsensitive)};
            } else {
                continue;
            }
            seen << info.fileName();
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(items.begin(), items.end(), [&collator](const Item &a, const Item &b) {
        // the default one leads: the other pages draw their previews on it
        if (a.first != b.first)
            return a.first;
        if (a.raye != b.raye)
            return a.raye;
        return collator.compare(a.name, b.name) < 0;
    });
    QVariantList list;
    for (const Item &i : std::as_const(items)) {
        list << QVariantMap{{QStringLiteral("url"), i.url},
                            {QStringLiteral("name"), i.name},
                            {QStringLiteral("preview"), QUrl::fromLocalFile(i.preview)}};
    }
    return list;
}

QString BackgroundSettings::packagePreview(const QString &folder)
{
    return previewOf(folder);
}

QUrl BackgroundSettings::previewImage() const
{
    if (m_mode != QLatin1String("picture") || !m_image.isLocalFile())
        return {};
    const QString path = m_image.toLocalFile();
    if (QFileInfo(path).isDir()) {
        const QString preview = previewOf(path);
        return preview.isEmpty() ? QUrl() : QUrl::fromLocalFile(preview);
    }
    return m_image;
}

QStringList BackgroundSettings::pushRecent(const QStringList &recent, const QString &path)
{
    QStringList list = recent;
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > recentCount)
        list.removeLast();
    return list;
}

void BackgroundSettings::reload()
{
    const QString appletsrc = QStandardPaths::locate(QStandardPaths::GenericConfigLocation,
                                                     QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"));
    const Current c = readCurrent(appletsrc);
    m_mode = c.mode;
    m_image = c.image;
    m_fillMode = fillModeName(c.fillMode);
    m_color = c.color;
    m_folder = c.folder;
    m_interval = c.interval;
    m_shuffle = c.shuffle;

    QStringList folders;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        folders << dir + QLatin1String("/wallpapers");
    m_wallpapers = scanWallpapers(folders);
    QStringList motion;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        motion << dir + QLatin1String("/raye/wallpapers/motion");
    m_animations = scanAnimations(motion);
    m_animation = c.animation;
    m_pauseCovered = c.pauseCovered;
    m_pauseOnBattery = c.pauseOnBattery;
    Q_EMIT wallpapersChanged();
    Q_EMIT changed();
}

QVariantList BackgroundSettings::recentImages() const
{
    const KConfigGroup g = KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"))->group(QStringLiteral("Background"));
    QVariantList list;
    for (const QString &path : g.readEntry("Recent", QStringList())) {
        if (QFileInfo::exists(path))
            list << QUrl::fromLocalFile(path);
    }
    return list;
}

void BackgroundSettings::remember(const QUrl &image)
{
    if (!image.isLocalFile() || !QFileInfo(image.toLocalFile()).isFile())
        return;
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"));
    KConfigGroup g = config->group(QStringLiteral("Background"));
    g.writeEntry("Recent", pushRecent(g.readEntry("Recent", QStringList()), image.toLocalFile()));
    config->sync();
}

void BackgroundSettings::run(const QString &script)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                       QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    call << script;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const bool failed = watcher->isError();
        watcher->deleteLater();
        if (failed != m_failed) {
            m_failed = failed;
            Q_EMIT changed();
        }
    });
}

void BackgroundSettings::choose()
{
    ++m_choice;
    m_preparing = false;
}

void BackgroundSettings::setPicture(const QUrl &image)
{
    if (!image.isValid() || !image.isLocalFile() || !QFileInfo::exists(image.toLocalFile()))
        return;
    choose();
    m_mode = QStringLiteral("picture");
    m_image = image;
    run(pictureScript(image, fillModeNumber(m_fillMode)));
    remember(image);
    Q_EMIT changed();
}

void BackgroundSettings::setFillMode(const QString &mode)
{
    m_fillMode = fillModeName(fillModeNumber(mode));
    if (m_mode == QLatin1String("picture") && m_image.isValid())
        run(pictureScript(m_image, fillModeNumber(m_fillMode)));
    Q_EMIT changed();
}

void BackgroundSettings::setColor(const QColor &color)
{
    if (!color.isValid())
        return;
    choose();
    m_mode = QStringLiteral("color");
    m_color = color;
    run(colorScript(color));
    Q_EMIT changed();
}

void BackgroundSettings::setSlideshow(const QUrl &folder, int intervalSeconds, bool shuffle)
{
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return;
    choose();
    m_mode = QStringLiteral("slideshow");
    m_folder = path;
    m_interval = intervalSeconds;
    m_shuffle = shuffle;
    run(slideshowScript(path, intervalSeconds, shuffle));
    Q_EMIT changed();
}

QUrl BackgroundSettings::picturesFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
}

void BackgroundSettings::applyAnimation(const QUrl &source, const QUrl &still)
{
    m_mode = QStringLiteral("animated");
    m_animation = source;
    m_preparing = false;
    run(animatedScript(source, still, m_pauseCovered, m_pauseOnBattery));
    Q_EMIT changed();
}

void BackgroundSettings::setAnimation(const QUrl &file)
{
    const QFileInfo info(file.toLocalFile());
    if (!file.isLocalFile() || !info.isFile() || !animationSuffixes.contains(info.suffix().toLower()))
        return;
    choose();
    // Raye's own are already light, and a GIF is held to its cap by the wallpaper
    const QString jpg = info.path() + QLatin1Char('/') + info.completeBaseName() + QLatin1String(".jpg");
    if (QFileInfo::exists(jpg)) {
        applyAnimation(file, QUrl::fromLocalFile(jpg));
        return;
    }
    if (info.suffix().toLower() == QLatin1String("gif")) {
        applyAnimation(file, file);
        return;
    }
    prepareVideo(info.absoluteFilePath());
}

void BackgroundSettings::setAnimationRest(bool covered, bool battery)
{
    m_pauseCovered = covered;
    m_pauseOnBattery = battery;
    if (m_mode == QLatin1String("animated") && m_animation.isValid()) {
        const QString path = m_animation.toLocalFile();
        const QString jpg = QFileInfo(path).path() + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QLatin1String(".jpg");
        run(animatedScript(m_animation, QFileInfo::exists(jpg) ? QUrl::fromLocalFile(jpg) : QUrl(), covered, battery));
    }
    Q_EMIT changed();
}

void BackgroundSettings::prepareVideo(const QString &path)
{
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffprobe.isEmpty() || ffmpeg.isEmpty()) {
        applyAnimation(QUrl::fromLocalFile(path), {});
        return;
    }
    // the copy and its still are named after the file and its date, so the
    // same video chosen again is not converted again
    const QFileInfo info(path);
    const QString key = QString::fromLatin1(QCryptographicHash::hash((path + info.lastModified().toString(Qt::ISODate)).toUtf8(),
                                                                     QCryptographicHash::Sha1).toHex().left(10));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/raye/wallpapers");
    QDir().mkpath(dir);
    const QString light = dir + QLatin1Char('/') + info.completeBaseName() + QLatin1Char('-') + key + QLatin1String(".mp4");
    const QString still = dir + QLatin1Char('/') + info.completeBaseName() + QLatin1Char('-') + key + QLatin1String(".jpg");

    m_preparing = true;
    Q_EMIT changed();
    QPointer<BackgroundSettings> self(this);
    const quint64 choice = m_choice;
    auto *probe = new QProcess(this);
    connect(probe, &QProcess::finished, this, [=](int code) {
        double fps = 0;
        int height = 0;
        for (const QString &line : QString::fromUtf8(probe->readAllStandardOutput()).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("avg_frame_rate=")))
                fps = parseRate(line.section(QLatin1Char('='), 1));
            else if (line.startsWith(QLatin1String("height=")))
                height = line.section(QLatin1Char('='), 1).toInt();
        }
        probe->deleteLater();
        const bool lighter = code == 0 && needsLighterCopy(fps, height);
        const QString shown = lighter ? light : path;
        auto finish = [=] {
            if (!self)
                return;
            auto *grab = new QProcess(self);
            connect(grab, &QProcess::finished, self, [=](int c) {
                grab->deleteLater();
                if (self && self->m_choice == choice)
                    self->applyAnimation(QUrl::fromLocalFile(shown), c == 0 ? QUrl::fromLocalFile(still) : QUrl());
            });
            grab->start(ffmpeg, stillArguments(shown, still));
        };
        if (!lighter || QFileInfo::exists(light)) {
            finish();
            return;
        }
        auto *cut = new QProcess(this);
        connect(cut, &QProcess::finished, this, [=](int c) {
            cut->deleteLater();
            if (c != 0) {
                // keep the original rather than nothing
                QFile::remove(light);
                if (m_choice == choice)
                    applyAnimation(QUrl::fromLocalFile(path), {});
                return;
            }
            finish();
        });
        cut->start(ffmpeg, lighterArguments(path, light));
    });
    probe->start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"), QStringLiteral("stream=avg_frame_rate,height"),
                           QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1"), path});
}
