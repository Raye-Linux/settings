// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"

#include "performance.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QCollator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {

const QString night = QStringLiteral("org.raye.night.desktop");
const QString day = QStringLiteral("org.raye.day.desktop");
const QColor rayeViolet(0x7b, 0x3c, 0xff);

KSharedConfigPtr globals()
{
    return KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
}

QString localized(const QJsonObject &plugin, const QString &key)
{
    const QString language = QLocale().name().section(QLatin1Char('_'), 0, 0);
    const QString translated = plugin.value(QStringLiteral("%1[%2]").arg(key, language)).toString();
    return translated.isEmpty() ? plugin.value(key).toString() : translated;
}

int rank(const QString &id)
{
    if (id == night)
        return 0;
    if (id == day)
        return 1;
    return id.startsWith(QLatin1String("org.raye.")) ? 2 : 3;
}

} // namespace

AppearanceSettings::AppearanceSettings(QObject *parent)
    : QObject(parent)
{
    reload();
}

QStringList AppearanceSettings::swatchColors()
{
    // six by four, every one readable under white text (contrast 4:1 or more)
    return {QStringLiteral("#7b3cff"), QStringLiteral("#9450f0"), QStringLiteral("#b23ae0"), QStringLiteral("#c42e9a"),
            QStringLiteral("#d6336c"), QStringLiteral("#d63c3c"), QStringLiteral("#c75a1a"), QStringLiteral("#b06a0a"),
            QStringLiteral("#8f7a0e"), QStringLiteral("#3f8f3a"), QStringLiteral("#16866b"), QStringLiteral("#0f7f93"),
            QStringLiteral("#2672d9"), QStringLiteral("#3d55e6"), QStringLiteral("#5a49d6"), QStringLiteral("#7a5ac0"),
            QStringLiteral("#9a4fb3"), QStringLiteral("#a94a7f"), QStringLiteral("#5f6b7d"), QStringLiteral("#786353"),
            QStringLiteral("#3d7a52"), QStringLiteral("#2e6f99"), QStringLiteral("#8a5530"), QStringLiteral("#645878")};
}

QVariantList AppearanceSettings::swatches() const
{
    QVariantList list;
    for (const QString &c : swatchColors())
        list << QColor(c);
    return list;
}

int AppearanceSettings::swatchIndex(const QColor &color) const
{
    const QStringList colors = swatchColors();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor s(colors.at(i));
        if (qAbs(s.red() - color.red()) + qAbs(s.green() - color.green()) + qAbs(s.blue() - color.blue()) <= 6)
            return i;
    }
    return -1;
}

QVariantList AppearanceSettings::scanThemes(const QStringList &folders, const QString &current)
{
    struct Theme {
        QString id, name, description, preview;
    };
    QList<Theme> themes;
    QStringList seen;
    for (const QString &folder : folders) {
        const QDir dir(folder);
        for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QFile f(dir.filePath(entry + QLatin1String("/metadata.json")));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QJsonObject plugin = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("KPlugin")).toObject();
            if (plugin.isEmpty())
                continue;
            const QString id = plugin.value(QStringLiteral("Id")).toString(entry);
            if (id.isEmpty() || seen.contains(id))
                continue;
            seen << id;
            const QString preview = dir.filePath(entry + QLatin1String("/contents/previews/preview.png"));
            themes << Theme{id, localized(plugin, QStringLiteral("Name")), localized(plugin, QStringLiteral("Description")),
                            QFileInfo::exists(preview) ? preview : QString()};
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(themes.begin(), themes.end(), [&collator](const Theme &a, const Theme &b) {
        if (rank(a.id) != rank(b.id))
            return rank(a.id) < rank(b.id);
        return collator.compare(a.name, b.name) < 0;
    });
    QVariantList list;
    for (const Theme &t : std::as_const(themes)) {
        list << QVariantMap{{QStringLiteral("id"), t.id},
                            {QStringLiteral("name"), t.name.isEmpty() ? t.id : t.name},
                            {QStringLiteral("description"), t.description},
                            {QStringLiteral("preview"), t.preview.isEmpty() ? QUrl() : QUrl::fromLocalFile(t.preview)},
                            {QStringLiteral("current"), t.id == current},
                            {QStringLiteral("raye"), rank(t.id) < 3}};
    }
    return list;
}

QString AppearanceSettings::modeOf(const QString &scheme, const QColor &window)
{
    if (scheme.contains(QLatin1String("Day")) || scheme.contains(QLatin1String("Light")))
        return QStringLiteral("light");
    if (scheme.contains(QLatin1String("Night")) || scheme.contains(QLatin1String("Dark")))
        return QStringLiteral("dark");
    return window.isValid() && window.lightnessF() > 0.5 ? QStringLiteral("light") : QStringLiteral("dark");
}

QString AppearanceSettings::themeForMode(const QString &mode)
{
    if (mode == QLatin1String("light"))
        return day;
    if (mode == QLatin1String("dark"))
        return night;
    return {};
}

void AppearanceSettings::reload()
{
    globals()->reparseConfiguration();
    QStringList folders;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        folders << dir + QLatin1String("/plasma/look-and-feel");
    m_themes = scanThemes(folders, currentTheme());
    Q_EMIT changed();
}

QString AppearanceSettings::currentTheme() const
{
    return globals()->group(QStringLiteral("KDE")).readEntry("LookAndFeelPackage", night);
}

QString AppearanceSettings::mode() const
{
    const KSharedConfigPtr g = globals();
    return modeOf(g->group(QStringLiteral("General")).readEntry("ColorScheme", QStringLiteral("RayeNight")),
                  g->group(QStringLiteral("Colors:Window")).readEntry("BackgroundNormal", QColor()));
}

QColor AppearanceSettings::accentOf(const KConfigGroup &general, const KConfigGroup &selection)
{
    const QColor accent = general.readEntry("AccentColor", QColor());
    if (accent.isValid())
        return accent;
    return selection.readEntry("BackgroundNormal", rayeViolet);
}

QColor AppearanceSettings::accentColor() const
{
    const KSharedConfigPtr g = globals();
    return accentOf(g->group(QStringLiteral("General")), g->group(QStringLiteral("Colors:Selection")));
}

bool AppearanceSettings::accentFromWallpaper() const
{
    return globals()->group(QStringLiteral("General")).readEntry("accentColorFromWallpaper", false);
}

bool AppearanceSettings::transparency() const
{
    return globals()->group(QStringLiteral("Raye")).readEntry("TransparencyEffects", true);
}

bool AppearanceSettings::accentOnBorders() const
{
    return KSharedConfig::openConfig(QStringLiteral("rayerc"))->group(QStringLiteral("Windeco")).readEntry("AccentBorder", false);
}

bool AppearanceSettings::rayeBorders() const
{
    return KSharedConfig::openConfig(QStringLiteral("kwinrc"))
               ->group(QStringLiteral("org.kde.kdecoration2"))
               .readEntry("library", QStringLiteral("org.raye.decoration"))
        == QLatin1String("org.raye.decoration");
}

bool AppearanceSettings::applyTheme(const QString &id)
{
    bool known = false;
    for (const QVariant &t : std::as_const(m_themes))
        known = known || t.toMap().value(QStringLiteral("id")).toString() == id;
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("plasma-apply-lookandfeel"));
    if (!known || m_applying || tool.isEmpty())
        return false;

    // a theme brings its own accent: keep the one the user chose
    const bool automatic = accentFromWallpaper();
    const QColor accent = accentColor();
    m_applying = true;
    Q_EMIT changed();
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, automatic, accent] {
        process->deleteLater();
        globals()->reparseConfiguration();
        m_applying = false;
        if (automatic) {
            setAccentFromWallpaper(true);
        } else if (accent != accentColor()) {
            applyAccent(accent);
        }
        reload();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        m_applying = false;
        Q_EMIT changed();
    });
    process->start(tool, {QStringLiteral("-a"), id});
    return true;
}

bool AppearanceSettings::setMode(const QString &mode)
{
    const QString theme = themeForMode(mode);
    if (theme.isEmpty() || theme == currentTheme())
        return false;
    return applyTheme(theme);
}

void AppearanceSettings::applyAccent(const QColor &color)
{
    QStringList arguments{QStringLiteral("apply"), color.name(QColor::HexRgb)};
    QString tool = QStandardPaths::findExecutable(QStringLiteral("raye-accent"), {QStringLiteral("/usr/libexec")});
    if (tool.isEmpty()) {
        tool = QStandardPaths::findExecutable(QStringLiteral("plasma-apply-colorscheme"));
        arguments = {QStringLiteral("--accent-color"), color.name(QColor::HexRgb)};
    }
    if (tool.isEmpty()) {
        // no session tool (tests): the key alone
        KConfigGroup g = globals()->group(QStringLiteral("General"));
        g.writeEntry("AccentColor", color, KConfig::Notify);
        globals()->sync();
        Q_EMIT changed();
        return;
    }
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        globals()->reparseConfiguration();
        Q_EMIT changed();
    });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->deleteLater();
    });
    process->start(tool, arguments);
}

void AppearanceSettings::setAccentColor(const QColor &color)
{
    if (!color.isValid())
        return;
    KConfigGroup g = globals()->group(QStringLiteral("General"));
    g.writeEntry("accentColorFromWallpaper", false, KConfig::Notify);
    globals()->sync();
    applyAccent(color);
    Q_EMIT changed();
}

void AppearanceSettings::setAccentFromWallpaper(bool automatic)
{
    KConfigGroup g = globals()->group(QStringLiteral("General"));
    g.writeEntry("accentColorFromWallpaper", automatic, KConfig::Notify);
    globals()->sync();
    // back to manual: the colour shown now stays
    if (!automatic)
        applyAccent(accentColor());
    Q_EMIT changed();
}

void AppearanceSettings::setTransparency(bool on)
{
    PerformanceSettings::writeTransparency(on);
    reconfigureCompositor();
    Q_EMIT changed();
}

void AppearanceSettings::setAccentOnBorders(bool on)
{
    KSharedConfigPtr rayerc = KSharedConfig::openConfig(QStringLiteral("rayerc"));
    KConfigGroup g = rayerc->group(QStringLiteral("Windeco"));
    g.writeEntry("AccentBorder", on, KConfig::Notify);
    rayerc->sync();
    // the window borders read their file again on this signal
    QDBusConnection::sessionBus().send(
        QDBusMessage::createSignal(QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reloadConfig")));
    Q_EMIT changed();
}

void AppearanceSettings::reconfigureCompositor()
{
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure")));
}
