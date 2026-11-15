// SPDX-License-Identifier: GPL-3.0-or-later
#include "deviceinfo.h"

#include <KLocalizedString>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>

#include <cmath>

namespace {

QString readText(const QString &root, const QString &path, qint64 max = 1 << 20)
{
    QFile f(root + QLatin1Char('/') + path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.read(max));
}

QString readLine(const QString &root, const QString &path)
{
    return readText(root, path, 4096).trimmed();
}

// What firmware writes when the maker filled nothing in.
bool isJunk(const QString &text)
{
    static const QStringList junk = {
        QStringLiteral("to be filled by o.e.m."), QStringLiteral("to be filled by oem"),
        QStringLiteral("o.e.m."), QStringLiteral("oem"), QStringLiteral("default string"),
        QStringLiteral("system manufacturer"), QStringLiteral("system product name"),
        QStringLiteral("system version"), QStringLiteral("not applicable"), QStringLiteral("none"),
        QStringLiteral("not specified"), QStringLiteral("unknown"), QStringLiteral("type1productconfigid"),
        QStringLiteral("all series"), QStringLiteral("x.x"), QStringLiteral("0123456789"),
    };
    const QString t = text.trimmed().toLower();
    return t.isEmpty() || junk.contains(t);
}

QString collapse(QString text)
{
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    return text.replace(spaces, QStringLiteral(" ")).trimmed();
}

const qint64 KiB = 1024;
const qint64 GiB = qint64(1) << 30;

} // namespace

DeviceInfo::DeviceInfo(const QString &root, QObject *parent)
    : QObject(parent)
{
    const QString base = root.isEmpty() ? QStringLiteral("/") : root;
    // reading pci.ids and the DMI files takes a moment: not on the GUI thread
    QPointer<DeviceInfo> self(this);
    m_thread = QThread::create([self, base]() {
        const Facts facts = read(base);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, facts]() {
                if (self)
                    self->setFacts(facts);
            },
            Qt::QueuedConnection);
    });
    m_thread->start(QThread::LowPriority);
}

DeviceInfo::~DeviceInfo()
{
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

void DeviceInfo::setFacts(const Facts &facts)
{
    m_facts = facts;
    m_ready = true;
    Q_EMIT changed();
}

QString DeviceInfo::cleanVendor(const QString &vendor)
{
    if (isJunk(vendor))
        return QString();
    static const QList<std::pair<QString, QString>> known = {
        {QStringLiteral("dell"), QStringLiteral("Dell")},
        {QStringLiteral("lenovo"), QStringLiteral("Lenovo")},
        {QStringLiteral("hewlett-packard"), QStringLiteral("HP")},
        {QStringLiteral("hp"), QStringLiteral("HP")},
        {QStringLiteral("asustek"), QStringLiteral("ASUS")},
        {QStringLiteral("asus"), QStringLiteral("ASUS")},
        {QStringLiteral("micro-star"), QStringLiteral("MSI")},
        {QStringLiteral("gigabyte"), QStringLiteral("Gigabyte")},
        {QStringLiteral("acer"), QStringLiteral("Acer")},
        {QStringLiteral("samsung"), QStringLiteral("Samsung")},
        {QStringLiteral("toshiba"), QStringLiteral("Toshiba")},
        {QStringLiteral("dynabook"), QStringLiteral("Dynabook")},
        {QStringLiteral("apple"), QStringLiteral("Apple")},
        {QStringLiteral("microsoft"), QStringLiteral("Microsoft")},
        {QStringLiteral("huawei"), QStringLiteral("Huawei")},
        {QStringLiteral("xiaomi"), QStringLiteral("Xiaomi")},
        {QStringLiteral("razer"), QStringLiteral("Razer")},
        {QStringLiteral("asrock"), QStringLiteral("ASRock")},
        {QStringLiteral("intel"), QStringLiteral("Intel")},
        {QStringLiteral("fujitsu"), QStringLiteral("Fujitsu")},
        {QStringLiteral("medion"), QStringLiteral("Medion")},
    };
    const QString lower = vendor.trimmed().toLower();
    for (const auto &[prefix, name] : known) {
        if (lower == prefix || lower.startsWith(prefix + QLatin1Char(' ')) || lower.startsWith(prefix + QLatin1Char('-'))
            || (prefix.size() > 3 && lower.startsWith(prefix)))
            return name;
    }
    // "Framework Computer Inc" -> "Framework"
    static const QRegularExpression suffix(
        QStringLiteral("[ ,]+(computer|technology|co\\.?|corp\\.?|corporation|inc\\.?|ltd\\.?|limited|gmbh|s\\.?a\\.?)\\b.*$"),
        QRegularExpression::CaseInsensitiveOption);
    QString clean = collapse(vendor);
    clean.remove(suffix);
    return clean.trimmed();
}

QString DeviceInfo::modelOf(const QString &vendor, const QString &product, const QString &version,
                            const QString &boardVendor, const QString &boardName)
{
    QString maker = cleanVendor(vendor);
    QString name;
    // Lenovo puts its machine type in product_name, the model in the version
    if (maker == QLatin1String("Lenovo") && !isJunk(version))
        name = collapse(version);
    else if (!isJunk(product))
        name = collapse(product);
    if (name.isEmpty()) {
        // a desktop built from parts: the motherboard is the best name
        maker = cleanVendor(boardVendor);
        if (!isJunk(boardName))
            name = collapse(boardName);
    }
    if (name.isEmpty())
        return QString();
    if (maker.isEmpty() || name.startsWith(maker, Qt::CaseInsensitive))
        return name;
    return maker + QLatin1Char(' ') + name;
}

QString DeviceInfo::chassisOf(int t)
{
    // SMBIOS 3.x, 7.4.1 System Enclosure or Chassis Types
    switch (t) {
    case 8: // portable
    case 9: // laptop
    case 10: // notebook
    case 14: // sub notebook
    case 31: // convertible
        return QStringLiteral("laptop");
    case 11: // hand held
    case 30: // tablet
    case 32: // detachable
        return QStringLiteral("tablet");
    case 13: // all in one
        return QStringLiteral("all-in-one");
    default:
        return QStringLiteral("desktop");
    }
}

QString DeviceInfo::cleanProcessor(const QString &name)
{
    QString n = name;
    static const QRegularExpression marks(QStringLiteral("\\((R|TM|tm|r)\\)"));
    n.remove(marks);
    return collapse(n);
}

DeviceInfo::Cpu DeviceInfo::parseCpuinfo(const QString &text)
{
    Cpu cpu;
    QSet<QString> cores;
    QString physical;
    QString model;
    bool anyCoreId = false;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const int colon = raw.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = raw.left(colon).trimmed();
        const QString value = raw.mid(colon + 1).trimmed();
        if (key == QLatin1String("processor")) {
            ++cpu.threads;
            physical.clear();
        } else if (key == QLatin1String("model name") && cpu.name.isEmpty()) {
            cpu.name = cleanProcessor(value);
        } else if (key == QLatin1String("Model") && model.isEmpty()) {
            model = cleanProcessor(value);
        } else if (key == QLatin1String("physical id")) {
            physical = value;
        } else if (key == QLatin1String("core id")) {
            anyCoreId = true;
            cores.insert(physical + QLatin1Char('/') + value);
        }
    }
    if (cpu.name.isEmpty())
        cpu.name = model;
    cpu.cores = anyCoreId ? int(cores.size()) : cpu.threads;
    return cpu;
}

qint64 DeviceInfo::parseMemTotal(const QString &meminfo)
{
    static const QRegularExpression total(QStringLiteral("^MemTotal:\\s*(\\d+)\\s*kB"), QRegularExpression::MultilineOption);
    const auto m = total.match(meminfo);
    return m.hasMatch() ? m.captured(1).toLongLong() * KiB : 0;
}

qint64 DeviceInfo::installedMemory(qint64 usable)
{
    if (usable <= 0)
        return 0;
    // the kernel and the firmware keep a little: the size fitted is the
    // next size memory is sold in
    static const int sizes[] = {1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 40, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024};
    for (int s : sizes) {
        if (usable <= s * GiB)
            return s * GiB;
    }
    return qint64(std::ceil(double(usable) / GiB)) * GiB;
}

QString DeviceInfo::parseInputDevices(const QString &text)
{
    bool touch = false;
    bool pen = false;
    for (const QString &block : text.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        quint64 prop = 0;
        QString name;
        QStringList keyWords;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("B: PROP=")))
                prop = line.mid(8).trimmed().toULongLong(nullptr, 16);
            else if (line.startsWith(QLatin1String("B: KEY=")))
                keyWords = line.mid(7).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            else if (line.startsWith(QLatin1String("N: Name=")))
                name = line.mid(8);
        }
        // INPUT_PROP_DIRECT: the device is the screen (a touchpad is not)
        if (!(prop & 0x2))
            continue;
        // BTN_TOOL_PEN is key 0x140: bit 0 of the sixth 64-bit word from the right
        bool hasPen = false;
        if (keyWords.size() >= 6) {
            const quint64 word = keyWords.at(keyWords.size() - 6).toULongLong(nullptr, 16);
            hasPen = word & 0x1;
        }
        if (hasPen || name.contains(QLatin1String("Pen"), Qt::CaseInsensitive)
            || name.contains(QLatin1String("Stylus"), Qt::CaseInsensitive))
            pen = true;
        else
            touch = true;
    }
    if (pen && touch)
        return QStringLiteral("pen-touch");
    if (pen)
        return QStringLiteral("pen");
    return touch ? QStringLiteral("touch") : QString();
}

QString DeviceInfo::deviceIdOf(const QString &machineId)
{
    const QString id = machineId.trimmed().toUpper();
    static const QRegularExpression hex(QStringLiteral("^[0-9A-F]{32}$"));
    if (!hex.match(id).hasMatch())
        return QString();
    return id.mid(0, 8) + QLatin1Char('-') + id.mid(8, 4) + QLatin1Char('-') + id.mid(12, 4) + QLatin1Char('-')
        + id.mid(16, 4) + QLatin1Char('-') + id.mid(20);
}

QHash<QString, QString> DeviceInfo::parseOsRelease(const QString &text)
{
    QHash<QString, QString> values;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || line.left(eq).contains(QLatin1Char(' ')))
            continue;
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2
            && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
            value = value.mid(1, value.size() - 2);
        }
        values.insert(line.left(eq), value);
    }
    return values;
}

QString DeviceInfo::gpuName(const QString &vendorId, const QString &deviceId, const QString &pciIds)
{
    const QString v = vendorId.toLower();
    const QString d = deviceId.toLower();
    QString maker;
    if (v == QLatin1String("10de"))
        maker = QStringLiteral("NVIDIA");
    else if (v == QLatin1String("1002") || v == QLatin1String("1022"))
        maker = QStringLiteral("AMD");
    else if (v == QLatin1String("8086"))
        maker = QStringLiteral("Intel");

    QString vendorName;
    QString deviceName;
    bool inVendor = false;
    for (const QStringView line : QStringView(pciIds).split(QLatin1Char('\n'))) {
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (!line.startsWith(QLatin1Char('\t'))) {
            if (inVendor)
                break;
            // "C 00 ..." starts the class list: no vendor after it
            if (line.startsWith(QLatin1String("C ")))
                break;
            if (line.left(4).compare(v, Qt::CaseInsensitive) == 0) {
                inVendor = true;
                vendorName = line.mid(4).trimmed().toString();
            }
            continue;
        }
        if (inVendor && !line.startsWith(QLatin1String("\t\t")) && line.mid(1, 4).compare(d, Qt::CaseInsensitive) == 0) {
            deviceName = line.mid(5).trimmed().toString();
            break;
        }
    }
    if (maker.isEmpty())
        maker = cleanVendor(vendorName);
    if (deviceName.isEmpty())
        return maker.isEmpty() ? QString() : i18nc("@info a graphics card pci.ids does not know, %1 its maker", "%1 graphics", maker);
    // "GA104 [GeForce RTX 3070]": the marketing name is in brackets
    static const QRegularExpression bracket(QStringLiteral("\\[([^\\]]+)\\]"));
    const auto m = bracket.match(deviceName);
    const QString name = m.hasMatch() ? m.captured(1).trimmed() : deviceName;
    if (maker.isEmpty() || name.startsWith(maker, Qt::CaseInsensitive) || !m.hasMatch())
        return name;
    return maker + QLatin1Char(' ') + name;
}

DeviceInfo::Facts DeviceInfo::read(const QString &root)
{
    Facts f;
    const QString dmi = QStringLiteral("sys/class/dmi/id/");
    const QString vendor = readLine(root, dmi + QStringLiteral("sys_vendor"));
    f.model = modelOf(vendor, readLine(root, dmi + QStringLiteral("product_name")),
                      readLine(root, dmi + QStringLiteral("product_version")),
                      readLine(root, dmi + QStringLiteral("board_vendor")), readLine(root, dmi + QStringLiteral("board_name")));
    f.manufacturer = cleanVendor(vendor);
    bool ok = false;
    const int chassisType = readLine(root, dmi + QStringLiteral("chassis_type")).toInt(&ok);
    f.chassis = chassisOf(ok ? chassisType : 0);

    const Cpu cpu = parseCpuinfo(readText(root, QStringLiteral("proc/cpuinfo"), 4 << 20));
    f.processor = cpu.name;
    f.cores = cpu.cores;
    f.threads = cpu.threads;

    f.memoryUsable = parseMemTotal(readText(root, QStringLiteral("proc/meminfo"), 64 << 10));
    f.memoryInstalled = installedMemory(f.memoryUsable);

    // graphics: every DRM card, once
    QString pciIds;
    for (const QString &path : {QStringLiteral("usr/share/misc/pci.ids"), QStringLiteral("usr/share/hwdata/pci.ids")}) {
        pciIds = readText(root, path, 8 << 20);
        if (!pciIds.isEmpty())
            break;
    }
    QSet<QString> seen;
    const QDir drm(root + QStringLiteral("/sys/class/drm"));
    static const QRegularExpression card(QStringLiteral("^card\\d+$"));
    for (const QString &name : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        if (!card.match(name).hasMatch())
            continue;
        const QString base = QStringLiteral("sys/class/drm/") + name + QStringLiteral("/device/");
        const QString v = readLine(root, base + QStringLiteral("vendor")).remove(QStringLiteral("0x"));
        const QString d = readLine(root, base + QStringLiteral("device")).remove(QStringLiteral("0x"));
        if (v.isEmpty() || seen.contains(v + d))
            continue;
        seen.insert(v + d);
        const QString gpu = gpuName(v, d, pciIds);
        if (!gpu.isEmpty())
            f.graphics.append(gpu);
    }

    // internal disks
    const QDir block(root + QStringLiteral("/sys/block"));
    static const QRegularExpression virtualDisk(QStringLiteral("^(loop|ram|zram|sr|fd|dm-|md|nbd|mmcblk\\d+boot)"));
    for (const QString &name : block.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        if (virtualDisk.match(name).hasMatch())
            continue;
        const QString base = QStringLiteral("sys/block/") + name + QLatin1Char('/');
        if (readLine(root, base + QStringLiteral("removable")) == QLatin1String("1"))
            continue;
        const qint64 sectors = readLine(root, base + QStringLiteral("size")).toLongLong();
        if (sectors <= 0)
            continue;
        const bool rotational = readLine(root, base + QStringLiteral("queue/rotational")) == QLatin1String("1");
        const QString kind = rotational ? i18nc("@info a hard disk", "HDD") : i18nc("@info a solid-state drive", "SSD");
        const QString model = collapse(readLine(root, base + QStringLiteral("device/model")));
        QString line = formatSize(sectors * 512) + QLatin1Char(' ') + kind;
        if (!model.isEmpty())
            line += QLatin1Char(' ') + model;
        f.disks.append(line);
    }

    if (root == QLatin1String("/")) {
        const QStorageInfo rootFs(QStringLiteral("/"));
        f.storageTotal = rootFs.bytesTotal();
        f.storageFree = rootFs.bytesAvailable();
    }

    f.architecture = QSysInfo::currentCpuArchitecture();
    f.penAndTouch = parseInputDevices(readText(root, QStringLiteral("proc/bus/input/devices"), 1 << 20));
    f.deviceId = deviceIdOf(readLine(root, QStringLiteral("etc/machine-id")));

    QString osRelease = readText(root, QStringLiteral("etc/os-release"));
    if (osRelease.isEmpty())
        osRelease = readText(root, QStringLiteral("usr/lib/os-release"));
    const auto os = parseOsRelease(osRelease);
    f.edition = os.value(QStringLiteral("NAME"));
    f.version = os.value(QStringLiteral("VERSION"), os.value(QStringLiteral("VERSION_ID")));

    // the installer leaves its logs behind; else the birth of the root
    const QFileInfo installer(root + QStringLiteral("/var/log/installer"));
    if (installer.exists())
        f.installedOn = installer.birthTime().isValid() ? installer.birthTime() : installer.lastModified();
    if (!f.installedOn.isValid()) {
        const QFileInfo fs(root);
        f.installedOn = fs.birthTime();
    }
    f.kernelRelease = readLine(root, QStringLiteral("proc/sys/kernel/osrelease"));
    return f;
}

QString DeviceInfo::formatSize(qint64 bytes)
{
    const QLocale locale;
    const double gib = double(bytes) / GiB;
    if (gib >= 1024.0) {
        const double tib = gib / 1024.0;
        return i18nc("@info size in terabytes, Windows' binary units", "%1 TB",
                     locale.toString(tib, 'f', tib < 10 ? 2 : 1));
    }
    if (gib < 1.0)
        return i18nc("@info size in megabytes", "%1 MB", locale.toString(double(bytes) / (1 << 20), 'f', 0));
    return i18nc("@info size in gigabytes, Windows' binary units", "%1 GB",
                 locale.toString(gib, 'f', gib < 100 ? 1 : 0));
}

QString DeviceInfo::memory() const
{
    if (m_facts.memoryUsable <= 0)
        return QString();
    const QString installed = formatSize(m_facts.memoryInstalled);
    const QString usable = formatSize(m_facts.memoryUsable);
    if (installed == usable)
        return installed;
    return i18nc("@info installed RAM, then what the system can use", "%1 (%2 usable)", installed, usable);
}

QString DeviceInfo::systemType() const
{
    const QString a = m_facts.architecture;
    if (a == QLatin1String("x86_64"))
        return i18nc("@info system type", "64-bit operating system, x64-based processor");
    if (a == QLatin1String("arm64") || a == QLatin1String("aarch64"))
        return i18nc("@info system type", "64-bit operating system, ARM-based processor");
    if (a.isEmpty())
        return QString();
    return i18nc("@info system type, %1 the processor architecture", "%1 processor", a);
}

QString DeviceInfo::penAndTouch() const
{
    const QString p = m_facts.penAndTouch;
    if (p == QLatin1String("pen-touch"))
        return i18nc("@info", "Pen and touch support");
    if (p == QLatin1String("pen"))
        return i18nc("@info", "Pen support");
    if (p == QLatin1String("touch"))
        return i18nc("@info", "Touch support");
    return i18nc("@info", "No pen or touch input is available for this display");
}

QString DeviceInfo::kernelKind() const
{
    return m_facts.kernelRelease.endsWith(QLatin1String("-raye")) ? QStringLiteral("raye") : QStringLiteral("standard");
}

QString DeviceInfo::specificationsText(const QString &deviceName) const
{
    QStringList lines;
    auto add = [&lines](const QString &label, const QString &value) {
        if (!value.isEmpty())
            lines.append(label + QLatin1Char('\t') + value);
    };
    add(i18nc("@label device specification", "Device name"), deviceName);
    add(i18nc("@label device specification", "Processor"), m_facts.processor);
    add(i18nc("@label device specification", "Installed RAM"), memory());
    add(i18nc("@label device specification", "Graphics"), m_facts.graphics.join(QStringLiteral(", ")));
    add(i18nc("@label device specification", "Storage"), m_facts.disks.join(QStringLiteral(", ")));
    add(i18nc("@label device specification", "Device ID"), m_facts.deviceId);
    add(i18nc("@label device specification", "System type"), systemType());
    add(i18nc("@label device specification", "Pen and touch"), penAndTouch());
    add(i18nc("@label device specification", "Device model"), m_facts.model);
    return lines.join(QLatin1Char('\n'));
}

QString DeviceInfo::rayeSpecificationsText() const
{
    QStringList lines;
    auto add = [&lines](const QString &label, const QString &value) {
        if (!value.isEmpty())
            lines.append(label + QLatin1Char('\t') + value);
    };
    add(i18nc("@label Raye specification", "Edition"), m_facts.edition);
    add(i18nc("@label Raye specification", "Version"), m_facts.version);
    if (m_facts.installedOn.isValid())
        add(i18nc("@label Raye specification", "Installed on"),
            QLocale().toString(m_facts.installedOn.date(), QLocale::ShortFormat));
    add(i18nc("@label Raye specification", "Kernel"),
        kernelKind() == QLatin1String("raye") ? i18nc("@info kernel kind", "Raye") : i18nc("@info kernel kind", "Standard"));
    add(i18nc("@label Raye specification", "Kernel version"), m_facts.kernelRelease);
    return lines.join(QLatin1Char('\n'));
}

void DeviceInfo::copyToClipboard(const QString &text) const
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

bool DeviceInfo::isDetailsModule(const QString &module)
{
    static const QStringList modules = {
        QStringLiteral("kcm_cpu"), QStringLiteral("kcm_memory"), QStringLiteral("kcm_pci"), QStringLiteral("kcm_usb"),
        QStringLiteral("kcm_vulkan"), QStringLiteral("kcm_egl"), QStringLiteral("kcm_glx"), QStringLiteral("kcm_firmware_security"),
        QStringLiteral("kcm_disks"), QStringLiteral("kcm_block_devices"), QStringLiteral("kcm_audio_information"),
        QStringLiteral("kcm_edid"), QStringLiteral("kcm_energyinfo"), QStringLiteral("kcm_network"),
        QStringLiteral("kcm_wayland"), QStringLiteral("kcm_xserver"), QStringLiteral("kcm_interrupts"),
    };
    return modules.contains(module);
}

bool DeviceInfo::hasDetails(const QString &module)
{
    if (!isDetailsModule(module))
        return false;
    // RAYE_HARDWARE_PREVIEW=all lists every part, for a look at the page
    if (qEnvironmentVariable("RAYE_HARDWARE_PREVIEW") == QLatin1String("all"))
        return true;
    for (const QString &dir : QCoreApplication::libraryPaths()) {
        for (const QString &sub : {QStringLiteral("/plasma/kcms/kinfocenter/"), QStringLiteral("/plasma/kcms/systemsettings/"),
                                   QStringLiteral("/plasma/kcms/")}) {
            if (QFile::exists(dir + sub + module + QStringLiteral(".so")))
                return true;
        }
    }
    return false;
}

bool DeviceInfo::openDetails(const QString &module)
{
    if (!isDetailsModule(module))
        return false;
    // KDE's own program, never our relay, which would send the module
    // back here: the one the package moved aside, else the one on PATH
    QString program = QStandardPaths::findExecutable(QStringLiteral("kcmshell6.kde"));
    if (program.isEmpty())
        program = QStandardPaths::findExecutable(QStringLiteral("kcmshell6"));
    return !program.isEmpty() && QProcess::startDetached(program, {module});
}
