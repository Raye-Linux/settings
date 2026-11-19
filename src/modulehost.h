// SPDX-License-Identifier: GPL-3.0-or-later
// The window around the pages.
//
// ModuleHost opens a Plasma settings module inside a Raye page. Modules
// written in QML are loaded into our own window. The older ones, still
// made of widgets in Plasma 6.6, open in their own window through KDE's
// own kcmshell6 (kcmshell6.kde once the package has put ours in its
// place), never through our relay, which would send them back here.
//
// FrameSettings is what the frame keeps between two runs (the size of
// the window, the cards hidden from the launch screen, in
// raye-settingsrc) and the few facts the launch screen shows.
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>
#include <QtQml/qqmlregistration.h>
#include <memory>

class KQuickConfigModule;
class QQmlEngine;

class ModuleHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *content READ content NOTIFY contentChanged)
    Q_PROPERTY(QString current READ current NOTIFY contentChanged)
    Q_PROPERTY(bool needsSave READ needsSave NOTIFY needsSaveChanged)
    // the module went to a window of its own (it is made of widgets)
    Q_PROPERTY(bool external READ external NOTIFY contentChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit ModuleHost(QQmlEngine *engine, QObject *parent = nullptr);
    ~ModuleHost() override;

    QQuickItem *content() const;
    QString current() const { return m_current; }
    bool needsSave() const;
    bool external() const { return m_external; }
    QString error() const { return m_error; }

    // Opens the module; returns false when it went to its own window or
    // could not be found.
    Q_INVOKABLE bool open(const QString &kcm);
    Q_INVOKABLE void close();
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();
    // Opens the module in a window of its own, whatever it is made of.
    Q_INVOKABLE void openExternal(const QString &kcm);

    // KDE's own command, never our relay: the diverted one when the
    // package moved it aside, else the one on PATH. `root` is for tests.
    static QString kcmshellProgram(const QString &root = QString());
    // A module id is a plugin name: letters, digits, '_', '-', '.'. Anything
    // else is not handed to a program.
    static bool isModuleId(const QString &kcm);

Q_SIGNALS:
    void contentChanged();
    void needsSaveChanged();
    void errorChanged();

private:
    void setError(const QString &error);
    void startExternal(const QString &kcm);

    std::shared_ptr<QQmlEngine> m_engine;
    QPointer<KQuickConfigModule> m_module;
    QString m_current;
    QString m_error;
    bool m_external = false;
};

class FrameSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // ids of the launch screen cards the user hid ("your-device")
    Q_PROPERTY(QStringList hiddenCards READ hiddenCards NOTIFY hiddenCardsChanged)
    // Your device: model, chassis, processor, memory, storage
    Q_PROPERTY(QVariantMap device READ device NOTIFY deviceChanged)
    // Make it yours: [{ id, name, preview, current }]
    Q_PROPERTY(QVariantList looks READ looks NOTIFY looksChanged)

public:
    // `root` is where /proc, /sys and /usr/share are read (the tests
    // give a folder that looks like a laptop). QML makes one per window.
    explicit FrameSettings(const QString &root = QString(), QObject *parent = nullptr);

    QStringList hiddenCards() const;
    Q_INVOKABLE void hideCard(const QString &id);
    Q_INVOKABLE void showAllCards();

    QVariantMap device();
    QVariantList looks() const;
    // Applies a look of the list above (Raye Night, Raye Day).
    Q_INVOKABLE bool applyLook(const QString &id);

    // Is this QML file of the module built in? ("pages/system/SystemPage.qml")
    Q_INVOKABLE bool hasComponent(const QString &component) const;

    // The size the window had when it was closed, on this screen layout.
    Q_INVOKABLE void restoreWindow(QWindow *window);
    Q_INVOKABLE void saveWindow(QWindow *window);

    // Gives back what the pages left behind once they are gone: their
    // objects, the types no page uses any more, the heap's free blocks.
    Q_INVOKABLE void releaseMemory();

    // Parsing, public for the tests.
    static QString processorName(const QString &cpuinfo);
    static qint64 memoryBytes(const QString &meminfo);
    static QString chassisOf(const QString &chassisType);
    // "16 GB", "512 GB", "1.5 TB": what a shop calls the size
    static QString sizeText(qint64 bytes);

Q_SIGNALS:
    void hiddenCardsChanged();
    void deviceChanged();
    void looksChanged();

private:
    QString readFirstLine(const QString &path) const;

    QString m_root;
    QVariantMap m_device;
};
