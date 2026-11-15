// SPDX-License-Identifier: GPL-3.0-or-later
#include "indexing.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KRecentDocument>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
// --- file index -------------------------------------------------------
const QString indexConfig = QStringLiteral("baloofilerc");
const QString indexEnabledGroup = QStringLiteral("Basic Settings");
const QString indexEnabledKey = QStringLiteral("Indexing-Enabled");
const QString indexFoldersGroup = QStringLiteral("General");
const QString excludeKey = QStringLiteral("exclude folders");
const QString indexModule = QStringLiteral("kcm_baloofile");

// --- activity history -----------------------------------------------
const QString activityConfig = QStringLiteral("kactivitymanagerd-pluginsrc");
const QString scoringGroup = QStringLiteral("Plugin-org.kde.ActivityManager.Resources.Scoring");
const QString rememberKey = QStringLiteral("what-to-remember");
// what-to-remember: every app, only some, none
constexpr int rememberAll = 0;
constexpr int rememberNone = 2;

// --- usage statistics -----------------------------------------------
const QString feedbackConfig = QStringLiteral("PlasmaUserFeedback");

KSharedConfig::Ptr config(const QString &name)
{
    auto file = KSharedConfig::openConfig(name, KConfig::NoGlobals);
    // another process may have changed it since it was opened
    file->reparseConfiguration();
    return file;
}

QString cleanFolder(const QString &path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        return {};
    return QDir::cleanPath(path);
}
} // namespace

// --- FileIndexing ------------------------------------------------------

FileIndexing::FileIndexing(QObject *parent)
    : QObject(parent)
{
    const QString control = QStandardPaths::findExecutable(QStringLiteral("balooctl6"));
    if (!control.isEmpty())
        m_control = {control};
}

bool FileIndexing::enabled() const
{
    return config(indexConfig)->group(indexEnabledGroup).readEntry(indexEnabledKey, true);
}

void FileIndexing::setEnabled(bool on)
{
    if (on == enabled())
        return;
    auto file = config(indexConfig);
    file->group(indexEnabledGroup).writeEntry(indexEnabledKey, on, KConfig::Notify);
    file->sync();
    // starts or stops the indexer; it writes the same setting
    tellIndexer({on ? QStringLiteral("enable") : QStringLiteral("disable")});
    Q_EMIT changed();
}

QStringList FileIndexing::excludedFolders() const
{
    QStringList folders = config(indexConfig)->group(indexFoldersGroup).readPathEntry(excludeKey, QStringList());
    folders.removeAll(QString());
    return folders;
}

bool FileIndexing::addExcludedFolder(const QString &path)
{
    const QString folder = cleanFolder(path);
    if (folder.isEmpty())
        return false;
    QStringList folders = excludedFolders();
    if (folders.contains(folder))
        return false;
    folders.append(folder);
    auto file = config(indexConfig);
    file->group(indexFoldersGroup).writePathEntry(excludeKey, folders, KConfig::Notify);
    file->sync();
    tellIndexer({});
    Q_EMIT changed();
    return true;
}

bool FileIndexing::removeExcludedFolder(const QString &path)
{
    QStringList folders = excludedFolders();
    if (folders.removeAll(path) == 0 && folders.removeAll(cleanFolder(path)) == 0)
        return false;
    auto file = config(indexConfig);
    file->group(indexFoldersGroup).writePathEntry(excludeKey, folders, KConfig::Notify);
    file->sync();
    tellIndexer({});
    Q_EMIT changed();
    return true;
}

void FileIndexing::chooseFolderToExclude()
{
    auto *dialog = new QFileDialog(nullptr, i18n("Choose a folder to leave out of search"), QDir::homePath());
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &folder) {
        addExcludedFolder(folder);
    });
    dialog->open();
}

QString FileIndexing::displayPath(const QString &path) const
{
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

void FileIndexing::tellIndexer(const QStringList &arguments) const
{
    if (arguments.isEmpty()) {
        // the folders changed: a running indexer reads its settings again
        if (enabled()) {
            QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.baloo"), QStringLiteral("/"), QStringLiteral("org.kde.baloo.main"),
                QStringLiteral("updateConfig")));
        }
        return;
    }
    if (!m_control.isEmpty())
        QProcess::startDetached(m_control.first(), m_control.mid(1) + arguments);
}

bool FileIndexing::canOpenAdvanced() const
{
    return KPluginMetaData::findPluginById(QStringLiteral("plasma/kcms/systemsettings"), indexModule).isValid();
}

void FileIndexing::openAdvanced() const
{
    QProcess::startDetached(QStringLiteral("kcmshell6"), {indexModule});
}

// --- ActivityHistory ---------------------------------------------------

ActivityHistory::ActivityHistory(QObject *parent)
    : QObject(parent)
{
}

bool ActivityHistory::remember() const
{
    return config(activityConfig)->group(scoringGroup).readEntry(rememberKey, rememberAll) != rememberNone;
}

void ActivityHistory::setRemember(bool on)
{
    if (on == remember())
        return;
    auto activities = config(activityConfig);
    activities->group(scoringGroup).writeEntry(rememberKey, on ? rememberAll : rememberNone, KConfig::Notify);
    activities->sync();
    // the recent files list of the open and save dialogs, and of Files
    auto globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    globals->reparseConfiguration();
    globals->group(QStringLiteral("RecentDocuments")).writeEntry(QStringLiteral("UseRecent"), on, KConfig::Notify);
    globals->sync();
    Q_EMIT changed();
}

void ActivityHistory::clear()
{
    // every activity, everything, whatever its age
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.ActivityManager"), QStringLiteral("/ActivityManager/Resources/Scoring"),
        QStringLiteral("org.kde.ActivityManager.ResourcesScoring"), QStringLiteral("DeleteRecentStats"));
    call << QString() << 0 << QStringLiteral("everything");
    QDBusConnection::sessionBus().asyncCall(call);
    KRecentDocument::clear();
    Q_EMIT cleared();
}

// --- UsageStatistics ---------------------------------------------------

UsageStatistics::UsageStatistics(QObject *parent)
    : QObject(parent)
{
}

bool UsageStatistics::enabled() const
{
    return config(feedbackConfig)->group(QStringLiteral("Global")).readEntry(QStringLiteral("FeedbackLevel"), levelOff)
        > levelOff;
}

void UsageStatistics::setEnabled(bool on)
{
    if (on == enabled())
        return;
    auto file = config(feedbackConfig);
    file->group(QStringLiteral("Global")).writeEntry(QStringLiteral("FeedbackLevel"), on ? levelBasicUsage : levelOff,
                                                    KConfig::Notify);
    file->sync();
    Q_EMIT changed();
}
