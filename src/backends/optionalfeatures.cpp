// SPDX-License-Identifier: GPL-3.0-or-later
#include "optionalfeatures.h"

#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QTimer>
#include <PackageKit/Daemon>
#include <PackageKit/Transaction>

#include <memory>

namespace {

QString titleOf(const QString &id)
{
    if (id == QLatin1String("gaming"))
        return i18nc("@title optional feature", "Gaming tools");
    if (id == QLatin1String("windows"))
        return i18nc("@title optional feature", "Windows programs");
    if (id == QLatin1String("kernel"))
        return i18nc("@title optional feature", "Raye kernel");
    if (id == QLatin1String("lang-fr"))
        return i18nc("@title optional feature", "French language");
    if (id == QLatin1String("lang-es"))
        return i18nc("@title optional feature", "Spanish language");
    if (id == QLatin1String("lang-de"))
        return i18nc("@title optional feature", "German language");
    return id;
}

QString descriptionOf(const QString &id)
{
    if (id == QLatin1String("gaming"))
        return i18n("Game Mode, the frame rate counter and what games need to run well");
    if (id == QLatin1String("windows"))
        return i18n("Run programs made for Windows");
    if (id == QLatin1String("kernel"))
        return i18n("A kernel tuned for games and a quick desktop. Turn it on in Raye Update.");
    if (id.startsWith(QLatin1String("lang-")))
        return i18n("Menus, spelling and the dictionary in this language");
    return {};
}

} // namespace

OptionalFeatures::OptionalFeatures(QObject *parent)
    : QAbstractListModel(parent)
{
    rebuild();
    refresh();
}

QList<FeaturePack> OptionalFeatures::packs()
{
    return {
        {QStringLiteral("gaming"), {QStringLiteral("raye-gaming")}, {}, QStringLiteral("org.raye.settings-gaming")},
        {QStringLiteral("windows"), {QStringLiteral("raye-windows-apps")}, {}, QStringLiteral("org.raye.settings-windows-programs")},
        {QStringLiteral("kernel"), {}, QStringLiteral("update/advanced"), QStringLiteral("org.raye.settings-update-advanced")},
        {QStringLiteral("lang-fr"),
         {QStringLiteral("language-pack-fr"), QStringLiteral("language-pack-kde-fr"), QStringLiteral("hunspell-fr")},
         {},
         QStringLiteral("org.raye.settings-language")},
        {QStringLiteral("lang-es"),
         {QStringLiteral("language-pack-es"), QStringLiteral("language-pack-kde-es"), QStringLiteral("hunspell-es")},
         {},
         QStringLiteral("org.raye.settings-language")},
        {QStringLiteral("lang-de"),
         {QStringLiteral("language-pack-de"), QStringLiteral("language-pack-kde-de"), QStringLiteral("hunspell-de-de")},
         {},
         QStringLiteral("org.raye.settings-language")},
    };
}

QString OptionalFeatures::stateOf(const FeaturePack &pack, const QHash<QString, bool> &known)
{
    if (!pack.route.isEmpty())
        return QStringLiteral("link");
    if (pack.packages.isEmpty())
        return {};
    int installed = 0;
    int offered = 0;
    for (const QString &name : pack.packages) {
        if (!known.contains(name))
            continue;
        ++offered;
        if (known.value(name))
            ++installed;
    }
    // the first package is the pack itself: without it there is nothing to offer
    if (!known.contains(pack.packages.first()))
        return {};
    if (installed == offered)
        return QStringLiteral("installed");
    return installed == 0 ? QStringLiteral("available") : QStringLiteral("partial");
}

QString OptionalFeatures::packageName(const QString &packageId)
{
    return packageId.section(QLatin1Char(';'), 0, 0);
}

int OptionalFeatures::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_shown.size();
}

QVariant OptionalFeatures::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_shown.size())
        return {};
    const FeaturePack &pack = m_shown.at(index.row());
    switch (role) {
    case FeatureIdRole:
        return pack.id;
    case Qt::DisplayRole:
    case TitleRole:
        return titleOf(pack.id);
    case DescriptionRole:
        return descriptionOf(pack.id);
    case IconNameRole:
        return pack.iconName;
    case StateRole: {
        if (m_checking && pack.route.isEmpty() && m_installed.isEmpty())
            return QStringLiteral("unknown");
        return stateOf(pack, m_installed);
    }
    case RouteRole:
        return pack.route;
    }
    return {};
}

QHash<int, QByteArray> OptionalFeatures::roleNames() const
{
    return {{FeatureIdRole, "featureId"}, {TitleRole, "title"}, {DescriptionRole, "description"},
            {IconNameRole, "iconName"},   {StateRole, "state"}, {RouteRole, "route"}};
}

void OptionalFeatures::rebuild()
{
    const int before = m_shown.size();
    beginResetModel();
    m_shown.clear();
    for (const FeaturePack &pack : packs()) {
        // while the package list is unknown everything is shown; after,
        // only what this PC can have
        if (m_checking || m_installed.isEmpty() || !stateOf(pack, m_installed).isEmpty())
            m_shown << pack;
    }
    endResetModel();
    if (before != m_shown.size())
        Q_EMIT countChanged();
}

void OptionalFeatures::refresh()
{
    if (m_checking)
        return;
    QStringList names;
    for (const FeaturePack &pack : packs())
        names << pack.packages;
    // no package manager on this system bus: nothing can be offered
    const QString service = QStringLiteral("org.freedesktop.PackageKit");
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus || (!bus->isServiceRegistered(service) && !bus->activatableServiceNames().value().contains(service))) {
        m_installed.clear();
        rebuild();
        return;
    }
    m_checking = true;
    Q_EMIT checkingChanged();
    rebuild();

    auto installed = std::make_shared<QHash<QString, bool>>();
    auto available = std::make_shared<QHash<QString, QString>>();
    auto installedIds = std::make_shared<QHash<QString, QString>>();
    PackageKit::Transaction *t = PackageKit::Daemon::resolve(names, PackageKit::Transaction::FilterArch);
    connect(t, &PackageKit::Transaction::package, this,
            [installed, available, installedIds](PackageKit::Transaction::Info info, const QString &id, const QString &) {
                const QString name = packageName(id);
                const bool isInstalled = info == PackageKit::Transaction::InfoInstalled;
                (*installed)[name] = installed->value(name) || isInstalled;
                if (isInstalled)
                    installedIds->insert(name, id);
                else
                    available->insert(name, id);
            });
    // a package manager that never answers must not leave the page waiting
    auto *watchdog = new QTimer(t);
    watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, this, [this, t] {
        if (!m_checking)
            return;
        t->disconnect(this);
        m_checking = false;
        Q_EMIT checkingChanged();
        rebuild();
    });
    watchdog->start(60000);
    connect(t, &PackageKit::Transaction::finished, this, [this, installed, available, installedIds](PackageKit::Transaction::Exit) {
        m_installed = *installed;
        m_availableIds = *available;
        m_installedIds = *installedIds;
        m_checking = false;
        Q_EMIT checkingChanged();
        rebuild();
    });
}

bool OptionalFeatures::install(const QString &id)
{
    return runOn(id, true);
}

bool OptionalFeatures::remove(const QString &id)
{
    return runOn(id, false);
}

bool OptionalFeatures::runOn(const QString &id, bool installing)
{
    if (!m_busyId.isEmpty() || m_checking)
        return false;
    FeaturePack pack;
    for (const FeaturePack &p : packs()) {
        if (p.id == id)
            pack = p;
    }
    if (pack.id.isEmpty() || !pack.route.isEmpty())
        return false;
    QStringList ids;
    for (const QString &name : std::as_const(pack.packages)) {
        const QString packageId = installing ? m_availableIds.value(name) : m_installedIds.value(name);
        if (!packageId.isEmpty() && m_installed.value(name) != installing)
            ids << packageId;
    }
    if (ids.isEmpty())
        return false;

    m_busyId = id;
    m_busyPercent = 0;
    Q_EMIT busyChanged();
    PackageKit::Transaction *t = installing ? PackageKit::Daemon::installPackages(ids)
                                            // what the pack brought and nothing else needs goes too
                                            : PackageKit::Daemon::removePackages(ids, false, true);
    m_transaction = t;
    auto error = std::make_shared<QString>();
    connect(t, &PackageKit::Transaction::percentageChanged, this, [this, t] {
        const uint p = t->percentage();
        // 101 means "not known"
        if (p <= 100 && int(p) != m_busyPercent) {
            m_busyPercent = int(p);
            Q_EMIT busyChanged();
        }
    });
    connect(t, &PackageKit::Transaction::errorCode, this, [error](PackageKit::Transaction::Error, const QString &details) {
        *error = details;
    });
    connect(t, &PackageKit::Transaction::finished, this, [this, id, error](PackageKit::Transaction::Exit exit) {
        const bool ok = exit == PackageKit::Transaction::ExitSuccess;
        m_busyId.clear();
        m_busyPercent = 0;
        Q_EMIT busyChanged();
        Q_EMIT finished(id, ok, *error);
        refresh();
    });
    return true;
}
