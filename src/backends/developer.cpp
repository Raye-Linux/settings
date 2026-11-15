// SPDX-License-Identifier: GPL-3.0-or-later
#include "developer.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <PackageKit/Daemon>
#include <PackageKit/Transaction>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <memory>

namespace {
constexpr int pkexecDismissed = 126;
const QString rcName = QStringLiteral("raye-developerrc");

KConfigGroup modeGroup()
{
    return KSharedConfig::openConfig(rcName, KConfig::SimpleConfig)->group(QStringLiteral("General"));
}
} // namespace

DeveloperSettings::DeveloperSettings(QObject *parent)
    : QObject(parent)
    , m_portal{QStringLiteral(RAYE_LIBEXECDIR "/device-portal")}
    , m_helper{QStringLiteral("pkexec"), QStringLiteral(RAYE_LIBEXECDIR "/developer")}
    , m_systemctl{QStringLiteral("systemctl")}
    , m_sshd(QStringLiteral("/usr/sbin/sshd"))
    , m_keysFile(QStringLiteral("/etc/ssh/sshd_config.d/01-raye-keys-only.conf"))
{
    m_developerMode = modeGroup().readEntry("DeveloperMode", false);
    refresh();
}

QString DeveloperSettings::environmentFile() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/environment.d/90-raye-developer.conf");
}

void DeveloperSettings::writeEnvironment(bool on)
{
    const QString path = environmentFile();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        // read by the session at the next sign-in
        file.write("# Settings > System > Advanced > Developer Mode\n"
                   "QT_LOGGING_RULES=raye.*.debug=true;org.raye.*.debug=true\n"
                   "RAYE_DEBUG=1\n");
        file.commit();
    }
}

void DeveloperSettings::setDeveloperMode(bool on)
{
    if (on == m_developerMode)
        return;
    m_developerMode = on;
    KConfigGroup group = modeGroup();
    group.writeEntry("DeveloperMode", on);
    group.sync();
    writeEnvironment(on);
    // the portal lives under Developer Mode, as on Windows
    if (!on && m_portalEnabled)
        setPortalEnabled(false);
    Q_EMIT changed();
}

void DeveloperSettings::run(const QStringList &command, const QByteArray &input, Done done)
{
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        done(status == QProcess::NormalExit ? code : -1, process->readAllStandardOutput());
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            done(-1, {});
        }
    });
    process->start(command.first(), command.mid(1));
    if (!input.isNull()) {
        process->write(input);
        process->closeWriteChannel();
    } else {
        process->closeWriteChannel();
    }
}

void DeveloperSettings::portal(const QStringList &args, Done done, const QByteArray &input)
{
    run(QStringList(m_portal) << args, input, std::move(done));
}

void DeveloperSettings::helper(const QStringList &args, std::function<void(bool ok)> done)
{
    run(QStringList(m_helper) << args, QByteArray(), [this, done](int code, const QByteArray &) {
        if (code != 0 && code != pkexecDismissed)
            m_error = i18nc("@info", "That change didn't go through. Try again.");
        done(code == 0);
    });
}

bool DeveloperSettings::readStatus(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    m_portalEnabled = o.value(QStringLiteral("enabled")).toBool();
    m_addresses.clear();
    const QJsonArray addresses = o.value(QStringLiteral("addresses")).toArray();
    for (const QJsonValue &a : addresses)
        m_addresses << a.toString();
    m_fingerprint = o.value(QStringLiteral("fingerprint")).toString();
    m_user = o.value(QStringLiteral("user")).toString();
    m_paired = o.value(QStringLiteral("paired")).toInt();
    m_allowScreenshot = o.value(QStringLiteral("allow_screenshot")).toBool();
    m_discovery = o.value(QStringLiteral("discovery")).toBool(false);
    m_firewall = o.value(QStringLiteral("firewall")).toBool();
    m_log = o.value(QStringLiteral("log")).toArray().toVariantList();
    return true;
}

void DeveloperSettings::refresh()
{
    portal({QStringLiteral("status")}, [this](int code, const QByteArray &out) {
        if (code == 0)
            readStatus(out);
        Q_EMIT changed();
    });
    refreshSsh();
}

void DeveloperSettings::refreshSsh()
{
    // ssh.socket on Ubuntu, ssh.service elsewhere: either one counts
    run(QStringList(m_systemctl) << QStringLiteral("is-active") << QStringLiteral("--quiet") << QStringLiteral("ssh.socket"),
        QByteArray(), [this](int code, const QByteArray &) {
            if (code == 0) {
                m_sshRunning = true;
                Q_EMIT changed();
                return;
            }
            run(QStringList(m_systemctl) << QStringLiteral("is-active") << QStringLiteral("--quiet") << QStringLiteral("ssh.service"),
                QByteArray(), [this](int code, const QByteArray &) {
                    m_sshRunning = code == 0;
                    Q_EMIT changed();
                });
        });
}

void DeveloperSettings::setPortalEnabled(bool on)
{
    if (m_portalBusy || (on && !m_developerMode))
        return;
    m_portalBusy = true;
    m_error.clear();
    m_pairCode.clear();
    Q_EMIT changed();
    const bool firewall = m_firewall;
    auto finish = [this] {
        m_portalBusy = false;
        refresh();
    };
    portal({on ? QStringLiteral("enable") : QStringLiteral("disable")}, [this, on, firewall, finish](int code, const QByteArray &) {
        if (code != 0) {
            m_error = i18nc("@info", "Device Portal couldn't be turned on.");
            finish();
            return;
        }
        if (!firewall) {
            finish();
            return;
        }
        // the firewall is on: other devices need its door open
        helper({QStringLiteral("portal-firewall"), on ? QStringLiteral("allow") : QStringLiteral("deny")},
               [finish](bool) { finish(); });
    });
}

void DeveloperSettings::setPortalLogin(const QString &user, const QString &password)
{
    if (password.size() < 8) {
        Q_EMIT loginSaved(false, QStringLiteral("short"));
        return;
    }
    portal({QStringLiteral("set-login"), user.trimmed()},
           [this](int code, const QByteArray &) {
               Q_EMIT loginSaved(code == 0, code == 0 ? QString() : QStringLiteral("refused"));
               refresh();
           },
           password.toUtf8() + '\n');
}

void DeveloperSettings::newPairCode()
{
    portal({QStringLiteral("pair")}, [this](int code, const QByteArray &out) {
        m_pairCode = code == 0 ? QString::fromUtf8(out).trimmed() : QString();
        Q_EMIT changed();
    });
}

void DeveloperSettings::forgetPairedDevices()
{
    portal({QStringLiteral("forget-devices")}, [this](int, const QByteArray &) { refresh(); });
}

void DeveloperSettings::setAllowScreenshot(bool on)
{
    m_allowScreenshot = on;
    Q_EMIT changed();
    portal({QStringLiteral("set"), QStringLiteral("allow-screenshot"), on ? QStringLiteral("on") : QStringLiteral("off")},
           [this](int, const QByteArray &) { refresh(); });
}

void DeveloperSettings::setDiscovery(bool on)
{
    m_discovery = on;
    Q_EMIT changed();
    portal({QStringLiteral("set"), QStringLiteral("discovery"), on ? QStringLiteral("on") : QStringLiteral("off")},
           [this](int, const QByteArray &) { refresh(); });
}

bool DeveloperSettings::sshInstalled() const
{
    return QFileInfo(m_sshd).isExecutable();
}

QString DeveloperSettings::sshCommand() const
{
    const QString user = qEnvironmentVariable("USER");
    for (const QString &address : m_addresses) {
        // the portal's addresses, IPv4 first: https://192.168.1.20:50443
        const QString host = address.section(QLatin1Char('/'), 2).section(QLatin1Char(':'), 0, 0);
        if (!host.isEmpty() && !host.startsWith(QLatin1Char('[')))
            return QStringLiteral("ssh %1@%2").arg(user, host);
    }
    return QStringLiteral("ssh %1@%2").arg(user, QSysInfo::machineHostName() + QStringLiteral(".local"));
}

// The file whose PasswordAuthentication sshd keeps: it takes the first
// value it reads, the drop-ins (in name order) before sshd_config.
// Returns "" when passwords are allowed.
QString DeveloperSettings::passwordSettingFile() const
{
    const QDir drop = QFileInfo(m_keysFile).dir();
    QStringList files;
    for (const QString &name : drop.entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name))
        files << drop.filePath(name);
    files << QDir::cleanPath(drop.filePath(QStringLiteral("../sshd_config")));
    for (const QString &path : std::as_const(files)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!file.atEnd()) {
            const QStringList words = QString::fromUtf8(file.readLine()).simplified().split(QLatin1Char(' '));
            // what follows a Match line is for some connections only
            if (words[0].compare(QLatin1String("Match"), Qt::CaseInsensitive) == 0)
                break;
            if (words.size() < 2 || words[0].compare(QLatin1String("PasswordAuthentication"), Qt::CaseInsensitive) != 0)
                continue;
            return words[1].compare(QLatin1String("no"), Qt::CaseInsensitive) == 0 ? path : QString();
        }
    }
    return {};
}

bool DeveloperSettings::sshKeysOnly() const
{
    return !passwordSettingFile().isEmpty();
}

bool DeveloperSettings::sshKeysOnlyFixed() const
{
    const QString file = passwordSettingFile();
    return !file.isEmpty() && QFileInfo(file) != QFileInfo(m_keysFile);
}

void DeveloperSettings::setSshEnabled(bool on)
{
    if (m_sshBusy)
        return;
    m_sshBusy = true;
    m_error.clear();
    Q_EMIT changed();
    // a server turned on for the first time takes keys only
    const bool passwords = !sshKeysOnly() && modeGroup().readEntry("SshPasswords", false);
    const QStringList args = on ? QStringList{QStringLiteral("ssh"), QStringLiteral("on"),
                                              passwords ? QStringLiteral("passwords") : QStringLiteral("keys")}
                                : QStringList{QStringLiteral("ssh"), QStringLiteral("off")};
    helper(args, [this](bool) {
        m_sshBusy = false;
        refreshSsh();
        Q_EMIT changed();
    });
}

void DeveloperSettings::setSshKeysOnly(bool on)
{
    if (m_sshBusy)
        return;
    m_sshBusy = true;
    Q_EMIT changed();
    KConfigGroup group = modeGroup();
    group.writeEntry("SshPasswords", !on);
    group.sync();
    helper({QStringLiteral("ssh"), QStringLiteral("keys-only"), on ? QStringLiteral("on") : QStringLiteral("off")}, [this](bool) {
        m_sshBusy = false;
        Q_EMIT changed();
    });
}

void DeveloperSettings::installSsh()
{
    if (m_sshBusy || sshInstalled())
        return;
    m_sshBusy = true;
    m_sshPercent = -1;
    m_error.clear();
    Q_EMIT changed();
    auto id = std::make_shared<QString>();
    auto fail = [this] {
        m_sshBusy = false;
        m_error = i18nc("@info", "OpenSSH server couldn't be installed. Check your connection and try again.");
        Q_EMIT changed();
    };
    PackageKit::Transaction *resolve = PackageKit::Daemon::resolve(QStringLiteral("openssh-server"),
                                                                   PackageKit::Transaction::FilterNotInstalled);
    connect(resolve, &PackageKit::Transaction::package, this,
            [id](PackageKit::Transaction::Info, const QString &packageId, const QString &) { *id = packageId; });
    connect(resolve, &PackageKit::Transaction::finished, this, [this, id, fail](PackageKit::Transaction::Exit) {
        if (id->isEmpty()) {
            fail();
            return;
        }
        PackageKit::Transaction *t = PackageKit::Daemon::installPackage(*id);
        connect(t, &PackageKit::Transaction::percentageChanged, this, [this, t] {
            const uint p = t->percentage();
            m_sshPercent = p <= 100 ? int(p) : -1;
            Q_EMIT changed();
        });
        connect(t, &PackageKit::Transaction::finished, this, [this, fail](PackageKit::Transaction::Exit exit) {
            if (exit != PackageKit::Transaction::ExitSuccess && exit != PackageKit::Transaction::ExitCancelled) {
                fail();
                return;
            }
            m_sshBusy = false;
            m_sshPercent = -1;
            // Debian starts the server it just installed: keep it to keys only
            if (sshInstalled() && exit == PackageKit::Transaction::ExitSuccess)
                setSshKeysOnly(true);
            refreshSsh();
            Q_EMIT changed();
        });
    });
}
