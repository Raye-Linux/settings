// SPDX-License-Identifier: GPL-3.0-or-later
#include "troubleshoot.h"

#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <QProcess>
#include <QRegularExpression>

namespace {

struct CheckWords {
    const char *id;
    KLazyLocalizedString title;
    KLazyLocalizedString running;
    KLazyLocalizedString ok;
    KLazyLocalizedString problem;
    KLazyLocalizedString fixed;
};

// What each check of the helper is called, and what it says in each state.
const CheckWords checkWords[] = {
    {"audio-service", kli18n("Sound service"), kli18n("Checking the sound service…"),
     kli18n("The sound service is running."), kli18n("The sound service is not running."),
     kli18n("The sound service was restarted.")},
    {"audio-output", kli18n("Speakers or headphones"), kli18n("Looking for speakers or headphones…"),
     kli18n("Raye found a device to play sound on."), kli18n("Raye did not find speakers or headphones."),
     kli18n("Raye found a device to play sound on.")},
    {"audio-volume", kli18n("Volume"), kli18n("Checking the volume…"), kli18n("The sound is on."),
     kli18n("The sound is muted or turned all the way down."), kli18n("The sound was turned back on.")},
    {"network-service", kli18n("Network service"), kli18n("Checking the network service…"),
     kli18n("The network service is running."), kli18n("The network service is not running."),
     kli18n("The network service was restarted.")},
    {"network-enabled", kli18n("Networking"), kli18n("Checking that networking is on…"), kli18n("Networking is on."),
     kli18n("Networking is turned off."), kli18n("Networking was turned on.")},
    {"network-wifi", kli18n("Wi-Fi"), kli18n("Checking Wi-Fi…"), kli18n("Wi-Fi is on."), kli18n("Wi-Fi is turned off."),
     kli18n("Wi-Fi was turned on.")},
    {"network-connected", kli18n("Internet connection"), kli18n("Checking the connection to the internet…"),
     kli18n("This PC is connected to the internet."), kli18n("This PC can't reach the internet."),
     kli18n("This PC is connected to the internet again.")},
    {"bluetooth-service", kli18n("Bluetooth service"), kli18n("Checking the Bluetooth service…"),
     kli18n("The Bluetooth service is running."), kli18n("The Bluetooth service is not running."),
     kli18n("The Bluetooth service was restarted.")},
    {"bluetooth-adapter", kli18n("Bluetooth adapter"), kli18n("Looking for a Bluetooth adapter…"),
     kli18n("Raye found a Bluetooth adapter."), kli18n("Raye did not find a Bluetooth adapter."),
     kli18n("Raye found a Bluetooth adapter.")},
    {"bluetooth-blocked", kli18n("Bluetooth switch"), kli18n("Checking whether Bluetooth is switched off…"),
     kli18n("Bluetooth is not switched off."), kli18n("Bluetooth is switched off."),
     kli18n("Bluetooth was switched back on.")},
    {"bluetooth-powered", kli18n("Bluetooth"), kli18n("Checking that Bluetooth is on…"), kli18n("Bluetooth is on."),
     kli18n("Bluetooth is turned off."), kli18n("Bluetooth was turned on.")},
    {"printer-service", kli18n("Printing service"), kli18n("Checking the printing service…"),
     kli18n("The printing service is running."), kli18n("The printing service is not running."),
     kli18n("The printing service was restarted.")},
    {"printer-found", kli18n("Printers"), kli18n("Looking for printers…"), kli18n("A printer is set up on this PC."),
     kli18n("No printer is set up on this PC."), kli18n("A printer is set up on this PC.")},
    {"printer-paused", kli18n("Print queues"), kli18n("Checking the print queues…"), kli18n("No printer is paused."),
     kli18n("A printer is paused."), kli18n("The paused printers were started again.")},
    {"display-screens", kli18n("Screens"), kli18n("Checking your screens…"),
     kli18n("Every screen that is plugged in is on."), kli18n("A screen is plugged in but shows nothing."),
     kli18n("The screen layout was reset.")},
    {"update-service", kli18n("Update schedule"), kli18n("Checking that Raye Update runs on its own…"),
     kli18n("Raye Update checks for updates on its own."),
     kli18n("Raye Update does not check for updates on its own."),
     kli18n("Raye Update checks for updates on its own again.")},
    {"update-state", kli18n("Last update"), kli18n("Checking the last update…"), kli18n("The last update went well."),
     kli18n("The last update did not finish."), kli18n("Raye Update is looking for updates again.")},
    {"update-packages", kli18n("Installed software"), kli18n("Checking for an install that stopped half way…"),
     kli18n("No install was left half done."), kli18n("An install stopped half way."),
     kli18n("The install that stopped was finished.")},
    {"update-space", kli18n("Free space"), kli18n("Checking free space…"),
     kli18n("There is enough free space for updates."), kli18n("There is not enough free space for updates."),
     kli18n("There is enough free space for updates.")},
};

struct RemedyWords {
    const char *id;
    KLazyLocalizedString text;
};

// What fix() does, in the list the page shows before it asks.
const RemedyWords remedyWords[] = {
    {"restart-audio", kli18n("Restart the sound service")},
    {"unmute", kli18n("Turn the sound back on")},
    {"enable-networking", kli18n("Turn networking on")},
    {"wifi-on", kli18n("Turn Wi-Fi on")},
    {"unblock-bluetooth", kli18n("Switch Bluetooth back on")},
    {"power-bluetooth", kli18n("Turn Bluetooth on")},
    {"reset-display", kli18n("Reset the saved screen layout")},
    {"restart-network", kli18n("Restart the network service")},
    {"restart-bluetooth", kli18n("Restart the Bluetooth service")},
    {"restart-printing", kli18n("Restart the printing service")},
    {"resume-printers", kli18n("Start the paused printers again")},
    {"repair-updates", kli18n("Repair Raye Update")},
};

// Problems Raye cannot fix itself: what to do instead.
const RemedyWords adviceWords[] = {
    {"plug-in-audio", kli18n("No speakers or headphones were found. Plug them in or turn them on, then try again.")},
    {"sign-in", kli18n("This network asks you to sign in. Open your browser to see its sign-in page.")},
    {"no-adapter", kli18n("No Bluetooth adapter was found. If your PC has one, it may be turned off in its firmware settings.")},
    {"hardware-switch", kli18n("Bluetooth is turned off by a switch or a key on your PC. Turn it on there, or turn off airplane mode.")},
    {"add-printer", kli18n("No printer is set up yet. Add one in Bluetooth & devices > Printers & scanners.")},
    {"free-space", kli18n("Free up some space, for example in System > Storage, then try again.")},
};

const CheckWords *wordsOf(const QString &id)
{
    for (const CheckWords &w : checkWords) {
        if (id == QLatin1String(w.id))
            return &w;
    }
    return nullptr;
}

QString textOf(const RemedyWords *table, size_t size, const QString &id)
{
    for (size_t i = 0; i < size; ++i) {
        if (id == QLatin1String(table[i].id))
            return table[i].text.toString();
    }
    return QString();
}

const QRegularExpression &idPattern()
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    return re;
}

bool validId(const QString &id)
{
    return id.size() <= 40 && idPattern().match(id).hasMatch();
}

} // namespace

Troubleshooter::Troubleshooter(QObject *parent)
    : QObject(parent)
    , m_helper(QStringLiteral(RAYE_LIBEXECDIR "/troubleshoot"))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_process)
            m_process->kill();
    });
}

Troubleshooter::~Troubleshooter()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QStringList Troubleshooter::names()
{
    return {QStringLiteral("audio"), QStringLiteral("network"), QStringLiteral("printer"),
            QStringLiteral("update"), QStringLiteral("bluetooth"), QStringLiteral("display")};
}

void Troubleshooter::setName(const QString &name)
{
    if (name == m_name || !names().contains(name) || busy())
        return;
    m_name = name;
    m_steps.clear();
    setState(QStringLiteral("idle"));
    Q_EMIT nameChanged();
    Q_EMIT stepsChanged();
}

QString Troubleshooter::title() const
{
    if (m_name == QLatin1String("audio"))
        return i18nc("@title troubleshooter", "Audio");
    if (m_name == QLatin1String("network"))
        return i18nc("@title troubleshooter", "Internet connections");
    if (m_name == QLatin1String("printer"))
        return i18nc("@title troubleshooter", "Printer");
    if (m_name == QLatin1String("update"))
        return i18nc("@title troubleshooter", "Raye Update");
    if (m_name == QLatin1String("bluetooth"))
        return i18nc("@title troubleshooter", "Bluetooth");
    if (m_name == QLatin1String("display"))
        return i18nc("@title troubleshooter", "Display");
    return QString();
}

bool Troubleshooter::busy() const
{
    return m_state == QLatin1String("checking") || m_state == QLatin1String("fixing")
        || m_state == QLatin1String("verifying");
}

Troubleshooter::Line Troubleshooter::parseLine(const QString &raw)
{
    Line line;
    const QString text = raw.trimmed();
    const QStringList parts = text.split(QLatin1Char(' '));
    if (parts.size() < 2 || text.contains(QLatin1String("  ")) || !validId(parts.at(1)))
        return line;
    const QString &verb = parts.at(0);
    if (verb == QLatin1String("begin") && parts.size() == 2) {
        line.kind = Line::Begin;
    } else if ((verb == QLatin1String("fixed") || verb == QLatin1String("failed")) && parts.size() == 2) {
        line.kind = verb == QLatin1String("fixed") ? Line::Fixed : Line::Failed;
    } else if (verb == QLatin1String("check") && parts.size() == 3
               && (parts.at(2) == QLatin1String("ok") || parts.at(2) == QLatin1String("unknown"))) {
        line.kind = Line::Check;
        line.result = parts.at(2);
    } else if (verb == QLatin1String("check") && parts.size() == 5 && parts.at(2) == QLatin1String("problem")
               && (parts.at(3) == QLatin1String("-") || validId(parts.at(3)))) {
        const QString &level = parts.at(4);
        if (level == QLatin1String("user"))
            line.level = Level::User;
        else if (level == QLatin1String("admin"))
            line.level = Level::Admin;
        else if (level != QLatin1String("none"))
            return line;
        // something to run needs a remedy to run
        if (line.level != Level::None && parts.at(3) == QLatin1String("-"))
            return line;
        line.kind = Line::Check;
        line.result = parts.at(2);
        line.remedy = parts.at(3);
    } else {
        return line;
    }
    line.id = parts.at(1);
    return line;
}

bool Troubleshooter::knowsCheck(const QString &id)
{
    return wordsOf(id) != nullptr;
}

bool Troubleshooter::knowsRemedy(const QString &remedy)
{
    return !remedyText(remedy).isEmpty();
}

QString Troubleshooter::checkTitle(const QString &id)
{
    const CheckWords *w = wordsOf(id);
    return w ? w->title.toString() : QString();
}

QString Troubleshooter::checkDetail(const QString &id, const QString &status)
{
    const CheckWords *w = wordsOf(id);
    if (!w)
        return QString();
    if (status == QLatin1String("running"))
        return w->running.toString();
    if (status == QLatin1String("ok"))
        return w->ok.toString();
    if (status == QLatin1String("problem"))
        return w->problem.toString();
    if (status == QLatin1String("fixed"))
        return w->fixed.toString();
    if (status == QLatin1String("notFixed"))
        return i18nc("@info the problem, then this", "%1 Restarting your PC may help.", w->problem.toString());
    if (status == QLatin1String("unknown"))
        return i18n("Raye could not check this.");
    return QString();
}

QString Troubleshooter::remedyText(const QString &remedy)
{
    return textOf(remedyWords, std::size(remedyWords), remedy);
}

QString Troubleshooter::adviceText(const QString &checkId, const QString &remedy)
{
    Q_UNUSED(checkId)
    return textOf(adviceWords, std::size(adviceWords), remedy);
}

QVariantList Troubleshooter::steps() const
{
    QVariantList list;
    for (const Step &s : m_steps) {
        QString detail;
        if (s.status == QLatin1String("fixing")) {
            detail = i18nc("@info a fix is running", "%1…", remedyText(s.remedy));
        } else if (s.status == QLatin1String("notFixed")) {
            const QString advice = s.level == Level::None ? adviceText(s.id, s.remedy) : QString();
            const QString problem = checkDetail(s.id, QStringLiteral("problem"));
            if (!advice.isEmpty())
                detail = advice;
            else if (s.denied)
                detail = i18nc("@info the problem, then this", "%1 Fixing this needs root access.", problem);
            else if (s.remedyFailed)
                detail = i18nc("@info the problem, then this", "%1 Raye could not fix it. Restarting your PC may help.", problem);
            else
                detail = checkDetail(s.id, s.status);
        } else {
            detail = checkDetail(s.id, s.status);
        }
        list.append(QVariantMap{{QStringLiteral("id"), s.id},
                                {QStringLiteral("title"), checkTitle(s.id)},
                                {QStringLiteral("detail"), detail},
                                {QStringLiteral("status"), s.status}});
    }
    return list;
}

QStringList Troubleshooter::remediesAt(Level level) const
{
    QStringList list;
    for (const Step &s : m_steps) {
        if (s.status == QLatin1String("problem") && s.level == level && !s.remedy.isEmpty() && !list.contains(s.remedy))
            list.append(s.remedy);
    }
    return list;
}

QVariantList Troubleshooter::remedies() const
{
    QVariantList list;
    for (Level level : {Level::User, Level::Admin}) {
        const QStringList ids = remediesAt(level);
        for (const QString &id : ids) {
            list.append(QVariantMap{{QStringLiteral("remedy"), id},
                                    {QStringLiteral("text"), remedyText(id)},
                                    {QStringLiteral("admin"), level == Level::Admin}});
        }
    }
    return list;
}

bool Troubleshooter::needsAdmin() const
{
    return !remediesAt(Level::Admin).isEmpty();
}

void Troubleshooter::setTimeouts(int checkMs, int adminMs)
{
    m_checkTimeout = checkMs;
    m_adminTimeout = adminMs;
}

void Troubleshooter::setState(const QString &state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

Troubleshooter::Step *Troubleshooter::step(const QString &id)
{
    for (Step &s : m_steps) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

void Troubleshooter::run()
{
    if (busy())
        return;
    m_steps.clear();
    m_fixedRemedies.clear();
    m_failedRemedies.clear();
    m_adminDenied = false;
    m_resultTitle.clear();
    m_resultText.clear();
    m_resultStatus.clear();
    Q_EMIT stepsChanged();
    if (!names().contains(m_name)) {
        m_resultTitle = i18n("The troubleshooter could not run");
        m_resultText = i18n("Try again, or restart your PC.");
        setState(QStringLiteral("failed"));
        return;
    }
    setState(QStringLiteral("checking"));
    start(Phase::Check, m_helper, {QStringLiteral("check"), m_name});
}

void Troubleshooter::fix()
{
    if (m_state != QLatin1String("found"))
        return;
    const QStringList user = remediesAt(Level::User);
    for (Step &s : m_steps) {
        if (s.status == QLatin1String("problem") && s.level != Level::None)
            s.status = QStringLiteral("fixing");
    }
    Q_EMIT stepsChanged();
    setState(QStringLiteral("fixing"));
    if (!user.isEmpty())
        start(Phase::UserFix, m_helper, QStringList{QStringLiteral("fix")} + user);
    else
        startAdminFixes();
}

void Troubleshooter::startAdminFixes()
{
    QStringList admin;
    for (const Step &s : std::as_const(m_steps)) {
        if (s.status == QLatin1String("fixing") && s.level == Level::Admin && !admin.contains(s.remedy))
            admin.append(s.remedy);
    }
    if (admin.isEmpty()) {
        startVerify();
        return;
    }
    // one password for all of them; pkexec runs the helper by its full
    // path, the one org.raye.settings.troubleshoot names
    start(Phase::AdminFix, m_pkexec, QStringList{m_helper, QStringLiteral("fix")} + admin);
}

void Troubleshooter::startVerify()
{
    setState(QStringLiteral("verifying"));
    start(Phase::Verify, m_helper, {QStringLiteral("check"), m_name});
}

void Troubleshooter::cancel()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_timeout.stop();
    m_phase = Phase::None;
    m_buffer.clear();
    m_steps.clear();
    Q_EMIT stepsChanged();
    setState(QStringLiteral("idle"));
}

void Troubleshooter::start(Phase phase, const QString &program, const QStringList &arguments)
{
    m_phase = phase;
    m_buffer.clear();
    m_parsedAny = false;
    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(arguments);
    // what the helper says goes to the page; what its tools say does not
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setStandardErrorFile(QProcess::nullDevice());
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Troubleshooter::readOutput);
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        processFinished(exitCode, status != QProcess::NormalExit);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            processFinished(-1, true);
    });
    m_timeout.start(phase == Phase::AdminFix ? m_adminTimeout : m_checkTimeout);
    m_process->start();
}

void Troubleshooter::readOutput()
{
    if (!m_process)
        return;
    m_buffer += m_process->readAllStandardOutput();
    // a line that never ends is not a line the helper would print
    if (m_buffer.size() > 64 * 1024)
        m_buffer.clear();
    int end;
    bool changed = false;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const Line line = parseLine(QString::fromUtf8(m_buffer.left(end)));
        m_buffer.remove(0, end + 1);
        if (line.kind != Line::Invalid) {
            handle(line);
            changed = true;
        }
    }
    if (changed)
        Q_EMIT stepsChanged();
}

void Troubleshooter::handle(const Line &line)
{
    switch (line.kind) {
    case Line::Begin:
    case Line::Check: {
        if ((m_phase != Phase::Check && m_phase != Phase::Verify) || !knowsCheck(line.id))
            return;
        // a remedy the page cannot name is not offered
        if (line.kind == Line::Check && line.result == QLatin1String("problem") && line.level != Level::None
            && !knowsRemedy(line.remedy))
            return;
        Step *s = step(line.id);
        if (!s) {
            Step added;
            added.id = line.id;
            added.status = QStringLiteral("running");
            m_steps.append(added);
            s = &m_steps.last();
        }
        m_parsedAny = true;
        if (line.kind == Line::Begin) {
            if (m_phase == Phase::Check)
                s->status = QStringLiteral("running");
            return;
        }
        if (m_phase == Phase::Check) {
            s->status = line.result;
            if (line.result == QLatin1String("problem")) {
                s->hadProblem = true;
                s->remedy = line.remedy == QLatin1String("-") ? QString() : line.remedy;
                s->level = line.level;
            }
        } else if (line.result == QLatin1String("ok")) {
            s->status = s->hadProblem ? QStringLiteral("fixed") : QStringLiteral("ok");
        } else if (line.result == QLatin1String("problem")) {
            if (!s->hadProblem) {
                s->hadProblem = true;
                s->remedy = line.remedy == QLatin1String("-") ? QString() : line.remedy;
                s->level = line.level;
            }
            s->status = QStringLiteral("notFixed");
        } else {
            s->status = s->hadProblem ? QStringLiteral("notFixed") : QStringLiteral("unknown");
        }
        return;
    }
    case Line::Fixed:
    case Line::Failed:
        if (m_phase != Phase::UserFix && m_phase != Phase::AdminFix)
            return;
        m_parsedAny = true;
        (line.kind == Line::Fixed ? m_fixedRemedies : m_failedRemedies).append(line.id);
        return;
    case Line::Invalid:
        return;
    }
}

void Troubleshooter::processFinished(int exitCode, bool crashed)
{
    if (!m_process)
        return;
    m_timeout.stop();
    readOutput();
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;
    const Phase phase = m_phase;
    m_phase = Phase::None;

    switch (phase) {
    case Phase::Check:
        finishCheck(crashed || !m_parsedAny);
        return;
    case Phase::UserFix:
        for (Step &s : m_steps) {
            if (s.status == QLatin1String("fixing") && s.level == Level::User && !m_fixedRemedies.contains(s.remedy))
                s.remedyFailed = true;
        }
        startAdminFixes();
        return;
    case Phase::AdminFix: {
        // 126: the password dialog was closed; 127: not allowed
        const bool denied = !crashed && (exitCode == 126 || exitCode == 127) && !m_parsedAny;
        m_adminDenied = m_adminDenied || denied;
        for (Step &s : m_steps) {
            if (s.status != QLatin1String("fixing") || s.level != Level::Admin)
                continue;
            if (denied)
                s.denied = true;
            else if (!m_fixedRemedies.contains(s.remedy))
                s.remedyFailed = true;
        }
        startVerify();
        return;
    }
    case Phase::Verify:
        finishVerify();
        return;
    case Phase::None:
        return;
    }
}

void Troubleshooter::finishCheck(bool helperFailed)
{
    if (helperFailed) {
        m_steps.clear();
        Q_EMIT stepsChanged();
        m_resultTitle = i18n("The troubleshooter could not run");
        m_resultText = i18n("Try again, or restart your PC.");
        m_resultStatus = QStringLiteral("notFixed");
        setState(QStringLiteral("failed"));
        return;
    }
    bool fixable = false;
    for (Step &s : m_steps) {
        if (s.status == QLatin1String("running"))
            s.status = QStringLiteral("unknown");
        if (s.status == QLatin1String("problem") && s.level != Level::None)
            fixable = true;
    }
    Q_EMIT stepsChanged();
    if (fixable) {
        setState(QStringLiteral("found"));
        return;
    }
    finish();
}

void Troubleshooter::finishVerify()
{
    // what the second look did not reach keeps its first answer
    for (Step &s : m_steps) {
        if (s.status == QLatin1String("fixing") || s.status == QLatin1String("problem") || s.status == QLatin1String("running"))
            s.status = s.hadProblem ? QStringLiteral("notFixed") : QStringLiteral("unknown");
    }
    finish();
}

void Troubleshooter::finish()
{
    int problems = 0;
    int fixed = 0;
    for (Step &s : m_steps) {
        if (s.status == QLatin1String("problem"))
            s.status = QStringLiteral("notFixed");
        if (s.hadProblem)
            ++problems;
        if (s.status == QLatin1String("fixed"))
            ++fixed;
    }
    Q_EMIT stepsChanged();

    bool unchecked = false;
    for (const Step &s : std::as_const(m_steps))
        unchecked = unchecked || s.status == QLatin1String("unknown");
    if (problems == 0) {
        m_resultStatus = QStringLiteral("ok");
        m_resultTitle = i18n("No problems found");
        m_resultText = unchecked
            ? i18n("Raye could not check everything. If something still doesn't work, restart your PC and try again.")
            : i18n("Everything Raye checked works. If something still doesn't, restart your PC and try again.");
    } else if (fixed == problems) {
        m_resultStatus = QStringLiteral("fixed");
        m_resultTitle = i18np("Problem fixed", "Problems fixed", problems);
        m_resultText = i18np("Raye found a problem and fixed it.", "Raye found %1 problems and fixed them.", problems);
    } else if (fixed > 0) {
        m_resultStatus = QStringLiteral("partly");
        m_resultTitle = i18n("Some problems were fixed");
        m_resultText = i18n("Raye fixed %1 of %2 problems. The list says what to try for the others.", fixed, problems);
    } else {
        m_resultStatus = QStringLiteral("notFixed");
        m_resultTitle = i18np("Raye couldn't fix the problem", "Raye couldn't fix the problems", problems);
        m_resultText = i18n("The list says what to try next.");
    }
    if (m_adminDenied)
        m_resultText += QLatin1Char(' ') + i18n("Some fixes need root access.");
    setState(QStringLiteral("done"));
}
