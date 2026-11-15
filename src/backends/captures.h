// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Captures: where screenshots and screen recordings go. The screen
// capture tool keeps them in spectaclerc [ImageSave] imageSaveLocation and
// [VideoSave] videoSaveLocation, and falls back on Pictures/Screenshots and
// Videos/Screencasts.
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class CapturesSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl screenshots READ screenshots NOTIFY changed)
    Q_PROPERTY(QUrl recordings READ recordings NOTIFY changed)

public:
    explicit CapturesSettings(QObject *parent = nullptr);

    QUrl screenshots() const;
    QUrl recordings() const;

    Q_INVOKABLE void setScreenshots(const QUrl &folder);
    Q_INVOKABLE void setRecordings(const QUrl &folder);
    // the path as a person reads it
    Q_INVOKABLE QString pathOf(const QUrl &folder) const;
    // opens the folder, making it first if nothing was captured yet
    Q_INVOKABLE bool open(const QUrl &folder) const;

Q_SIGNALS:
    void changed();

private:
    void write(const char *group, const char *key, const QUrl &folder);

    KSharedConfigPtr m_config;
};
