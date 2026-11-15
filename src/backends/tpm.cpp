// SPDX-License-Identifier: GPL-3.0-or-later
#include "tpm.h"

#include <QFile>
#include <QProcess>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

namespace {
constexpr char kernelSetup[] = "/usr/libexec/raye-kernel-setup";
// left by the boot check when a TPM disk was opened with its password
constexpr char needsPasswordFlag[] = "/var/lib/raye-tpm/needs-password";
// a disk password is a line typed at start-up, not a file
constexpr qsizetype maxPassword = 4096;
// pkexec: the prompt was dismissed, or the user may not do this
constexpr int pkexecDismissed = 126;

// Writes all of `data` to `fd`. A helper that died before reading would
// make write() raise SIGPIPE: hold it back for this thread and drop it.
bool writeAll(int fd, const char *data, qsizetype size)
{
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
} // namespace

Tpm::Tpm(QObject *parent)
    : QObject(parent)
    , m_statusCommand{QString::fromLatin1(kernelSetup), QStringLiteral("tpm-status")}
    , m_rebindCommand{QStringLiteral("pkexec"), QString::fromLatin1(kernelSetup), QStringLiteral("rebind")}
    , m_needsPasswordFlag(QString::fromLatin1(needsPasswordFlag))
{
}

Tpm::~Tpm()
{
    // a page closed while the state was being read
    if (m_status) {
        m_status->disconnect(this);
        m_status->kill();
        m_status->waitForFinished(1000);
    }
}

QString Tpm::parseStatus(const QByteArray &output)
{
    const QString line = QString::fromUtf8(output).section(QLatin1Char('\n'), 0, 0).trimmed();
    if (line == QLatin1String("ok") || line == QLatin1String("stale") || line == QLatin1String("none"))
        return line;
    if (line == QLatin1String("tpm-needs-password"))
        return QStringLiteral("stale");
    return QStringLiteral("unknown");
}

void Tpm::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

void Tpm::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void Tpm::refresh()
{
    if (m_status || m_statusCommand.isEmpty())
        return;
    // Read only: it must answer without asking for a password, so it is
    // never run through pkexec here.
    m_status = new QProcess(this);
    m_status->setStandardInputFile(QProcess::nullDevice());
    m_status->setStandardErrorFile(QProcess::nullDevice());
    auto done = [this](const QByteArray &output) {
        QString state = parseStatus(output);
        if (state == QLatin1String("unknown") && QFile::exists(m_needsPasswordFlag))
            state = QStringLiteral("stale");
        m_status->deleteLater();
        m_status = nullptr;
        setState(state);
        Q_EMIT refreshed();
    };
    connect(m_status, &QProcess::finished, this, [this, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0 ? m_status->readAllStandardOutput() : QByteArray());
    });
    connect(m_status, &QProcess::errorOccurred, this, [this, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(QByteArray());
    });
    m_status->start(m_statusCommand.first(), m_statusCommand.mid(1));
}

void Tpm::finish(const QString &result)
{
    m_result = result;
    setBusy(false);
    Q_EMIT resultChanged();
    Q_EMIT rebindFinished(result);
}

void Tpm::clearResult()
{
    if (m_result.isEmpty())
        return;
    m_result.clear();
    Q_EMIT resultChanged();
}

bool Tpm::rebind(const QString &password)
{
    if (m_busy || m_rebindCommand.isEmpty())
        return false;
    // The helper reads one line: a line break would cut the password,
    // a NUL would end it early. Neither can be typed in the field.
    if (password.isEmpty() || password.size() > maxPassword || password.contains(QLatin1Char('\n'))
        || password.contains(QLatin1Char('\r')) || password.contains(QChar(0))) {
        finish(QStringLiteral("wrong-password"));
        return false;
    }

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        finish(QStringLiteral("failed"));
        return false;
    }
    const int readEnd = fds[0];
    const int writeEnd = fds[1];

    m_result.clear();
    Q_EMIT resultChanged();
    setBusy(true);

    auto *process = new QProcess(this);
    // its output may name devices; it is not shown and not kept
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    // in the child, once QProcess has set up its channels: our pipe is
    // its standard input (both ends close by themselves on exec)
    process->setChildProcessModifier([readEnd] {
        ::dup2(readEnd, STDIN_FILENO);
    });

    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        QString result;
        if (status != QProcess::NormalExit)
            result = QStringLiteral("failed");
        else if (code == 0)
            result = QStringLiteral("done");
        else if (code == 1)
            result = QStringLiteral("wrong-password");
        else if (code == pkexecDismissed)
            result = QStringLiteral("cancelled");
        else
            result = QStringLiteral("failed");
        finish(result);
        refresh();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        finish(QStringLiteral("failed"));
    });

    process->start(m_rebindCommand.first(), m_rebindCommand.mid(1));
    // the child has its copy of the read end by now (or never will)
    ::close(readEnd);

    // one buffer holds the password, wiped once written; the newline the
    // helper reads up to goes after it (appending could leave a copy
    // behind in a buffer given up by a reallocation)
    QByteArray secret = password.toUtf8();
    // far below a pipe's buffer: this never waits for the reader
    if (writeAll(writeEnd, secret.constData(), secret.size()))
        writeAll(writeEnd, "\n", 1);
    wipe(secret);
    ::close(writeEnd);
    return true;
}
