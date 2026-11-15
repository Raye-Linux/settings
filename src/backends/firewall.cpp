// SPDX-License-Identifier: GPL-3.0-or-later
#include "firewall.h"

#include <KPluginMetaData>

#include <QFile>
#include <QProcess>

namespace {
// pkexec: the password prompt was closed
constexpr int pkexecDismissed = 126;
const QString rulesModule = QStringLiteral("kcm_firewall");
} // namespace

Firewall::Firewall(QObject *parent)
    : QObject(parent)
    , m_confPath(QStringLiteral("/etc/ufw/ufw.conf"))
    , m_helper{QStringLiteral("pkexec"), QStringLiteral(RAYE_LIBEXECDIR "/firewall")}
{
    refresh();
}

int Firewall::parseConf(const QString &text)
{
    int enabled = 0;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#')) || !line.startsWith(QLatin1String("ENABLED")))
            continue;
        const QString key = line.section(QLatin1Char('='), 0, 0).trimmed();
        if (key != QLatin1String("ENABLED"))
            continue;
        QString value = line.section(QLatin1Char('='), 1).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
            && value.endsWith(value.at(0)))
            value = value.mid(1, value.size() - 2);
        // the last one wins, the way the shell reads the file
        enabled = value.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0 ? 1 : 0;
    }
    return enabled;
}

void Firewall::refresh()
{
    QFile conf(m_confPath);
    m_installed = conf.exists();
    m_enabled = conf.open(QIODevice::ReadOnly) && parseConf(QString::fromUtf8(conf.readAll())) == 1;
    Q_EMIT changed();
}

bool Firewall::setEnabled(bool on)
{
    if (m_busy || !m_installed || m_helper.isEmpty())
        return false;
    m_busy = true;
    m_failed = false;
    Q_EMIT changed();
    auto *process = new QProcess(this);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    auto done = [this, process](bool ok, bool dismissed) {
        process->deleteLater();
        m_busy = false;
        m_failed = !ok && !dismissed;
        refresh();
        Q_EMIT finished(ok);
    };
    connect(process, &QProcess::finished, this, [done](int code, QProcess::ExitStatus status) {
        const bool normal = status == QProcess::NormalExit;
        done(normal && code == 0, normal && code == pkexecDismissed);
    });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false, false);
    });
    process->start(m_helper.first(), m_helper.mid(1) << (on ? QStringLiteral("on") : QStringLiteral("off")));
    return true;
}

bool Firewall::canOpenRules() const
{
    return KPluginMetaData::findPluginById(QStringLiteral("plasma/kcms/systemsettings"), rulesModule).isValid();
}

void Firewall::openRules() const
{
    // Raye Settings sends this one past the kcmshell6 relay, which would
    // otherwise open Device security again
    QProcess::startDetached(QStringLiteral("kcmshell6"), {rulesModule});
}
