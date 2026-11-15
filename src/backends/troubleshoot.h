// SPDX-License-Identifier: GPL-3.0-or-later
// System > Troubleshoot: one troubleshooter at a time (Audio, Internet
// connections, Bluetooth, Printer, Display, Raye Update).
//
// The checks and the fixes live in /usr/libexec/raye-settings/troubleshoot.
// This class runs it and turns what it prints into plain words:
//
//   1. "check <name>" as the user: one step per check, as it runs;
//   2. if something can be fixed, the page asks, then fix() runs the
//      fixes the user may run as the user, and the ones that need an
//      administrator with pkexec, all at once (one password);
//   3. the checks run again, and each problem ends Fixed or Not fixed.
//
// What the helper prints, one line each (anything else is ignored):
//   begin <check>
//   check <check> ok | unknown
//   check <check> problem <remedy> user|admin|none
//   fixed <remedy> | failed <remedy>
// A remedy with "none" is only advice ("plug in your speakers").
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QProcess;

class Troubleshooter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // "audio", "network", "bluetooth", "printer", "display", "update"
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString title READ title NOTIFY nameChanged)
    // idle, checking, found (problems that can be fixed), fixing,
    // verifying, done, failed (the troubleshooter itself could not run)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    // [{ id, title, detail, status }]; status is running, ok, problem,
    // fixing, fixed, notFixed or unknown
    Q_PROPERTY(QVariantList steps READ steps NOTIFY stepsChanged)
    // what fix() will do, in words: [{ remedy, text, admin }]
    Q_PROPERTY(QVariantList remedies READ remedies NOTIFY stepsChanged)
    Q_PROPERTY(bool needsAdmin READ needsAdmin NOTIFY stepsChanged)
    Q_PROPERTY(QString resultTitle READ resultTitle NOTIFY stateChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY stateChanged)
    // ok, fixed, partly, notFixed (for the glyph of the result)
    Q_PROPERTY(QString resultStatus READ resultStatus NOTIFY stateChanged)

public:
    enum class Level { None, User, Admin };

    struct Line {
        enum Kind { Invalid, Begin, Check, Fixed, Failed };
        Kind kind = Invalid;
        QString id;        // the check, or the remedy for fixed/failed
        QString result;    // ok, unknown, problem
        QString remedy;    // for a problem, "-" when there is none
        Level level = Level::None;
    };

    struct Step {
        QString id;
        QString status;    // see `steps`
        QString remedy;    // empty when there is nothing to do
        Level level = Level::None;
        bool hadProblem = false;
        bool remedyFailed = false;
        bool denied = false;
    };

    explicit Troubleshooter(QObject *parent = nullptr);
    ~Troubleshooter() override;

    QString name() const { return m_name; }
    void setName(const QString &name);
    QString title() const;
    QString state() const { return m_state; }
    bool busy() const;
    QVariantList steps() const;
    QVariantList remedies() const;
    bool needsAdmin() const;
    QString resultTitle() const { return m_resultTitle; }
    QString resultText() const { return m_resultText; }
    QString resultStatus() const { return m_resultStatus; }

    Q_INVOKABLE void run();
    Q_INVOKABLE void fix();
    Q_INVOKABLE void cancel();

    // the troubleshooters, in the order of the page
    static QStringList names();
    // what a line of the helper means; Invalid for anything else
    static Line parseLine(const QString &line);
    // the words for a check, a remedy and an advice: empty when unknown
    static QString checkTitle(const QString &id);
    static QString checkDetail(const QString &id, const QString &status);
    static QString remedyText(const QString &remedy);
    static QString adviceText(const QString &checkId, const QString &remedy);
    static bool knowsCheck(const QString &id);
    static bool knowsRemedy(const QString &remedy);

    // the tests run fake programs
    void setHelper(const QString &program) { m_helper = program; }
    void setPkexec(const QString &program) { m_pkexec = program; }
    void setTimeouts(int checkMs, int adminMs);

Q_SIGNALS:
    void nameChanged();
    void stateChanged();
    void stepsChanged();

private:
    enum class Phase { None, Check, UserFix, AdminFix, Verify };

    void setState(const QString &state);
    void start(Phase phase, const QString &program, const QStringList &arguments);
    void readOutput();
    void handle(const Line &line);
    void processFinished(int exitCode, bool crashed);
    void finishCheck(bool helperFailed);
    void startAdminFixes();
    void startVerify();
    void finishVerify();
    void finish();
    Step *step(const QString &id);
    QStringList remediesAt(Level level) const;

    QString m_name;
    QString m_state = QStringLiteral("idle");
    QList<Step> m_steps;
    QString m_resultTitle;
    QString m_resultText;
    QString m_resultStatus;

    QString m_helper;
    QString m_pkexec = QStringLiteral("pkexec");
    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    Phase m_phase = Phase::None;
    QTimer m_timeout;
    int m_checkTimeout = 90 * 1000;
    // pkexec waits for the password: give the person time to type it
    int m_adminTimeout = 5 * 60 * 1000;
    bool m_parsedAny = false;
    QStringList m_fixedRemedies;
    QStringList m_failedRemedies;
    bool m_adminDenied = false;
};
