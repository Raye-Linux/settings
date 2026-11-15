// SPDX-License-Identifier: GPL-3.0-or-later
// raye-settings-runner: answers the Start menu's search with the pages
// and rows of Raye Settings (org.kde.krunner1 on the session bus, name
// org.raye.settings.runner, path /runner). Started by D-Bus on the first
// search; quits after two quiet minutes, D-Bus starts it again.
//
// It never loads a settings module: their keywords come from the copy in
// ~/.cache/raye-settings, which "raye-settings-runner --write-module-keywords"
// writes in a process of its own when the modules or the layout changed.

#include "runnermatch.h"
#include "searchcache.h"
#include "searchindex.h"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QProcess>
#include <QTimer>

namespace {

constexpr int idleQuitMs = 2 * 60 * 1000;
// reading the modules takes about a second; past this something is wrong
constexpr int writeKeywordsTimeoutMs = 30 * 1000;
const QString writeKeywordsOption = QStringLiteral("--write-module-keywords");

struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};

struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    int categoryRelevance = 0;
    double relevance = 0;
    QVariantMap properties;
};

// Reads the modules (loading their libraries) and leaves with them.
int writeModuleKeywordsFile()
{
    const QString stamp = moduleKeywordStamp(moduleKeywordRoots());
    const ModuleKeywordMap words = recordModuleKeywords(SearchIndex::installedSources());
    return writeModuleKeywords(moduleKeywordFile(), stamp, words) ? 0 : 1;
}

// The copy, written again by a child process when it is stale. When
// the cache folder cannot be written, the modules are read here after
// all: complete results matter more than the memory.
void useCachedModuleKeywords()
{
    const auto words = cachedModuleKeywords(moduleKeywordFile(), moduleKeywordStamp(moduleKeywordRoots()), [] {
        QProcess child;
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.start(QCoreApplication::applicationFilePath(), {writeKeywordsOption});
        if (!child.waitForFinished(writeKeywordsTimeoutMs))
            child.kill();
    });
    if (words)
        SearchIndex::setModuleKeywordSource(moduleKeywordsFrom(*words));
    else
        qWarning("raye-settings-runner: no copy of the module keywords, reading the modules");
}

} // namespace

typedef QList<RemoteAction> RemoteActions;
typedef QList<RemoteMatch> RemoteMatches;
Q_DECLARE_METATYPE(RemoteAction)
Q_DECLARE_METATYPE(RemoteActions)
Q_DECLARE_METATYPE(RemoteMatch)
Q_DECLARE_METATYPE(RemoteMatches)

QDBusArgument &operator<<(QDBusArgument &argument, const RemoteAction &action)
{
    argument.beginStructure();
    argument << action.id << action.text << action.iconName;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteAction &action)
{
    argument.beginStructure();
    argument >> action.id >> action.text >> action.iconName;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const RemoteMatch &match)
{
    argument.beginStructure();
    argument << match.id << match.text << match.iconName << match.categoryRelevance << match.relevance
             << match.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteMatch &match)
{
    argument.beginStructure();
    argument >> match.id >> match.text >> match.iconName >> match.categoryRelevance >> match.relevance
        >> match.properties;
    argument.endStructure();
    return argument;
}

class Runner : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    explicit Runner(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_idle.setSingleShot(true);
        m_idle.setInterval(idleQuitMs);
        connect(&m_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
        m_idle.start();
    }

public Q_SLOTS:
    RemoteActions Actions()
    {
        m_idle.start();
        return {};
    }

    QVariantMap Config()
    {
        m_idle.start();
        return {};
    }

    RemoteMatches Match(const QString &query)
    {
        m_idle.start();
        RemoteMatches out;
        const auto matches = runnerMatches(*SearchIndex::shared(), query);
        for (const RunnerMatch &m : matches)
            out.append({m.id, m.text, m.iconName, m.categoryRelevance, m.relevance, m.properties});
        return out;
    }

    void Run(const QString &matchId, const QString &actionId)
    {
        Q_UNUSED(actionId)
        m_idle.start();
        QString program;
        QStringList arguments;
        // only ids of the index are opened, never what came as it came
        if (!runnerCommand(*SearchIndex::shared(), matchId, &program, &arguments))
            return;
        QProcess::startDetached(program, arguments);
    }

    void Teardown()
    {
        m_idle.start();
    }

private:
    QTimer m_idle;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("raye-settings-runner"));
    KLocalizedString::setApplicationDomain("raye-settings");
    if (app.arguments().contains(writeKeywordsOption))
        return writeModuleKeywordsFile();

    qDBusRegisterMetaType<RemoteAction>();
    qDBusRegisterMetaType<RemoteActions>();
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();

    Runner runner;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/runner"), &runner, QDBusConnection::ExportAllSlots)) {
        qCritical("raye-settings-runner: cannot put /runner on the session bus");
        return 1;
    }
    if (!bus.registerService(QStringLiteral("org.raye.settings.runner"))) {
        qCritical("raye-settings-runner: org.raye.settings.runner is taken");
        return 1;
    }
    // build the index now, not on the first key the user types
    useCachedModuleKeywords();
    SearchIndex::shared();
    return app.exec();
}

#include "main.moc"
