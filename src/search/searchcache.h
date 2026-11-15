// SPDX-License-Identifier: GPL-3.0-or-later
// The keywords of the settings modules, kept on disk for the Start menu
// runner. KPluginMetaData reads them by loading the plugin libraries,
// which leaves dozens of libraries (and what they link) in the memory
// of whoever asked: the runner reads this file instead, and a short-
// lived copy of itself writes it again when one of the files it was
// made from changes.
#pragma once

#include "searchindex.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

using ModuleKeywordMap = QHash<QString, ModuleKeywords>;

// ~/.cache/raye-settings/module-keywords
QString moduleKeywordFile();
// What the copy depends on: the layout and search folders the installed
// Settings reads, and the folders of the settings modules.
QStringList moduleKeywordRoots();
// Changes when a root, or what is up to two folders below it, changes,
// appears or goes away. Only stat() calls, nothing is read or loaded.
QString moduleKeywordStamp(const QStringList &roots);

// A file written from other sources (another stamp), cut short or of
// another format is not read.
bool writeModuleKeywords(const QString &file, const QString &stamp, const ModuleKeywordMap &words);
std::optional<ModuleKeywordMap> readModuleKeywords(const QString &file, const QString &stamp);

// The copy in `file` when it was made from `stamp`. Otherwise `rebuild`
// runs once (it is expected to write the file) and the file is read
// again; nothing when it still does not match.
std::optional<ModuleKeywordMap> cachedModuleKeywords(const QString &file, const QString &stamp,
                                                     const std::function<void()> &rebuild);

// Builds an index from `sources` and keeps every module it asked about
// with the answer, so the copy holds exactly what the index uses.
ModuleKeywordMap recordModuleKeywords(const SearchIndex::Sources &sources);
// Sources::moduleKeywords answered from a copy.
std::function<ModuleKeywords(const QString &)> moduleKeywordsFrom(const ModuleKeywordMap &words);
