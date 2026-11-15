// SPDX-License-Identifier: GPL-3.0-or-later
// Where the window is, and how it got there.
//
// Every link in Raye Settings goes through the router: a page id
// ("system/display"), a module id ("kcm_kscreen"), or a settings: URL
// from another app. It keeps the back and forward history, carries the
// anchor of a row to flash, and hides the pages this computer has no
// hardware for.
#pragma once

#include "layout.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QJSEngine;
class QQmlEngine;

// What this computer has. Paths are read under `root` so the tests can
// hand it a folder that looks like a laptop, or like a desktop.
class Requirements
{
public:
    explicit Requirements(const QString &root = QStringLiteral("/"));

    // "bluetooth", "wifi", "battery", "touchpad", "tablet", "two-gpus",
    // "windows", "btrfs", "fingerprint", "raye-account" (the account
    // preview is switched on and its QML module is installed). An empty
    // name is "always"; "wifi|bluetooth" is either of them.
    // Each probe reads files only, so asking costs nothing at start.
    bool has(const QString &name) const;
    // Answer a probe without looking at the computer (the tests do
    // this to stand for a laptop, or for a desktop).
    void set(const QString &name, bool present);
    void forget();

private:
    bool probe(const QString &name) const;

    QString m_root;
    mutable QHash<QString, bool> m_answers;
};

class Router : public QObject
{
    Q_OBJECT
    // "home", a category id, or a page id
    Q_PROPERTY(QString route READ route NOTIFY routeChanged)
    // the row to scroll to and flash, empty most of the time
    Q_PROPERTY(QString anchor READ anchor NOTIFY routeChanged)
    // the category the route belongs to, empty on the launch screen
    Q_PROPERTY(QString categoryId READ categoryId NOTIFY routeChanged)
    Q_PROPERTY(bool atHome READ atHome NOTIFY routeChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY routeChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY routeChanged)

public:
    explicit Router(QObject *parent = nullptr);
    ~Router() override;

    // The one QML talks to; main() builds it and registers it. Null
    // before that, and again once it is gone.
    static Router *instance();
    // What `import org.raye.settings` hands out as Router: the one
    // main() built. A QML file loaded on its own (the load test, a
    // gallery) gets an empty router of its own.
    static Router *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    void setLayout(const Layout &layout);
    const Layout &layout() const { return m_layout; }
    Requirements &requirements() { return m_requirements; }

    QString route() const { return m_route; }
    QString anchor() const { return m_anchor; }
    QString categoryId() const;
    bool atHome() const { return m_route == QLatin1String("home"); }
    bool canGoBack() const { return m_position > 0; }
    bool canGoForward() const { return m_position + 1 < m_history.size(); }

    // "system/display", "system/recovery#restart-windows", "kcm_kscreen",
    // "settings:network/wifi", "home", "". Returns false when nothing in
    // the layout answers for it: the caller then passes the id on to the
    // real settings modules.
    Q_INVOKABLE bool navigate(const QString &target);
    Q_INVOKABLE void goHome() { navigate(QStringLiteral("home")); }
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();

    // The route a target would open, "" when the layout does not know it.
    Q_INVOKABLE QString resolve(const QString &target) const;
    // What the window of today opens for a target: a module id asked for
    // by name stays that module, a page gives the module it hosts (or
    // its category when it has none yet), home gives "". A target the
    // layout does not know is passed on as it came.
    Q_INVOKABLE QString moduleFor(const QString &target) const;
    // A link from outside (the command line, a second raye-settings):
    // the route moves, and the module for the window of today comes back.
    QString handOff(const QString &target);
    // The bare id of a target: no settings: scheme, slashes, query,
    // anchor or .desktop; a settings: address is percent-decoded.
    static QString idOf(const QString &target);
    // The row a target points at, after '#', decoded the same way.
    static QString anchorOf(const QString &target);
    // Does this computer have what the page needs?
    Q_INVOKABLE bool isAvailable(const QString &pageId) const;
    Q_INVOKABLE bool has(const QString &requirement) const { return m_requirements.has(requirement); }
    // The hardware changed while the window is open (a USB Wi-Fi key);
    // requirementsChanged() goes out when the answer is a new one.
    void setRequirement(const QString &name, bool present);
    // The QML file named by the layout, "" for a page that hosts a
    // settings module.
    Q_INVOKABLE QString componentOf(const QString &pageId) const;
    // The QML file to load for real: the page when it is built, the
    // placeholder until then. Empty for a page that hosts a module.
    Q_INVOKABLE QString componentUrlOf(const QString &pageId) const;
    // How the router decides whether a page file is there (the tests
    // and the gallery hand it their own answer).
    void setComponentCheck(std::function<bool(const QString &)> check);

Q_SIGNALS:
    void routeChanged();
    // the row to flash, after the page is on screen
    void anchorRequested(const QString &anchor);
    void requirementsChanged();

private:
    struct Step {
        QString route;
        QString anchor;
    };
    void go(const Step &step);

    Layout m_layout;
    Requirements m_requirements;
    std::function<bool(const QString &)> m_hasComponent;
    QList<Step> m_history;
    int m_position = -1;
    QString m_route = QStringLiteral("home");
    QString m_anchor;
};

// What `import org.raye.settings` names Router. Qt builds a singleton
// with its default constructor when the class has one, and Router has:
// this wrapper makes QML ask Router::create(), which hands out the one
// main() built.
struct RouterForeign {
    Q_GADGET
    QML_FOREIGN(Router)
    QML_NAMED_ELEMENT(Router)
    QML_SINGLETON
public:
    static Router *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine) { return Router::create(qmlEngine, jsEngine); }
};

