// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Game Mode: the choices raye-game-watch and raye-game-start read
// (~/.config/raye/game-mode.conf), the games the watcher has seen
// (~/.local/state/raye/game-mode/games) and the Windows programs, each of
// which can be turned on or off. Changes count at once, a running game
// included: the watcher reads the file every two seconds.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class GameModeSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool schedulerAvailable READ schedulerAvailable NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool effects READ effects WRITE setEffects NOTIFY changed)
    Q_PROPERTY(bool scheduler READ scheduler WRITE setScheduler NOTIFY changed)
    Q_PROPERTY(bool power READ power WRITE setPower NOTIFY changed)
    // a game runs and Game Mode is on for it right now
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // {key, name, source (steam, store, windows), on}, the last one played first
    Q_PROPERTY(QVariantList games READ games NOTIFY changed)

public:
    explicit GameModeSettings(QObject *parent = nullptr);

    bool installed() const;
    bool schedulerAvailable() const;
    bool enabled() const { return m_enabled; }
    bool effects() const { return m_effects; }
    bool scheduler() const { return m_scheduler; }
    bool power() const { return m_power; }
    bool active() const { return m_active; }
    QVariantList games() const;

    void setEnabled(bool on);
    void setEffects(bool on);
    void setScheduler(bool on);
    void setPower(bool on);

    Q_INVOKABLE void setGameOn(const QString &key, bool on);
    Q_INVOKABLE void refresh();
    // what raye-windows-profile says of each space (its "game" key)
    void setWindowsKinds(const QByteArray &json);

    static bool validKey(const QString &key);

Q_SIGNALS:
    void changed();

private:
    void read();
    void write();
    void setFlag(bool &flag, bool on);
    bool onByDefault(const QString &key) const;
    void askWindowsKinds();

    bool m_enabled = true;
    bool m_effects = true;
    bool m_scheduler = true;
    bool m_power = true;
    bool m_active = false;
    QStringList m_on;
    QStringList m_off;
    // space: game kind ("yes", "launcher") and its profile's name
    QVariantMap m_windowsKinds;
    QVariantMap m_windowsNames;
};
