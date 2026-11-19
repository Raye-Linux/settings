// SPDX-License-Identifier: GPL-3.0-or-later
#include "modulehost.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KQuickConfigModule>
#include <KQuickConfigModuleLoader>
#include <KSharedConfig>
#include <KWindowConfig>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QWindow>

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {
// where Plasma installs its settings modules: the System Settings ones,
// and the system information ones (About and friends)
const QStringList quickModules = {QStringLiteral("plasma/kcms/systemsettings"), QStringLiteral("plasma/kcms")};
const QString widgetModules = QStringLiteral("plasma/kcms/systemsettings_qwidgets");

KPluginMetaData findQuick(const QString &kcm)
{
    for (const QString &dir : quickModules) {
        const KPluginMetaData md = KPluginMetaData::findPluginById(dir, kcm);
        if (md.isValid())
            return md;
    }
    return {};
}

KSharedConfig::Ptr frameConfig()
{
    return KSharedConfig::openConfig(QStringLiteral("raye-settingsrc"));
}
} // namespace

ModuleHost::ModuleHost(QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    // the window owns the engine; the loader only borrows it
    , m_engine(engine, [](QQmlEngine *) {})
{
}

ModuleHost::~ModuleHost()
{
    close();
}

QQuickItem *ModuleHost::content() const
{
    return m_module ? m_module->mainUi() : nullptr;
}

bool ModuleHost::needsSave() const
{
    return m_module && m_module->needsSave();
}

QString ModuleHost::kcmshellProgram(const QString &root)
{
    const QString diverted = root + QStringLiteral("/usr/bin/kcmshell6.kde");
    if (QFileInfo(diverted).isExecutable())
        return diverted;
    // a system without our package: KDE's command is still the real one
    return QStringLiteral("kcmshell6");
}

bool ModuleHost::isModuleId(const QString &kcm)
{
    static const QRegularExpression id(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$"));
    return id.match(kcm).hasMatch();
}

void ModuleHost::startExternal(const QString &kcm)
{
    // the id goes as one argument after "--", never through a shell
    QProcess::startDetached(kcmshellProgram(), {QStringLiteral("--"), kcm});
}

bool ModuleHost::open(const QString &kcm)
{
    if (kcm == m_current && (m_module || m_external))
        return m_module;
    close();
    setError({});
    if (!isModuleId(kcm)) {
        setError(i18n("This settings page could not be found."));
        return false;
    }

    const KPluginMetaData quick = findQuick(kcm);
    if (quick.isValid()) {
        auto result = KQuickConfigModuleLoader::loadModule(quick, this, {}, m_engine);
        if (result) {
            m_module = result.plugin;
            m_current = kcm;
            connect(m_module, &KQuickConfigModule::needsSaveChanged, this, &ModuleHost::needsSaveChanged);
            m_module->load();
            Q_EMIT contentChanged();
            Q_EMIT needsSaveChanged();
            return true;
        }
        // could not be embedded: still give the user the page, in its
        // own window, and keep the reason for the log
        qWarning("raye-settings: %s: %s", qPrintable(kcm), qPrintable(result.errorString));
    } else if (!KPluginMetaData::findPluginById(widgetModules, kcm).isValid()) {
        setError(i18n("This part of Settings is not installed."));
        return false;
    }

    m_current = kcm;
    m_external = true;
    startExternal(kcm);
    Q_EMIT contentChanged();
    return false;
}

void ModuleHost::openExternal(const QString &kcm)
{
    if (isModuleId(kcm))
        startExternal(kcm);
}

void ModuleHost::close()
{
    const bool had = m_module || m_external || !m_current.isEmpty();
    if (m_module) {
        if (QQuickItem *ui = m_module->mainUi())
            ui->setParentItem(nullptr);
        m_module->deleteLater();
        m_module.clear();
    }
    m_current.clear();
    m_external = false;
    if (had) {
        Q_EMIT contentChanged();
        Q_EMIT needsSaveChanged();
    }
}

void ModuleHost::save()
{
    if (m_module)
        m_module->save();
}

void ModuleHost::reset()
{
    if (m_module)
        m_module->load();
}

void ModuleHost::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

// --- FrameSettings ----------------------------------------------------

FrameSettings::FrameSettings(const QString &root, QObject *parent)
    : QObject(parent)
    , m_root(root == QLatin1String("/") ? QString() : root)
{
}

void FrameSettings::releaseMemory()
{
    if (QQmlEngine *engine = qmlEngine(this)) {
        engine->collectGarbage();
        engine->trimComponentCache();
    }
#ifdef __GLIBC__
    // glibc keeps freed blocks for later otherwise, and Settings would
    // stay the size of every page it ever showed
    malloc_trim(0);
#endif
}

QStringList FrameSettings::hiddenCards() const
{
    return frameConfig()->group(QStringLiteral("Home")).readEntry(QStringLiteral("Hidden"), QStringList());
}

void FrameSettings::hideCard(const QString &id)
{
    QStringList hidden = hiddenCards();
    if (id.isEmpty() || hidden.contains(id))
        return;
    hidden.append(id);
    KConfigGroup group = frameConfig()->group(QStringLiteral("Home"));
    group.writeEntry(QStringLiteral("Hidden"), hidden);
    group.sync();
    Q_EMIT hiddenCardsChanged();
}

void FrameSettings::showAllCards()
{
    KConfigGroup group = frameConfig()->group(QStringLiteral("Home"));
    if (!group.hasKey(QStringLiteral("Hidden")))
        return;
    group.deleteEntry(QStringLiteral("Hidden"));
    group.sync();
    Q_EMIT hiddenCardsChanged();
}

QString FrameSettings::readFirstLine(const QString &path) const
{
    QFile file(m_root + path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

QString FrameSettings::processorName(const QString &cpuinfo)
{
    static const QRegularExpression model(QStringLiteral("^model name\\s*:\\s*(.+)$"),
                                          QRegularExpression::MultilineOption);
    QString name = model.match(cpuinfo).captured(1).simplified();
    // "(R)" and "(TM)" add nothing a person reads
    name.remove(QStringLiteral("(R)"), Qt::CaseInsensitive);
    name.remove(QStringLiteral("(TM)"), Qt::CaseInsensitive);
    return name.simplified();
}

qint64 FrameSettings::memoryBytes(const QString &meminfo)
{
    static const QRegularExpression total(QStringLiteral("^MemTotal:\\s*(\\d+)\\s*kB"),
                                          QRegularExpression::MultilineOption);
    return total.match(meminfo).captured(1).toLongLong() * 1024;
}

QString FrameSettings::chassisOf(const QString &chassisType)
{
    // SMBIOS chassis types
    switch (chassisType.toInt()) {
    case 8: case 9: case 10: case 14: case 31: case 32:
        return QStringLiteral("laptop");
    case 30:
        return QStringLiteral("tablet");
    case 13:
        return QStringLiteral("all-in-one");
    }
    return QStringLiteral("desktop");
}

QString FrameSettings::sizeText(qint64 bytes)
{
    const double gib = double(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 1000.0) {
        const double tib = gib / 1024.0;
        return i18nc("@label size of a disk", "%1 TB", QLocale().toString(tib, 'f', tib < 10 ? 1 : 0));
    }
    if (gib >= 10.0 || gib < 0.05)
        return i18nc("@label size of a disk or memory", "%1 GB", QLocale().toString(qRound64(gib)));
    return i18nc("@label size of a disk or memory", "%1 GB", QLocale().toString(gib, 'f', 1));
}

QVariantMap FrameSettings::device()
{
    if (!m_device.isEmpty())
        return m_device;

    QString vendor = readFirstLine(QStringLiteral("/sys/class/dmi/id/sys_vendor"));
    QString product = readFirstLine(QStringLiteral("/sys/class/dmi/id/product_name"));
    // what a board maker leaves when nobody filled the fields in
    static const QRegularExpression placeholder(
        QStringLiteral("to be filled|system product name|system manufacturer|default string|^o\\.?e\\.?m\\.?$|^none$"),
        QRegularExpression::CaseInsensitiveOption);
    if (placeholder.match(vendor).hasMatch())
        vendor.clear();
    if (placeholder.match(product).hasMatch())
        product.clear();
    QString model = product.startsWith(vendor, Qt::CaseInsensitive) ? product : (vendor + QLatin1Char(' ') + product);
    model = model.simplified();

    QString cpuinfo, meminfo;
    QFile cpu(m_root + QStringLiteral("/proc/cpuinfo"));
    if (cpu.open(QIODevice::ReadOnly | QIODevice::Text))
        cpuinfo = QString::fromUtf8(cpu.read(64 * 1024));
    QFile mem(m_root + QStringLiteral("/proc/meminfo"));
    if (mem.open(QIODevice::ReadOnly | QIODevice::Text))
        meminfo = QString::fromUtf8(mem.read(16 * 1024));

    const qint64 memory = memoryBytes(meminfo);
    // MemTotal leaves out what the firmware keeps: round up to what was bought
    const qint64 gib = qint64(1024) * 1024 * 1024;
    const qint64 shownMemory = memory > 0 ? ((memory + gib / 2) / gib) * gib : 0;

    const QStorageInfo storage(m_root.isEmpty() ? QStringLiteral("/") : m_root);
    const qint64 total = storage.isValid() ? storage.bytesTotal() : 0;
    const qint64 free = storage.isValid() ? storage.bytesAvailable() : 0;

    m_device = QVariantMap{
        {QStringLiteral("model"), model},
        {QStringLiteral("chassis"), chassisOf(readFirstLine(QStringLiteral("/sys/class/dmi/id/chassis_type")))},
        {QStringLiteral("processor"), processorName(cpuinfo)},
        {QStringLiteral("memory"), shownMemory > 0 ? sizeText(shownMemory) : QString()},
        {QStringLiteral("storageTotal"), total},
        {QStringLiteral("storageFree"), free},
        {QStringLiteral("storageText"),
         total > 0 ? i18nc("@label disk space", "%1 free of %2", sizeText(free), sizeText(total)) : QString()},
    };
    return m_device;
}

QVariantList FrameSettings::looks() const
{
    const QString current = KSharedConfig::openConfig(QStringLiteral("kdeglobals"))
                                ->group(QStringLiteral("KDE"))
                                .readEntry(QStringLiteral("LookAndFeelPackage"), QStringLiteral("org.raye.night.desktop"));
    const QList<QPair<QString, QString>> known = {
        {QStringLiteral("org.raye.night.desktop"), i18nc("@label name of a theme", "Raye Night")},
        {QStringLiteral("org.raye.day.desktop"), i18nc("@label name of a theme", "Raye Day")},
    };
    QVariantList list;
    for (const auto &[id, name] : known) {
        const QString relative = QStringLiteral("plasma/look-and-feel/") + id;
        const QString folder = m_root.isEmpty()
            ? QStandardPaths::locate(QStandardPaths::GenericDataLocation, relative, QStandardPaths::LocateDirectory)
            : m_root + QStringLiteral("/usr/share/") + relative;
        if (folder.isEmpty() || !QFileInfo(folder).isDir())
            continue;
        const QString preview = folder + QStringLiteral("/contents/previews/preview.png");
        list.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("preview"), QFileInfo::exists(preview) ? QUrl::fromLocalFile(preview) : QUrl()},
            {QStringLiteral("current"), id == current},
        });
    }
    return list;
}

bool FrameSettings::applyLook(const QString &id)
{
    bool known = false;
    const QVariantList list = looks();
    for (const QVariant &look : list)
        known = known || look.toMap().value(QStringLiteral("id")).toString() == id;
    if (!known)
        return false;
    // the colours, style and icons of the look; the panel layout stays
    // as the user left it (no --resetLayout)
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        // the tool wrote kdeglobals: read which look is current again
        KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->reparseConfiguration();
        Q_EMIT looksChanged();
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(QStringLiteral("plasma-apply-lookandfeel"), {QStringLiteral("-a"), id});
    return true;
}

bool FrameSettings::hasComponent(const QString &component) const
{
    if (component.isEmpty() || component.contains(QStringLiteral("..")))
        return false;
    return QFile::exists(QStringLiteral(":/qt/qml/org/raye/settings/") + component);
}

void FrameSettings::restoreWindow(QWindow *window)
{
    if (!window)
        return;
    const KConfigGroup group = frameConfig()->group(QStringLiteral("Window"));
    KWindowConfig::restoreWindowSize(window, group);
    KWindowConfig::restoreWindowPosition(window, group);
}

void FrameSettings::saveWindow(QWindow *window)
{
    if (!window)
        return;
    KConfigGroup group = frameConfig()->group(QStringLiteral("Window"));
    KWindowConfig::saveWindowSize(window, group);
    KWindowConfig::saveWindowPosition(window, group);
    group.sync();
}
