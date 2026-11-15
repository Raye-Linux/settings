// SPDX-License-Identifier: GPL-3.0-or-later
#include "diskencryption.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

namespace {
constexpr char helper[] = "/usr/libexec/raye-encryption";
constexpr qsizetype maxPassword = 512;
constexpr int pkexecDismissed = 126;
constexpr int refusedCode = 2;
constexpr int wrongPasswordCode = 3;

bool writeAll(int fd, const char *data, qsizetype size)
{
    // a helper that died before reading would raise SIGPIPE
    sigset_t pipeSignal;
    sigset_t previous;
    sigemptyset(&pipeSignal);
    sigaddset(&pipeSignal, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipeSignal, &previous);
    bool ok = true;
    qsizetype done = 0;
    while (done < size) {
        const ssize_t n = ::write(fd, data + done, size_t(size - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        done += n;
    }
    if (!ok && errno == EPIPE) {
        const timespec now{};
        sigtimedwait(&pipeSignal, nullptr, &now);
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    return ok;
}

void wipe(QByteArray &secret)
{
    if (!secret.isEmpty())
        explicit_bzero(secret.data(), size_t(secret.size()));
    secret.clear();
}

void wipe(QString &secret)
{
    if (!secret.isEmpty())
        explicit_bzero(secret.data(), size_t(secret.size()) * sizeof(QChar));
    secret.clear();
}

QByteArray readSmall(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(256).trimmed() : QByteArray();
}
} // namespace

DiskEncryption::DiskEncryption(QObject *parent)
    : QObject(parent)
    , m_statusCommand{QString::fromLatin1(helper), QStringLiteral("status"), QStringLiteral("--json")}
    , m_adminCommand{QStringLiteral("pkexec"), QString::fromLatin1(helper)}
{
    m_poll = new QTimer(this);
    m_poll->setInterval(2000);
    connect(m_poll, &QTimer::timeout, this, &DiskEncryption::refresh);
}

DiskEncryption::~DiskEncryption()
{
    wipe(m_recoveryKey);
    if (m_status) {
        m_status->disconnect(this);
        m_status->kill();
        m_status->waitForFinished(1000);
    }
}

int DiskEncryption::tpmVersion() const
{
    const QByteArray major = readSmall(m_sysRoot + QStringLiteral("/sys/class/tpm/tpm0/tpm_version_major"));
    if (!major.isEmpty())
        return major.toInt();
    return QFile::exists(m_sysRoot + QStringLiteral("/sys/class/tpm/tpm0")) ? 1 : 0;
}

bool DiskEncryption::setupMode() const
{
    // attributes (4 bytes), then 1 when the platform key is missing
    QFile f(m_sysRoot + QStringLiteral("/sys/firmware/efi/efivars/SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.read(8);
    return raw.size() >= 5 && raw.at(4) == 1;
}

bool DiskEncryption::applyStatus(DiskEncryption *target, const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.isEmpty())
        return false;
    target->m_phase = o.value(QStringLiteral("phase")).toString();
    target->m_mode = o.value(QStringLiteral("mode")).toString();
    target->m_percent = o.value(QStringLiteral("percent")).toDouble(0);
    target->m_blocked = o.value(QStringLiteral("blocked")).toString();
    target->m_error = o.value(QStringLiteral("error")).toString();
    target->m_known = true;
    return true;
}

QString DiskEncryption::parseRecoveryKey(const QByteArray &output)
{
    static const QRegularExpression line(QStringLiteral("^recovery-key ((?:\\d{6}-){7}\\d{6})$"),
                                         QRegularExpression::MultilineOption);
    const auto m = line.match(QString::fromLatin1(output));
    return m.hasMatch() ? m.captured(1) : QString();
}

int DiskEncryption::resultFor(int exitCode, bool normalExit, QString *result)
{
    if (!normalExit)
        *result = QStringLiteral("failed");
    else if (exitCode == 0)
        *result = QStringLiteral("started");
    else if (exitCode == wrongPasswordCode)
        *result = QStringLiteral("wrong-password");
    else if (exitCode == refusedCode)
        *result = QStringLiteral("refused");
    else if (exitCode == pkexecDismissed)
        *result = QStringLiteral("cancelled");
    else
        *result = QStringLiteral("failed");
    return exitCode;
}

void DiskEncryption::refresh()
{
    if (m_status || m_statusCommand.isEmpty())
        return;
    m_status = new QProcess(this);
    m_status->setStandardInputFile(QProcess::nullDevice());
    m_status->setStandardErrorFile(QProcess::nullDevice());
    connect(m_status, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QByteArray out = m_status->readAllStandardOutput();
        m_status->deleteLater();
        m_status = nullptr;
        if (status == QProcess::NormalExit && code == 0 && applyStatus(this, out)) {
            Q_EMIT changed();
            const bool moving = m_phase == QLatin1String("running");
            if (moving && !m_poll->isActive())
                m_poll->start();
            else if (!moving)
                m_poll->stop();
        }
    });
    connect(m_status, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_status->deleteLater();
        m_status = nullptr;
    });
    m_status->start(m_statusCommand.first(), m_statusCommand.mid(1));
}

void DiskEncryption::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void DiskEncryption::setResult(const QString &result)
{
    m_result = result;
    Q_EMIT resultChanged();
}

void DiskEncryption::clearResult()
{
    if (!m_result.isEmpty())
        setResult(QString());
}

bool DiskEncryption::turnOn(const QString &password)
{
    return runWithPassword(QStringLiteral("encrypt"), password);
}

bool DiskEncryption::turnOff(const QString &password)
{
    return runWithPassword(QStringLiteral("decrypt"), password);
}

bool DiskEncryption::runWithPassword(const QString &action, const QString &password)
{
    if (m_busy || m_adminCommand.isEmpty())
        return false;
    if (password.isEmpty() || password.size() > maxPassword || password.contains(QLatin1Char('\n'))
        || password.contains(QLatin1Char('\r')) || password.contains(QChar(0))) {
        setResult(QStringLiteral("wrong-password"));
        return false;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        setResult(QStringLiteral("failed"));
        return false;
    }
    const int readEnd = fds[0];
    const int writeEnd = fds[1];
    m_result.clear();
    Q_EMIT resultChanged();
    setBusy(true);

    auto *process = new QProcess(this);
    process->setStandardErrorFile(QProcess::nullDevice());
    process->setChildProcessModifier([readEnd] {
        ::dup2(readEnd, STDIN_FILENO);
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        QByteArray out = process->readAllStandardOutput();
        process->deleteLater();
        QString result;
        resultFor(code, status == QProcess::NormalExit, &result);
        const QString key = parseRecoveryKey(out);
        wipe(out);
        if (!key.isEmpty()) {
            m_recoveryKey = key;
            Q_EMIT recoveryKeyChanged();
        }
        setBusy(false);
        setResult(result);
        refresh();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        setBusy(false);
        setResult(QStringLiteral("failed"));
    });
    process->start(m_adminCommand.first(), m_adminCommand.mid(1) << action);
    ::close(readEnd);
    QByteArray secret = password.toUtf8();
    if (writeAll(writeEnd, secret.constData(), secret.size()))
        writeAll(writeEnd, "\n", 1);
    wipe(secret);
    ::close(writeEnd);
    return true;
}

void DiskEncryption::resume()
{
    if (m_busy || m_adminCommand.isEmpty())
        return;
    QProcess::startDetached(m_adminCommand.first(), m_adminCommand.mid(1) << QStringLiteral("start"));
    QTimer::singleShot(1500, this, &DiskEncryption::refresh);
}

void DiskEncryption::restart()
{
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
}

void DiskEncryption::forgetRecoveryKey()
{
    if (m_recoveryKey.isEmpty())
        return;
    wipe(m_recoveryKey);
    Q_EMIT recoveryKeyChanged();
}

QString DiskEncryption::saveRecoveryKey(const QUrl &file)
{
    const QString path = file.toLocalFile();
    if (path.isEmpty() || m_recoveryKey.isEmpty())
        return QStringLiteral("failed");
    // like Windows: a key kept on the disk it opens helps nobody
    const QStorageInfo target(QFileInfo(path).absolutePath());
    const QStorageInfo system(m_sysRoot.isEmpty() ? QStringLiteral("/") : m_sysRoot);
    if (target.isValid() && system.isValid() && target.device() == system.device())
        return QStringLiteral("same-disk");
    const int fd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return QStringLiteral("failed");
    QByteArray text = "Raye Linux disk recovery key\n\n" + m_recoveryKey.toLatin1() + "\n";
    const bool ok = writeAll(fd, text.constData(), text.size());
    wipe(text);
    return (::close(fd) == 0 && ok) ? QString() : QStringLiteral("failed");
}

bool DiskEncryption::canPrint() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("lp")).isEmpty();
}

bool DiskEncryption::printRecoveryKey()
{
    if (m_recoveryKey.isEmpty() || !canPrint())
        return false;
    QProcess lp;
    lp.start(QStringLiteral("lp"), {QStringLiteral("-t"), QStringLiteral("Raye recovery key"), QStringLiteral("-")});
    if (!lp.waitForStarted(3000))
        return false;
    QByteArray text = "Raye Linux disk recovery key\n\n" + m_recoveryKey.toLatin1() + "\n";
    lp.write(text);
    wipe(text);
    lp.closeWriteChannel();
    return lp.waitForFinished(10000) && lp.exitStatus() == QProcess::NormalExit && lp.exitCode() == 0;
}
