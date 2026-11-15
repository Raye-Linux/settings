// SPDX-License-Identifier: GPL-3.0-or-later
// The two things Raye's pages hand over to a settings window of their own:
// a drawing tablet's fine settings, and adding an online account. Also
// whether adding an account can do anything here at all: the module has
// nothing to offer without providers, and the page says so instead of
// opening an empty window.
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class ExternalModule : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList accountProviders READ accountProviders CONSTANT)

public:
    explicit ExternalModule(QObject *parent = nullptr);
    // for the tests: another folder of *.provider files
    explicit ExternalModule(const QStringList &providerFolders, QObject *parent = nullptr);

    QStringList accountProviders() const;
    // false when the module could not be started
    Q_INVOKABLE bool open(const QString &kcm);

private:
    QStringList m_folders;
};
