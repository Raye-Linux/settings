// SPDX-License-Identifier: GPL-3.0-or-later
#include "signin.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTimer>

#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr int pkexecDismissed = 126;
const QStringList powerProfiles{QStringLiteral("AC"), QStringLiteral("Battery"), QStringLiteral("LowBattery")};

QString loginName()
{
    const passwd *pw = ::getpwuid(::getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QString();
}

void scrub(QByteArray &secret)
{
    explicit_bzero(secret.data(), size_t(secret.size()));
    secret.clear();
}
} // namespace

SignInSettings::SignInSettings(QObject *parent)
    : SignInSettings({QStringLiteral("/usr/lib/sddm/sddm.conf.d"), QStringLiteral("/etc/sddm.conf.d"), QStringLiteral("/etc/sddm.conf")},
                     {QStringLiteral("pkexec"), QStringLiteral(RAYE_LIBEXECDIR "/autologin")},
                     QStringLiteral("passwd"), QStringLiteral("powerdevilrc"), loginName(), parent)
{
}

SignInSettings::SignInSettings(const QStringList &sddmConfigs, const QStringList &autologinHelper, const QString &passwdProgram,
                               const QString &powerConfig, const QString &login, QObject *parent)
    : QObject(parent)
    , m_power(KSharedConfig::openConfig(powerConfig))
    , m_sddmConfigs(sddmConfigs)
    , m_helper(autologinHelper)
    , m_passwd(passwdProgram)
    , m_login(login)
{
    refresh();
}

SignInSettings::~SignInSettings()
{
    scrub(m_answerCurrent);
    scrub(m_answerNew);
    if (m_pid > 0) {
        ::kill(m_pid, SIGTERM);
        ::waitpid(m_pid, nullptr, 0);
    }
    if (m_pty >= 0)
        ::close(m_pty);
}

QString SignInSettings::autoLoginUserIn(const QStringList &configs)
{
    QStringList files;
    for (const QString &config : configs) {
        const QFileInfo info(config);
        if (info.isDir()) {
            const QDir dir(config);
            const QStringList names = dir.entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name);
            for (const QString &name : names)
                files << dir.filePath(name);
        } else if (info.isFile()) {
            files << config;
        }
    }
    QString user;
    for (const QString &name : std::as_const(files)) {
        QFile file(name);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QString section;
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                section = line;
            } else if (section == QLatin1String("[Autologin]") && line.startsWith(QLatin1String("User"))) {
                if (line.section(QLatin1Char('='), 0, 0).trimmed() == QLatin1String("User"))
                    user = line.section(QLatin1Char('='), 1).trimmed();
            }
        }
    }
    return user;
}

void SignInSettings::refresh()
{
    m_autoLoginUser = autoLoginUserIn(m_sddmConfigs);
    Q_EMIT changed();
}

void SignInSettings::setAutoLogin(bool on)
{
    if (m_autoLoginBusy || m_helper.isEmpty())
        return;
    m_autoLoginBusy = true;
    m_autoLoginFailed = false;
    Q_EMIT changed();
    auto *process = new QProcess(this);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::finished, this, [this, process, on](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        m_autoLoginBusy = false;
        const bool normal = status == QProcess::NormalExit;
        m_autoLoginFailed = !normal || (code != 0 && code != pkexecDismissed);
        refresh();
        // a helper that ends well but leaves SDDM reading the other way failed
        if (normal && code == 0 && autoLogin() != on) {
            m_autoLoginFailed = true;
            Q_EMIT changed();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        m_autoLoginBusy = false;
        m_autoLoginFailed = true;
        refresh();
    });
    process->start(m_helper.first(), m_helper.mid(1) << (on ? QStringLiteral("on") : QStringLiteral("off")));
}

bool SignInSettings::lockWhenAway() const
{
    // on unless the user said otherwise: the theme turns it on
    for (const QString &profile : powerProfiles) {
        const KConfigGroup display = m_power->group(profile).group(QStringLiteral("Display"));
        if (!display.readEntry("LockBeforeTurnOffDisplay", true))
            return false;
    }
    return true;
}

void SignInSettings::setLockWhenAway(bool on)
{
    if (on == lockWhenAway())
        return;
    for (const QString &profile : powerProfiles) {
        KConfigGroup display = m_power->group(profile).group(QStringLiteral("Display"));
        display.writeEntry("LockBeforeTurnOffDisplay", on, KConfig::Notify);
    }
    m_power->sync();
    // the power daemon reads its file again when asked
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Solid.PowerManagement"), QStringLiteral("/org/kde/Solid/PowerManagement"),
        QStringLiteral("org.kde.Solid.PowerManagement"), QStringLiteral("refreshStatus")));
    Q_EMIT changed();
}

QString SignInSettings::describePasswdOutput(const QString &output, int exitCode)
{
    if (exitCode == 0 && output.contains(QLatin1String("successfully"), Qt::CaseInsensitive))
        return {};
    const QString said = output.toLower();
    if (said.contains(QLatin1String("authentication token manipulation error")) || said.contains(QLatin1String("authentication failure")))
        return i18nc("@info", "The current password is not right.");
    // pam_pwquality and pam_unix: "BAD PASSWORD: it is too short" and the like
    if (said.contains(QLatin1String("same as the old")) || said.contains(QLatin1String("password unchanged")))
        return i18nc("@info", "The new password must be different from the current one.");
    if (said.contains(QLatin1String("too short")) || said.contains(QLatin1String("shorter than")) || said.contains(QLatin1String("longer password")))
        return i18nc("@info", "The new password is too short.");
    if (said.contains(QLatin1String("too simple")) || said.contains(QLatin1String("dictionary")) || said.contains(QLatin1String("palindrome"))
        || said.contains(QLatin1String("simplistic")) || said.contains(QLatin1String("bad password")))
        return i18nc("@info", "The new password is too easy to guess.");
    if (said.contains(QLatin1String("do not match")) || said.contains(QLatin1String("don't match")))
        return i18nc("@info", "The two passwords are not the same.");
    return i18nc("@info", "The password could not be changed.");
}

void SignInSettings::changePassword(const QString &current, const QString &replacement)
{
    if (m_passwordBusy)
        return;
    m_passwordBusy = true;
    m_passwordError.clear();
    m_output.clear();
    m_newAsked = 0;
    m_currentAsked = false;
    m_answerCurrent = current.toUtf8() + '\n';
    m_answerNew = replacement.toUtf8() + '\n';
    Q_EMIT changed();

    int master = -1;
    const QByteArray program = m_passwd.toLocal8Bit();
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
    if (pid == 0) {
        ::setenv("LC_ALL", "C", 1);
        ::setenv("LANGUAGE", "C", 1);
        ::execlp(program.constData(), program.constData(), static_cast<char *>(nullptr));
        ::_exit(127);
    }
    if (pid < 0) {
        scrub(m_answerCurrent);
        scrub(m_answerNew);
        m_passwordBusy = false;
        m_passwordError = i18nc("@info", "The password could not be changed.");
        Q_EMIT changed();
        return;
    }
    m_pid = pid;
    m_pty = master;
    m_notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &SignInSettings::passwdOutput);
    m_stuck = new QTimer(this);
    m_stuck->setSingleShot(true);
    connect(m_stuck, &QTimer::timeout, this, &SignInSettings::passwdStuck);
    m_stuck->start(passwordTimeout);
}

void SignInSettings::passwdOutput()
{
    char chunk[512];
    const ssize_t got = ::read(m_pty, chunk, sizeof chunk);
    if (got <= 0) {
        // the other end closed: the program is done
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
        ::close(m_pty);
        m_pty = -1;
        int status = 0;
        ::waitpid(m_pid, &status, 0);
        m_pid = -1;
        passwdFinished(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return;
    }
    m_output.append(chunk, got);
    // answer the prompt at the end of what has come so far
    const QString tail = QString::fromUtf8(m_output.right(120)).toLower().trimmed();
    if (!tail.endsWith(QLatin1Char(':')))
        return;
    if (tail.endsWith(QLatin1String("current password:")) || tail.endsWith(QLatin1String("(current) unix password:"))) {
        if (m_currentAsked) {
            // asked twice: the first answer was wrong and it tries again
            ::kill(m_pid, SIGTERM);
            m_output.append("authentication failure");
            return;
        }
        m_currentAsked = true;
        (void)!::write(m_pty, m_answerCurrent.constData(), size_t(m_answerCurrent.size()));
    } else if (tail.endsWith(QLatin1String("new password:")) || tail.endsWith(QLatin1String("new password again:"))) {
        if (++m_newAsked > 2) {
            // it rejected the new one and asks for another: what it said
            // before asking is the reason
            ::kill(m_pid, SIGTERM);
            return;
        }
        (void)!::write(m_pty, m_answerNew.constData(), size_t(m_answerNew.size()));
    }
}

void SignInSettings::passwdStuck()
{
    // closing the terminal ends it, and passwdOutput() sees the end
    if (m_pid > 0)
        ::kill(m_pid, SIGKILL);
}

void SignInSettings::passwdFinished(int exitCode)
{
    if (m_stuck) {
        m_stuck->stop();
        m_stuck->deleteLater();
        m_stuck = nullptr;
    }
    scrub(m_answerCurrent);
    scrub(m_answerNew);
    m_passwordBusy = false;
    m_passwordError = describePasswdOutput(QString::fromUtf8(m_output), exitCode);
    m_output.clear();
    Q_EMIT changed();
    if (m_passwordError.isEmpty())
        Q_EMIT passwordChanged();
}
