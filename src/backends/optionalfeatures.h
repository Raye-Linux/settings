// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Optional features: packs of Raye that can be added or removed
// (gaming tools, Windows programs, more languages), each a few system
// packages installed or removed through PackageKit, which asks for the
// administrator password itself. The Raye kernel is a pack too, but it
// is turned on in Raye Update: its row only leads there.
// (The file is not named features.h: that is a system header, and this
// folder is on the include path.)
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace PackageKit
{
class Transaction;
}

struct FeaturePack {
    QString id;
    QStringList packages;
    QString route; // set: a link to that page instead of Install/Remove
    QString iconName;
};

class OptionalFeatures : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // true while the package list is being read
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    // the pack being installed or removed, "" when none
    Q_PROPERTY(QString busyId READ busyId NOTIFY busyChanged)
    Q_PROPERTY(int busyPercent READ busyPercent NOTIFY busyChanged)

public:
    enum Roles {
        FeatureIdRole = Qt::UserRole + 1,
        TitleRole,
        DescriptionRole,
        IconNameRole,
        // "installed", "available", "partial" (some packages missing), "link", "unknown"
        StateRole,
        RouteRole,
    };
    Q_ENUM(Roles)

    explicit OptionalFeatures(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool checking() const { return m_checking; }
    QString busyId() const { return m_busyId; }
    int busyPercent() const { return m_busyPercent; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool install(const QString &id);
    Q_INVOKABLE bool remove(const QString &id);

    static QList<FeaturePack> packs();
    // what a pack is, from what the package manager said about its
    // packages: name -> installed (a name it does not know is missing);
    // "" when the pack cannot be offered on this PC
    static QString stateOf(const FeaturePack &pack, const QHash<QString, bool> &known);
    // "name;version;arch;data" -> "name"
    static QString packageName(const QString &packageId);

Q_SIGNALS:
    void countChanged();
    void checkingChanged();
    void busyChanged();
    // after install() or remove()
    void finished(const QString &id, bool ok, const QString &message);

private:
    void rebuild();
    bool runOn(const QString &id, bool installing);

    QList<FeaturePack> m_shown;
    QHash<QString, bool> m_installed;           // package name -> installed
    QHash<QString, QString> m_availableIds;     // package name -> id to install
    QHash<QString, QString> m_installedIds;     // package name -> id to remove
    bool m_checking = false;
    QString m_busyId;
    int m_busyPercent = 0;
    QPointer<PackageKit::Transaction> m_transaction;
};
