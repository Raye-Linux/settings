// SPDX-License-Identifier: GPL-3.0-or-later
#include "externalmodule.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

ExternalModule::ExternalModule(QObject *parent)
    : ExternalModule(QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("accounts/providers"),
                                               QStandardPaths::LocateDirectory),
                     parent)
{
}

ExternalModule::ExternalModule(const QStringList &providerFolders, QObject *parent)
    : QObject(parent)
    , m_folders(providerFolders)
{
}

QStringList ExternalModule::accountProviders() const
{
    QStringList providers;
    for (const QString &folder : m_folders) {
        const QStringList files = QDir(folder).entryList({QStringLiteral("*.provider")}, QDir::Files, QDir::Name);
        for (const QString &file : files)
            providers << QFileInfo(file).completeBaseName();
    }
    return providers;
}

bool ExternalModule::open(const QString &kcm)
{
    // ours sends a module back to the page that asked for it
    const QString kde = QStandardPaths::findExecutable(QStringLiteral("kcmshell6.kde"));
    return QProcess::startDetached(kde.isEmpty() ? QStringLiteral("kcmshell6") : kde, {kcm});
}
