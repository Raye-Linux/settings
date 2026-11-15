// SPDX-License-Identifier: GPL-3.0-or-later
#include "proxy.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {

// what "Don't use the proxy server for local addresses" adds
const QStringList localAddresses = {QStringLiteral("localhost"), QStringLiteral("127.0.0.0/8"), QStringLiteral("::1")};

// KIO's ProxyType values
enum KioType { NoProxy = 0, Manual = 1, Script = 2, Detect = 3, Environment = 4 };

bool hasControl(const QString &text)
{
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return true;
    }
    return false;
}

bool isHost(const QString &host)
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$"));
    if (name.match(host).hasMatch())
        return true;
    QHostAddress address;
    return host.contains(QLatin1Char(':')) && address.setAddress(host) && !host.contains(QLatin1Char('%'));
}

QStringList exceptionList(const QString &text)
{
    QStringList list;
    const auto parts = text.split(QRegularExpression(QStringLiteral("[;,]")));
    for (const QString &part : parts) {
        if (!part.trimmed().isEmpty())
            list.append(part.trimmed());
    }
    return list;
}

QString hostForUrl(const QString &host)
{
    return host.contains(QLatin1Char(':')) ? QLatin1Char('[') + host + QLatin1Char(']') : host;
}

// "http://host port", "http://host:port", "host:port" -> host, port
void readAddress(const QString &entry, QString *host, int *port)
{
    QString text = entry.trimmed();
    text.remove(QRegularExpression(QStringLiteral("^[a-z]+://")));
    QString portText;
    const int space = text.lastIndexOf(QLatin1Char(' '));
    if (space > 0) {
        portText = text.mid(space + 1);
        text = text.left(space);
    } else if (text.startsWith(QLatin1Char('['))) {
        const int close = text.indexOf(QLatin1Char(']'));
        if (close > 0) {
            portText = text.mid(close + 2);
            text = text.mid(1, close - 1);
        }
    } else if (text.count(QLatin1Char(':')) == 1) {
        portText = text.section(QLatin1Char(':'), 1);
        text = text.section(QLatin1Char(':'), 0, 0);
    }
    if (text.endsWith(QLatin1Char('/')))
        text.chop(1);
    *host = text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']')) ? text.mid(1, text.size() - 2) : text;
    *port = portText.trimmed().toInt();
}

} // namespace

namespace ProxyText
{

ProxyConfig fromKio(const QMap<QString, QString> &entries)
{
    ProxyConfig config;
    const int type = entries.value(QStringLiteral("ProxyType")).toInt();
    config.autoDetect = type == Detect;
    config.useScript = type == Script;
    config.useManual = type == Manual;
    config.scriptUrl = entries.value(QStringLiteral("Proxy Config Script"));
    readAddress(entries.value(QStringLiteral("httpProxy")), &config.host, &config.port);
    QStringList exceptions = exceptionList(entries.value(QStringLiteral("NoProxyFor")));
    config.bypassLocal = exceptions.contains(QStringLiteral("localhost"));
    for (const QString &local : localAddresses)
        exceptions.removeAll(local);
    config.exceptions = exceptions.join(QStringLiteral("; "));
    return config;
}

QMap<QString, QString> toKio(const ProxyConfig &config)
{
    const int type = config.useManual ? Manual : config.useScript ? Script : config.autoDetect ? Detect : NoProxy;
    QMap<QString, QString> entries;
    entries.insert(QStringLiteral("ProxyType"), QString::number(type));
    entries.insert(QStringLiteral("Proxy Config Script"), config.scriptUrl);
    const QString address = config.host.isEmpty()
        ? QString()
        : QStringLiteral("http://") + hostForUrl(config.host) + QLatin1Char(' ') + QString::number(config.port);
    for (const char *key : {"httpProxy", "httpsProxy", "ftpProxy"})
        entries.insert(QString::fromLatin1(key), address);
    QStringList exceptions = exceptionList(config.exceptions);
    if (config.bypassLocal)
        exceptions += localAddresses;
    entries.insert(QStringLiteral("NoProxyFor"), exceptions.join(QLatin1Char(',')));
    entries.insert(QStringLiteral("ReversedException"), QStringLiteral("false"));
    return entries;
}

QString environment(const ProxyConfig &config)
{
    if (!config.useManual || config.host.isEmpty())
        return {};
    const QString address = QStringLiteral("http://") + hostForUrl(config.host) + QLatin1Char(':') + QString::number(config.port);
    QString text = QStringLiteral("# Written by Raye Settings (Network & internet > Proxy)\n");
    for (const char *name : {"http_proxy", "https_proxy", "ftp_proxy"}) {
        text += QString::fromLatin1(name) + QLatin1Char('=') + address + QLatin1Char('\n');
        text += QString::fromLatin1(name).toUpper() + QLatin1Char('=') + address + QLatin1Char('\n');
    }
    QStringList exceptions = exceptionList(config.exceptions);
    if (config.bypassLocal)
        exceptions += localAddresses;
    if (!exceptions.isEmpty()) {
        const QString list = exceptions.join(QLatin1Char(','));
        text += QStringLiteral("no_proxy=") + list + QLatin1Char('\n');
        text += QStringLiteral("NO_PROXY=") + list + QLatin1Char('\n');
    }
    return text;
}

QString check(const ProxyConfig &config)
{
    if (config.useScript) {
        const QUrl url(config.scriptUrl, QUrl::StrictMode);
        const QString scheme = url.scheme();
        if (hasControl(config.scriptUrl) || config.scriptUrl.contains(QLatin1Char(' ')) || !url.isValid()
            || (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("file"))
            || (scheme != QLatin1String("file") && url.host().isEmpty()))
            return QStringLiteral("script");
    }
    if (config.useManual) {
        if (!isHost(config.host))
            return QStringLiteral("host");
        if (config.port < 1 || config.port > 65535)
            return QStringLiteral("port");
        static const QRegularExpression entry(QStringLiteral("^[A-Za-z0-9.*_:/\\[\\]-]+$"));
        if (hasControl(config.exceptions))
            return QStringLiteral("exceptions");
        for (const QString &exception : exceptionList(config.exceptions)) {
            if (!entry.match(exception).hasMatch())
                return QStringLiteral("exceptions");
        }
    }
    return {};
}

QString errorText(const QString &field)
{
    if (field == QLatin1String("script"))
        return i18nc("@info", "Enter the address of the setup script, like http://example.com/proxy.pac.");
    if (field == QLatin1String("host"))
        return i18nc("@info", "Enter the address of the proxy server, like proxy.example.com.");
    if (field == QLatin1String("port"))
        return i18nc("@info", "Enter a port from 1 to 65535.");
    if (field == QLatin1String("exceptions"))
        return i18nc("@info", "Separate the addresses with semicolons, like *.example.com; intranet.");
    return {};
}

} // namespace ProxyText

ProxySettings::ProxySettings(QObject *parent)
    : QObject(parent)
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    m_kioFile = config + QStringLiteral("/kioslaverc");
    m_envFile = config + QStringLiteral("/environment.d/90-raye-proxy.conf");
    load();
}

void ProxySettings::setFiles(const QString &kioslaverc, const QString &environmentFile)
{
    m_kioFile = kioslaverc;
    m_envFile = environmentFile;
    load();
}

void ProxySettings::load()
{
    KConfig file(m_kioFile, KConfig::SimpleConfig);
    const KConfigGroup group = file.group(QStringLiteral("Proxy Settings"));
    QMap<QString, QString> entries;
    const auto keys = group.keyList();
    for (const QString &key : keys)
        entries.insert(key, group.readEntry(key));
    m_config = ProxyText::fromKio(entries);
    Q_EMIT changed();
}

QString ProxySettings::apply(const ProxyConfig &config)
{
    const QString problem = ProxyText::check(config);
    if (!problem.isEmpty())
        return ProxyText::errorText(problem);

    KConfig file(m_kioFile, KConfig::SimpleConfig);
    KConfigGroup group = file.group(QStringLiteral("Proxy Settings"));
    const auto entries = ProxyText::toKio(config);
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        group.writeEntry(it.key(), it.value());
    if (!file.sync())
        return i18nc("@info", "Raye couldn't save the proxy settings.");

    const QString environment = ProxyText::environment(config);
    if (environment.isEmpty()) {
        QFile::remove(m_envFile);
    } else {
        QDir().mkpath(QFileInfo(m_envFile).absolutePath());
        QSaveFile save(m_envFile);
        if (!save.open(QIODevice::WriteOnly) || save.write(environment.toUtf8()) < 0 || !save.commit())
            return i18nc("@info", "Raye couldn't save the proxy settings.");
    }

    // KDE apps that are running read their proxy again
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KIO/Scheduler"), QStringLiteral("org.kde.KIO.Scheduler"),
                                                      QStringLiteral("reparseSlaveConfiguration"));
    message << QString();
    QDBusConnection::sessionBus().send(message);

    m_config = config;
    Q_EMIT changed();
    return {};
}

void ProxySettings::setAutoDetect(bool on)
{
    ProxyConfig config = m_config;
    config.autoDetect = on;
    if (on) {
        config.useScript = false;
        config.useManual = false;
    }
    apply(config);
}

QString ProxySettings::setScript(bool on, const QString &url)
{
    ProxyConfig config = m_config;
    config.scriptUrl = url.trimmed();
    config.useScript = on;
    if (on) {
        config.autoDetect = false;
        config.useManual = false;
    } else {
        // turning it off never fails over a half-typed address
        ProxyConfig probe;
        probe.useScript = true;
        probe.scriptUrl = config.scriptUrl;
        if (!ProxyText::check(probe).isEmpty())
            config.scriptUrl = m_config.scriptUrl;
    }
    return apply(config);
}

QString ProxySettings::setManual(bool on, const QString &host, int port, const QString &exceptions, bool bypassLocal)
{
    ProxyConfig config = m_config;
    config.host = host.trimmed();
    config.port = port;
    config.exceptions = exceptions.trimmed();
    config.bypassLocal = bypassLocal;
    config.useManual = on;
    if (on) {
        config.autoDetect = false;
        config.useScript = false;
    } else {
        ProxyConfig probe = config;
        probe.useManual = true;
        if (!ProxyText::check(probe).isEmpty()) {
            config.host = m_config.host;
            config.port = m_config.port;
            config.exceptions = m_config.exceptions;
        }
    }
    return apply(config);
}
