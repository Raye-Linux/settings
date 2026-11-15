// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace {

// pkexec's own answers: the password window was closed, or the
// password was not accepted
constexpr int kDismissed = 126;
constexpr int kNotAuthorized = 127;
// the helpers' "this computer cannot do it"
constexpr int kNotSupported = 3;
// restore points offered at most
constexpr int kMaxSnapshots = 30;

bool plasmaLogout()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"),
                                                          QStringLiteral("/Shutdown"),
                                                          QStringLiteral("org.kde.Shutdown"),
                                                          QStringLiteral("logout"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.send(message);
}

// "28 October" or "October 28", the way the language puts a day and
// its month; with the year when it is not this one
QString dayAndMonth(const QDate &date, const QDate &today, const QLocale &locale)
{
    // the day of the month is "d" or "dd"; "ddd" and "dddd" name the weekday
    const QString pattern = locale.dateFormat(QLocale::LongFormat);
    static const QRegularExpression dayToken(QStringLiteral("(?<!d)dd?(?!d)"));
    const QRegularExpressionMatch dayMatch = dayToken.match(pattern);
    const qsizetype day = dayMatch.hasMatch() ? dayMatch.capturedStart() : -1;
    const qsizetype month = pattern.indexOf(QLatin1Char('M'));
    const bool dayFirst = day >= 0 && (month < 0 || day < month);
    QString format = dayFirst ? QStringLiteral("d MMMM") : QStringLiteral("MMMM d");
    if (date.year() != today.year())
        format += dayFirst ? QStringLiteral(" yyyy") : QStringLiteral(", yyyy");
    return locale.toString(date, format);
}

} // namespace

Recovery::Recovery(QObject *parent)
    : QObject(parent)
    , m_helperDir(QStringLiteral(RAYE_LIBEXECDIR))
    , m_bootWindows(QStringLiteral("/usr/libexec/raye-boot-windows"))
    , m_logout(plasmaLogout)
{
    // to try the page against fake helpers and a fake computer (the
    // renders of the page are made this way); nothing here gains rights
    const QString helpers = qEnvironmentVariable("RAYE_RECOVERY_HELPERS");
    if (!helpers.isEmpty())
        m_helperDir = helpers;
    const QString bootWindows = qEnvironmentVariable("RAYE_RECOVERY_BOOT_WINDOWS");
    if (!bootWindows.isEmpty())
        m_bootWindows = bootWindows;
    const QString root = qEnvironmentVariable("RAYE_RECOVERY_ROOT");
    if (!root.isEmpty()) {
        m_root = root;
        m_askLogind = false;
    }
}

Recovery::~Recovery()
{
    // a repair that runs on goes on as root without a window to show
    // it: nothing is killed, apt must not stop half way
    if (m_repairProcess) {
        m_repairProcess->setParent(nullptr);
        connect(m_repairProcess.data(), &QProcess::finished, m_repairProcess.data(), &QObject::deleteLater);
    }
}

QString Recovery::helper(const QString &name) const
{
    return m_helperDir + QLatin1Char('/') + name;
}

void Recovery::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

QProcess *Recovery::run(const QString &program, const QStringList &arguments, Finished done)
{
    auto *process = new QProcess(this);
    process->setProgram(program);
    process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    auto called = std::make_shared<bool>(false);
    auto finish = [process, done, called](int code) {
        if (*called)
            return;
        *called = true;
        const QByteArray out = process->readAllStandardOutput();
        process->deleteLater();
        if (done)
            done(code, out);
    };
    connect(process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit ? code : -1);
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(-1);
    });
    process->start();
    return process;
}

QProcess *Recovery::runAsAdmin(const QString &name, const QStringList &arguments, Finished done)
{
    return run(QStringLiteral("pkexec"), QStringList{helper(name)} + arguments, std::move(done));
}

void Recovery::refresh()
{
    const bool goBack = goBackSupported(m_root);
    if (goBack != m_goBack) {
        m_goBack = goBack;
        Q_EMIT availabilityChanged();
    }

    // Restart to Windows: the button runs raye-restart-to-windows, which
    // asks raye-boot-windows for the entry
    if (QFileInfo(m_bootWindows).isExecutable()
        && !QStandardPaths::findExecutable(QStringLiteral("raye-restart-to-windows")).isEmpty()) {
        run(m_bootWindows, {QStringLiteral("--check")}, [this](int code, const QByteArray &) {
            if (m_windows != (code == 0)) {
                m_windows = code == 0;
                Q_EMIT availabilityChanged();
            }
        });
    } else if (m_windows) {
        m_windows = false;
        Q_EMIT availabilityChanged();
    }

    // reading the boot menu needs no password
    run(helper(QStringLiteral("bootmenu")), {QStringLiteral("--check")}, [this](int code, const QByteArray &) {
        if (m_bootMenu != (code == 0)) {
            m_bootMenu = code == 0;
            Q_EMIT availabilityChanged();
        }
    });

    const bool uefi = QFileInfo::exists(m_root + QStringLiteral("/sys/firmware/efi"));
    auto setFirmware = [this](bool available) {
        if (m_firmware != available) {
            m_firmware = available;
            Q_EMIT availabilityChanged();
        }
    };
    if (!uefi || !m_askLogind) {
        setFirmware(uefi);
    } else {
        QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                              QStringLiteral("/org/freedesktop/login1"),
                                                              QStringLiteral("org.freedesktop.login1.Manager"),
                                                              QStringLiteral("CanRebootToFirmwareSetup"));
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, setFirmware]() {
            QDBusPendingReply<QString> reply = *watcher;
            const QString answer = reply.isValid() ? reply.value() : QString();
            setFirmware(answer == QLatin1String("yes") || answer == QLatin1String("challenge"));
            watcher->deleteLater();
        });
    }

    refreshReset();
}

void Recovery::refreshReset()
{
    run(helper(QStringLiteral("reset-settings")), {QStringLiteral("status")}, [this](int code, const QByteArray &out) {
        const ResetStatus status = code == 0 ? parseResetStatus(QString::fromUtf8(out)) : ResetStatus();
        if (status.pending == m_resetPending && status.daysLeft == m_undoDays && status.when == m_undoWhen)
            return;
        m_resetPending = status.pending;
        m_undoDays = status.daysLeft;
        m_undoWhen = status.when;
        Q_EMIT resetChanged();
    });
}

QString Recovery::undoDate() const
{
    if (!m_undoWhen.isValid())
        return QString();
    const QLocale locale;
    return dayAndMonth(m_undoWhen.date(), QDate::currentDate(), locale);
}

void Recovery::reboot(const QStringList &extra)
{
    // a restart in the middle of apt would leave the system half repaired
    if (m_repairProcess) {
        setBusy(false);
        Q_EMIT failed(QStringLiteral("restart-failed"));
        return;
    }
    setBusy(true);
    run(QStringLiteral("systemctl"), QStringList{QStringLiteral("reboot")} + extra, [this](int code, const QByteArray &) {
        if (code == 0) {
            Q_EMIT leaving();
            return;
        }
        setBusy(false);
        Q_EMIT failed(QStringLiteral("restart-failed"));
    });
}

void Recovery::restartToWindows()
{
    if (m_busy || !m_windows || m_repairProcess)
        return;
    setBusy(true);
    run(QStringLiteral("raye-restart-to-windows"), {}, [this](int code, const QByteArray &) {
        if (code == 0) {
            Q_EMIT leaving();
            return;
        }
        setBusy(false);
        Q_EMIT failed(QStringLiteral("restart-failed"));
    });
}

// --- Repair --------------------------------------------------------------

void Recovery::setRepair(const QString &state, const QString &error)
{
    m_repairState = state;
    m_repairError = error;
    Q_EMIT repairChanged();
}

Recovery::RepairLine Recovery::parseRepairLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    const qsizetype space = trimmed.indexOf(QLatin1Char(' '));
    if (space < 0)
        return {trimmed, QString()};
    return {trimmed.left(space), trimmed.mid(space + 1).trimmed()};
}

void Recovery::handleRepairLine(const QString &line)
{
    static const QStringList stages{QStringLiteral("update"), QStringLiteral("check"), QStringLiteral("download"),
                                    QStringLiteral("install")};
    static const QStringList errors{QStringLiteral("network"), QStringLiteral("locked"), QStringLiteral("apt")};
    const RepairLine parsed = parseRepairLine(line);
    if (parsed.key == QLatin1String("stage")) {
        if (stages.contains(parsed.value))
            setRepair(parsed.value);
    } else if (parsed.key == QLatin1String("progress")) {
        bool ok = false;
        const int value = parsed.value.toInt(&ok);
        if (ok) {
            // never backwards: a stage that starts at a lower number does not undo the bar
            const int clamped = std::clamp(value, 0, 100);
            if (clamped > m_repairProgress) {
                m_repairProgress = clamped;
                Q_EMIT repairChanged();
            }
        }
    } else if (parsed.key == QLatin1String("found")) {
        bool ok = false;
        const int value = parsed.value.toInt(&ok);
        if (ok && value >= 0) {
            m_repairFound = value;
            Q_EMIT repairChanged();
        }
    } else if (parsed.key == QLatin1String("done")) {
        m_repairProgress = 100;
        if (m_repairFound < 0)
            m_repairFound = 0;
        setRepair(QStringLiteral("done"));
    } else if (parsed.key == QLatin1String("error")) {
        setRepair(QStringLiteral("error"), errors.contains(parsed.value) ? parsed.value : QStringLiteral("failed"));
    }
}

void Recovery::startRepair()
{
    if (m_repairProcess)
        return;
    m_repairProgress = 0;
    m_repairFound = -1;
    m_repairBuffer.clear();
    setRepair(QStringLiteral("update"));

    auto *process = new QProcess(this);
    m_repairProcess = process;
    process->setProgram(QStringLiteral("pkexec"));
    process->setArguments({helper(QStringLiteral("repair"))});
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        m_repairBuffer += QString::fromUtf8(process->readAllStandardOutput());
        qsizetype end;
        while ((end = m_repairBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
            const QString line = m_repairBuffer.left(end);
            m_repairBuffer.remove(0, end + 1);
            handleRepairLine(line);
        }
    });
    auto finish = [this, process](int code) {
        if (m_repairProcess != process)
            return;
        m_repairProcess = nullptr;
        if (!m_repairBuffer.isEmpty()) {
            handleRepairLine(m_repairBuffer);
            m_repairBuffer.clear();
        }
        process->deleteLater();
        if (m_repairState == QLatin1String("done") || m_repairState == QLatin1String("error"))
            return;
        if (code == kDismissed) {
            m_repairProgress = 0;
            setRepair(QStringLiteral("idle"));
        } else if (code == kNotAuthorized) {
            setRepair(QStringLiteral("error"), QStringLiteral("denied"));
        } else {
            setRepair(QStringLiteral("error"), QStringLiteral("failed"));
        }
    };
    connect(process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit ? code : -1);
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(-1);
    });
    process->start();
}

void Recovery::resetRepair()
{
    if (m_repairProcess)
        return;
    m_repairProgress = 0;
    m_repairFound = -1;
    setRepair(QStringLiteral("idle"));
}

void Recovery::restartAfterRepair()
{
    if (m_busy || m_repairProcess)
        return;
    reboot();
}

// --- Go back ---------------------------------------------------------------

bool Recovery::goBackSupported(const QString &root)
{
    if (!QFileInfo::exists(root + QStringLiteral("/etc/snapper/configs/root")))
        return false;
    QFile mounts(root + QStringLiteral("/proc/self/mountinfo"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    // a /proc file says its size is 0, so atEnd() is true before anything is read
    const QList<QByteArray> lines = mounts.readAll().split('\n');
    for (const QByteArray &line : lines) {
        // id parent dev <root in the volume> <mount point> ... - <type> <source> <options>
        const QStringList fields = QString::fromUtf8(line).trimmed().split(QLatin1Char(' '));
        const qsizetype separator = fields.indexOf(QStringLiteral("-"));
        if (fields.size() < 5 || separator < 0 || separator + 1 >= fields.size())
            continue;
        if (fields.at(4) != QLatin1String("/"))
            continue;
        const QString inVolume = fields.at(3);
        // a subvolume of its own, at the top of the volume: "/@"
        return fields.at(separator + 1) == QLatin1String("btrfs") && inVolume.size() > 1
               && inVolume.lastIndexOf(QLatin1Char('/')) == 0;
    }
    return false;
}

QString Recovery::whenInWords(const QDateTime &when, const QDateTime &now, const QLocale &locale)
{
    const QString time = locale.toString(when.time(), QLocale::ShortFormat);
    const qint64 days = when.date().daysTo(now.date());
    if (days == 0)
        return i18nc("@info a restore point made today, at a time", "today, %1", time);
    if (days == 1)
        return i18nc("@info a restore point made yesterday, at a time", "yesterday, %1", time);
    return i18nc("@info a restore point made on a date, at a time", "%1, %2",
                 dayAndMonth(when.date(), now.date(), locale), time);
}

QVariantList Recovery::parseSnapshots(const QByteArray &json, const QDateTime &now, const QLocale &locale)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonArray entries;
    if (doc.isObject()) {
        const QJsonObject configs = doc.object();
        if (configs.contains(QStringLiteral("root")))
            entries = configs.value(QStringLiteral("root")).toArray();
        else if (!configs.isEmpty())
            entries = configs.constBegin().value().toArray();
    } else if (doc.isArray()) {
        entries = doc.array();
    }

    struct Point {
        int number;
        QVariantMap map;
    };
    QList<Point> points;
    for (const QJsonValue &value : std::as_const(entries)) {
        const QJsonObject o = value.toObject();
        const int number = o.value(QStringLiteral("number")).toInt(-1);
        const QString type = o.value(QStringLiteral("type")).toString();
        // 0 is the system as it is; a "post" snapshot is the state right
        // after a change, the one before it is what people go back to
        if (number <= 0 || type == QLatin1String("post"))
            continue;
        const QDateTime when = QDateTime::fromString(o.value(QStringLiteral("date")).toString(),
                                                     QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!when.isValid())
            continue;
        const QString description = o.value(QStringLiteral("description")).toString().trimmed();
        // what made it: "update" (Raye Update), "daily", "manual"
        const QString maker = o.value(QStringLiteral("userdata")).toObject().value(QStringLiteral("raye")).toString();
        QString title;
        if (maker == QLatin1String("update"))
            title = i18nc("@info restore point", "Before installing updates");
        else if (maker == QLatin1String("daily"))
            title = i18nc("@info restore point", "Daily restore point");
        else if (maker == QLatin1String("manual") && !description.isEmpty())
            title = description;
        else if (description == QLatin1String("apt") || description.startsWith(QLatin1String("apt ")))
            title = i18nc("@info restore point", "Before installing updates or apps");
        else if (description == QLatin1String("timeline"))
            title = i18nc("@info restore point", "Automatic restore point");
        else if (description == QLatin1String("boot"))
            title = i18nc("@info restore point", "When the computer started");
        else if (description.isEmpty())
            title = i18nc("@info restore point", "Restore point");
        else
            title = description;
        points.append({number,
                       QVariantMap{{QStringLiteral("number"), number},
                                   {QStringLiteral("title"), title},
                                   {QStringLiteral("when"), whenInWords(when, now, locale)},
                                   {QStringLiteral("date"), when.toString(Qt::ISODate)}}});
    }
    std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.number > b.number; });
    QVariantList list;
    for (const Point &point : std::as_const(points)) {
        if (list.size() >= kMaxSnapshots)
            break;
        list.append(point.map);
    }
    return list;
}

void Recovery::loadSnapshots()
{
    if (m_snapshotsState == QLatin1String("loading"))
        return;
    m_snapshotsState = QStringLiteral("loading");
    Q_EMIT snapshotsChanged();
    runAsAdmin(QStringLiteral("snapshot-restore"), {QStringLiteral("list")}, [this](int code, const QByteArray &out) {
        if (code == 0) {
            m_snapshots = parseSnapshots(out, QDateTime::currentDateTime(), QLocale());
            m_snapshotsState = QStringLiteral("ready");
        } else {
            m_snapshots.clear();
            m_snapshotsState = code == kDismissed ? QStringLiteral("cancelled") : QStringLiteral("error");
        }
        Q_EMIT snapshotsChanged();
    });
}

void Recovery::goBack(int number)
{
    if (number <= 0 || m_goBackState == QLatin1String("working") || m_busy || m_repairProcess)
        return;
    m_goBackState = QStringLiteral("working");
    Q_EMIT goBackChanged();
    setBusy(true);
    runAsAdmin(QStringLiteral("snapshot-restore"), {QStringLiteral("go-back"), QString::number(number)},
               [this](int code, const QByteArray &out) {
                   const bool done = code == 0 && QString::fromUtf8(out).split(QLatin1Char('\n')).contains(QStringLiteral("done"));
                   setBusy(false);
                   if (done) {
                       m_goBackState = QStringLiteral("idle");
                       Q_EMIT goBackChanged();
                       reboot();
                       return;
                   }
                   m_goBackState = code == kDismissed ? QStringLiteral("cancelled") : QStringLiteral("error");
                   Q_EMIT goBackChanged();
               });
}

void Recovery::createRestorePoint(const QString &name)
{
    const QString clean = name.simplified().left(60);
    if (clean.isEmpty() || m_createState == QLatin1String("working") || m_busy)
        return;
    m_createState = QStringLiteral("working");
    Q_EMIT createChanged();
    runAsAdmin(QStringLiteral("snapshot-restore"), {QStringLiteral("create"), clean}, [this](int code, const QByteArray &out) {
        const bool done = code == 0 && QString::fromUtf8(out).startsWith(QLatin1String("done"));
        m_createState = done ? QStringLiteral("done") : code == kDismissed ? QStringLiteral("cancelled") : QStringLiteral("error");
        Q_EMIT createChanged();
        if (done) {
            m_snapshotsState = QStringLiteral("idle");
            loadSnapshots();
        }
    });
}

void Recovery::resetCreate()
{
    if (m_createState == QLatin1String("working"))
        return;
    m_createState = QStringLiteral("idle");
    Q_EMIT createChanged();
}

void Recovery::resetGoBack()
{
    if (m_goBackState == QLatin1String("working"))
        return;
    m_goBackState = QStringLiteral("idle");
    Q_EMIT goBackChanged();
}

// --- Reset your settings ---------------------------------------------------

Recovery::ResetStatus Recovery::parseResetStatus(const QString &output)
{
    ResetStatus status;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList words = line.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() == 2 && words.at(0) == QLatin1String("pending")) {
            static const QStringList known{QStringLiteral("none"), QStringLiteral("reset"),
                                           QStringLiteral("reset-keep-wallpaper"), QStringLiteral("undo")};
            if (known.contains(words.at(1)))
                status.pending = words.at(1);
        } else if (words.size() == 3 && words.at(0) == QLatin1String("undo")) {
            const QDateTime when = QDateTime::fromString(words.at(1), QStringLiteral("yyyyMMddHHmmss"));
            const int days = words.at(2).toInt();
            if (when.isValid() && days > 0) {
                status.when = when;
                status.daysLeft = days;
            }
        }
    }
    return status;
}

void Recovery::resetSettings(bool keepWallpaper)
{
    if (m_busy)
        return;
    QStringList arguments{QStringLiteral("schedule")};
    if (keepWallpaper)
        arguments << QStringLiteral("--keep-wallpaper");
    setBusy(true);
    run(helper(QStringLiteral("reset-settings")), arguments, [this](int code, const QByteArray &) {
        refreshReset();
        if (code != 0) {
            setBusy(false);
            Q_EMIT failed(QStringLiteral("reset-failed"));
            return;
        }
        if (m_logout && m_logout()) {
            Q_EMIT leaving();
            return;
        }
        // planned all the same: it happens at the next sign-in
        setBusy(false);
        Q_EMIT failed(QStringLiteral("logout-failed"));
    });
}

void Recovery::undoReset()
{
    if (m_busy || m_undoDays <= 0)
        return;
    setBusy(true);
    run(helper(QStringLiteral("reset-settings")), {QStringLiteral("schedule-undo")}, [this](int code, const QByteArray &) {
        refreshReset();
        if (code != 0) {
            setBusy(false);
            Q_EMIT failed(QStringLiteral("reset-failed"));
            return;
        }
        if (m_logout && m_logout()) {
            Q_EMIT leaving();
            return;
        }
        setBusy(false);
        Q_EMIT failed(QStringLiteral("logout-failed"));
    });
}

void Recovery::cancelReset()
{
    run(helper(QStringLiteral("reset-settings")), {QStringLiteral("cancel")}, [this](int, const QByteArray &) {
        refreshReset();
    });
}

void Recovery::signOut()
{
    if (m_busy)
        return;
    if (m_logout && m_logout()) {
        setBusy(true);
        Q_EMIT leaving();
        return;
    }
    Q_EMIT failed(QStringLiteral("logout-failed"));
}

// --- Advanced startup ------------------------------------------------------

void Recovery::restartToFirmware()
{
    if (m_busy || !m_firmware)
        return;
    reboot({QStringLiteral("--firmware-setup")});
}

void Recovery::restartToBootMenu()
{
    if (m_busy || m_repairProcess)
        return;
    setBusy(true);
    runAsAdmin(QStringLiteral("bootmenu"), {}, [this](int code, const QByteArray &) {
        if (code == 0) {
            reboot();
            return;
        }
        setBusy(false);
        if (code == kDismissed)
            return;
        if (code == kNotAuthorized)
            Q_EMIT failed(QStringLiteral("denied"));
        else if (code == kNotSupported)
            Q_EMIT failed(QStringLiteral("boot-menu-unavailable"));
        else
            Q_EMIT failed(QStringLiteral("restart-failed"));
    });
}
