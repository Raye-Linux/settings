// SPDX-License-Identifier: GPL-3.0-or-later
#include "fonts.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace {

// a dropped folder is searched this deep, for this many files at most
constexpr int maxDroppedFiles = 500;
// no font is anywhere near this big
constexpr qint64 maxFontSize = 64LL * 1024 * 1024;

QByteArray digest(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return hash.result();
}

bool inside(const QString &path, const QString &folder)
{
    const QString file = QFileInfo(path).canonicalFilePath();
    const QString root = QFileInfo(folder).canonicalFilePath();
    return !file.isEmpty() && !root.isEmpty() && file.startsWith(root + QLatin1Char('/'));
}

} // namespace

FontSettings::FontSettings(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

QString FontSettings::userFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/fonts");
}

QList<FontFamily> FontSettings::parseList(const QByteArray &output, const QString &userFolder)
{
    QMap<QString, FontFamily> byKey;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &raw : lines) {
        const QStringList fields = QString::fromUtf8(raw).split(QLatin1Char('\t'));
        if (fields.size() < 3)
            continue;
        const QString name = fields.at(0).trimmed();
        // fonts whose names start with a dot are hidden on purpose
        if (name.isEmpty() || name.startsWith(QLatin1Char('.')))
            continue;
        FontFamily &f = byKey[name.toCaseFolded()];
        if (f.family.isEmpty())
            f.family = name;
        const QString style = fields.at(1).trimmed();
        if (!style.isEmpty() && !f.styles.contains(style))
            f.styles << style;
        const QString file = fields.at(2).trimmed();
        if (!file.isEmpty() && !f.files.contains(file))
            f.files << file;
    }
    QList<FontFamily> list;
    const QString prefix = QDir::cleanPath(userFolder) + QLatin1Char('/');
    for (FontFamily f : std::as_const(byKey)) {
        f.user = !f.files.isEmpty() && std::all_of(f.files.cbegin(), f.files.cend(), [&prefix](const QString &file) {
            return QDir::cleanPath(file).startsWith(prefix);
        });
        list << f;
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const FontFamily &a, const FontFamily &b) {
        return collator.compare(a.family, b.family) < 0;
    });
    return list;
}

bool FontSettings::isFontFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() < 12 || info.size() > maxFontSize)
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(4);
    return head == QByteArray("\x00\x01\x00\x00", 4) // TrueType
        || head == "OTTO"                              // OpenType with CFF outlines
        || head == "true"                              // old Apple TrueType
        || head == "ttcf";                             // collection (.ttc, .otc)
}

QString FontSettings::copyFont(const QString &path, const QString &folder)
{
    if (!isFontFile(path) || !QDir().mkpath(folder))
        return {};
    // the name only, never a path; a hidden or empty name gets a plain one
    QString name = QFileInfo(path).fileName();
    while (name.startsWith(QLatin1Char('.')))
        name.remove(0, 1);
    if (name.isEmpty())
        name = QStringLiteral("font.ttf");
    const QString base = QFileInfo(name).completeBaseName();
    const QString suffix = QFileInfo(name).suffix();
    QString target = folder + QLatin1Char('/') + name;
    const QByteArray sum = digest(path);
    for (int n = 2; QFileInfo::exists(target); ++n) {
        // the same font again: nothing to do
        if (digest(target) == sum)
            return QFileInfo(target).fileName();
        target = folder + QLatin1Char('/') + base + QLatin1Char('-') + QString::number(n)
            + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
    }
    if (!QFile::copy(path, target))
        return {};
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    return QFileInfo(target).fileName();
}

QStringList FontSettings::removableFiles(const FontFamily &family, const QString &folder)
{
    QStringList files;
    for (const QString &file : family.files) {
        const QFileInfo info(file);
        if (info.isFile() && !info.isSymLink() && inside(file, folder))
            files << file;
    }
    return files;
}

int FontSettings::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant FontSettings::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const FontFamily &f = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case FamilyRole:
        return f.family;
    case StylesRole:
        return f.styles;
    case FaceCountRole:
        return qMax(1, int(f.styles.size()));
    case UserRole:
        return f.user;
    }
    return {};
}

QHash<int, QByteArray> FontSettings::roleNames() const
{
    return {{FamilyRole, "family"}, {StylesRole, "styles"}, {FaceCountRole, "faceCount"}, {UserRole, "userFont"}};
}

void FontSettings::setSearchText(const QString &text)
{
    if (text == m_search)
        return;
    m_search = text;
    filter();
    Q_EMIT searchTextChanged();
}

void FontSettings::filter()
{
    const int before = m_rows.size();
    beginResetModel();
    m_rows.clear();
    const QString needle = m_search.trimmed();
    for (const FontFamily &f : std::as_const(m_all)) {
        if (needle.isEmpty() || f.family.contains(needle, Qt::CaseInsensitive))
            m_rows << f;
    }
    endResetModel();
    if (before != m_rows.size())
        Q_EMIT countChanged();
}

void FontSettings::reload()
{
    const QString fcList = QStandardPaths::findExecutable(QStringLiteral("fc-list"));
    if (fcList.isEmpty()) {
        // no fontconfig tool: what Qt knows, without files
        m_all.clear();
        const QStringList families = QFontDatabase::families();
        for (const QString &family : families) {
            if (!QFontDatabase::isPrivateFamily(family))
                m_all << FontFamily{family, QFontDatabase::styles(family), {}, false};
        }
        filter();
        Q_EMIT countChanged();
        return;
    }
    m_loading = true;
    Q_EMIT loadingChanged();
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        m_all = parseList(process->readAllStandardOutput(), userFolder());
        process->deleteLater();
        m_loading = false;
        filter();
        Q_EMIT countChanged();
        Q_EMIT loadingChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        m_loading = false;
        Q_EMIT loadingChanged();
    });
    process->start(fcList, {QStringLiteral("--format=%{family[0]}\\t%{style[0]}\\t%{file}\\n")});
}

void FontSettings::refreshCache()
{
    const QString fcCache = QStandardPaths::findExecutable(QStringLiteral("fc-cache"));
    if (fcCache.isEmpty()) {
        reload();
        return;
    }
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        reload();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        reload();
    });
    process->start(fcCache, {QStringLiteral("-f"), userFolder()});
}

int FontSettings::install(const QList<QUrl> &urls)
{
    QStringList candidates;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isDir()) {
            QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && candidates.size() < maxDroppedFiles)
                candidates << it.next();
        } else {
            candidates << path;
        }
    }
    const QString folder = userFolder();
    int installed = 0;
    QStringList refusedNames;
    for (const QString &path : std::as_const(candidates)) {
        const QString name = copyFont(path, folder);
        if (name.isEmpty()) {
            refusedNames << QFileInfo(path).fileName();
            continue;
        }
        ++installed;
        // the previews of this window can use it before fontconfig is told
        QFontDatabase::addApplicationFont(folder + QLatin1Char('/') + name);
    }
    if (!refusedNames.isEmpty())
        Q_EMIT refused(refusedNames);
    if (installed > 0)
        refreshCache();
    return installed;
}

QVariantMap FontSettings::family(const QString &family) const
{
    for (const FontFamily &f : m_all) {
        if (f.family == family) {
            return {{QStringLiteral("family"), f.family},
                    {QStringLiteral("styles"), f.styles},
                    {QStringLiteral("userFont"), f.user},
                    {QStringLiteral("faceCount"), qMax(1, int(f.styles.size()))}};
        }
    }
    return {};
}

bool FontSettings::uninstall(const QString &family)
{
    for (const FontFamily &f : std::as_const(m_all)) {
        if (f.family != family)
            continue;
        const QStringList files = removableFiles(f, userFolder());
        if (files.isEmpty())
            return false;
        bool ok = true;
        for (const QString &file : files)
            ok = QFile::remove(file) && ok;
        refreshCache();
        return ok;
    }
    return false;
}
