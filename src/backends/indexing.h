// SPDX-License-Identifier: GPL-3.0-or-later
// What the desktop keeps about what you do, for three Privacy pages:
// - FileIndexing, "Searching your files": the file index (baloo) on or
//   off, and the folders left out of it;
// - ActivityHistory: whether the files you open are remembered (Start's
//   recommendations, Files' Recent), and clearing what is;
// - UsageStatistics, "Diagnostics & feedback": the anonymous statistics
//   the desktop may send to its makers, off unless turned on.
// All three write the desktop's own settings files, in the home folder.
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class FileIndexing : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QStringList excludedFolders READ excludedFolders NOTIFY changed)

public:
    explicit FileIndexing(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool on);
    QStringList excludedFolders() const;

    // false when the folder is not absolute or already left out
    Q_INVOKABLE bool addExcludedFolder(const QString &path);
    Q_INVOKABLE bool removeExcludedFolder(const QString &path);
    // asks for a folder, then leaves it out
    Q_INVOKABLE void chooseFolderToExclude();
    // what is in a folder path for a person: ~ for the home folder
    Q_INVOKABLE QString displayPath(const QString &path) const;
    Q_INVOKABLE bool canOpenAdvanced() const;
    Q_INVOKABLE void openAdvanced() const;

    // For the tests: the indexer's command (empty: nothing is run).
    void setControlCommand(const QStringList &command) { m_control = command; }

Q_SIGNALS:
    void changed();

private:
    void tellIndexer(const QStringList &arguments) const;

    QStringList m_control;
};

class ActivityHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY changed)

public:
    explicit ActivityHistory(QObject *parent = nullptr);

    bool remember() const;
    void setRemember(bool on);
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void changed();
    void cleared();
};

class UsageStatistics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)

public:
    explicit UsageStatistics(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool on);

    // KUserFeedback's levels: nothing, and basic usage statistics
    static constexpr int levelOff = 0;
    static constexpr int levelBasicUsage = 0x20;

Q_SIGNALS:
    void changed();
};
