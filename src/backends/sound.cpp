// SPDX-License-Identifier: GPL-3.0-or-later
#include "sound.h"

#include <KLocalizedString>
#include <PulseAudioQt/Card>
#include <PulseAudioQt/Client>
#include <PulseAudioQt/Context>
#include <PulseAudioQt/Port>
#include <PulseAudioQt/Profile>
#include <PulseAudioQt/Server>
#include <PulseAudioQt/Sink>
#include <PulseAudioQt/SinkInput>
#include <PulseAudioQt/Source>
#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>
#include <QtEndian>

#include <algorithm>
#include <cmath>

using PulseAudioQt::Card;
using PulseAudioQt::Context;
using PulseAudioQt::Device;
using PulseAudioQt::Port;
using PulseAudioQt::Profile;
using PulseAudioQt::Sink;
using PulseAudioQt::SinkInput;
using PulseAudioQt::Source;

namespace
{
// Many changes come in a burst (a device plugged in brings its card,
// ports and streams): rebuild the list once after the burst.
constexpr int rebuildDelay = 50;

Context *context()
{
    // our streams are named after us, so the mixer can leave them out
    static bool named = false;
    if (!named) {
        Context::setApplicationId(QStringLiteral("org.raye.settings"));
        named = true;
    }
    return Context::instance();
}

QString portTypeName(Port::Type type)
{
    switch (type) {
    case Port::Speaker:
        return QStringLiteral("speaker");
    case Port::Headphones:
        return QStringLiteral("headphones");
    case Port::Headset:
    case Port::Handset:
    case Port::Handsfree:
        return QStringLiteral("headset");
    case Port::HDMI:
    case Port::TV:
        return QStringLiteral("hdmi");
    case Port::Mic:
        return QStringLiteral("mic");
    case Port::USB:
        return QStringLiteral("usb");
    case Port::Bluetooth:
        return QStringLiteral("bluetooth");
    case Port::SPDIF:
        return QStringLiteral("spdif");
    default:
        return {};
    }
}

Port *activePort(const Device *device)
{
    const auto ports = device->ports();
    const quint32 active = device->activePortIndex();
    return active < quint32(ports.size()) ? ports.at(active) : nullptr;
}

Card *cardOf(const Device *device)
{
    const auto cards = context()->cards();
    for (Card *card : cards) {
        if (card->index() == device->cardIndex())
            return card;
    }
    return nullptr;
}

QString cardDescription(const Card *card)
{
    if (!card)
        return {};
    const QVariantMap p = card->properties();
    const QString description = p.value(QStringLiteral("device.description")).toString();
    return description.isEmpty() ? card->name() : description;
}
} // namespace

// --- SoundText ---------------------------------------------------------

namespace SoundText
{
int percentOf(qint64 volume)
{
    const qint64 clamped = std::clamp<qint64>(volume, 0, normalVolume);
    return int(std::lround(double(clamped) * 100.0 / double(normalVolume)));
}

qint64 volumeOf(int percent)
{
    const int clamped = std::clamp(percent, 0, 100);
    return std::llround(double(clamped) * double(normalVolume) / 100.0);
}

Label deviceLabel(const QString &description, const QString &portDescription, const QString &cardDescription)
{
    if (portDescription.isEmpty() || portDescription.compare(description, Qt::CaseInsensitive) == 0)
        return {description, cardDescription};
    return {portDescription, cardDescription.isEmpty() ? description : cardDescription};
}

QString deviceIcon(const QString &portType, const QString &formFactor, bool input)
{
    const QString type = portType.toLower();
    const QString form = formFactor.toLower();
    if (type == QLatin1String("headset") || form == QLatin1String("headset") || form == QLatin1String("handset")
        || form == QLatin1String("hands-free"))
        return QStringLiteral("audio-headset");
    if (type == QLatin1String("headphones") || form == QLatin1String("headphone"))
        return QStringLiteral("audio-headphones");
    if (type == QLatin1String("hdmi") || form == QLatin1String("tv"))
        return QStringLiteral("video-display");
    if (form == QLatin1String("webcam"))
        return QStringLiteral("camera-web");
    if (type == QLatin1String("bluetooth"))
        return QStringLiteral("preferences-system-bluetooth");
    return input ? QStringLiteral("audio-input-microphone") : QStringLiteral("audio-speakers-symbolic");
}

namespace
{
qint64 loudest(const QStringList &channels, const QList<qint64> &volumes, const QLatin1String &side)
{
    qint64 best = -1;
    for (int i = 0; i < channels.size() && i < volumes.size(); ++i) {
        if (channels.at(i).contains(side, Qt::CaseInsensitive))
            best = std::max(best, volumes.at(i));
    }
    return best;
}
} // namespace

int balanceOf(const QStringList &channels, const QList<qint64> &volumes)
{
    const qint64 left = loudest(channels, volumes, QLatin1String("left"));
    const qint64 right = loudest(channels, volumes, QLatin1String("right"));
    if (left < 0 || right < 0 || left == right)
        return 0;
    if (left > right)
        return -int(std::lround(100.0 * (1.0 - double(right) / double(left))));
    return int(std::lround(100.0 * (1.0 - double(left) / double(right))));
}

QList<qint64> volumesFor(const QStringList &channels, qint64 volume, int balance)
{
    balance = std::clamp(balance, -100, 100);
    const qint64 quieter = std::llround(double(volume) * (1.0 - std::abs(balance) / 100.0));
    QList<qint64> volumes;
    volumes.reserve(channels.size());
    for (const QString &channel : channels) {
        const bool left = channel.contains(QLatin1String("left"), Qt::CaseInsensitive);
        const bool right = channel.contains(QLatin1String("right"), Qt::CaseInsensitive);
        if ((left && balance > 0) || (right && balance < 0))
            volumes.append(quieter);
        else
            volumes.append(volume);
    }
    return volumes;
}

bool isMonitor(const QVariantMap &properties)
{
    return properties.value(QStringLiteral("device.class")).toString() == QLatin1String("monitor");
}

QString appName(const QVariantMap &properties, const QString &streamName)
{
    for (const char *key : {"application.name", "application.process.binary"}) {
        const QString value = properties.value(QLatin1String(key)).toString().trimmed();
        if (!value.isEmpty())
            return value;
    }
    return streamName;
}

QString appIcon(const QVariantMap &properties)
{
    for (const char *key : {"application.icon_name", "application.process.binary"}) {
        const QString value = properties.value(QLatin1String(key)).toString().trimmed();
        if (!value.isEmpty())
            return value;
    }
    return QStringLiteral("applications-multimedia");
}

bool isHiddenStream(const QVariantMap &properties)
{
    const QString role = properties.value(QStringLiteral("media.role")).toString();
    const QString id = properties.value(QStringLiteral("application.id")).toString();
    return role == QLatin1String("event") || role == QLatin1String("filter")
        || id == QLatin1String("org.kde.kmixd") || id == QLatin1String("org.PulseAudio.pavucontrol")
        || id == QLatin1String("org.raye.settings") || id == QLatin1String("org.kde.plasma-pa");
}

QString profileTitle(const QString &name, const QString &description)
{
    if (name == QLatin1String("off"))
        return i18nc("@item a sound card turned off", "Off");
    return description.isEmpty() ? name : description;
}

bool deviceLessThan(bool aDefault, const QString &aTitle, bool bDefault, const QString &bTitle)
{
    if (aDefault != bDefault)
        return aDefault;
    return aTitle.toCaseFolded().localeAwareCompare(bTitle.toCaseFolded()) < 0;
}
} // namespace SoundText

// --- SoundDevices ---------------------------------------------------------

SoundDevices::SoundDevices(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(rebuildDelay);
    connect(&m_rebuild, &QTimer::timeout, this, &SoundDevices::rebuild);
    Context *c = context();
    connect(c, &Context::sinkAdded, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::sinkRemoved, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::sourceAdded, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::sourceRemoved, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::cardAdded, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::stateChanged, this, [this] {
        Q_EMIT readyChanged();
        m_rebuild.start();
    });
    if (c->server()) {
        connect(c->server(), &PulseAudioQt::Server::defaultSinkChanged, &m_rebuild, qOverload<>(&QTimer::start));
        connect(c->server(), &PulseAudioQt::Server::defaultSourceChanged, &m_rebuild, qOverload<>(&QTimer::start));
    }
    rebuild();
}

SoundDevices::~SoundDevices() = default;

void SoundDevices::setInput(bool input)
{
    if (input == m_input)
        return;
    m_input = input;
    Q_EMIT inputChanged();
    rebuild();
}

bool SoundDevices::ready() const
{
    return context()->state() == Context::State::Ready;
}

void SoundDevices::watch(Device *device)
{
    // a new default or a new port can change the order: list again
    connect(device, &Device::defaultChanged, &m_rebuild, qOverload<>(&QTimer::start), Qt::UniqueConnection);
    connect(device, &Device::activePortIndexChanged, &m_rebuild, qOverload<>(&QTimer::start), Qt::UniqueConnection);
    connect(device, &Device::descriptionChanged, this, &SoundDevices::deviceChanged, Qt::UniqueConnection);
    connect(device, &PulseAudioQt::VolumeObject::volumeChanged, this, &SoundDevices::deviceChanged, Qt::UniqueConnection);
    connect(device, &PulseAudioQt::VolumeObject::mutedChanged, this, &SoundDevices::deviceChanged, Qt::UniqueConnection);
    connect(device, &PulseAudioQt::VolumeObject::channelVolumesChanged, this, &SoundDevices::deviceChanged,
            Qt::UniqueConnection);
}

void SoundDevices::deviceChanged()
{
    auto *device = qobject_cast<Device *>(sender());
    const int row = m_devices.indexOf(device);
    if (row < 0)
        return;
    Q_EMIT dataChanged(index(row), index(row));
}

void SoundDevices::rebuild()
{
    QList<Device *> found;
    if (m_input) {
        for (Source *source : context()->sources()) {
            if (!SoundText::isMonitor(source->properties()))
                found.append(source);
        }
    } else {
        for (Sink *sink : context()->sinks())
            found.append(sink);
    }
    std::sort(found.begin(), found.end(), [](Device *a, Device *b) {
        const Port *pa = activePort(a);
        const Port *pb = activePort(b);
        const auto la = SoundText::deviceLabel(a->description(), pa ? pa->description() : QString(), {});
        const auto lb = SoundText::deviceLabel(b->description(), pb ? pb->description() : QString(), {});
        return SoundText::deviceLessThan(a->isDefault(), la.title, b->isDefault(), lb.title);
    });

    QList<QPointer<Device>> next;
    for (Device *d : found)
        next.append(d);
    if (next == m_devices) {
        if (!m_devices.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_devices.size()) - 1));
        Q_EMIT defaultRowChanged();
        return;
    }
    beginResetModel();
    m_devices = next;
    for (Device *d : found)
        watch(d);
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT defaultRowChanged();
}

Device *SoundDevices::deviceAt(int row) const
{
    return row >= 0 && row < m_devices.size() ? m_devices.at(row).data() : nullptr;
}

int SoundDevices::defaultRow() const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i) && m_devices.at(i)->isDefault())
            return i;
    }
    return -1;
}

int SoundDevices::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_devices.size());
}

QVariant SoundDevices::data(const QModelIndex &index, int role) const
{
    const Device *d = index.isValid() ? deviceAt(index.row()) : nullptr;
    if (!d)
        return {};
    const Port *port = activePort(d);
    switch (role) {
    case DeviceIndexRole:
        return int(d->index());
    case NameRole:
        return d->name();
    case TitleRole:
    case Qt::DisplayRole:
    case SubtitleRole: {
        const auto label = SoundText::deviceLabel(d->description(), port ? port->description() : QString(),
                                                  cardDescription(cardOf(d)));
        return role == SubtitleRole ? label.subtitle : label.title;
    }
    case IconRole:
        return SoundText::deviceIcon(port ? portTypeName(port->type()) : QString(), d->formFactor(), m_input);
    case DefaultRole:
        return d->isDefault();
    case VolumeRole:
        return SoundText::percentOf(d->volume());
    case MutedRole:
        return d->isMuted();
    case BalanceRole:
        return SoundText::balanceOf(d->rawChannels(), d->channelVolumes());
    case HasBalanceRole: {
        const QStringList channels = d->rawChannels();
        const bool left = std::any_of(channels.cbegin(), channels.cend(), [](const QString &c) {
            return c.contains(QLatin1String("left"), Qt::CaseInsensitive);
        });
        const bool right = std::any_of(channels.cbegin(), channels.cend(), [](const QString &c) {
            return c.contains(QLatin1String("right"), Qt::CaseInsensitive);
        });
        return left && right;
    }
    case CardIndexRole:
        return int(d->cardIndex());
    }
    return {};
}

QHash<int, QByteArray> SoundDevices::roleNames() const
{
    return {{DeviceIndexRole, "deviceIndex"}, {NameRole, "name"},       {TitleRole, "title"},
            {SubtitleRole, "subtitle"},       {IconRole, "iconName"},   {DefaultRole, "isDefault"},
            {VolumeRole, "volume"},           {MutedRole, "muted"},     {BalanceRole, "balance"},
            {HasBalanceRole, "hasBalance"},   {CardIndexRole, "cardIndex"}};
}

QVariantMap SoundDevices::get(int row) const
{
    QVariantMap map;
    if (!deviceAt(row))
        return map;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

void SoundDevices::setDefault(int row)
{
    if (Device *d = deviceAt(row))
        d->setDefault(true);
}

void SoundDevices::setVolume(int row, int percent)
{
    Device *d = deviceAt(row);
    if (!d || !d->isVolumeWritable())
        return;
    const int balance = SoundText::balanceOf(d->rawChannels(), d->channelVolumes());
    if (balance == 0)
        d->setVolume(SoundText::volumeOf(percent));
    else
        d->setChannelVolumes(SoundText::volumesFor(d->rawChannels(), SoundText::volumeOf(percent), balance));
    // a volume moved up is a volume the user wants to hear
    if (percent > 0 && d->isMuted())
        d->setMuted(false);
}

void SoundDevices::setMuted(int row, bool muted)
{
    if (Device *d = deviceAt(row))
        d->setMuted(muted);
}

void SoundDevices::setBalance(int row, int balance)
{
    Device *d = deviceAt(row);
    if (!d || !d->isVolumeWritable())
        return;
    d->setChannelVolumes(SoundText::volumesFor(d->rawChannels(), d->volume(), balance));
}

// --- SoundStreams -------------------------------------------------------

SoundStreams::SoundStreams(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(rebuildDelay);
    connect(&m_rebuild, &QTimer::timeout, this, &SoundStreams::rebuild);
    Context *c = context();
    connect(c, &Context::sinkInputAdded, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::sinkInputRemoved, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::stateChanged, &m_rebuild, qOverload<>(&QTimer::start));
    rebuild();
}

SoundStreams::~SoundStreams() = default;

void SoundStreams::rebuild()
{
    QList<QPointer<SinkInput>> next;
    for (SinkInput *s : context()->sinkInputs()) {
        if (s->isVirtualStream() || SoundText::isHiddenStream(s->properties()))
            continue;
        next.append(s);
    }
    std::sort(next.begin(), next.end(), [](const QPointer<SinkInput> &a, const QPointer<SinkInput> &b) {
        return SoundText::appName(a->properties(), a->name()).localeAwareCompare(SoundText::appName(b->properties(), b->name())) < 0;
    });
    if (next == m_streams) {
        if (!m_streams.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_streams.size()) - 1));
        return;
    }
    beginResetModel();
    m_streams = next;
    for (SinkInput *s : std::as_const(next)) {
        // once per stream, however often the list is rebuilt
        disconnect(s, nullptr, this, nullptr);
        auto changed = [this, s] {
            const int row = m_streams.indexOf(s);
            if (row >= 0)
                Q_EMIT dataChanged(index(row), index(row));
        };
        connect(s, &SinkInput::volumeChanged, this, changed);
        connect(s, &SinkInput::mutedChanged, this, changed);
        connect(s, &SinkInput::deviceIndexChanged, this, changed);
        connect(s, &SinkInput::corkedChanged, this, changed);
    }
    endResetModel();
    Q_EMIT countChanged();
}

SinkInput *SoundStreams::streamAt(int row) const
{
    return row >= 0 && row < m_streams.size() ? m_streams.at(row).data() : nullptr;
}

int SoundStreams::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_streams.size());
}

QVariant SoundStreams::data(const QModelIndex &index, int role) const
{
    const SinkInput *s = index.isValid() ? streamAt(index.row()) : nullptr;
    if (!s)
        return {};
    switch (role) {
    case StreamIndexRole:
        return int(s->index());
    case AppNameRole:
    case Qt::DisplayRole:
        return SoundText::appName(s->properties(), s->name());
    case IconRole:
        return SoundText::appIcon(s->properties());
    case VolumeRole:
        return SoundText::percentOf(s->volume());
    case MutedRole:
        return s->isMuted();
    case DeviceIndexRole:
        return int(s->deviceIndex());
    case PlayingRole:
        return !s->isCorked();
    }
    return {};
}

QHash<int, QByteArray> SoundStreams::roleNames() const
{
    return {{StreamIndexRole, "streamIndex"}, {AppNameRole, "appName"},         {IconRole, "iconName"},
            {VolumeRole, "volume"},           {MutedRole, "muted"},             {DeviceIndexRole, "deviceIndex"},
            {PlayingRole, "playing"}};
}

void SoundStreams::setVolume(int row, int percent)
{
    SinkInput *s = streamAt(row);
    if (!s || !s->isVolumeWritable())
        return;
    s->setVolume(SoundText::volumeOf(percent));
    if (percent > 0 && s->isMuted())
        s->setMuted(false);
}

void SoundStreams::setMuted(int row, bool muted)
{
    if (SinkInput *s = streamAt(row))
        s->setMuted(muted);
}

void SoundStreams::setDevice(int row, int deviceIndex)
{
    SinkInput *s = streamAt(row);
    if (!s || deviceIndex < 0)
        return;
    // only to an output that exists
    const auto sinks = context()->sinks();
    const bool known = std::any_of(sinks.cbegin(), sinks.cend(), [deviceIndex](const Sink *sink) {
        return sink->index() == quint32(deviceIndex);
    });
    if (known)
        s->setDeviceIndex(quint32(deviceIndex));
}

// --- SoundCards -----------------------------------------------------------

SoundCards::SoundCards(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(rebuildDelay);
    connect(&m_rebuild, &QTimer::timeout, this, &SoundCards::rebuild);
    Context *c = context();
    connect(c, &Context::cardAdded, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::cardRemoved, &m_rebuild, qOverload<>(&QTimer::start));
    connect(c, &Context::stateChanged, &m_rebuild, qOverload<>(&QTimer::start));
    rebuild();
}

SoundCards::~SoundCards() = default;

QString SoundCards::profileToRestore(const QList<ProfileChoice> &profiles, const QString &remembered)
{
    if (!remembered.isEmpty() && remembered != QLatin1String("off")) {
        for (const ProfileChoice &p : profiles) {
            if (p.name == remembered)
                return p.name;
        }
    }
    const ProfileChoice *best = nullptr;
    for (const ProfileChoice &p : profiles) {
        if (p.name == QLatin1String("off") || !p.available)
            continue;
        if (!best || p.priority > best->priority)
            best = &p;
    }
    return best ? best->name : QString();
}

void SoundCards::rebuild()
{
    QList<QPointer<Card>> next;
    for (Card *card : context()->cards())
        next.append(card);
    std::sort(next.begin(), next.end(), [](const QPointer<Card> &a, const QPointer<Card> &b) {
        return cardDescription(a).localeAwareCompare(cardDescription(b)) < 0;
    });
    beginResetModel();
    m_cards = next;
    for (Card *card : std::as_const(next)) {
        disconnect(card, nullptr, this, nullptr);
        auto changed = [this, card] {
            const int row = m_cards.indexOf(card);
            if (row >= 0)
                Q_EMIT dataChanged(index(row), index(row));
        };
        connect(card, &Card::activeProfileIndexChanged, this, changed);
        connect(card, &Card::profilesChanged, this, changed);
    }
    endResetModel();
    Q_EMIT countChanged();
}

Card *SoundCards::cardAt(int row) const
{
    return row >= 0 && row < m_cards.size() ? m_cards.at(row).data() : nullptr;
}

int SoundCards::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_cards.size());
}

QVariant SoundCards::data(const QModelIndex &index, int role) const
{
    const Card *card = index.isValid() ? cardAt(index.row()) : nullptr;
    if (!card)
        return {};
    const auto profiles = card->profiles();
    const quint32 active = card->activeProfileIndex();
    const QString activeName = active < quint32(profiles.size()) ? profiles.at(active)->name() : QString();
    switch (role) {
    case CardIndexRole:
        return int(card->index());
    case TitleRole:
    case Qt::DisplayRole:
        return cardDescription(card);
    case IconRole: {
        const QString icon = card->properties().value(QStringLiteral("device.icon_name")).toString();
        return icon.isEmpty() ? QStringLiteral("audio-card") : icon;
    }
    case AllowedRole:
        return !activeName.isEmpty() && activeName != QLatin1String("off");
    case ProfilesRole: {
        QVariantList list;
        for (const Profile *p : profiles) {
            if (p->name() == QLatin1String("off"))
                continue;
            list.append(QVariantMap{
                {QStringLiteral("name"), p->name()},
                {QStringLiteral("title"), SoundText::profileTitle(p->name(), p->description())},
                {QStringLiteral("available"), p->availability() != Profile::Unavailable},
            });
        }
        return list;
    }
    case ActiveProfileRole:
        return activeName;
    }
    return {};
}

QHash<int, QByteArray> SoundCards::roleNames() const
{
    return {{CardIndexRole, "cardIndex"}, {TitleRole, "title"},       {IconRole, "iconName"},
            {AllowedRole, "allowed"},     {ProfilesRole, "profiles"}, {ActiveProfileRole, "activeProfile"}};
}

void SoundCards::setProfile(int row, const QString &name)
{
    Card *card = cardAt(row);
    if (!card)
        return;
    const auto profiles = card->profiles();
    for (const Profile *p : profiles) {
        if (p->name() == name) {
            context()->setCardProfile(card->index(), name);
            return;
        }
    }
}

void SoundCards::setAllowed(int row, bool allowed)
{
    Card *card = cardAt(row);
    if (!card)
        return;
    const auto profiles = card->profiles();
    const quint32 active = card->activeProfileIndex();
    const QString current = active < quint32(profiles.size()) ? profiles.at(active)->name() : QString();
    if (!allowed) {
        if (current == QLatin1String("off"))
            return;
        m_lastProfile.insert(card->index(), current);
        context()->setCardProfile(card->index(), QStringLiteral("off"));
        return;
    }
    QList<ProfileChoice> choices;
    for (const Profile *p : profiles)
        choices.append({p->name(), p->priority(), p->availability() != Profile::Unavailable});
    const QString profile = profileToRestore(choices, m_lastProfile.value(card->index()));
    if (!profile.isEmpty())
        context()->setCardProfile(card->index(), profile);
}

// --- LevelMeter -----------------------------------------------------------

LevelMeter::LevelMeter(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &LevelMeter::read);
}

LevelMeter::~LevelMeter()
{
    if (m_source)
        m_source->stop();
}

void LevelMeter::setDevice(const QString &device)
{
    if (device == m_device)
        return;
    m_device = device;
    Q_EMIT deviceChanged();
    restart();
}

void LevelMeter::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    restart();
}

void LevelMeter::restart()
{
    m_timer.stop();
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }
    m_io = nullptr;
    setLevel(0);
    if (!m_active || m_device.isEmpty())
        return;

    const auto inputs = QMediaDevices::audioInputs();
    const auto found = std::find_if(inputs.cbegin(), inputs.cend(), [this](const QAudioDevice &d) {
        return d.id() == m_device.toUtf8();
    });
    if (found == inputs.cend())
        return;
    // a small mono capture is enough to draw a level
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    m_source = std::make_unique<QAudioSource>(*found, format);
    m_source->setBufferSize(3200);
    m_io = m_source->start();
    if (m_io)
        m_timer.start();
}

void LevelMeter::read()
{
    if (!m_io)
        return;
    const QByteArray samples = m_io->readAll();
    setLevel(smoothed(m_level, peakOf(samples)));
}

void LevelMeter::setLevel(qreal level)
{
    if (qFuzzyCompare(level + 1, m_level + 1))
        return;
    m_level = level;
    Q_EMIT levelChanged();
}

qreal LevelMeter::peakOf(const QByteArray &samples)
{
    int peak = 0;
    const qsizetype count = samples.size() / 2;
    const auto *data = reinterpret_cast<const uchar *>(samples.constData());
    for (qsizetype i = 0; i < count; ++i) {
        const int value = std::abs(int(qFromLittleEndian<qint16>(data + 2 * i)));
        peak = std::max(peak, value);
    }
    return std::min(1.0, qreal(peak) / 32768.0);
}

qreal LevelMeter::smoothed(qreal previous, qreal peak, qreal fall)
{
    if (peak >= previous)
        return peak;
    return std::max(peak, previous - fall);
}
