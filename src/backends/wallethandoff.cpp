// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallethandoff.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageAuthenticationCode>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

namespace
{
constexpr int keySize = 56;
constexpr int saltSize = 56;
constexpr int rounds = 50000;
const QString walletName = QStringLiteral("kdewallet");
const QString keeperUnit = QStringLiteral("raye-wallet-handoff");

void wipe(QByteArray &b)
{
    b.fill('\0');
    b.clear();
}

// one line from the socket, at most 256 bytes, within the deadline
QByteArray readLine(QLocalSocket &s, int ms)
{
    QDeadlineTimer deadline(ms);
    while (!s.canReadLine() && s.bytesAvailable() < 256 && !deadline.hasExpired()) {
        if (!s.waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime()))))
            break;
    }
    return s.readLine(257).trimmed();
}
}

QString WalletHandoff::walletFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kwalletd");
}

bool WalletHandoff::loginWalletExists(const QString &folder)
{
    return QFileInfo(folder + QLatin1Char('/') + walletName + QStringLiteral(".kwl")).isFile()
        && QFileInfo(folder + QLatin1Char('/') + walletName + QStringLiteral(".salt")).isFile();
}

QByteArray WalletHandoff::readSalt(const QString &folder)
{
    QFile f(folder + QLatin1Char('/') + walletName + QStringLiteral(".salt"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray salt = f.read(saltSize + 1);
    return salt.size() == saltSize ? salt : QByteArray();
}

QByteArray WalletHandoff::deriveKey(const QByteArray &password, const QByteArray &salt)
{
    // PBKDF2 (RFC 8018) with HMAC-SHA512: 56 bytes fit in the first block
    QMessageAuthenticationCode mac(QCryptographicHash::Sha512, password);
    QByteArray block = salt;
    block.append(char(0)).append(char(0)).append(char(0)).append(char(1));
    mac.addData(block);
    QByteArray u = mac.result();
    QByteArray t = u;
    for (int i = 1; i < rounds; ++i) {
        mac.reset();
        mac.addData(u);
        u = mac.result();
        for (int j = 0; j < t.size(); ++j)
            t[j] = char(t[j] ^ u[j]);
    }
    wipe(u);
    return t.left(keySize);
}

QString WalletHandoff::socketPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/raye-wallet-handoff");
}

void WalletHandoff::checkPassword(const QString &login, const QString &password, std::function<void(bool)> done,
                                  const QString &chkpwd)
{
    if (login.isEmpty() || password.isEmpty()) {
        done(false);
        return;
    }
    auto *p = new QProcess;
    QObject::connect(p, &QProcess::finished, p, [p, done](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        done(status == QProcess::NormalExit && code == 0);
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        done(false);
    });
    // unix_chkpwd reads the password on its standard input, up to a NUL
    p->start(chkpwd, {login, QStringLiteral("nullok")});
    QByteArray secret = password.toUtf8();
    secret.append('\0');
    p->write(secret);
    wipe(secret);
    p->closeWriteChannel();
}

void WalletHandoff::handOver(const QByteArray &key, std::function<void(bool)> done)
{
    // a keeper from a switch taken back is stopped first: one key at a time
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("stop"), keeperUnit});
    const int started = QProcess::execute(QStringLiteral("systemd-run"),
        {QStringLiteral("--user"), QStringLiteral("--quiet"), QStringLiteral("--collect"),
         QStringLiteral("--unit=") + keeperUnit, QStringLiteral("--property=RuntimeMaxSec=200"),
         QCoreApplication::applicationFilePath(), QStringLiteral("--wallet-keep")});
    if (started != 0) {
        done(false);
        return;
    }
    QLocalSocket s;
    QDeadlineTimer deadline(5000);
    while (!deadline.hasExpired()) {
        s.connectToServer(socketPath());
        if (s.waitForConnected(200))
            break;
        QThread::msleep(100);
    }
    if (s.state() != QLocalSocket::ConnectedState) {
        done(false);
        return;
    }
    QByteArray line = "put " + key.toBase64() + '\n';
    s.write(line);
    wipe(line);
    s.waitForBytesWritten(2000);
    const QByteArray answer = readLine(s, 2000);
    s.disconnectFromServer();
    done(answer == "ok");
}

int WalletHandoff::keep(int timeoutMs)
{
    const QString path = socketPath();
    if (path.startsWith(QLatin1Char('/')) == false)
        return 1;
    QLocalServer::removeServer(path);
    QLocalServer server;
    // the runtime folder is the person's own (0700); the socket too
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(path))
        return 1;
    QByteArray key;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, &key] {
        while (QLocalSocket *c = server.nextPendingConnection()) {
            const QByteArray line = readLine(*c, 2000);
            if (line.startsWith("put ") && key.isEmpty()) {
                QByteArray b64 = line.mid(4);
                key = QByteArray::fromBase64(b64);
                wipe(b64);
                c->write(key.size() == keySize ? "ok\n" : "no\n");
                if (key.size() != keySize)
                    wipe(key);
            } else if (line == "get" && !key.isEmpty()) {
                QByteArray out = key.toBase64() + '\n';
                c->write(out);
                wipe(out);
                c->waitForBytesWritten(2000);
                wipe(key);
                c->disconnectFromServer();
                QCoreApplication::quit();
                return;
            } else {
                c->write("no\n");
            }
            c->waitForBytesWritten(2000);
            c->disconnectFromServer();
            c->deleteLater();
        }
    });
    QTimer::singleShot(timeoutMs, qApp, &QCoreApplication::quit);
    QCoreApplication::exec();
    wipe(key);
    server.close();
    QLocalServer::removeServer(path);
    return 0;
}

int WalletHandoff::open()
{
    if (!QFileInfo::exists(socketPath()))
        return 0;
    QLocalSocket s;
    s.connectToServer(socketPath());
    if (!s.waitForConnected(1000))
        return 0;
    s.write("get\n");
    s.waitForBytesWritten(1000);
    QByteArray line = readLine(s, 3000);
    QByteArray key = QByteArray::fromBase64(line);
    wipe(line);
    s.disconnectFromServer();
    if (key.size() != keySize) {
        wipe(key);
        return 1;
    }
    // KWallet 6: ksecretd keeps the wallets (org.kde.KWallet on /ksecretd,
    // started as org.kde.secretservicecompat); the kwalletd6 compatibility
    // service on /modules/kwalletd6 is tried after it, for older KWallets.
    // pamOpen answers nothing: isOpen afterwards also makes sure it went out.
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString iface = QStringLiteral("org.kde.KWallet");
    const QList<QPair<QString, QString>> daemons{
        {QStringLiteral("org.kde.secretservicecompat"), QStringLiteral("/ksecretd")},
        {QStringLiteral("org.kde.kwalletd6"), QStringLiteral("/modules/kwalletd6")},
    };
    for (const auto &[service, objectPath] : daemons) {
        QDBusMessage msg = QDBusMessage::createMethodCall(service, objectPath, iface, QStringLiteral("pamOpen"));
        msg << walletName << key << 0;
        bus.send(msg);
        QDBusMessage isOpen = QDBusMessage::createMethodCall(service, objectPath, iface, QStringLiteral("isOpen"));
        isOpen << walletName;
        for (int i = 0; i < 12; ++i) {
            const QDBusReply<bool> reply = bus.call(isOpen, QDBus::Block, 5000);
            if (!reply.isValid())
                break;
            if (reply.value()) {
                wipe(key);
                return 0;
            }
            QThread::msleep(250);
        }
    }
    wipe(key);
    qWarning("raye-settings: the saved passwords did not open");
    return 1;
}
