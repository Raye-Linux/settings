// SPDX-License-Identifier: GPL-3.0-or-later
// System > Notifications and System > Focus.
//
// The notification server of the desktop reads plasmanotifyrc and
// follows changes at once (KConfig notify):
//   [DoNotDisturb] Until (date and time), WhenFullscreen,
//     WhenScreensMirrored, WhenScreenSharing, NotificationSoundsMuted
//   [Applications][<desktop id>] ShowPopups, ShowInHistory,
//     ShowPopupsInDndMode, ShowBadges
// Defaults come from /etc/xdg/plasmanotifyrc, which the cascade merges.
//
// "Notifications" off is do not disturb with no end: Until is set in
// the year 9999, a date nothing else writes. Do not disturb turned on
// here is "until I turn it off", which the desktop writes as a year
// from now. Turning do not disturb off anywhere clears Until, so the
// master switch comes back on by itself.
//
// A focus session writes its end to rayeshellrc [Focus] Until (ISO
// time), where the taskbar clock reads it to show the timer, and turns
// do not disturb on until the same time when the user wants that.
// The choices of the Focus page live in raye-settingsrc [Focus].
#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>
#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QTimer;

class NotificationSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Get notifications from apps and other senders
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb WRITE setDoNotDisturb NOTIFY changed)
    // when do not disturb ends, invalid when off or with no end
    Q_PROPERTY(QDateTime doNotDisturbUntil READ doNotDisturbUntil NOTIFY changed)
    Q_PROPERTY(bool whenFullscreen READ whenFullscreen WRITE setWhenFullscreen NOTIFY changed)
    Q_PROPERTY(bool whenScreensMirrored READ whenScreensMirrored WRITE setWhenScreensMirrored NOTIFY changed)
    Q_PROPERTY(bool whenScreenSharing READ whenScreenSharing WRITE setWhenScreenSharing NOTIFY changed)
    Q_PROPERTY(bool sounds READ sounds WRITE setSounds NOTIFY changed)
    // the lock screen row is shown only when the shell reads its key
    Q_PROPERTY(bool lockScreenAvailable READ lockScreenAvailable CONSTANT)
    Q_PROPERTY(bool lockScreen READ lockScreen WRITE setLockScreen NOTIFY changed)
    // [{ id, name, icon, enabled }], sorted by name
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)

    // focus
    Q_PROPERTY(bool focusActive READ focusActive NOTIFY changed)
    Q_PROPERTY(QDateTime focusUntil READ focusUntil NOTIFY changed)
    Q_PROPERTY(int focusMinutes READ focusMinutes WRITE setFocusMinutes NOTIFY changed)
    Q_PROPERTY(bool focusDoNotDisturb READ focusDoNotDisturb WRITE setFocusDoNotDisturb NOTIFY changed)

public:
    static constexpr int defaultFocusMinutes = 30;
    static constexpr int minFocusMinutes = 5;
    static constexpr int maxFocusMinutes = 240;

    explicit NotificationSettings(QObject *parent = nullptr);
    ~NotificationSettings() override;

    bool enabled() const;
    void setEnabled(bool on);
    bool doNotDisturb() const;
    void setDoNotDisturb(bool on);
    QDateTime doNotDisturbUntil() const;
    bool whenFullscreen() const;
    void setWhenFullscreen(bool on);
    bool whenScreensMirrored() const;
    void setWhenScreensMirrored(bool on);
    bool whenScreenSharing() const;
    void setWhenScreenSharing(bool on);
    bool sounds() const;
    void setSounds(bool on);
    bool lockScreenAvailable() const;
    bool lockScreen() const;
    void setLockScreen(bool on);
    QVariantList apps() const { return m_apps; }

    // { id, name, icon, enabled, popups, history, dnd, badges }
    Q_INVOKABLE QVariantMap app(const QString &id) const;
    // option: "enabled" (banners and notification centre together),
    // "popups", "history", "dnd", "badges"
    Q_INVOKABLE void setAppOption(const QString &id, const QString &option, bool on);
    // the desktop's own settings of one app (sounds of each event), in
    // a window of their own
    Q_INVOKABLE void openMoreOptions(const QString &id) const;

    bool focusActive() const;
    QDateTime focusUntil() const;
    int focusMinutes() const;
    void setFocusMinutes(int minutes);
    bool focusDoNotDisturb() const;
    void setFocusDoNotDisturb(bool on);
    Q_INVOKABLE void startFocus();
    Q_INVOKABLE void stopFocus();

    Q_INVOKABLE void reload();

    // the tests set the time
    void setClock(std::function<QDateTime()> clock);
    // the date "notifications off" writes
    static QDateTime offMarker();
    // A desktop id is a file name: anything else is not written to a
    // config group or handed to a program.
    static bool isAppId(const QString &id);

Q_SIGNALS:
    void changed();
    void appsChanged();

private:
    QDateTime now() const;
    QDateTime rawUntil() const;
    void writeUntil(const QDateTime &until);
    void writeDnd(const char *key, bool value);
    void loadApps();
    void scheduleExpiry();

    KSharedConfigPtr m_notify;   // plasmanotifyrc
    KSharedConfigPtr m_shell;    // rayeshellrc
    KSharedConfigPtr m_own;      // raye-settingsrc
    QList<KConfigWatcher::Ptr> m_watchers;
    QTimer *m_expiry = nullptr;
    QVariantList m_apps;
    // apps known from their desktop entry or their notifyrc
    QSet<QString> m_senders;
    bool m_scanned = false;
    std::function<QDateTime()> m_clock;
};
