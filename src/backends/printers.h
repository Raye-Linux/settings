// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Printers & scanners. The print service answers through its
// own small programs (lpstat, lpoptions, lp, lpadmin), run in the C locale
// so what they print can be read here. A printer found on the network
// needs no setting up: CUPS lists it and prints to it as it is.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class PrinterList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the print service answers
    Q_PROPERTY(bool serviceRunning READ serviceRunning NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    // { name, state ("idle", "printing", "stopped"), isDefault, network }
    Q_PROPERTY(QVariantList printers READ printers NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit PrinterList(QObject *parent = nullptr);
    // for the tests: what starts the service
    PrinterList(const QStringList &serviceHelper, QObject *parent = nullptr);

    bool serviceRunning() const { return m_serviceRunning; }
    bool loaded() const { return m_loaded; }
    QVariantList printers() const { return m_printers; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void turnOnService();
    Q_INVOKABLE void setDefault(const QString &name);
    Q_INVOKABLE void printTestPage(const QString &name);
    Q_INVOKABLE void openQueue(const QString &name);
    Q_INVOKABLE void remove(const QString &name);
    // a printer on the network that speaks IPP: "ipp://printer.local/ipp/print"
    Q_INVOKABLE void add(const QString &name, const QString &address);
    Q_INVOKABLE void clearError();

    // lpstat -p: "printer NAME is idle.  enabled since ...",
    // "printer NAME now printing NAME-3.  enabled since ...",
    // "printer NAME disabled since ..."; a map of name to state
    static QVariantMap parseStates(const QString &output);
    // lpstat -e: one name a line, permanent and found ones alike
    static QStringList parseDestinations(const QString &output);
    // lpstat -d: "system default destination: NAME"
    static QString parseDefault(const QString &output);
    Q_INVOKABLE static QString nameProblem(const QString &name);
    Q_INVOKABLE static QString addressProblem(const QString &address);
    // "printer.local" or a pasted URL, as a queue name
    Q_INVOKABLE static QString suggestName(const QString &address);

Q_SIGNALS:
    void changed();
    void testPageSent();

private:
    using Done = std::function<void(int code, const QString &output)>;
    void run(const QString &program, const QStringList &arguments, Done done);
    void action(const QString &program, const QStringList &arguments, std::function<void()> then = {});

    QStringList m_helper;
    bool m_serviceRunning = false;
    bool m_loaded = false;
    QVariantList m_printers;
    bool m_busy = false;
    QString m_error;
};
