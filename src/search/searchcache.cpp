// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchcache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// "RSMK", then the format; a new format makes old files stale
constexpr quint32 cacheMagic = 0x52534d4b;
constexpr quint32 cacheFormat = 1;
// a layout names a few dozen modules; a broken count must not allocate gigabytes
constexpr qint32 maxModules = 10000;
// root, its folders, their files: plasma/kcms/<namespace>/kcm_x.so
constexpr int stampDepth = 2;

void stampOf(const QString &path, int depth, QCryptographicHash &hash)
{
    const QFileInfo info(path);
    hash.addData(path.toUtf8());
    if (!info.exists()) {
        hash.addData(":-\n");
        return;
    }
    const QByteArray line = ':' + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ':'
        + QByteArray::number(info.isDir() ? -1 : info.size()) + '\n';
    hash.addData(line);
    if (!info.isDir() || depth <= 0)
        return;
    const QStringList children = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                      QDir::Name);
    for (const QString &child : children)
        stampOf(path + QLatin1Char('/') + child, depth - 1, hash);
}

} // namespace

QString moduleKeywordFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/raye-settings/module-keywords");
}

QStringList moduleKeywordRoots()
{
    QStringList roots = SearchIndex::installedSourcePaths();
    // where KPluginMetaData looks for the modules
    for (const QString &dir : QCoreApplication::libraryPaths())
        roots.append(dir + QStringLiteral("/plasma/kcms"));
    return roots;
}

QString moduleKeywordStamp(const QStringList &roots)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &root : roots)
        stampOf(root, stampDepth, hash);
    return QString::fromLatin1(hash.result().toHex());
}

bool writeModuleKeywords(const QString &file, const QString &stamp, const ModuleKeywordMap &words)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&out);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << cacheMagic << cacheFormat << stamp << qint32(words.size());
    // sorted, so the same modules give the same file
    QStringList modules = words.keys();
    modules.sort();
    for (const QString &module : std::as_const(modules)) {
        const ModuleKeywords &k = words[module];
        stream << module << k.english << k.french;
    }
    if (stream.status() != QDataStream::Ok) {
        out.cancelWriting();
        return false;
    }
    return out.commit();
}

std::optional<ModuleKeywordMap> readModuleKeywords(const QString &file, const QString &stamp)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return std::nullopt;
    QDataStream stream(&in);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint32 format = 0;
    QString builtFrom;
    qint32 count = -1;
    stream >> magic >> format;
    if (stream.status() != QDataStream::Ok || magic != cacheMagic || format != cacheFormat)
        return std::nullopt;
    stream >> builtFrom >> count;
    if (stream.status() != QDataStream::Ok || builtFrom != stamp || count < 0 || count > maxModules)
        return std::nullopt;
    ModuleKeywordMap words;
    words.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        QString module;
        ModuleKeywords k;
        stream >> module >> k.english >> k.french;
        if (stream.status() != QDataStream::Ok)
            return std::nullopt;
        words.insert(module, k);
    }
    if (!stream.atEnd())
        return std::nullopt;
    return words;
}

std::optional<ModuleKeywordMap> cachedModuleKeywords(const QString &file, const QString &stamp,
                                                     const std::function<void()> &rebuild)
{
    if (auto words = readModuleKeywords(file, stamp))
        return words;
    rebuild();
    return readModuleKeywords(file, stamp);
}

ModuleKeywordMap recordModuleKeywords(const SearchIndex::Sources &sources)
{
    ModuleKeywordMap seen;
    SearchIndex::Sources recording = sources;
    recording.moduleKeywords = [&seen, ask = sources.moduleKeywords](const QString &module) {
        const ModuleKeywords k = ask ? ask(module) : ModuleKeywords();
        seen.insert(module, k);
        return k;
    };
    SearchIndex index;
    index.load(recording);
    return seen;
}

std::function<ModuleKeywords(const QString &)> moduleKeywordsFrom(const ModuleKeywordMap &words)
{
    return [words](const QString &module) {
        return words.value(module);
    };
}
