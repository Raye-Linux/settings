// SPDX-License-Identifier: GPL-3.0-or-later
#include "display.h"

#include <KLocalizedString>
#include <KScreen/Config>
#include <KScreen/ConfigMonitor>
#include <KScreen/EDID>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <limits>

using KScreen::ConfigPtr;
using KScreen::ModeList;
using KScreen::ModePtr;
using KScreen::Output;
using KScreen::OutputPtr;

// --- DisplayLogic --------------------------------------------------------

namespace DisplayLogic
{
QList<Resolution> resolutions(const ModeList &modes, const QString &preferredModeId)
{
    QSize preferred;
    if (const ModePtr mode = modes.value(preferredModeId))
        preferred = mode->size();
    QList<Resolution> list;
    for (const ModePtr &mode : modes) {
        const QSize size = mode->size();
        if (size.isEmpty())
            continue;
        const bool known = std::any_of(list.cbegin(), list.cend(), [size](const Resolution &r) { return r.size == size; });
        if (!known)
            list.append({size, size == preferred});
    }
    std::sort(list.begin(), list.end(), [](const Resolution &a, const Resolution &b) {
        const qint64 areaA = qint64(a.size.width()) * a.size.height();
        const qint64 areaB = qint64(b.size.width()) * b.size.height();
        return areaA != areaB ? areaA > areaB : a.size.width() > b.size.width();
    });
    return list;
}

QList<double> refreshRates(const ModeList &modes, const QSize &size)
{
    QList<double> rates;
    for (const ModePtr &mode : modes) {
        if (mode->size() != size)
            continue;
        // the server says 59.9400024: what a person reads is 59.94
        const double hz = std::round(double(mode->refreshRate()) * 100.0) / 100.0;
        if (!std::any_of(rates.cbegin(), rates.cend(), [hz](double r) { return std::abs(r - hz) < 0.005; }))
            rates.append(hz);
    }
    std::sort(rates.begin(), rates.end(), std::greater<double>());
    return rates;
}

QString refreshText(double hz)
{
    const double rounded = std::round(hz * 100.0) / 100.0;
    const bool whole = std::abs(rounded - std::round(rounded)) < 0.005;
    const QString number = whole ? QLocale().toString(qRound(rounded)) : QLocale().toString(rounded, 'f', 2);
    return i18nc("@item refresh rate of a screen", "%1 Hz", number);
}

QString resolutionText(const QSize &size, bool recommended)
{
    const QString text = i18nc("@item screen resolution, width × height", "%1 × %2", QString::number(size.width()), QString::number(size.height()));
    return recommended ? i18nc("@item %1 is a resolution", "%1 (Recommended)", text) : text;
}

QString modeFor(const ModeList &modes, const QSize &size, double hz)
{
    QString best;
    double distance = std::numeric_limits<double>::max();
    for (const ModePtr &mode : modes) {
        if (mode->size() != size)
            continue;
        const double d = std::abs(double(mode->refreshRate()) - hz);
        if (d < distance) {
            distance = d;
            best = mode->id();
        }
    }
    return best;
}

int recommendedScale(const QSize &pixels, const QSize &millimetres)
{
    if (pixels.isEmpty() || millimetres.width() <= 0 || millimetres.height() <= 0)
        return 100;
    // a TV or a projector reports a made-up size (16 × 9 mm): ignore it
    if (millimetres.width() < 100)
        return 100;
    const double diagonalPixels = std::hypot(pixels.width(), pixels.height());
    const double diagonalInches = std::hypot(millimetres.width(), millimetres.height()) / 25.4;
    const double dpi = diagonalPixels / diagonalInches;
    const int scale = int(std::lround(dpi / 110.0 * 4.0)) * 25;
    return std::clamp(scale, 100, 300);
}

QList<int> scaleSteps(int current)
{
    QList<int> steps;
    for (int s = 100; s <= 300; s += 25)
        steps.append(s);
    if (current > 0 && !steps.contains(current)) {
        steps.append(current);
        std::sort(steps.begin(), steps.end());
    }
    return steps;
}

int degreesOf(int rotation)
{
    switch (rotation) {
    case Output::Left:
        return 90;
    case Output::Inverted:
        return 180;
    case Output::Right:
        return 270;
    default:
        return 0;
    }
}

int rotationOf(int degrees)
{
    switch (((degrees % 360) + 360) % 360) {
    case 90:
        return Output::Left;
    case 180:
        return Output::Inverted;
    case 270:
        return Output::Right;
    default:
        return Output::None;
    }
}

QList<QRect> normalized(QList<QRect> screens)
{
    if (screens.isEmpty())
        return screens;
    int left = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    for (const QRect &r : std::as_const(screens)) {
        left = std::min(left, r.x());
        top = std::min(top, r.y());
    }
    for (QRect &r : screens)
        r.translate(-left, -top);
    return screens;
}

QList<QRect> placeDropped(const QList<QRect> &screens, int moving, const QRect &dropped)
{
    QList<QRect> result = screens;
    if (moving < 0 || moving >= screens.size())
        return normalized(result);
    QList<QRect> others = screens;
    others.removeAt(moving);
    if (others.isEmpty()) {
        result[moving] = QRect(QPoint(0, 0), dropped.size());
        return normalized(result);
    }

    const int w = dropped.width();
    const int h = dropped.height();
    auto overlaps = [&others](const QRect &candidate) {
        return std::any_of(others.cbegin(), others.cend(), [&candidate](const QRect &o) { return o.intersects(candidate); });
    };
    // every place touching an edge of a screen, slid along that edge to
    // be as close to the drop as it can
    QRect best;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (const QRect &o : std::as_const(others)) {
        const int y = std::clamp(dropped.y(), o.y() - h + 1, o.y() + o.height() - 1);
        const int x = std::clamp(dropped.x(), o.x() - w + 1, o.x() + o.width() - 1);
        const QList<QRect> candidates = {
            QRect(o.x() + o.width(), y, w, h), // right
            QRect(o.x() - w, y, w, h),         // left
            QRect(x, o.y() + o.height(), w, h), // below
            QRect(x, o.y() - h, w, h),         // above
        };
        for (const QRect &c : candidates) {
            if (overlaps(c))
                continue;
            const qint64 dx = c.x() - dropped.x();
            const qint64 dy = c.y() - dropped.y();
            const qint64 distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = c;
            }
        }
    }
    if (bestDistance == std::numeric_limits<qint64>::max()) {
        // boxed in: to the right of everything
        int right = 0;
        for (const QRect &o : std::as_const(others))
            right = std::max(right, o.x() + o.width());
        best = QRect(right, 0, w, h);
    }
    result[moving] = best;
    return normalized(result);
}
} // namespace DisplayLogic

// --- DisplayConfig ---------------------------------------------------------

namespace
{
bool applyThroughKScreen(const ConfigPtr &config)
{
    // the outputs must be the ones the system has now, each one usable
    if (!KScreen::Config::canBeApplied(config))
        return false;
    auto *operation = new KScreen::SetConfigOperation(config);
    QObject::connect(operation, &KScreen::ConfigOperation::finished, operation, [](KScreen::ConfigOperation *op) {
        if (op->hasError())
            qWarning("raye-settings: display: %s", qPrintable(op->errorString()));
    });
    // a configuration operation starts by itself and deletes itself after
    return true;
}

// Where an output sits and how big it is on the desktop, in the
// logical pixels positions are given in, as the configuration computes
// it from the mode, the rotation and the scale.
QRect logicalGeometry(const ConfigPtr &config, const OutputPtr &output)
{
    return QRect(output->pos(), config->logicalSizeForOutputInt(*output));
}

// After a new mode, rotation or scale: the size the system will give it.
void refreshLogicalSize(const ConfigPtr &config, const OutputPtr &output)
{
    output->setExplicitLogicalSize(config->logicalSizeForOutput(*output));
}

QString outputName(const OutputPtr &output)
{
    if (output->type() == Output::Panel)
        return i18nc("@item the screen of a laptop", "Built-in display");
    if (const KScreen::Edid *edid = output->edid()) {
        const QString name = edid->name().trimmed();
        if (!name.isEmpty())
            return name;
        const QString vendor = edid->vendor().trimmed();
        if (!vendor.isEmpty())
            return vendor;
    }
    return output->name();
}
} // namespace

DisplayConfig::DisplayConfig(QObject *parent)
    : QObject(parent)
    , m_apply(applyThroughKScreen)
{
    m_confirm.setInterval(1000);
    connect(&m_confirm, &QTimer::timeout, this, &DisplayConfig::tick);
    load();
}

DisplayConfig::DisplayConfig(const ConfigPtr &config, std::function<bool(const ConfigPtr &)> apply, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_apply(std::move(apply))
{
    m_confirm.setInterval(1000);
    connect(&m_confirm, &QTimer::timeout, this, &DisplayConfig::tick);
    const OutputPtr primary = m_config ? m_config->primaryOutput() : OutputPtr();
    const auto list = connected();
    m_selected = primary ? primary->id() : (list.isEmpty() ? -1 : list.first()->id());
}

DisplayConfig::~DisplayConfig()
{
    // leaving the page with a change still waiting: it was not kept
    if (m_secondsLeft > 0)
        revertChanges();
}

void DisplayConfig::load()
{
    auto *operation = new KScreen::GetConfigOperation();
    connect(operation, &KScreen::ConfigOperation::finished, this, [this](KScreen::ConfigOperation *op) {
        if (op->hasError()) {
            setError(i18n("Raye can't read your display settings right now."));
            return;
        }
        m_config = qobject_cast<KScreen::GetConfigOperation *>(op)->config();
        KScreen::ConfigMonitor::instance()->addConfig(m_config);
        connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged, this,
                &DisplayConfig::changed, Qt::UniqueConnection);
        const OutputPtr primary = m_config->primaryOutput();
        const auto list = connected();
        m_selected = primary ? primary->id() : (list.isEmpty() ? -1 : list.first()->id());
        Q_EMIT changed();
    });
}

QList<OutputPtr> DisplayConfig::connected() const
{
    QList<OutputPtr> list;
    if (!m_config)
        return list;
    const auto all = m_config->outputs();
    for (const OutputPtr &o : all) {
        if (o->isConnected())
            list.append(o);
    }
    // numbered as the system names them: by output id
    std::sort(list.begin(), list.end(), [](const OutputPtr &a, const OutputPtr &b) { return a->id() < b->id(); });
    return list;
}

OutputPtr DisplayConfig::selectedOutput() const
{
    return m_config ? m_config->output(m_selected) : OutputPtr();
}

void DisplayConfig::setSelected(int outputId)
{
    if (outputId == m_selected || !m_config || !m_config->output(outputId))
        return;
    m_selected = outputId;
    Q_EMIT changed();
}

QVariantList DisplayConfig::outputs() const
{
    QVariantList list;
    const auto all = connected();
    int number = 0;
    for (const OutputPtr &o : all) {
        ++number;
        const QRect g = logicalGeometry(m_config, o);
        list.append(QVariantMap{
            {QStringLiteral("id"), o->id()},
            {QStringLiteral("number"), number},
            {QStringLiteral("name"), outputName(o)},
            {QStringLiteral("connector"), o->name()},
            {QStringLiteral("enabled"), o->isEnabled()},
            {QStringLiteral("primary"), o->priority() == 1},
            {QStringLiteral("builtIn"), o->type() == Output::Panel},
            {QStringLiteral("x"), g.x()},
            {QStringLiteral("y"), g.y()},
            {QStringLiteral("width"), g.width()},
            {QStringLiteral("height"), g.height()},
        });
    }
    return list;
}

QVariantList DisplayConfig::resolutions() const
{
    QVariantList list;
    const OutputPtr o = selectedOutput();
    if (!o)
        return list;
    const auto all = DisplayLogic::resolutions(o->modes(), o->preferredModeId());
    for (const auto &r : all) {
        list.append(QVariantMap{{QStringLiteral("text"), DisplayLogic::resolutionText(r.size, r.recommended)},
                                {QStringLiteral("width"), r.size.width()},
                                {QStringLiteral("height"), r.size.height()},
                                {QStringLiteral("recommended"), r.recommended}});
    }
    return list;
}

int DisplayConfig::resolutionIndex() const
{
    const OutputPtr o = selectedOutput();
    const ModePtr mode = o ? o->currentMode() : ModePtr();
    if (!mode)
        return -1;
    const auto all = DisplayLogic::resolutions(o->modes(), o->preferredModeId());
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).size == mode->size())
            return i;
    }
    return -1;
}

QVariantList DisplayConfig::refreshRates() const
{
    QVariantList list;
    const OutputPtr o = selectedOutput();
    const ModePtr mode = o ? o->currentMode() : ModePtr();
    if (!mode)
        return list;
    for (double hz : DisplayLogic::refreshRates(o->modes(), mode->size()))
        list.append(QVariantMap{{QStringLiteral("text"), DisplayLogic::refreshText(hz)}, {QStringLiteral("hz"), hz}});
    return list;
}

int DisplayConfig::refreshIndex() const
{
    const OutputPtr o = selectedOutput();
    const ModePtr mode = o ? o->currentMode() : ModePtr();
    if (!mode)
        return -1;
    const auto rates = DisplayLogic::refreshRates(o->modes(), mode->size());
    const double hz = std::round(double(mode->refreshRate()) * 100.0) / 100.0;
    for (int i = 0; i < rates.size(); ++i) {
        if (std::abs(rates.at(i) - hz) < 0.005)
            return i;
    }
    return -1;
}

int DisplayConfig::scale() const
{
    const OutputPtr o = selectedOutput();
    return o ? int(std::lround(o->scale() * 100.0)) : 100;
}

int DisplayConfig::recommendedScale() const
{
    const OutputPtr o = selectedOutput();
    if (!o)
        return 100;
    const ModePtr preferred = o->preferredMode();
    return DisplayLogic::recommendedScale(preferred ? preferred->size() : QSize(), o->sizeMm());
}

QVariantList DisplayConfig::scales() const
{
    QVariantList list;
    const int recommended = recommendedScale();
    for (int s : DisplayLogic::scaleSteps(scale())) {
        const QString percent = i18nc("@item a scale in percent", "%1%", s);
        list.append(QVariantMap{
            {QStringLiteral("text"), s == recommended ? i18nc("@item %1 is a scale", "%1 (Recommended)", percent) : percent},
            {QStringLiteral("value"), s}});
    }
    return list;
}

int DisplayConfig::orientation() const
{
    const OutputPtr o = selectedOutput();
    return o ? DisplayLogic::degreesOf(o->rotation()) : 0;
}

bool DisplayConfig::hdrCapable() const
{
    const OutputPtr o = selectedOutput();
    return o && o->capabilities().testFlag(Output::Capability::HighDynamicRange);
}

bool DisplayConfig::hdrEnabled() const
{
    const OutputPtr o = selectedOutput();
    return o && o->isHdrEnabled();
}

int DisplayConfig::sdrBrightness() const
{
    const OutputPtr o = selectedOutput();
    return o ? int(o->sdrBrightness()) : 0;
}

bool DisplayConfig::vrrCapable() const
{
    const OutputPtr o = selectedOutput();
    return o && o->capabilities().testFlag(Output::Capability::Vrr);
}

bool DisplayConfig::vrrEnabled() const
{
    const OutputPtr o = selectedOutput();
    return o && vrrCapable() && o->vrrPolicy() != Output::VrrPolicy::Never;
}

QString DisplayConfig::mode() const
{
    const auto list = connected();
    int enabled = 0;
    bool replicated = false;
    for (const OutputPtr &o : list) {
        if (o->isEnabled())
            ++enabled;
        if (o->isEnabled() && o->replicationSource() != 0)
            replicated = true;
    }
    if (list.size() > 1 && enabled == 1)
        return QStringLiteral("only");
    return replicated ? QStringLiteral("duplicate") : QStringLiteral("extend");
}

void DisplayConfig::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

void DisplayConfig::apply(bool needsConfirmation)
{
    if (!m_config)
        return;
    if (!m_apply(m_config)) {
        setError(i18n("These display settings can't be used. Nothing was changed."));
        if (m_before) {
            m_config->apply(m_before);
            m_before.reset();
        }
        Q_EMIT changed();
        return;
    }
    setError({});
    if (needsConfirmation) {
        m_secondsLeft = confirmSeconds;
        m_confirm.start();
        Q_EMIT confirmingChanged();
    }
    Q_EMIT changed();
}

// Before a change that asks to be kept: remember what to go back to,
// unless a change is already waiting (going back then undoes both).
void DisplayConfig::rememberBefore()
{
    if (!m_before && m_config)
        m_before = m_config->clone();
}

void DisplayConfig::setResolution(int index)
{
    const OutputPtr o = selectedOutput();
    if (!o)
        return;
    const auto all = DisplayLogic::resolutions(o->modes(), o->preferredModeId());
    if (index < 0 || index >= all.size() || index == resolutionIndex())
        return;
    // keep the rate when the new resolution has it, else its best one
    const ModePtr current = o->currentMode();
    const QSize size = all.at(index).size;
    const auto rates = DisplayLogic::refreshRates(o->modes(), size);
    const double hz = current ? double(current->refreshRate()) : (rates.isEmpty() ? 60.0 : rates.first());
    const QString mode = DisplayLogic::modeFor(o->modes(), size, hz);
    if (mode.isEmpty())
        return;
    rememberBefore();
    o->setCurrentModeId(mode);
    refreshLogicalSize(m_config, o);
    // a smaller or bigger screen still touches its neighbours
    place(o->id(), o->pos());
    apply(true);
}

void DisplayConfig::setRefreshRate(int index)
{
    const OutputPtr o = selectedOutput();
    const ModePtr current = o ? o->currentMode() : ModePtr();
    if (!current)
        return;
    const auto rates = DisplayLogic::refreshRates(o->modes(), current->size());
    if (index < 0 || index >= rates.size() || index == refreshIndex())
        return;
    const QString mode = DisplayLogic::modeFor(o->modes(), current->size(), rates.at(index));
    if (mode.isEmpty())
        return;
    rememberBefore();
    o->setCurrentModeId(mode);
    refreshLogicalSize(m_config, o);
    apply(true);
}

void DisplayConfig::setOrientation(int degrees)
{
    const OutputPtr o = selectedOutput();
    if (!o || DisplayLogic::degreesOf(o->rotation()) == ((degrees % 360) + 360) % 360)
        return;
    rememberBefore();
    o->setRotation(Output::Rotation(DisplayLogic::rotationOf(degrees)));
    refreshLogicalSize(m_config, o);
    place(o->id(), o->pos());
    apply(true);
}

void DisplayConfig::setMode(const QString &mode, int outputId)
{
    const auto list = connected();
    if (!m_config || list.size() < 2)
        return;
    if (mode == QLatin1String("only") && !m_config->output(outputId))
        return;
    if (mode != QLatin1String("extend") && mode != QLatin1String("duplicate") && mode != QLatin1String("only"))
        return;
    rememberBefore();
    if (mode == QLatin1String("only")) {
        for (const OutputPtr &o : list) {
            o->setEnabled(o->id() == outputId);
            o->setReplicationSource(0);
        }
        m_config->setOutputPriority(m_config->output(outputId), 1);
        m_selected = outputId;
    } else if (mode == QLatin1String("duplicate")) {
        const OutputPtr primary = m_config->primaryOutput() ? m_config->primaryOutput() : list.first();
        for (const OutputPtr &o : list) {
            o->setEnabled(true);
            o->setPos(primary->pos());
            o->setReplicationSource(o == primary ? 0 : primary->id());
        }
    } else {
        // side by side, in their order, starting at 0,0
        int x = 0;
        for (const OutputPtr &o : list) {
            o->setEnabled(true);
            o->setReplicationSource(0);
            o->setPos(QPoint(x, 0));
            x += logicalGeometry(m_config, o).width() > 0 ? logicalGeometry(m_config, o).width() : 1920;
        }
    }
    apply(true);
}

void DisplayConfig::setScale(int percent)
{
    const OutputPtr o = selectedOutput();
    if (!o || percent < 100 || percent > 500 || percent == scale())
        return;
    // the other screens keep touching this one once its size changes
    const QRect before = logicalGeometry(m_config, o);
    o->setScale(percent / 100.0);
    refreshLogicalSize(m_config, o);
    const QRect after = logicalGeometry(m_config, o);
    if (before.size() != after.size())
        place(o->id(), after.topLeft());
    apply(false);
}

void DisplayConfig::setHdrEnabled(bool on)
{
    const OutputPtr o = selectedOutput();
    if (!o || !hdrCapable() || o->isHdrEnabled() == on)
        return;
    o->setHdrEnabled(on);
    apply(false);
}

void DisplayConfig::setSdrBrightness(int nits)
{
    const OutputPtr o = selectedOutput();
    if (!o || !hdrCapable())
        return;
    o->setSdrBrightness(uint32_t(std::clamp(nits, 80, 500)));
    apply(false);
}

void DisplayConfig::setVrrEnabled(bool on)
{
    const OutputPtr o = selectedOutput();
    if (!o || !vrrCapable() || vrrEnabled() == on)
        return;
    o->setVrrPolicy(on ? Output::VrrPolicy::Automatic : Output::VrrPolicy::Never);
    apply(false);
}

void DisplayConfig::makePrimary(int outputId)
{
    const OutputPtr o = m_config ? m_config->output(outputId) : OutputPtr();
    if (!o || o->priority() == 1)
        return;
    m_config->setOutputPriority(o, 1);
    apply(false);
}

void DisplayConfig::moveOutput(int outputId, int x, int y)
{
    if (place(outputId, QPoint(x, y)))
        apply(false);
}

bool DisplayConfig::place(int outputId, const QPoint &where)
{
    const auto list = connected();
    QList<QRect> rects;
    QList<OutputPtr> enabled;
    int moving = -1;
    for (const OutputPtr &o : list) {
        if (!o->isEnabled())
            continue;
        if (o->id() == outputId)
            moving = int(enabled.size());
        enabled.append(o);
        rects.append(logicalGeometry(m_config, o));
    }
    if (moving < 0)
        return false;
    const QRect dropped(where, rects.at(moving).size());
    const QList<QRect> placed = DisplayLogic::placeDropped(rects, moving, dropped);
    for (int i = 0; i < enabled.size(); ++i)
        enabled.at(i)->setPos(placed.at(i).topLeft());
    return true;
}

void DisplayConfig::keepChanges()
{
    m_confirm.stop();
    m_secondsLeft = 0;
    m_before.reset();
    Q_EMIT confirmingChanged();
}

void DisplayConfig::revertChanges()
{
    m_confirm.stop();
    m_secondsLeft = 0;
    if (m_before && m_config) {
        m_config->apply(m_before);
        m_before.reset();
        m_apply(m_config);
    }
    Q_EMIT confirmingChanged();
    Q_EMIT changed();
}

void DisplayConfig::tick()
{
    if (m_secondsLeft <= 0)
        return;
    --m_secondsLeft;
    Q_EMIT confirmingChanged();
    if (m_secondsLeft == 0)
        revertChanges();
}
