// SPDX-License-Identifier: GPL-3.0-or-later
#include "currentuser.h"

#include <KLocalizedString>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QFileInfo>

#include <pwd.h>
#include <unistd.h>

namespace {
const QString accountsService = QStringLiteral("org.freedesktop.Accounts");
const QString accountsPath = QStringLiteral("/org/freedesktop/Accounts");
const QString userInterface = QStringLiteral("org.freedesktop.Accounts.User");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

// AccountsService answers in a few ms; a stuck daemon must not hold the
// window back
constexpr int callTimeout = 2000;
} // namespace

CurrentUser::CurrentUser(QObject *parent)
    : CurrentUser(QDBusConnection::systemBus(), ::getuid(), parent)
{
}

CurrentUser::CurrentUser(const QDBusConnection &bus, uint uid, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_uid(uid)
{
    m_info = readAccount();
}

QString CurrentUser::displayName() const
{
    return m_info.realName.isEmpty() ? m_info.userName : m_info.realName;
}

QUrl CurrentUser::picture() const
{
    // AccountsService names a file even when the user never chose one
    const QFileInfo file(m_info.iconFile);
    if (m_info.iconFile.isEmpty() || !file.isFile() || file.size() == 0)
        return {};
    QUrl url = QUrl::fromLocalFile(file.absoluteFilePath());
    if (m_pictureRevision > 0)
        url.setQuery(QStringLiteral("rev=%1").arg(m_pictureRevision));
    return url;
}

QString CurrentUser::accountLabel() const
{
    return m_info.administrator ? i18nc("@label kind of account", "Sudo user")
                                : i18nc("@label kind of account", "Standard user");
}

CurrentUser::Info CurrentUser::fromProperties(const QVariantMap &properties)
{
    Info info;
    info.userName = properties.value(QStringLiteral("UserName")).toString();
    info.realName = properties.value(QStringLiteral("RealName")).toString().trimmed();
    info.iconFile = properties.value(QStringLiteral("IconFile")).toString();
    info.administrator = properties.value(QStringLiteral("AccountType")).toInt() == 1;
    return info;
}

CurrentUser::Info CurrentUser::fromPasswd(const QString &login, const QString &gecos)
{
    Info info;
    info.userName = login;
    info.realName = gecos.section(QLatin1Char(','), 0, 0).trimmed();
    return info;
}

CurrentUser::Info CurrentUser::readAccount()
{
    QDBusInterface accounts(accountsService, accountsPath, accountsService, m_bus);
    accounts.setTimeout(callTimeout);
    const QDBusReply<QDBusObjectPath> path = accounts.call(QStringLiteral("FindUserById"), qint64(m_uid));
    if (path.isValid()) {
        QDBusInterface properties(accountsService, path.value().path(), propertiesInterface, m_bus);
        properties.setTimeout(callTimeout);
        const QDBusReply<QVariantMap> all = properties.call(QStringLiteral("GetAll"), userInterface);
        if (all.isValid()) {
            watch(path.value().path());
            return fromProperties(all.value());
        }
    }

    // no AccountsService (a container, a test): the passwd entry
    if (const passwd *pw = ::getpwuid(m_uid))
        return fromPasswd(QString::fromLocal8Bit(pw->pw_name), QString::fromLocal8Bit(pw->pw_gecos));
    return {};
}

void CurrentUser::watch(const QString &path)
{
    if (path == m_watched)
        return;
    if (!m_watched.isEmpty())
        m_bus.disconnect(accountsService, m_watched, userInterface, QStringLiteral("Changed"), this,
                         SLOT(accountChanged()));
    m_watched = path;
    m_bus.connect(accountsService, path, userInterface, QStringLiteral("Changed"), this, SLOT(accountChanged()));
}

void CurrentUser::accountChanged()
{
    reload();
}

void CurrentUser::reload()
{
    const Info info = readAccount();
    if (info.userName == m_info.userName && info.realName == m_info.realName && info.iconFile == m_info.iconFile
        && info.administrator == m_info.administrator) {
        // the same file may hold a new picture
        if (!info.iconFile.isEmpty()) {
            ++m_pictureRevision;
            Q_EMIT changed();
        }
        return;
    }
    m_info = info;
    Q_EMIT changed();
}
