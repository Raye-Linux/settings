// SPDX-License-Identifier: GPL-3.0-or-later
// System › Recovery: restart to Windows, repair Raye, go back to a
// snapshot, reset the settings, advanced startup.
//
// The work is done by small programs next to Settings (data/libexec):
// repair, snapshot-restore and bootmenu run as root through pkexec,
// reset-settings as the user. This class starts them, reads the lines
// they print and turns them into what the page shows. Nothing here runs
// as root, and nothing destructive starts without a call from the page,
// which asks first.
#pragma once

#include <QDateTime>
#include <QLocale>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class Recovery : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // a Windows boot entry that Raye can restart into, once
    Q_PROPERTY(bool windowsAvailable READ windowsAvailable NOTIFY availabilityChanged)
    // the system is a btrfs subvolume with snapshots
    Q_PROPERTY(bool goBackAvailable READ goBackAvailable NOTIFY availabilityChanged)
    // UEFI, and the firmware can be asked to open its settings
    Q_PROPERTY(bool firmwareAvailable READ firmwareAvailable NOTIFY availabilityChanged)
    // the boot menu can be asked to show at the next start
    Q_PROPERTY(bool bootMenuAvailable READ bootMenuAvailable NOTIFY availabilityChanged)

    // idle, update, check, download, install, done, error
    Q_PROPERTY(QString repairState READ repairState NOTIFY repairChanged)
    Q_PROPERTY(int repairProgress READ repairProgress NOTIFY repairChanged)
    // packages found damaged, -1 until the check is over
    Q_PROPERTY(int repairFound READ repairFound NOTIFY repairChanged)
    // network, locked, apt, denied, failed (empty unless repairState is error)
    Q_PROPERTY(QString repairError READ repairError NOTIFY repairChanged)

    // idle, loading, ready, error, cancelled
    Q_PROPERTY(QString snapshotsState READ snapshotsState NOTIFY snapshotsChanged)
    // [{ number, title, when }], newest first
    Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY snapshotsChanged)
    // idle, working, error, cancelled; done restarts the computer
    Q_PROPERTY(QString goBackState READ goBackState NOTIFY goBackChanged)
    // idle, working, done, error, cancelled: a restore point made by hand
    Q_PROPERTY(QString createState READ createState NOTIFY createChanged)

    // none, reset, reset-keep-wallpaper, undo: planned for the next sign-in
    Q_PROPERTY(QString resetPending READ resetPending NOTIFY resetChanged)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY resetChanged)
    Q_PROPERTY(int undoDaysLeft READ undoDaysLeft NOTIFY resetChanged)
    // "Reset on 31 October" in words, empty without one
    Q_PROPERTY(QString undoDate READ undoDate NOTIFY resetChanged)

    // a restart or a sign-out is under way, or a helper is asked something
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit Recovery(QObject *parent = nullptr);
    ~Recovery() override;

    bool windowsAvailable() const { return m_windows; }
    bool goBackAvailable() const { return m_goBack; }
    bool firmwareAvailable() const { return m_firmware; }
    bool bootMenuAvailable() const { return m_bootMenu; }
    QString repairState() const { return m_repairState; }
    int repairProgress() const { return m_repairProgress; }
    int repairFound() const { return m_repairFound; }
    QString repairError() const { return m_repairError; }
    QString snapshotsState() const { return m_snapshotsState; }
    QVariantList snapshots() const { return m_snapshots; }
    QString goBackState() const { return m_goBackState; }
    QString createState() const { return m_createState; }
    QString resetPending() const { return m_resetPending; }
    bool undoAvailable() const { return m_undoDays > 0; }
    int undoDaysLeft() const { return m_undoDays; }
    QString undoDate() const;
    bool busy() const { return m_busy; }

    // Look again at what this computer offers (the page calls it when
    // it opens).
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void restartToWindows();
    Q_INVOKABLE void startRepair();
    Q_INVOKABLE void resetRepair();
    // the repair put files back that running programs still hold
    Q_INVOKABLE void restartAfterRepair();
    Q_INVOKABLE void loadSnapshots();
    Q_INVOKABLE void goBack(int number);
    Q_INVOKABLE void resetGoBack();
    Q_INVOKABLE void createRestorePoint(const QString &name);
    Q_INVOKABLE void resetCreate();
    Q_INVOKABLE void resetSettings(bool keepWallpaper);
    Q_INVOKABLE void undoReset();
    Q_INVOKABLE void cancelReset();
    // end the session now, so a planned reset happens
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void restartToFirmware();
    Q_INVOKABLE void restartToBootMenu();

    // The parsing, public for the tests.
    struct RepairLine {
        QString key;   // stage, progress, package, found, done, error
        QString value;
    };
    static RepairLine parseRepairLine(const QString &line);
    // snapper's JSON list -> the restore points to offer, newest first
    static QVariantList parseSnapshots(const QByteArray &json, const QDateTime &now, const QLocale &locale);
    // "today, 14:05", "yesterday, 14:05", "28 October, 14:05"
    static QString whenInWords(const QDateTime &when, const QDateTime &now, const QLocale &locale);
    // the system is a subvolume of a btrfs volume, with snapper set up
    static bool goBackSupported(const QString &root);
    // `reset-settings status` -> pending, days left, date of the reset
    struct ResetStatus {
        QString pending = QStringLiteral("none");
        int daysLeft = 0;
        QDateTime when;
    };
    static ResetStatus parseResetStatus(const QString &output);

    // Where the programs are: the helpers' folder, raye-boot-windows,
    // and the folder the probes read / from. The tests point these at
    // fakes; the defaults are the installed paths.
    void setHelperDir(const QString &dir) { m_helperDir = dir; }
    void setBootWindowsProgram(const QString &path) { m_bootWindows = path; }
    void setRoot(const QString &root) { m_root = root; }
    // What ends the session after a reset is planned (Plasma's logout
    // by default); the tests count the calls instead.
    void setLogout(std::function<bool()> logout) { m_logout = std::move(logout); }
    // Ask logind about the firmware (off in the tests).
    void setAskLogind(bool ask) { m_askLogind = ask; }

    // One line of the repair helper, as it comes; public for the tests.
    void handleRepairLine(const QString &line);

Q_SIGNALS:
    void availabilityChanged();
    void repairChanged();
    void snapshotsChanged();
    void goBackChanged();
    void createChanged();
    void resetChanged();
    void busyChanged();
    // something the page has to say: "restart-failed", "logout-failed",
    // "boot-menu-unavailable", "reset-failed", "denied"
    void failed(const QString &what);
    // the computer is restarting or the session is ending
    void leaving();

private:
    using Finished = std::function<void(int exitCode, const QByteArray &out)>;
    QProcess *run(const QString &program, const QStringList &arguments, Finished done);
    QProcess *runAsAdmin(const QString &helper, const QStringList &arguments, Finished done);
    QString helper(const QString &name) const;
    void setBusy(bool busy);
    void reboot(const QStringList &extra = {});
    void refreshReset();
    void setRepair(const QString &state, const QString &error = QString());

    QString m_helperDir;
    QString m_bootWindows;
    QString m_root = QStringLiteral("/");
    std::function<bool()> m_logout;
    bool m_askLogind = true;

    bool m_windows = false;
    bool m_goBack = false;
    bool m_firmware = false;
    bool m_bootMenu = false;

    QString m_repairState = QStringLiteral("idle");
    int m_repairProgress = 0;
    int m_repairFound = -1;
    QString m_repairError;
    QString m_repairBuffer;
    QPointer<QProcess> m_repairProcess;

    QString m_snapshotsState = QStringLiteral("idle");
    QVariantList m_snapshots;
    QString m_goBackState = QStringLiteral("idle");
    QString m_createState = QStringLiteral("idle");

    QString m_resetPending = QStringLiteral("none");
    int m_undoDays = 0;
    QDateTime m_undoWhen;

    bool m_busy = false;
};
