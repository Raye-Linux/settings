// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Background: a picture, a solid colour, a slideshow or
// an animation (GIF, mp4, webm, drawn by Raye's org.raye.animated) on
// every desktop.
//
// What the desktop shows is read from the shell's own file
// (plasma-org.kde.plasma.desktop-appletsrc, the first desktop); changes
// go to the shell as a script through org.kde.PlasmaShell.evaluateScript,
// which is how the shell itself wants its wallpaper set, on all desktops
// at once. Picture names go into the script as JSON strings, so a quote
// in a file name stays a quote. The last five pictures are kept in
// raye-settingsrc [Background] Recent for "Recent images".
#pragma once

#include <QColor>
#include <QPointer>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class BackgroundSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "picture", "color", "slideshow" or "animated"
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QUrl image READ image NOTIFY changed)
    // "fill", "fit", "stretch", "tile" or "centre"
    Q_PROPERTY(QString fillMode READ fillMode NOTIFY changed)
    Q_PROPERTY(QColor color READ color NOTIFY changed)
    Q_PROPERTY(QString slideshowFolder READ slideshowFolder NOTIFY changed)
    Q_PROPERTY(QUrl slideshowFolderUrl READ slideshowFolderUrl NOTIFY changed)
    // seconds
    Q_PROPERTY(int slideshowInterval READ slideshowInterval NOTIFY changed)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY changed)
    // [url], newest first, at most five
    Q_PROPERTY(QVariantList recentImages READ recentImages NOTIFY changed)
    // [{ url, name, preview }], Raye's own first
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY wallpapersChanged)
    // the animation on the desktop, and the ones Raye ships: [{ url, name, preview }]
    Q_PROPERTY(QUrl animation READ animation NOTIFY changed)
    Q_PROPERTY(QVariantList animations READ animations NOTIFY wallpapersChanged)
    Q_PROPERTY(bool pauseCovered READ pauseCovered NOTIFY changed)
    Q_PROPERTY(bool pauseOnBattery READ pauseOnBattery NOTIFY changed)
    // a video being made lighter (30 frames a second, at most 1080 lines)
    Q_PROPERTY(bool preparing READ preparing NOTIFY changed)
    // a picture of what the desktop shows (a package's best image), for previews
    Q_PROPERTY(QUrl previewImage READ previewImage NOTIFY changed)
    // the shell did not take the last change
    Q_PROPERTY(bool failed READ failed NOTIFY changed)

public:
    explicit BackgroundSettings(QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    QUrl image() const { return m_image; }
    QString fillMode() const { return m_fillMode; }
    QColor color() const { return m_color; }
    QString slideshowFolder() const { return m_folder; }
    QUrl slideshowFolderUrl() const { return m_folder.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_folder); }
    int slideshowInterval() const { return m_interval; }
    bool shuffle() const { return m_shuffle; }
    QVariantList recentImages() const;
    QVariantList wallpapers() const { return m_wallpapers; }
    bool failed() const { return m_failed; }
    QUrl animation() const { return m_animation; }
    QVariantList animations() const { return m_animations; }
    bool pauseCovered() const { return m_pauseCovered; }
    bool pauseOnBattery() const { return m_pauseOnBattery; }
    bool preparing() const { return m_preparing; }
    QUrl previewImage() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void setPicture(const QUrl &image);
    Q_INVOKABLE void setFillMode(const QString &mode);
    Q_INVOKABLE void setColor(const QColor &color);
    Q_INVOKABLE void setSlideshow(const QUrl &folder, int intervalSeconds, bool shuffle);
    // a GIF, mp4 or webm; a video faster than 30 frames a second or taller
    // than 1080 lines gets a lighter copy first
    Q_INVOKABLE void setAnimation(const QUrl &file);
    Q_INVOKABLE void setAnimationRest(bool covered, bool battery);
    // the folder the slideshow starts with (Pictures)
    Q_INVOKABLE QUrl picturesFolder() const;

    // --- the parts the tests drive ---
    // "fill" -> 2 ... (the Image fill modes of Qt Quick the shell uses)
    static int fillModeNumber(const QString &mode);
    static QString fillModeName(int number);
    // a JavaScript string literal for the shell's script
    static QString quoted(const QString &text);
    static QString pictureScript(const QUrl &image, int fillMode);
    static QString colorScript(const QColor &color);
    static QString slideshowScript(const QString &folder, int intervalSeconds, bool shuffle);
    static QString animatedScript(const QUrl &source, const QUrl &still, bool pauseCovered, bool pauseOnBattery);
    // "30000/1001" -> 29.97 (ffprobe's rates), 0 when unreadable
    static double parseRate(const QString &rate);
    static bool needsLighterCopy(double fps, int height);
    // ffmpeg's arguments for the lighter copy and for its still
    static QStringList lighterArguments(const QString &in, const QString &out);
    static QStringList stillArguments(const QString &in, const QString &out);
    // the animations of a folder: name.webm|mp4|gif with name.jpg beside it,
    // names from the folder's metadata.json
    static QVariantList scanAnimations(const QStringList &folders);
    // what the first desktop of an appletsrc file shows
    struct Current {
        QString mode = QStringLiteral("picture");
        QUrl image;
        QUrl animation;
        bool pauseCovered = true;
        bool pauseOnBattery = true;
        int fillMode = 2;
        QColor color = QColor(0x0e, 0x0a, 0x17);
        QString folder;
        int interval = 1800;
        bool shuffle = true;
    };
    static Current readCurrent(const QString &appletsrc);
    // wallpaper packages and pictures in these folders
    static QVariantList scanWallpapers(const QStringList &folders);
    static QStringList pushRecent(const QStringList &recent, const QString &path);
    // the picture inside a wallpaper package folder ("" when none)
    static QString packagePreview(const QString &folder);

Q_SIGNALS:
    void changed();
    void wallpapersChanged();

private:
    void run(const QString &script);
    void remember(const QUrl &image);
    void applyAnimation(const QUrl &source, const QUrl &still);
    void prepareVideo(const QString &path);

    QString m_mode = QStringLiteral("picture");
    QUrl m_image;
    QString m_fillMode = QStringLiteral("fill");
    QColor m_color;
    QString m_folder;
    int m_interval = 1800;
    bool m_shuffle = true;
    bool m_failed = false;
    QVariantList m_wallpapers;
    QUrl m_animation;
    QVariantList m_animations;
    bool m_pauseCovered = true;
    bool m_pauseOnBattery = true;
    bool m_preparing = false;
    // bumped by every choice: a video ready after a newer choice is dropped
    quint64 m_choice = 0;
    void choose();
};
