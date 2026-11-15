// SPDX-License-Identifier: GPL-3.0-or-later
#include "printers.h"

#include <KLocalizedString>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

namespace {
constexpr int pkexecDismissed = 126;
const QString testPage = QStringLiteral("/usr/share/cups/data/default-testpage.pdf");
}

PrinterList::PrinterList(QObject *parent)
    : PrinterList(QStringList{QStringLiteral("pkexec"), QStringLiteral(RAYE_LIBEXECDIR "/printing")}, parent)
{
}

PrinterList::PrinterList(const QStringList &serviceHelper, QObject *parent)
    : QObject(parent)
    , m_helper(serviceHelper)
{
    refresh();
}

QVariantMap PrinterList::parseStates(const QString &output)
{
    QVariantMap states;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        // the lines under a printer, its alert and so on, start with a tab
        if (!line.startsWith(QLatin1String("printer ")))
            continue;
        const QStringList words = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() < 3)
            continue;
        QString state = QStringLiteral("idle");
        if (words.at(2) == QLatin1String("disabled"))
            state = QStringLiteral("stopped");
        else if (words.at(2) == QLatin1String("now"))
            state = QStringLiteral("printing");
        states.insert(words.at(1), state);
    }
    return states;
}

QStringList PrinterList::parseDestinations(const QString &output)
{
    QStringList names;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString name = line.trimmed().section(QLatin1Char(' '), 0, 0);
        if (!name.isEmpty() && !line.contains(QLatin1String("not running")))
            names << name;
    }
    return names;
}

QString PrinterList::parseDefault(const QString &output)
{
    static const QRegularExpression line(QStringLiteral("^system default destination:\\s*(\\S+)"), QRegularExpression::MultilineOption);
    return line.match(output).captured(1);
}

QString PrinterList::nameProblem(const QString &name)
{
    if (name.isEmpty())
        return i18nc("@info", "Enter a name for the printer.");
    // what lpadmin takes
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]{1,127}$"));
    if (!valid.match(name).hasMatch())
        return i18nc("@info", "Use letters, digits and dashes only, without spaces.");
    return {};
}

QString PrinterList::addressProblem(const QString &address)
{
    const QUrl url(address, QUrl::StrictMode);
    const QString scheme = url.scheme();
    if (url.isValid() && !url.host().isEmpty() && (scheme == QLatin1String("ipp") || scheme == QLatin1String("ipps")))
        return {};
    return i18nc("@info do not translate ipp://", "Enter the address the way the printer gives it, like ipp://printer.local/ipp/print");
}

QString PrinterList::suggestName(const QString &address)
{
    QString name = QUrl(address).host().section(QLatin1Char('.'), 0, 0);
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("-"));
    return name;
}

void PrinterList::run(const QString &program, const QStringList &arguments, Done done)
{
    auto *process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
    process->setProcessEnvironment(env);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        done(status == QProcess::NormalExit ? code : -1, QString::fromUtf8(process->readAll()));
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        done(-1, QString());
    });
    process->start(program, arguments);
}

void PrinterList::refresh()
{
    run(QStringLiteral("lpstat"), {QStringLiteral("-r")}, [this](int, const QString &status) {
        m_serviceRunning = status.contains(QLatin1String("scheduler is running"));
        if (!m_serviceRunning) {
            m_printers.clear();
            m_loaded = true;
            Q_EMIT changed();
            return;
        }
        run(QStringLiteral("lpstat"), {QStringLiteral("-e")}, [this](int, const QString &destinations) {
            run(QStringLiteral("lpstat"), {QStringLiteral("-p")}, [this, destinations](int, const QString &states) {
                run(QStringLiteral("lpstat"), {QStringLiteral("-d")}, [this, destinations, states](int, const QString &byDefault) {
                    const QVariantMap known = parseStates(states);
                    const QString chosen = parseDefault(byDefault);
                    m_printers.clear();
                    const QStringList names = parseDestinations(destinations);
                    for (const QString &name : names) {
                        m_printers << QVariantMap{
                            {QStringLiteral("name"), name},
                            {QStringLiteral("state"), known.value(name, QStringLiteral("idle"))},
                            {QStringLiteral("isDefault"), name == chosen},
                            // not set up here: CUPS found it on the network
                            {QStringLiteral("network"), !known.contains(name)},
                        };
                    }
                    m_loaded = true;
                    Q_EMIT changed();
                });
            });
        });
    });
}

void PrinterList::action(const QString &program, const QStringList &arguments, std::function<void()> then)
{
    if (m_busy)
        return;
    m_busy = true;
    m_error.clear();
    Q_EMIT changed();
    run(program, arguments, [this, then = std::move(then)](int code, const QString &output) {
        m_busy = false;
        if (code != 0) {
            if (output.contains(QLatin1String("Forbidden")) || output.contains(QLatin1String("not authorized"), Qt::CaseInsensitive))
                m_error = i18nc("@info", "Only a sudo user of this PC can change printers.");
            else if (code == -1)
                m_error = i18nc("@info", "The print service did not answer.");
            else
                m_error = i18nc("@info %1 is what the print service said", "The print service said: %1", output.trimmed());
        } else if (then) {
            then();
        }
        refresh();
    });
}

void PrinterList::turnOnService()
{
    if (m_busy || m_helper.isEmpty())
        return;
    m_busy = true;
    m_error.clear();
    Q_EMIT changed();
    run(m_helper.first(), m_helper.mid(1) << QStringLiteral("on"), [this](int code, const QString &) {
        m_busy = false;
        if (code != 0 && code != pkexecDismissed)
            m_error = i18nc("@info", "Printing could not be turned on.");
        refresh();
    });
}

void PrinterList::setDefault(const QString &name)
{
    action(QStringLiteral("lpoptions"), {QStringLiteral("-d"), name});
}

void PrinterList::printTestPage(const QString &name)
{
    action(QStringLiteral("lp"), {QStringLiteral("-d"), name, testPage}, [this] { Q_EMIT testPageSent(); });
}

void PrinterList::openQueue(const QString &name)
{
    if (!QProcess::startDetached(QStringLiteral("kde-print-queue"), {name})) {
        m_error = i18nc("@info", "The print queue could not be opened.");
        Q_EMIT changed();
    }
}

void PrinterList::remove(const QString &name)
{
    action(QStringLiteral("lpadmin"), {QStringLiteral("-x"), name});
}

void PrinterList::add(const QString &name, const QString &address)
{
    // the page checks these too; this is what stands before lpadmin
    const QString problem = nameProblem(name) + addressProblem(address);
    if (!problem.isEmpty()) {
        m_error = problem;
        Q_EMIT changed();
        return;
    }
    action(QStringLiteral("lpadmin"), {QStringLiteral("-p"), name, QStringLiteral("-E"), QStringLiteral("-v"), address,
                                       QStringLiteral("-m"), QStringLiteral("everywhere")});
}

void PrinterList::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    Q_EMIT changed();
}
