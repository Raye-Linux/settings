// SPDX-License-Identifier: GPL-3.0-or-later
// Proxy: the three ways Windows offers (detect it, a setup script, an
// address), written where apps look for them: KDE's kioslaverc for
// KDE apps, and environment.d for everything else at the next sign-in.
//
// Only one of the three applies at a time; turning one on turns the
// others off.
#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

struct ProxyConfig {
    bool autoDetect = false;
    bool useScript = false;
    QString scriptUrl;
    bool useManual = false;
    QString host;
    int port = 0;
    // "*.local; intranet" as typed, separated by ';'
    QString exceptions;
    // do not use the proxy for local addresses
    bool bypassLocal = true;

    bool operator==(const ProxyConfig &other) const = default;
};

namespace ProxyText
{
// [Proxy Settings] of kioslaverc <-> the page
ProxyConfig fromKio(const QMap<QString, QString> &entries);
QMap<QString, QString> toKio(const ProxyConfig &config);
// the lines of ~/.config/environment.d/90-raye-proxy.conf, "" when no
// address is set (a script or detection cannot be said there)
QString environment(const ProxyConfig &config);
// "" or the field that is wrong: "script", "host", "port", "exceptions"
QString check(const ProxyConfig &config);
QString errorText(const QString &field);
}

class ProxySettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool autoDetect READ autoDetect NOTIFY changed)
    Q_PROPERTY(bool useScript READ useScript NOTIFY changed)
    Q_PROPERTY(QString scriptUrl READ scriptUrl NOTIFY changed)
    Q_PROPERTY(bool useManual READ useManual NOTIFY changed)
    Q_PROPERTY(QString host READ host NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(QString exceptions READ exceptions NOTIFY changed)
    Q_PROPERTY(bool bypassLocal READ bypassLocal NOTIFY changed)

public:
    explicit ProxySettings(QObject *parent = nullptr);

    bool autoDetect() const { return m_config.autoDetect; }
    bool useScript() const { return m_config.useScript; }
    QString scriptUrl() const { return m_config.scriptUrl; }
    bool useManual() const { return m_config.useManual; }
    QString host() const { return m_config.host; }
    int port() const { return m_config.port; }
    QString exceptions() const { return m_config.exceptions; }
    bool bypassLocal() const { return m_config.bypassLocal; }
    ProxyConfig config() const { return m_config; }

    Q_INVOKABLE void setAutoDetect(bool on);
    // "" or the sentence that says what is wrong
    Q_INVOKABLE QString setScript(bool on, const QString &url);
    Q_INVOKABLE QString setManual(bool on, const QString &host, int port, const QString &exceptions,
                                  bool bypassLocal);

    // where the two files are; the tests point them into a folder
    void setFiles(const QString &kioslaverc, const QString &environmentFile);

Q_SIGNALS:
    void changed();

private:
    void load();
    QString apply(const ProxyConfig &config);

    QString m_kioFile;
    QString m_envFile;
    ProxyConfig m_config;
};
