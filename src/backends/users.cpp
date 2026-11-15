// SPDX-License-Identifier: GPL-3.0-or-later
#include "users.h"

#include <KLocalizedString>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <crypt.h>
#include <unistd.h>

namespace {
const QString service = QStringLiteral("org.freedesktop.Accounts");
const QString accountsPath = QStringLiteral("/org/freedesktop/Accounts");
const QString userInterface = QStringLiteral("org.freedesktop.Accounts.User");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
// the password prompt waits for a person
constexpr int promptTimeout = 300000;
constexpr int readTimeout = 2000;
constexpr int pictureSize = 256;
} // namespace

AccountUsers::AccountUsers(QObject *parent)
    : AccountUsers(QDBusConnection::systemBus(), ::getuid(), parent)
{
}

AccountUsers::AccountUsers(const QDBusConnection &bus, uint uid, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_uid(uid)
{
    m_bus.connect(service, accountsPath, service, QStringLiteral("UserAdded"), this, SLOT(usersChanged()));
    m_bus.connect(service, accountsPath, service, QStringLiteral("UserDeleted"), this, SLOT(usersChanged()));
    reload();
}

void AccountUsers::usersChanged()
{
    reload();
}

void AccountUsers::reload()
{
    m_others.clear();
    QDBusMessage list = QDBusMessage::createMethodCall(service, accountsPath, service, QStringLiteral("ListCachedUsers"));
    const QDBusReply<QList<QDBusObjectPath>> users = m_bus.call(list, QDBus::Block, readTimeout);
    m_available = users.isValid();
    if (m_available) {
        for (const QDBusObjectPath &path : users.value()) {
            QDBusMessage ask = QDBusMessage::createMethodCall(service, path.path(), propertiesInterface, QStringLiteral("GetAll"));
            ask.setArguments({userInterface});
            const QDBusReply<QVariantMap> properties = m_bus.call(ask, QDBus::Block, readTimeout);
            if (!properties.isValid())
                continue;
            const QVariantMap p = properties.value();
            const qlonglong uid = p.value(QStringLiteral("Uid")).toLongLong();
            if (uid == m_uid || p.value(QStringLiteral("SystemAccount")).toBool())
                continue;
            const QString login = p.value(QStringLiteral("UserName")).toString();
            const QString real = p.value(QStringLiteral("RealName")).toString().trimmed();
            const QString icon = p.value(QStringLiteral("IconFile")).toString();
            const QFileInfo iconInfo(icon);
            m_others << QVariantMap{
                {QStringLiteral("uid"), uid},
                {QStringLiteral("userName"), login},
                {QStringLiteral("realName"), real},
                {QStringLiteral("displayName"), real.isEmpty() ? login : real},
                {QStringLiteral("picture"), iconInfo.isFile() && iconInfo.size() > 0 ? QUrl::fromLocalFile(icon) : QUrl()},
                {QStringLiteral("administrator"), p.value(QStringLiteral("AccountType")).toInt() == 1},
            };
        }
    }
    Q_EMIT changed();
}

void AccountUsers::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    Q_EMIT changed();
}

QString AccountUsers::pathOf(qlonglong uid) const
{
    QDBusMessage find = QDBusMessage::createMethodCall(service, accountsPath, service, QStringLiteral("FindUserById"));
    find.setArguments({uid});
    const QDBusReply<QDBusObjectPath> path = m_bus.call(find, QDBus::Block, readTimeout);
    return path.isValid() ? path.value().path() : QString();
}

void AccountUsers::call(const QString &path, const QString &interface, const QString &method, const QVariantList &arguments,
                        std::function<void(const QDBusMessage &)> next)
{
    if (path.isEmpty()) {
        m_error = i18nc("@info", "This account could not be found.");
        finish();
        return;
    }
    if (!m_busy) {
        m_busy = true;
        m_error.clear();
        Q_EMIT changed();
    }
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    // without this AccountsService refuses instead of asking the password
    message.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, promptTimeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, next = std::move(next)] {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            failed(reply);
            return;
        }
        if (next) {
            next(reply);
            return;
        }
        finish();
        Q_EMIT done();
    });
}

void AccountUsers::failed(const QDBusMessage &reply)
{
    const QString name = reply.errorName();
    if (name.endsWith(QLatin1String("PermissionDenied")) || name.endsWith(QLatin1String("AccessDenied"))
        || name.endsWith(QLatin1String("AuthFailed"))) {
        m_error = i18nc("@info", "The change was not allowed. It needs root access.");
    } else if (name.endsWith(QLatin1String("UserExists"))) {
        m_error = i18nc("@info", "This user name is already taken.");
    } else if (name.endsWith(QLatin1String("UserDoesNotExist"))) {
        m_error = i18nc("@info", "This account is gone.");
    } else {
        m_error = i18nc("@info %1 is the reason from the system", "The change could not be made: %1", reply.errorMessage());
    }
    finish();
}

void AccountUsers::finish()
{
    m_busy = false;
    reload();
}

void AccountUsers::setMyName(const QString &realName)
{
    call(pathOf(m_uid), userInterface, QStringLiteral("SetRealName"), {realName.trimmed()});
}

QString AccountUsers::preparePicture(const QString &file, const QString &outputFolder)
{
    QImageReader reader(file);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return {};
    const int side = qMin(image.width(), image.height());
    image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
                .scaled(pictureSize, pictureSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (!QDir().mkpath(outputFolder))
        return {};
    const QString out = outputFolder + QStringLiteral("/account-picture.png");
    return image.save(out, "PNG") ? out : QString();
}

void AccountUsers::setMyPicture(const QUrl &file)
{
    const QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const QString prepared = preparePicture(local, QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    if (prepared.isEmpty()) {
        m_error = i18nc("@info", "This file can't be used as a picture.");
        Q_EMIT changed();
        return;
    }
    call(pathOf(m_uid), userInterface, QStringLiteral("SetIconFile"), {prepared});
}

QString AccountUsers::hashPassword(const QString &password)
{
    // the default of libxcrypt: what passwd itself would write
    const char *salt = crypt_gensalt(nullptr, 0, nullptr, 0);
    if (!salt)
        return {};
    crypt_data data = {};
    const char *hashed = crypt_r(password.toUtf8().constData(), salt, &data);
    const QString out = hashed && hashed[0] != '*' ? QString::fromLatin1(hashed) : QString();
    explicit_bzero(&data, sizeof data);
    return out;
}

void AccountUsers::addUser(const QString &realName, const QString &login, const QString &password, bool administrator)
{
    const QString hash = hashPassword(password);
    if (hash.isEmpty()) {
        m_error = i18nc("@info", "The password could not be prepared.");
        Q_EMIT changed();
        return;
    }
    call(accountsPath, service, QStringLiteral("CreateUser"), {login, realName.trimmed(), int(administrator ? 1 : 0)},
         [this, hash](const QDBusMessage &reply) {
             const QString path = reply.arguments().value(0).value<QDBusObjectPath>().path();
             // no password yet means the account cannot sign in: finish here
             call(path, userInterface, QStringLiteral("SetPassword"), {hash, QString()});
         });
}

void AccountUsers::setAdministrator(qlonglong uid, bool administrator)
{
    call(pathOf(uid), userInterface, QStringLiteral("SetAccountType"), {int(administrator ? 1 : 0)});
}

void AccountUsers::setPasswordOf(qlonglong uid, const QString &password)
{
    const QString hash = hashPassword(password);
    if (hash.isEmpty()) {
        m_error = i18nc("@info", "The password could not be prepared.");
        Q_EMIT changed();
        return;
    }
    call(pathOf(uid), userInterface, QStringLiteral("SetPassword"), {hash, QString()});
}

void AccountUsers::removeUser(qlonglong uid, bool deleteFiles)
{
    if (uid == m_uid)
        return;
    call(accountsPath, service, QStringLiteral("DeleteUser"), {uid, deleteFiles});
}

QString AccountUsers::suggestLogin(const QString &realName)
{
    // accents go, then everything that is not a letter or a digit; the
    // first word is what a person expects: "Élise Roy" is "elise"
    QString plain = realName.trimmed().normalized(QString::NormalizationForm_D);
    plain.remove(QRegularExpression(QStringLiteral("\\p{M}")));
    plain = plain.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLower();
    plain.remove(QRegularExpression(QStringLiteral("[^a-z0-9_-]")));
    plain.remove(QRegularExpression(QStringLiteral("^[^a-z_]+")));
    return plain.left(32);
}

QString AccountUsers::loginProblem(const QString &login) const
{
    static const QRegularExpression valid(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
    if (login.isEmpty())
        return i18nc("@info", "Enter a user name.");
    if (!valid.match(login).hasMatch())
        return i18nc("@info", "Use small letters and digits only, and start with a letter.");
    static const QStringList reserved{QStringLiteral("root"), QStringLiteral("daemon"), QStringLiteral("bin"),
                                      QStringLiteral("sys"), QStringLiteral("nobody"), QStringLiteral("admin")};
    if (reserved.contains(login))
        return i18nc("@info", "This user name is kept for the system.");
    for (const QVariant &other : m_others)
        if (other.toMap().value(QStringLiteral("userName")).toString() == login)
            return i18nc("@info", "This user name is already taken.");
    return {};
}

QString AccountUsers::passwordProblem(const QString &password, const QString &confirm)
{
    if (password.isEmpty())
        return i18nc("@info", "Enter a password.");
    if (password != confirm)
        return i18nc("@info", "The two passwords are not the same.");
    return {};
}

QStringList AccountUsers::avatars() const
{
    QStringList found;
    const QStringList folders = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("raye-settings/avatars"),
                                                          QStandardPaths::LocateDirectory);
    for (const QString &folder : folders) {
        const QDir dir(folder);
        const QStringList files = dir.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
        for (const QString &file : files)
            found << dir.filePath(file);
    }
    return found;
}
