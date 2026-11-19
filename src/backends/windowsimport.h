// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts > Files from Windows: brings Documents, Pictures, Music,
// Videos, Downloads and the Desktop of a Windows account over to this
// one, and the Windows wallpaper and browser bookmarks if asked.
//
// The work is done by two helpers. windows-import (as the person) lists
// the drives, finds the accounts and their folders on one, and copies,
// reading every file back. windows-import-mount (as root, through pkexec)
// mounts a Windows volume read-only and keeps it mounted while its
// standard input is open: this class holds that pipe, so the volume is
// let go of when the copy ends, when the page goes back, or when Settings
// quits for any reason.
//
// A copy keeps its plan and its progress in
// ~/.local/state/raye-settings/windows-import; a copy that stopped (Stop,
// a closed window, a power cut) can be picked up from there.
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QEventLoop;
class QProcess;

namespace WindowsImportData
{
// The drives of `windows-import volumes`: [{ device, fstype, label, uuid,
// size, external, mountPoint, state, windows }]
QVariantList parseVolumes(const QByteArray &json);
// The accounts of `windows-import scan`, every folder ticked, the
// wallpaper and the bookmarks too when there are some.
QVariantMap parseScan(const QByteArray &json);
// One line of windows-import-mount: ("mounted", path), ("refused",
// reason), ("warning", what); empty kind for anything else.
QPair<QString, QString> parseHoldLine(const QByteArray &line);
// What is ticked: { bytes, files, folders }
QVariantMap selection(const QVariantList &accounts);
// The plan the copy reads: only what is ticked, each folder to the
// receiving folder of its kind (dest: kind -> path).
QVariantMap plan(const QString &root, const QVariantList &accounts, const QVariantMap &dest,
                 const QString &lang, const QVariantMap &source);
// Seconds left at this pace; -1 while it cannot be said yet.
int secondsLeft(qint64 done, qint64 total, qint64 elapsedMs);
// The first sign-in asks only about a Windows next to Raye, not locked.
bool offerable(const QVariantList &volumes);
} // namespace WindowsImportData

class WindowsImport : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList volumes READ volumes NOTIFY volumesChanged)
    Q_PROPERTY(bool listing READ listing NOTIFY volumesChanged)
    // pick, opening, scanning, choose, copying, done, stopped, refused
    Q_PROPERTY(QString stage READ stage NOTIFY stageChanged)
    // bitlocker, hibernated, busy, not-windows, unreadable, denied,
    // nothing, room, failed
    Q_PROPERTY(QString refusal READ refusal NOTIFY stageChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stageChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY stageChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(qint64 selectedBytes READ selectedBytes NOTIFY accountsChanged)
    Q_PROPERTY(int selectedFiles READ selectedFiles NOTIFY accountsChanged)
    Q_PROPERTY(qint64 freeBytes READ freeBytes NOTIFY accountsChanged)
    Q_PROPERTY(qint64 neededBytes READ neededBytes NOTIFY stageChanged)
    Q_PROPERTY(qint64 doneBytes READ doneBytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)
    Q_PROPERTY(int doneFiles READ doneFiles NOTIFY progressChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY progressChanged)
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY progressChanged)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY progressChanged)
    // { files, bytes, already, links, windows, special, failed, examples,
    //   wallpaper, bookmarks, bookmarksFailed, folders }
    Q_PROPERTY(QVariantMap report READ report NOTIFY stageChanged)
    Q_PROPERTY(bool canResume READ canResume NOTIFY stageChanged)

public:
    explicit WindowsImport(QObject *parent = nullptr);
    ~WindowsImport() override;

    QVariantList volumes() const { return m_volumes; }
    bool listing() const { return m_listing; }
    QString stage() const { return m_stage; }
    QString refusal() const { return m_refusal; }
    bool dirty() const { return m_dirty; }
    QString sourceName() const { return m_sourceName; }
    QVariantList accounts() const { return m_accounts; }
    qint64 selectedBytes() const;
    int selectedFiles() const;
    qint64 freeBytes() const;
    qint64 neededBytes() const { return m_needed; }
    qint64 doneBytes() const { return m_doneBytes; }
    qint64 totalBytes() const { return m_totalBytes; }
    int doneFiles() const { return m_doneFiles; }
    int totalFiles() const { return m_totalFiles; }
    QString currentFolder() const { return m_currentFolder; }
    int secondsLeft() const { return m_secondsLeft; }
    QVariantMap report() const { return m_report; }
    bool canResume() const { return m_canResume; }

    Q_INVOKABLE void refresh();
    // a drive of volumes, by device
    Q_INVOKABLE void openVolume(const QString &device);
    // a folder the person picked: a drive, a Users folder or one account's
    Q_INVOKABLE void openFolder(const QUrl &folder);
    Q_INVOKABLE void setFolderChecked(int account, int folder, bool checked);
    Q_INVOKABLE void setWallpaperChecked(int account, bool checked);
    Q_INVOKABLE void setBookmarksChecked(int account, bool checked);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // picks up the copy that stopped, opening its drive again when needed
    Q_INVOKABLE void resume();
    // back to the list of drives; the Windows drive is let go of
    Q_INVOKABLE void back();
    Q_INVOKABLE void showInFiles(const QString &path);
    Q_INVOKABLE QString formatSize(qint64 bytes) const;

    // For the tests.
    void setHelper(const QString &path) { m_helper = path; }
    void setMountHelper(const QString &path) { m_mountHelper = path; }
    void setPkexec(const QString &path) { m_pkexec = path; }
    void setStateHome(const QString &path);
    void setDestinations(const QVariantMap &dest) { m_dest = dest; }
    bool waitFor(const std::function<bool()> &done, int msecs);

    // --offer-windows-import: once per account, a notification after the
    // first sign-in when Windows is on this PC. Returns the exit status.
    static int offerAtSignIn();

Q_SIGNALS:
    void volumesChanged();
    void stageChanged();
    void accountsChanged();
    void progressChanged();

private:
    void setStage(const QString &stage, const QString &refusal = {});
    void scan(const QString &root);
    void hold(const QString &device, std::function<void(const QString &)> mounted);
    void release();
    void runCopy(const QString &planFile);
    void copyLine(const QByteArray &line);
    void readState();
    QString stateDir() const;
    QVariantMap destinations() const;
    QStringList helperEnvironment() const;

    QVariantList m_volumes;
    bool m_listing = false;
    QString m_stage = QStringLiteral("pick");
    QString m_refusal;
    bool m_dirty = false;
    QString m_sourceName;
    QVariantMap m_source; // { device, uuid, label, mountPoint, folder }
    QString m_root;
    QVariantList m_accounts;
    qint64 m_needed = 0;
    qint64 m_doneBytes = 0;
    qint64 m_totalBytes = 0;
    int m_doneFiles = 0;
    int m_totalFiles = 0;
    QString m_currentFolder;
    int m_secondsLeft = -1;
    qint64 m_startedAt = 0;
    qint64 m_resumedBytes = 0;
    QVariantMap m_report;
    bool m_canResume = false;

    QString m_helper;
    QString m_mountHelper;
    QString m_pkexec = QStringLiteral("pkexec");
    QString m_stateHome;
    QVariantMap m_dest;

    QProcess *m_list = nullptr;
    QProcess *m_scan = nullptr;
    QProcess *m_hold = nullptr;
    QList<QProcess *> m_released;
    QProcess *m_copy = nullptr;
    QByteArray m_copyBuffer;
};

// Waits for the answer to the first sign-in notification.
class WindowsImportOfferListener : public QObject
{
    Q_OBJECT
public:
    WindowsImportOfferListener(uint id, QEventLoop *loop);
    bool opened = false;

public Q_SLOTS:
    void actionInvoked(uint id, const QString &action);
    void closed(uint id, uint reason);

private:
    uint m_id;
    QEventLoop *m_loop;
};
