// SPDX-License-Identifier: GPL-3.0-or-later
// System › Display › Graphics: which apps start on the dedicated
// graphics card of a computer that has two. The choice is the
// PrefersNonDefaultGPU key of the app's entry, which the desktop reads
// when it starts the app. The system's entry is never changed: the
// user gets a copy in ~/.local/share/applications, marked as ours, and
// turning the choice off again removes that copy.
#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class GpuApps : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, // "org.kde.krita.desktop"
        NameRole,
        IconRole,
        DedicatedRole,
    };

    explicit GpuApps(QObject *parent = nullptr);
    // For the tests: where the user's entries go and where the system's are.
    GpuApps(const QString &userDir, const QStringList &systemDirs, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    // returns false when the entry could not be written
    Q_INVOKABLE bool setDedicated(int row, bool on);

    // The marker in a copy Raye made only for this choice.
    static constexpr const char *marker = "X-Raye-GpuChoice";

Q_SIGNALS:
    void countChanged();

private:
    struct App {
        QString id;
        QString name;
        QString icon;
        QString path; // the entry in effect (the user's when there is one)
        bool dedicated = false;
    };

    QString m_userDir;
    QStringList m_systemDirs;
    QList<App> m_apps;
};
