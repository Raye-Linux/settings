// SPDX-License-Identifier: GPL-3.0-or-later
// System > Clipboard: clipboard history (the list Win+V opens) and
// clearing it. The history is the desktop's clipboard service:
//   klipperrc [General] MaxClipItems, KeepClipboardContents
//   D-Bus org.kde.klipper /klipper: reloadConfig, clearClipboardHistory,
//   clearClipboardContents
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class ClipboardSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool history READ history WRITE setHistory NOTIFY changed)
    Q_PROPERTY(bool clearing READ clearing NOTIFY changed)

public:
    // how many items the history keeps when it is on (Windows keeps 25)
    static constexpr int historySize = 25;

    explicit ClipboardSettings(QObject *parent = nullptr);

    bool history() const;
    void setHistory(bool on);
    bool clearing() const { return m_clearing > 0; }

    Q_INVOKABLE void reload();
    // forget everything copied: the history and what is on the clipboard now
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void changed();
    // after clear(): false when the clipboard service did not answer
    void cleared(bool ok);

private:
    void callKlipper(const QString &method, bool clearStep);

    KSharedConfigPtr m_config;
    int m_clearing = 0;
    bool m_clearFailed = false;
};
