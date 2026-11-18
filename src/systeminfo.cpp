// SPDX-License-Identifier: GPL-3.0-or-later
#include "systeminfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
// switches to the Raye kernel safely (raye-kernel package)
constexpr char kernelSetup[] = "/usr/libexec/raye-kernel-setup";

const QString secureBootVar =
    QStringLiteral("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c");

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString home(const QString &relative)
{
    return QDir::homePath() + QLatin1Char('/') + relative;
}

QString windowsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/raye/windows");
}

QString mangohudFile()
{
    // read by systemd for the whole session, so every game gets it
    return home(QStringLiteral(".config/environment.d/90-raye-mangohud.conf"));
}
} // namespace

SystemInfo::SystemInfo(QObject *parent)
    : QObject(parent)
{
    refresh();
}

int SystemInfo::parseSecureBoot(const QByteArray &efivar)
{
    // four bytes of attributes, then one byte: 1 when Secure Boot is on
    if (efivar.size() < 5)
        return -1;
    return efivar.at(4) == 1 ? 1 : 0;
}

bool SystemInfo::crypttabHasEntries(const QString &crypttab)
{
    for (const QString &line : crypttab.split(QLatin1Char('\n'))) {
        const QString l = line.trimmed();
        if (!l.isEmpty() && !l.startsWith(QLatin1Char('#')))
            return true;
    }
    return false;
}

QStringList SystemInfo::parseGpus(const QString &lspciMachine)
{
    // lspci -mm: slot "class" "vendor" "device" ...
    QStringList gpus;
    static const QRegularExpression fields(QStringLiteral("\"([^\"]*)\""));
    for (const QString &line : lspciMachine.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QStringList f;
        for (auto it = fields.globalMatch(line); it.hasNext();)
            f.append(it.next().captured(1));
        if (f.size() >= 3 && (f.at(0).contains(QLatin1String("VGA")) || f.at(0).contains(QLatin1String("3D"))
                              || f.at(0).contains(QLatin1String("Display"))))
            gpus.append(f.at(1) + QLatin1Char(' ') + f.at(2));
    }
    return gpus;
}

void SystemInfo::refresh()
{
    QFile sb(secureBootVar);
    m_secureBoot = QDir(QStringLiteral("/sys/firmware/efi")).exists()
        ? (sb.open(QIODevice::ReadOnly) ? parseSecureBoot(sb.readAll()) : 0)
        : -1;
    m_tpm = QFile::exists(QStringLiteral("/dev/tpmrm0")) || QFile::exists(QStringLiteral("/sys/class/tpm/tpm0"));
    m_encrypted = crypttabHasEntries(readAll(QStringLiteral("/etc/crypttab")));
    m_driversDone = readAll(QStringLiteral("/var/lib/raye/drivers-done")).trimmed();
    m_gamemode = !QStandardPaths::findExecutable(QStringLiteral("gamemoderun")).isEmpty();

    QString status;
    if (QFile::exists(QString::fromLatin1(kernelSetup))) {
        QProcess setup;
        setup.start(QString::fromLatin1(kernelSetup), {QStringLiteral("status")});
        if (setup.waitForFinished(5000))
            status = QString::fromUtf8(setup.readAllStandardOutput());
    }
    m_rayeKernel = kernelState(readAll(QStringLiteral("/proc/sys/kernel/osrelease")).trimmed(), status);

    QProcess lspci;
    lspci.start(QStringLiteral("lspci"), {QStringLiteral("-mm")});
    lspci.waitForFinished(5000);
    m_gpus = parseGpus(QString::fromUtf8(lspci.readAllStandardOutput()));
    Q_EMIT changed();
}

bool SystemInfo::recoveryKeyFile() const
{
    return QFile::exists(home(QStringLiteral(".local/share/raye/recovery-key")));
}

bool SystemInfo::mangohud() const
{
    return QFile::exists(mangohudFile());
}

void SystemInfo::setMangohud(bool on)
{
    if (on == mangohud())
        return;
    if (on) {
        QDir().mkpath(QFileInfo(mangohudFile()).absolutePath());
        QFile f(mangohudFile());
        if (f.open(QIODevice::WriteOnly))
            f.write("# set by Raye Settings > Gaming: frame rate overlay in every game\nMANGOHUD=1\n");
    } else {
        QFile::remove(mangohudFile());
    }
    Q_EMIT changed();
}

QStringList SystemInfo::windowsApps() const
{
    return QDir(windowsRoot()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

void SystemInfo::lookForDrivers()
{
    // the boot-time run happens once; this one looks again, and the sudo
    // group may start it without a password
    QProcess::startDetached(QStringLiteral("systemctl"),
                            {QStringLiteral("start"), QStringLiteral("--no-block"), QStringLiteral("raye-drivers-check.service")});
}

bool SystemInfo::forgetWindowsApp(const QString &name)
{
    // names come from our own folder listing; refuse anything else
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.')))
        return false;
    const bool ok = QDir(windowsRoot() + QLatin1Char('/') + name).removeRecursively();
    Q_EMIT changed();
    return ok;
}

QString SystemInfo::kernelState(const QString &uname, const QString &setupStatus)
{
    const QString status = setupStatus.trimmed();
    static const QStringList known = {QStringLiteral("enrolled"), QStringLiteral("pending"),
                                      QStringLiteral("trial"), QStringLiteral("failed"),
                                      QStringLiteral("not-enrolled"), QStringLiteral("no-secure-boot"),
                                      QStringLiteral("nvidia")};
    if (!known.contains(status))
        return QStringLiteral("unavailable");
    if (uname.endsWith(QLatin1String("-raye")))
        return QStringLiteral("running");
    // the states the page shows as they are; the others mean "not in use"
    if (status == QLatin1String("enrolled") || status == QLatin1String("pending")
        || status == QLatin1String("trial") || status == QLatin1String("failed")
        || status == QLatin1String("nvidia"))
        return status;
    return QStringLiteral("off");
}

QStringList SystemInfo::actionsNeeded(int secureBoot, const QString &tpmState, bool recoveryKeyFile, int firewall)
{
    QStringList actions;
    if (secureBoot == 0)
        actions.append(QStringLiteral("secure-boot"));
    if (tpmState == QLatin1String("stale"))
        actions.append(QStringLiteral("tpm"));
    if (recoveryKeyFile)
        actions.append(QStringLiteral("recovery-key"));
    if (firewall != 1)
        actions.append(QStringLiteral("firewall"));
    return actions;
}

void SystemInfo::enableRayeKernel()
{
    if (m_kernelBusy)
        return;
    m_kernelBusy = true;
    m_enrollCode.clear();
    Q_EMIT changed();
    // asks for the password: this changes what the computer starts
    auto *setup = new QProcess(this);
    connect(setup, &QProcess::finished, this, [this, setup](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(setup->readAllStandardOutput()).trimmed();
        // six digits: the code to type at the blue screen after restarting
        static const QRegularExpression digits(QStringLiteral("^[0-9]{6}$"));
        if (st == QProcess::NormalExit && code == 0 && digits.match(out).hasMatch())
            m_enrollCode = out;
        m_kernelBusy = false;
        setup->deleteLater();
        refresh();
    });
    setup->start(QStringLiteral("pkexec"), {QString::fromLatin1(kernelSetup), QStringLiteral("enroll")});
}

void SystemInfo::disableRayeKernel()
{
    if (m_kernelBusy)
        return;
    m_kernelBusy = true;
    Q_EMIT changed();
    auto *setup = new QProcess(this);
    connect(setup, &QProcess::finished, this, [this, setup](int, QProcess::ExitStatus) {
        m_kernelBusy = false;
        m_enrollCode.clear();
        setup->deleteLater();
        refresh();
    });
    setup->start(QStringLiteral("pkexec"), {QString::fromLatin1(kernelSetup), QStringLiteral("remove")});
}
