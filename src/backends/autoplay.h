// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > AutoPlay: what happens when a USB drive or a memory card is
// plugged in. The devices applet of the taskbar reads it from
// rayeshellrc:
//   [AutoPlay] Enabled          false: nothing happens, whatever is below
//   [AutoPlay] RemovableDrive   ask | open | nothing
//   [AutoPlay] MemoryCard       ask | open | nothing (unset: as RemovableDrive)
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class AutoPlaySettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString removableDrive READ removableDrive WRITE setRemovableDrive NOTIFY changed)
    Q_PROPERTY(QString memoryCard READ memoryCard WRITE setMemoryCard NOTIFY changed)

public:
    explicit AutoPlaySettings(QObject *parent = nullptr);
    // for the tests: another file than the user's rayeshellrc
    explicit AutoPlaySettings(const QString &configFile, QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool on);
    QString removableDrive() const;
    void setRemovableDrive(const QString &choice);
    QString memoryCard() const;
    void setMemoryCard(const QString &choice);

    // anything but "open" and "nothing" reads as "ask", as the applet does
    static QString normalized(const QString &choice);

Q_SIGNALS:
    void changed();

private:
    void write(const char *key, const QString &value);

    KSharedConfigPtr m_config;
};
