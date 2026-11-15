// SPDX-License-Identifier: GPL-3.0-or-later
#include "fingerprint.h"

#include <KLocalizedString>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QProcess>

#include <pwd.h>
#include <unistd.h>

namespace {
const QString service = QStringLiteral("net.reactivated.Fprint");
const QString managerPath = QStringLiteral("/net/reactivated/Fprint/Manager");
const QString managerInterface = QStringLiteral("net.reactivated.Fprint.Manager");
const QString deviceInterface = QStringLiteral("net.reactivated.Fprint.Device");

QString loginName()
{
    const passwd *pw = ::getpwuid(::getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QString();
}
} // namespace

FingerprintSettings::FingerprintSettings(QObject *parent)
    : FingerprintSettings(QDBusConnection::systemBus(), loginName(), parent)
{
}

FingerprintSettings::FingerprintSettings(const QDBusConnection &bus, const QString &login, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_login(login)
{
    refresh();
}

bool FingerprintSettings::signInEnabled() const
{
    QFile file(pamFile);
    return file.open(QIODevice::ReadOnly) && file.readAll().contains("pam_fprintd");
}

void FingerprintSettings::setSignInEnabled(bool on)
{
    auto *process = new QProcess(this);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        Q_EMIT changed();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            Q_EMIT changed();
        }
    });
    process->start(helper.first(), helper.mid(1) << (on ? QStringLiteral("on") : QStringLiteral("off")));
}

void FingerprintSettings::refresh()
{
    QDBusMessage ask = QDBusMessage::createMethodCall(service, managerPath, managerInterface, QStringLiteral("GetDefaultDevice"));
    const QDBusReply<QDBusObjectPath> device = m_bus.call(ask, QDBus::Block, 3000);
    m_device = device.isValid() ? device.value().path() : QString();
    m_fingers.clear();
    if (!m_device.isEmpty()) {
        QDBusMessage list = QDBusMessage::createMethodCall(service, m_device, deviceInterface, QStringLiteral("ListEnrolledFingers"));
        list.setArguments({m_login});
        // "no enrolled prints" comes back as an error: that is an empty list
        const QDBusReply<QStringList> fingers = m_bus.call(list, QDBus::Block, 3000);
        if (fingers.isValid())
            m_fingers = fingers.value();
        QDBusMessage stages = QDBusMessage::createMethodCall(service, m_device, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        stages.setArguments({deviceInterface, QStringLiteral("num-enroll-stages")});
        const QDBusReply<QDBusVariant> needed = m_bus.call(stages, QDBus::Block, 3000);
        m_stagesNeeded = needed.isValid() ? qMax(0, needed.value().variant().toInt()) : 0;
    }
    Q_EMIT changed();
}

bool FingerprintSettings::callDevice(const QString &method, const QVariantList &arguments, int timeout)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, m_device, deviceInterface, method);
    message.setArguments(arguments);
    message.setInteractiveAuthorizationAllowed(true);
    return m_bus.call(message, QDBus::Block, timeout).type() == QDBusMessage::ReplyMessage;
}

void FingerprintSettings::startEnroll(const QString &finger)
{
    if (m_enrolling || m_device.isEmpty())
        return;
    m_stagesDone = 0;
    // enrolling asks for the user's password first (polkit)
    if (!callDevice(QStringLiteral("Claim"), {m_login}, 120000)) {
        m_message = i18nc("@info", "The fingerprint reader is busy. Try again in a moment.");
        Q_EMIT changed();
        return;
    }
    m_bus.connect(service, m_device, deviceInterface, QStringLiteral("EnrollStatus"), this, SLOT(enrollStatus(QString,bool)));
    if (!callDevice(QStringLiteral("EnrollStart"), {finger}, 120000)) {
        stop();
        m_message = i18nc("@info", "The fingerprint could not be added. It needs your password.");
        Q_EMIT changed();
        return;
    }
    m_enrolling = true;
    m_message = i18nc("@info", "Touch the reader with your finger.");
    Q_EMIT changed();
}

void FingerprintSettings::stop()
{
    m_bus.disconnect(service, m_device, deviceInterface, QStringLiteral("EnrollStatus"), this, SLOT(enrollStatus(QString,bool)));
    if (m_enrolling)
        callDevice(QStringLiteral("EnrollStop"));
    callDevice(QStringLiteral("Release"));
    m_enrolling = false;
}

void FingerprintSettings::cancelEnroll()
{
    if (!m_enrolling)
        return;
    stop();
    m_message.clear();
    refresh();
}

bool FingerprintSettings::statusEnds(const QString &status)
{
    return status == QLatin1String("enroll-completed") || status == QLatin1String("enroll-failed")
        || status == QLatin1String("enroll-data-full") || status == QLatin1String("enroll-disconnected")
        || status == QLatin1String("enroll-unknown-error") || status == QLatin1String("enroll-duplicate");
}

QString FingerprintSettings::statusMessage(const QString &status)
{
    if (status == QLatin1String("enroll-completed"))
        return i18nc("@info", "All done. You can now sign in with this finger.");
    if (status == QLatin1String("enroll-stage-passed"))
        return i18nc("@info", "Good. Lift your finger and touch the reader again.");
    if (status == QLatin1String("enroll-retry-scan") || status == QLatin1String("enroll-remove-and-retry"))
        return i18nc("@info", "That one didn't read. Lift your finger and try again.");
    if (status == QLatin1String("enroll-swipe-too-short"))
        return i18nc("@info", "Too quick. Swipe your finger again, a little slower.");
    if (status == QLatin1String("enroll-finger-not-centered"))
        return i18nc("@info", "Put your finger in the middle of the reader.");
    if (status == QLatin1String("enroll-duplicate"))
        return i18nc("@info", "This finger is already saved.");
    if (status == QLatin1String("enroll-data-full"))
        return i18nc("@info", "The reader has no more room for fingerprints.");
    if (status == QLatin1String("enroll-disconnected"))
        return i18nc("@info", "The fingerprint reader was disconnected.");
    return i18nc("@info", "The fingerprint could not be added. Try again.");
}

void FingerprintSettings::enrollStatus(const QString &status, bool done)
{
    if (status == QLatin1String("enroll-stage-passed"))
        ++m_stagesDone;
    m_message = statusMessage(status);
    if (done || statusEnds(status)) {
        stop();
        const QString message = m_message;
        refresh();
        m_message = message;
        Q_EMIT changed();
        if (status == QLatin1String("enroll-completed"))
            Q_EMIT enrolled();
        return;
    }
    Q_EMIT changed();
}

void FingerprintSettings::removeAll()
{
    if (m_enrolling || m_device.isEmpty())
        return;
    if (callDevice(QStringLiteral("Claim"), {m_login}, 120000)) {
        callDevice(QStringLiteral("DeleteEnrolledFingers2"), {}, 120000);
        callDevice(QStringLiteral("Release"));
    } else {
        m_message = i18nc("@info", "The fingerprint reader is busy. Try again in a moment.");
    }
    refresh();
}

QString FingerprintSettings::fingerName(const QString &finger)
{
    if (finger == QLatin1String("left-thumb")) return i18nc("@item a finger", "Left thumb");
    if (finger == QLatin1String("left-index-finger")) return i18nc("@item a finger", "Left index finger");
    if (finger == QLatin1String("left-middle-finger")) return i18nc("@item a finger", "Left middle finger");
    if (finger == QLatin1String("left-ring-finger")) return i18nc("@item a finger", "Left ring finger");
    if (finger == QLatin1String("left-little-finger")) return i18nc("@item a finger", "Left little finger");
    if (finger == QLatin1String("right-thumb")) return i18nc("@item a finger", "Right thumb");
    if (finger == QLatin1String("right-index-finger")) return i18nc("@item a finger", "Right index finger");
    if (finger == QLatin1String("right-middle-finger")) return i18nc("@item a finger", "Right middle finger");
    if (finger == QLatin1String("right-ring-finger")) return i18nc("@item a finger", "Right ring finger");
    if (finger == QLatin1String("right-little-finger")) return i18nc("@item a finger", "Right little finger");
    return finger;
}
