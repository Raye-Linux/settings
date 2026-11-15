// SPDX-License-Identifier: GPL-3.0-or-later
// What System › Sound shows: the output and input devices to choose
// from, their volume, balance and mute, the apps playing sound (the
// volume mixer), the sound cards and their profiles (All sound
// devices), and the level of the microphone while it is tested. Read
// from the sound server (PipeWire, through its PulseAudio side) with
// PulseAudioQt, the library Plasma's own volume applet uses.
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QAudioSource;
class QIODevice;

namespace PulseAudioQt
{
class Card;
class Device;
class SinkInput;
}

// The words and numbers the pages show, kept apart so the tests can
// check them without a sound server.
namespace SoundText
{
// The volume the server calls 100 % (PA_VOLUME_NORM).
constexpr qint64 normalVolume = 65536;
// Raye lets the slider go to 100 %, as Windows does.
int percentOf(qint64 volume);
qint64 volumeOf(int percent);

// How a device reads in its card: the port ("Headphones") as the name
// and the card or device ("Built-in audio") under it. A port named like
// the device, or no port, leaves the device name alone.
struct Label {
    QString title;
    QString subtitle;
};
Label deviceLabel(const QString &description, const QString &portDescription, const QString &cardDescription);

// An icon for the device, from what it is plugged into (port type
// names: "speaker", "headphones", "headset", "hdmi", "mic", "usb",
// "bluetooth"...) and its form factor ("headphone", "webcam"...).
QString deviceIcon(const QString &portType, const QString &formFactor, bool input);

// Balance from the left and right channel volumes: -100 all left, 100
// all right. Mono or no left/right channel: 0.
int balanceOf(const QStringList &channels, const QList<qint64> &volumes);
// The channel volumes for a volume and a balance: the louder side keeps
// the volume, the other side goes down.
QList<qint64> volumesFor(const QStringList &channels, qint64 volume, int balance);

// What a monitor of an output or a peak meter is: not a device a person
// chooses.
bool isMonitor(const QVariantMap &properties);
// The name of the app behind a stream: application.name, else the
// process binary, else the stream's own name.
QString appName(const QVariantMap &properties, const QString &streamName);
QString appIcon(const QVariantMap &properties);
// Streams nobody sets a volume for: event sounds, peak meters, our own.
bool isHiddenStream(const QVariantMap &properties);

// A profile row of All sound devices: the "off" profile reads "Off".
QString profileTitle(const QString &name, const QString &description);

// Sorting: the default device first, then by name.
bool deviceLessThan(bool aDefault, const QString &aTitle, bool bDefault, const QString &bTitle);
}

// The output devices (input: false) or input devices (input: true).
class SoundDevices : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool input READ input WRITE setInput NOTIFY inputChanged)
    // the server answers
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // the row of the device apps use now, -1 when none
    Q_PROPERTY(int defaultRow READ defaultRow NOTIFY defaultRowChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        DeviceIndexRole = Qt::UserRole + 1,
        NameRole,       // server name ("alsa_output.pci-0000_00_1f.3.analog-stereo")
        TitleRole,      // "Speakers"
        SubtitleRole,   // "Built-in audio"
        IconRole,
        DefaultRole,
        VolumeRole,     // 0..100
        MutedRole,
        BalanceRole,    // -100..100
        HasBalanceRole,
        CardIndexRole,
    };

    explicit SoundDevices(QObject *parent = nullptr);
    ~SoundDevices() override;

    bool input() const { return m_input; }
    void setInput(bool input);
    bool ready() const;
    int defaultRow() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setDefault(int row);
    Q_INVOKABLE void setVolume(int row, int percent);
    Q_INVOKABLE void setMuted(int row, bool muted);
    Q_INVOKABLE void setBalance(int row, int balance);
    // everything a page needs about one row, as a map (empty if no row)
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void inputChanged();
    void readyChanged();
    void defaultRowChanged();
    void countChanged();

private:
    void rebuild();
    void watch(PulseAudioQt::Device *device);
    void deviceChanged();
    PulseAudioQt::Device *deviceAt(int row) const;

    bool m_input = false;
    QList<QPointer<PulseAudioQt::Device>> m_devices;
    QTimer m_rebuild;
};

// The apps playing sound, for the volume mixer.
class SoundStreams : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        StreamIndexRole = Qt::UserRole + 1,
        AppNameRole,
        IconRole,
        VolumeRole,
        MutedRole,
        DeviceIndexRole, // the output it plays on
        PlayingRole,     // not paused
    };

    explicit SoundStreams(QObject *parent = nullptr);
    ~SoundStreams() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setVolume(int row, int percent);
    Q_INVOKABLE void setMuted(int row, bool muted);
    // send the app to an output, by its device index
    Q_INVOKABLE void setDevice(int row, int deviceIndex);

Q_SIGNALS:
    void countChanged();

private:
    void rebuild();
    PulseAudioQt::SinkInput *streamAt(int row) const;

    QList<QPointer<PulseAudioQt::SinkInput>> m_streams;
    QTimer m_rebuild;
};

// The sound cards, their profiles, and whether each one is allowed.
class SoundCards : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        CardIndexRole = Qt::UserRole + 1,
        TitleRole,
        IconRole,
        AllowedRole,       // a profile other than "off" is on
        ProfilesRole,      // [{ name, title, available }], "off" left out
        ActiveProfileRole, // name
    };

    explicit SoundCards(QObject *parent = nullptr);
    ~SoundCards() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setProfile(int row, const QString &name);
    // off: the "off" profile; on: the profile it had, else the best
    // one that is available
    Q_INVOKABLE void setAllowed(int row, bool allowed);

    // The profile to turn a card back on with: the one remembered if it
    // still exists, else the available one with the highest priority.
    struct ProfileChoice {
        QString name;
        quint32 priority = 0;
        bool available = true;
    };
    static QString profileToRestore(const QList<ProfileChoice> &profiles, const QString &remembered);

Q_SIGNALS:
    void countChanged();

private:
    void rebuild();
    PulseAudioQt::Card *cardAt(int row) const;

    QList<QPointer<PulseAudioQt::Card>> m_cards;
    QHash<quint32, QString> m_lastProfile;
    QTimer m_rebuild;
};

// The level of a microphone while the page is on screen: 0 to 1, read
// about twenty times a second from a small mono capture. Off when
// `active` is false, so nothing records behind the user's back.
class LevelMeter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the server name of the input ("alsa_input...")
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)

public:
    explicit LevelMeter(QObject *parent = nullptr);
    ~LevelMeter() override;

    QString device() const { return m_device; }
    void setDevice(const QString &device);
    bool active() const { return m_active; }
    void setActive(bool active);
    qreal level() const { return m_level; }

    // The peak of 16-bit samples, 0..1, with the slow fall of a meter
    // (it drops at most `fall` per reading).
    static qreal peakOf(const QByteArray &samples);
    static qreal smoothed(qreal previous, qreal peak, qreal fall = 0.08);

Q_SIGNALS:
    void deviceChanged();
    void activeChanged();
    void levelChanged();

private:
    void restart();
    void read();
    void setLevel(qreal level);

    QString m_device;
    bool m_active = false;
    qreal m_level = 0;
    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_io = nullptr;
    QTimer m_timer;
};
