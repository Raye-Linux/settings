// SPDX-License-Identifier: GPL-3.0-or-later
#include "hostname.h"

#include <KLocalizedString>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QRegularExpression>
#include <QSysInfo>

namespace {
const QString service = QStringLiteral("org.freedesktop.hostname1");
const QString path = QStringLiteral("/org/freedesktop/hostname1");
const QString interface = QStringLiteral("org.freedesktop.hostname1");
} // namespace

Hostname::Hostname(QObject *parent)
    : Hostname(QDBusConnection::systemBus(), parent)
{
}

Hostname::Hostname(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    m_name = QSysInfo::machineHostName();
    m_bus.connect(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                  this, SLOT(reload()));
    reload();
}

void Hostname::reload()
{
    QDBusMessage get = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("GetAll"));
    get << interface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError())
            return;
        const QVariantMap props = reply.value();
        QString name = props.value(QStringLiteral("PrettyHostname")).toString().trimmed();
        if (name.isEmpty())
            name = props.value(QStringLiteral("StaticHostname")).toString().trimmed();
        if (name.isEmpty())
            name = props.value(QStringLiteral("Hostname")).toString().trimmed();
        if (!name.isEmpty() && name != m_name) {
            m_name = name;
            Q_EMIT changed();
        }
    });
}

QString Hostname::problemWith(const QString &name)
{
    if (name.isEmpty())
        return QStringLiteral("empty");
    if (name.size() > maxLength)
        return QStringLiteral("too-long");
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9-]+$"));
    if (!allowed.match(name).hasMatch())
        return QStringLiteral("characters");
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    if (digits.match(name).hasMatch())
        return QStringLiteral("digits");
    if (name.startsWith(QLatin1Char('-')) || name.endsWith(QLatin1Char('-')))
        return QStringLiteral("hyphen");
    return QString();
}

QString Hostname::problemText(const QString &problem)
{
    if (problem == QLatin1String("empty"))
        return i18nc("@info rename this PC", "Enter a name for your PC.");
    if (problem == QLatin1String("too-long"))
        return i18nc("@info rename this PC", "The name can't be longer than %1 characters.", maxLength);
    if (problem == QLatin1String("characters"))
        return i18nc("@info rename this PC", "Use only letters (A–Z), numbers and hyphens. No spaces.");
    if (problem == QLatin1String("digits"))
        return i18nc("@info rename this PC", "The name can't be only numbers.");
    if (problem == QLatin1String("hyphen"))
        return i18nc("@info rename this PC", "The name can't start or end with a hyphen.");
    return QString();
}

QString Hostname::staticNameOf(const QString &name)
{
    return name.toLower();
}

void Hostname::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void Hostname::finish(bool ok, const QString &error)
{
    setBusy(false);
    reload();
    Q_EMIT renamed(ok, error);
}

void Hostname::rename(const QString &name)
{
    if (m_busy)
        return;
    if (!problemWith(name).isEmpty()) {
        Q_EMIT renamed(false, QStringLiteral("failed"));
        return;
    }
    setBusy(true);
    // the static name first: that is the one that needs the password,
    // and the pretty name follows only once it is taken
    QDBusMessage setStatic = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("SetStaticHostname"));
    setStatic << staticNameOf(name) << true;
    // the password dialog can stay open a long time
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(setStatic, 10 * 60 * 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            const QString e = reply.error().name();
            finish(false, e.contains(QLatin1String("AccessDenied")) || e.contains(QLatin1String("NotAuthorized"))
                              || e.contains(QLatin1String("InteractiveAuthorizationRequired"))
                          ? QStringLiteral("denied")
                          : QStringLiteral("failed"));
            return;
        }
        QDBusMessage setPretty = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("SetPrettyHostname"));
        setPretty << name << true;
        auto *next = new QDBusPendingCallWatcher(m_bus.asyncCall(setPretty, 10 * 60 * 1000), this);
        connect(next, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w2) {
            w2->deleteLater();
            // the static name is set: the PC is renamed even if this fails
            finish(true, QString());
        });
    });
}
