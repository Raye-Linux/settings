// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Fonts: reading fontconfig's list, telling a font
// file by its bytes, installing into a fonts folder of the test's own
// and removing only what is in it.
#include "fonts.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

// the first bytes of a font of each kind, padded
QByteArray font(const QByteArray &magic, char fill = 'x')
{
    return magic + QByteArray(60, fill);
}

} // namespace

class TestFonts : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_fonts;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        // no fontconfig tools: nothing outside the test is touched
        qputenv("PATH", "/nonexistent");
        m_fonts = FontSettings::userFolder();
        QCOMPARE(m_fonts, m_dir.filePath(QStringLiteral("data/fonts")));
    }

    void init()
    {
        QDir(m_fonts).removeRecursively();
    }

    void parseList()
    {
        const QByteArray out = "Inter\tRegular\t/usr/share/fonts/inter/Inter-Regular.otf\n"
                               "Inter\tBold\t/usr/share/fonts/inter/Inter-Bold.otf\n"
                               "inter\tBold\t/usr/share/fonts/inter/Inter-Bold.otf\n"
                               ".Hidden\tRegular\t/x.ttf\n"
                               "Mine\tRegular\t" + m_fonts.toUtf8() + "/mine.ttf\n"
                               "Mixed\tRegular\t" + m_fonts.toUtf8() + "/mixed.ttf\n"
                               "Mixed\tBold\t/usr/share/fonts/mixed-bold.ttf\n"
                               "broken line\n";
        const QList<FontFamily> list = FontSettings::parseList(out, m_fonts);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).family, QStringLiteral("Inter"));
        QCOMPARE(list.at(0).styles, (QStringList{QStringLiteral("Regular"), QStringLiteral("Bold")}));
        QCOMPARE(list.at(0).files.size(), 2);
        QVERIFY(!list.at(0).user);
        QCOMPARE(list.at(1).family, QStringLiteral("Mine"));
        QVERIFY(list.at(1).user);
        // a family partly of the system is not the user's to remove
        QVERIFY(!list.at(2).user);
    }

    void fontFiles()
    {
        const QString dir = m_dir.filePath(QStringLiteral("in"));
        writeFile(dir + QLatin1String("/a.ttf"), font(QByteArray("\x00\x01\x00\x00", 4)));
        writeFile(dir + QLatin1String("/b.otf"), font("OTTO"));
        writeFile(dir + QLatin1String("/c.ttc"), font("ttcf"));
        writeFile(dir + QLatin1String("/fake.ttf"), font("<htm"));
        writeFile(dir + QLatin1String("/tiny.ttf"), "OTTO");
        QVERIFY(FontSettings::isFontFile(dir + QLatin1String("/a.ttf")));
        QVERIFY(FontSettings::isFontFile(dir + QLatin1String("/b.otf")));
        QVERIFY(FontSettings::isFontFile(dir + QLatin1String("/c.ttc")));
        QVERIFY(!FontSettings::isFontFile(dir + QLatin1String("/fake.ttf")));
        QVERIFY(!FontSettings::isFontFile(dir + QLatin1String("/tiny.ttf")));
        QVERIFY(!FontSettings::isFontFile(dir));
    }

    void copyNamesAndDuplicates()
    {
        const QString dir = m_dir.filePath(QStringLiteral("in2"));
        writeFile(dir + QLatin1String("/Nice.ttf"), font("OTTO", 'a'));
        writeFile(dir + QLatin1String("/other/Nice.ttf"), font("OTTO", 'b'));
        writeFile(dir + QLatin1String("/.hidden.otf"), font("OTTO", 'c'));

        QCOMPARE(FontSettings::copyFont(dir + QLatin1String("/Nice.ttf"), m_fonts), QStringLiteral("Nice.ttf"));
        // the same file again: no second copy
        QCOMPARE(FontSettings::copyFont(dir + QLatin1String("/Nice.ttf"), m_fonts), QStringLiteral("Nice.ttf"));
        // another font with that name keeps both
        QCOMPARE(FontSettings::copyFont(dir + QLatin1String("/other/Nice.ttf"), m_fonts), QStringLiteral("Nice-2.ttf"));
        QCOMPARE(FontSettings::copyFont(dir + QLatin1String("/.hidden.otf"), m_fonts), QStringLiteral("hidden.otf"));
        QCOMPARE(QDir(m_fonts).entryList(QDir::Files).size(), 3);
    }

    void installAndUninstall()
    {
        const QString dir = m_dir.filePath(QStringLiteral("drop"));
        writeFile(dir + QLatin1String("/One.ttf"), font("OTTO", 'o'));
        writeFile(dir + QLatin1String("/sub/Two.otf"), font("OTTO", 't'));
        writeFile(dir + QLatin1String("/readme.txt"), "not a font, plenty long enough");

        FontSettings fonts;
        QSignalSpy refused(&fonts, &FontSettings::refused);
        QCOMPARE(fonts.install({QUrl::fromLocalFile(dir), QUrl(QStringLiteral("https://example.org/x.ttf"))}), 2);
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused.first().first().toStringList(), QStringList{QStringLiteral("readme.txt")});
        QVERIFY(QFile::exists(m_fonts + QLatin1String("/One.ttf")));
        QVERIFY(QFile::exists(m_fonts + QLatin1String("/Two.otf")));

        // only files inside the folder are removed
        FontFamily mixed{QStringLiteral("Mixed"), {}, {m_fonts + QLatin1String("/One.ttf"), dir + QLatin1String("/sub/Two.otf")}, false};
        QCOMPARE(FontSettings::removableFiles(mixed, m_fonts), QStringList{m_fonts + QLatin1String("/One.ttf")});
        FontFamily sneaky{QStringLiteral("Sneaky"), {}, {m_fonts + QLatin1String("/../drop/One.ttf")}, true};
        QVERIFY(FontSettings::removableFiles(sneaky, m_fonts).isEmpty());
        QVERIFY(!fonts.uninstall(QStringLiteral("Not installed")));
    }
};

QTEST_MAIN(TestFonts)
#include "test_fonts.moc"
