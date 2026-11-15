// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Sounds: the sound theme of the desktop and whether
// apps and the system play sounds at all.
//   kdeglobals [Sounds] Theme  (a folder of XDG sounds/, with index.theme)
//   kdeglobals [Sounds] Enable (read by the notification library)
//   rayeshellrc [Sound] VolumeChime (the shell plays a sound on volume changes)
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QMediaPlayer;
class QAudioOutput;

class SoundTheme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{ id, name, comment }], by name
    Q_PROPERTY(QVariantList themes READ themes NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool volumeChime READ volumeChime WRITE setVolumeChime NOTIFY changed)

public:
    explicit SoundTheme(QObject *parent = nullptr);

    QVariantList themes() const;
    QString theme() const;
    void setTheme(const QString &id);
    bool enabled() const;
    void setEnabled(bool on);
    bool volumeChime() const;
    void setVolumeChime(bool on);

    // plays the sample of a theme (its Example= sound, else the bell)
    Q_INVOKABLE bool play(const QString &id);
    // the short sound of a volume change, if the switch is on
    Q_INVOKABLE bool playVolumeChime();

    // for tests and the above: the themes under these sounds/ folders,
    // names in that language
    static QVariantList themesIn(const QStringList &soundDirs, const QString &lang);
    // the file of one sound of a theme, following Inherits=; "" if none
    static QString soundFile(const QStringList &soundDirs, const QString &theme, const QString &name);
    static QString exampleOf(const QStringList &soundDirs, const QString &theme);

Q_SIGNALS:
    void changed();

private:
    bool playFile(const QString &file);

    KSharedConfigPtr m_config;
    KSharedConfigPtr m_shell;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
};
