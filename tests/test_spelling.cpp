// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Typing > Spelling: what reaches the spell checker's
// own settings file (KDE/Sonnet.conf), and the dictionaries offered.
#include "spelling.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace
{
void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
}
}

class TestSpelling : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
        touch(m_dir.filePath(QStringLiteral("dicts/en_US.dic")));
        touch(m_dir.filePath(QStringLiteral("dicts/en_US.aff")));
        touch(m_dir.filePath(QStringLiteral("dicts/fr_FR.dic")));
        touch(m_dir.filePath(QStringLiteral("dicts/fr_FR.aff")));
        touch(m_dir.filePath(QStringLiteral("dicts/hyph_fr.dic"))); // hyphenation, not a dictionary
        touch(m_dir.filePath(QStringLiteral("dicts/de_DE.dic")));    // no .aff: unusable
        touch(m_dir.filePath(QStringLiteral("plugins/sonnet_hunspell.so")));
    }

    void dictionaries()
    {
        const QStringList codes = SpellingSettings::dictionariesIn({m_dir.filePath(QStringLiteral("dicts")), QStringLiteral("/nonexistent")});
        QCOMPARE(codes, (QStringList{QStringLiteral("en_US"), QStringLiteral("fr_FR")}));
    }

    void settings()
    {
        SpellingSettings s;
        s.setDictionaryDirs({m_dir.filePath(QStringLiteral("dicts"))});
        s.setPluginDir(m_dir.filePath(QStringLiteral("plugins")));
        QVERIFY(s.available());
        QCOMPARE(s.languages().size(), 2);
        QVERIFY(!s.highlight());
        QCOMPARE(s.language(), QString()); // detect automatically

        s.setHighlight(true);
        s.setLanguage(QStringLiteral("fr_FR"));
        s.setLanguage(QStringLiteral("xx_YY")); // not installed: refused
        QCOMPARE(s.language(), QStringLiteral("fr_FR"));

        const QString file = m_dir.filePath(QStringLiteral("config/KDE/Sonnet.conf"));
        QVERIFY(QFile::exists(file));
        QSettings conf(file, QSettings::IniFormat);
        QCOMPARE(conf.value(QStringLiteral("checkerEnabledByDefault")).toBool(), true);
        QCOMPARE(conf.value(QStringLiteral("defaultLanguage")).toString(), QStringLiteral("fr_FR"));
        QCOMPARE(conf.value(QStringLiteral("autodetectLanguage")).toBool(), false);

        s.setLanguage(QString());
        QSettings again(file, QSettings::IniFormat);
        QCOMPARE(again.value(QStringLiteral("autodetectLanguage")).toBool(), true);

        SpellingSettings other;
        other.setDictionaryDirs({m_dir.filePath(QStringLiteral("dicts"))});
        QVERIFY(other.highlight());
        QCOMPARE(other.language(), QString());
    }

    void noPluginNoSetting()
    {
        SpellingSettings s;
        s.setDictionaryDirs({m_dir.filePath(QStringLiteral("dicts"))});
        s.setPluginDir(m_dir.filePath(QStringLiteral("nothing-here")));
        QVERIFY(!s.available());
    }
};

QTEST_GUILESS_MAIN(TestSpelling)
#include "test_spelling.moc"
