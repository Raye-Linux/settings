// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemode.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString env(const char *name, const QString &fallback)
{
    const QString v = qEnvironmentVariable(name);
    return v.isEmpty() ? fallback : v;
}

QString confFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/raye/game-mode.conf");
}

QString gamesFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/raye/game-mode/games");
}

QString activeFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/raye-game-mode/active");
}

QString windowsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/raye/windows");
}

QString sourceOf(const QString &key)
{
    if (key.startsWith(QLatin1String("steam:")))
        return QStringLiteral("steam");
    if (key.startsWith(QLatin1String("app:")))
        return QStringLiteral("store");
    return QStringLiteral("windows");
}

} // namespace

GameModeSettings::GameModeSettings(QObject *parent)
    : QObject(parent)
{
    read();
    askWindowsKinds();
    askWindowsPrograms();
}

bool GameModeSettings::validKey(const QString &key)
{
    static const QRegularExpression re(QStringLiteral("^(steam:[0-9]+|app:[A-Za-z0-9._-]+|windows:[a-z0-9][a-z0-9-]*)$"));
    return re.match(key).hasMatch();
}

bool GameModeSettings::installed() const
{
    return QFile::exists(env("RAYE_GAME_WATCH", QStringLiteral("/usr/libexec/raye-game-watch")));
}

bool GameModeSettings::schedulerAvailable() const
{
    return QFileInfo(env("RAYE_GAME_SYS", QStringLiteral("/sys")) + QStringLiteral("/kernel/sched_ext")).isDir()
        && QFile::exists(env("RAYE_GAME_SCX", QStringLiteral("/usr/bin/scx_lavd")));
}

void GameModeSettings::read()
{
    m_enabled = m_effects = m_scheduler = m_power = true;
    m_on.clear();
    m_off.clear();
    QFile f(confFile());
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw);
            const QString key = line.section(QLatin1Char('='), 0, 0);
            const QString value = line.section(QLatin1Char('='), 1);
            const bool no = value == QLatin1String("false");
            if (key == QLatin1String("enabled"))
                m_enabled = !no;
            else if (key == QLatin1String("effects"))
                m_effects = !no;
            else if (key == QLatin1String("scheduler"))
                m_scheduler = !no;
            else if (key == QLatin1String("power"))
                m_power = !no;
            else if (key == QLatin1String("on") || key == QLatin1String("off")) {
                QStringList keys;
                for (const QString &k : value.split(QLatin1Char(' '), Qt::SkipEmptyParts))
                    if (validKey(k))
                        keys << k;
                (key == QLatin1String("on") ? m_on : m_off) = keys;
            }
        }
    }
    m_active = QFile::exists(activeFile());
}

void GameModeSettings::write()
{
    QDir().mkpath(QFileInfo(confFile()).absolutePath());
    QSaveFile f(confFile());
    if (!f.open(QIODevice::WriteOnly))
        return;
    auto flag = [](bool on) { return on ? QByteArrayLiteral("true") : QByteArrayLiteral("false"); };
    QByteArray out = "# Raye Settings > Gaming > Game Mode\n";
    out += "enabled=" + flag(m_enabled) + "\neffects=" + flag(m_effects) + "\nscheduler=" + flag(m_scheduler)
        + "\npower=" + flag(m_power) + '\n';
    if (!m_on.isEmpty())
        out += "on=" + m_on.join(QLatin1Char(' ')).toUtf8() + '\n';
    if (!m_off.isEmpty())
        out += "off=" + m_off.join(QLatin1Char(' ')).toUtf8() + '\n';
    f.write(out);
    f.commit();
}

void GameModeSettings::setFlag(bool &flag, bool on)
{
    if (flag == on)
        return;
    flag = on;
    write();
    Q_EMIT changed();
}

void GameModeSettings::setEnabled(bool on) { setFlag(m_enabled, on); }
void GameModeSettings::setEffects(bool on) { setFlag(m_effects, on); }
void GameModeSettings::setScheduler(bool on) { setFlag(m_scheduler, on); }
void GameModeSettings::setPower(bool on) { setFlag(m_power, on); }

bool GameModeSettings::onByDefault(const QString &key) const
{
    // a Windows program is a game only when its profile says so
    if (key.startsWith(QLatin1String("windows:")))
        return !m_windowsKinds.value(key.mid(8)).toString().isEmpty();
    return true;
}

void GameModeSettings::setGameOn(const QString &key, bool on)
{
    if (!validKey(key))
        return;
    m_on.removeAll(key);
    m_off.removeAll(key);
    if (on && !onByDefault(key))
        m_on << key;
    else if (!on && onByDefault(key))
        m_off << key;
    write();
    Q_EMIT changed();
}

QVariantList GameModeSettings::games() const
{
    QVariantList list;
    QStringList keys;
    auto add = [&](const QString &key, QString name) {
        if (keys.contains(key))
            return;
        keys << key;
        if (name.isEmpty() && key.startsWith(QLatin1String("steam:")))
            name = i18nc("@info %1 is a number Steam gives the game", "Steam game %1", key.mid(6));
        if (name.isEmpty() && key.startsWith(QLatin1String("windows:"))) {
            const QString space = key.mid(8);
            name = m_windowsNames.value(space).toString();
            if (name.isEmpty())
                name = m_windowsPrograms.value(space).toString();
        }
        if (name.isEmpty())
            name = key.section(QLatin1Char(':'), 1);
        const bool on = (onByDefault(key) || m_on.contains(key)) && !m_off.contains(key);
        list.append(QVariantMap{{QStringLiteral("key"), key},
                                {QStringLiteral("name"), name},
                                {QStringLiteral("source"), sourceOf(key)},
                                {QStringLiteral("on"), on}});
    };
    QFile f(gamesFile());
    if (f.open(QIODevice::ReadOnly)) {
        QList<QByteArray> lines = f.readAll().split('\n');
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            const QString line = QString::fromUtf8(*it);
            const QString key = line.section(QLatin1Char('\t'), 0, 0);
            if (validKey(key))
                add(key, line.section(QLatin1Char('\t'), 1).trimmed());
        }
    }
    // every space with a Windows program installed, played or not, so any
    // of them can be turned on
    QStringList spaces = m_windowsPrograms.keys();
    spaces.sort();
    for (const QString &space : std::as_const(spaces)) {
        const QString key = QStringLiteral("windows:") + space;
        if (listed(key))
            add(key, QString());
    }
    // played once, gone since: not listed
    QVariantList shown;
    for (const QVariant &game : std::as_const(list)) {
        if (listed(game.toMap().value(QStringLiteral("key")).toString()))
            shown.append(game);
    }
    return shown;
}

bool GameModeSettings::listed(const QString &key) const
{
    if (key.startsWith(QLatin1String("steam:")))
        return steamGameInstalled(key.mid(6));
    if (key.startsWith(QLatin1String("app:")))
        return storeAppInstalled(key.mid(4));
    const QString space = key.mid(8);
    // the space every program starts from holds no program of its own
    return space != QLatin1String("default") && m_windowsPrograms.contains(space);
}

bool GameModeSettings::steamGameInstalled(const QString &id)
{
    static const QRegularExpression number(QStringLiteral("^[0-9]+$"));
    if (!number.match(id).hasMatch())
        return false;
    const QString home = QDir::homePath();
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList libraries{data + QStringLiteral("/Steam"), home + QStringLiteral("/.steam/steam"),
                          home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
                          home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam")};
    // more libraries (another disk) are listed in libraryfolders.vdf
    static const QRegularExpression path(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
    for (const QString &root : QStringList(libraries)) {
        QFile vdf(root + QStringLiteral("/steamapps/libraryfolders.vdf"));
        if (!vdf.open(QIODevice::ReadOnly))
            continue;
        auto it = path.globalMatch(QString::fromUtf8(vdf.read(1 << 20)));
        while (it.hasNext()) {
            const QString found = it.next().captured(1).replace(QLatin1String("\\\\"), QLatin1String("\\"));
            if (QDir::isAbsolutePath(found) && !libraries.contains(found))
                libraries << found;
        }
    }
    for (const QString &root : std::as_const(libraries)) {
        if (QFileInfo::exists(root + QStringLiteral("/steamapps/appmanifest_") + id + QStringLiteral(".acf")))
            return true;
    }
    return false;
}

bool GameModeSettings::storeAppInstalled(const QString &id)
{
    static const QRegularExpression appId(QStringLiteral("^[A-Za-z0-9._-]+$"));
    if (!appId.match(id).hasMatch())
        return false;
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString system = env("RAYE_GAME_FLATPAK_SYSTEM", QStringLiteral("/var/lib/flatpak"));
    for (const QString &root : QStringList{data + QStringLiteral("/flatpak"), system}) {
        if (QFileInfo(root + QStringLiteral("/app/") + id + QStringLiteral("/current")).exists())
            return true;
    }
    return false;
}

void GameModeSettings::refresh()
{
    read();
    askWindowsKinds();
    askWindowsPrograms();
    Q_EMIT changed();
}

void GameModeSettings::setWindowsPrograms(const QByteArray &json)
{
    m_windowsPrograms.clear();
    const QJsonArray programs = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &value : programs) {
        const QJsonObject o = value.toObject();
        const QString space = o.value(QStringLiteral("space")).toString();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!validKey(QStringLiteral("windows:") + space) || m_windowsPrograms.contains(space))
            continue;
        m_windowsPrograms.insert(space, name);
    }
    Q_EMIT changed();
}

void GameModeSettings::askWindowsPrograms()
{
    const QString lister = env("RAYE_WINDOWS_LIST", QStringLiteral("/usr/bin/raye-windows"));
    if (!QFileInfo(lister).isFile() || !QFileInfo(lister).isExecutable() || QDir(windowsRoot()).isEmpty(QDir::Dirs | QDir::NoDotAndDotDot))
        return;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0)
            setWindowsPrograms(process->readAllStandardOutput());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            process->deleteLater();
    });
    process->start(lister, {QStringLiteral("list"), QStringLiteral("--json")});
}

void GameModeSettings::setWindowsKinds(const QByteArray &json)
{
    m_windowsKinds.clear();
    m_windowsNames.clear();
    const QJsonObject spaces = QJsonDocument::fromJson(json).object();
    for (auto it = spaces.begin(); it != spaces.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        const QString kind = o.value(QStringLiteral("game")).toString();
        if (kind == QLatin1String("yes") || kind == QLatin1String("launcher"))
            m_windowsKinds.insert(it.key(), kind);
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            m_windowsNames.insert(it.key(), name);
    }
    Q_EMIT changed();
}

void GameModeSettings::askWindowsKinds()
{
    const QString reader = env("RAYE_WINDOWS_PROFILE", QStringLiteral("/usr/libexec/raye-windows-profile"));
    if (!QFileInfo(reader).isFile() || !QFileInfo(reader).isExecutable() || QDir(windowsRoot()).isEmpty(QDir::Dirs | QDir::NoDotAndDotDot))
        return;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0)
            setWindowsKinds(process->readAllStandardOutput());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            process->deleteLater();
    });
    process->start(reader, {});
}
