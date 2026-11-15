// SPDX-License-Identifier: GPL-3.0-or-later
#include "soundtheme.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

void write(const QString &path, const QByteArray &text)
{
    QDir().mkpath(path.section(QLatin1Char('/'), 0, -2));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

} // namespace

class TestSoundTheme : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void listsThemesByName()
    {
        QTemporaryDir dir;
        const QString sounds = dir.path();
        write(sounds + QStringLiteral("/raye/index.theme"),
              "[Sound Theme]\nName=Raye\nComment=The sounds of Raye Linux\nComment[fr]=Les sons de Raye Linux\n"
              "Inherits=freedesktop\nDirectories=stereo\nExample=theme-demo\n");
        write(sounds + QStringLiteral("/ocean/index.theme"), "[Sound Theme]\nName=Ocean\nName[fr]=Océan\nDirectories=stereo\n");
        write(sounds + QStringLiteral("/secret/index.theme"), "[Sound Theme]\nName=Secret\nHidden=true\n");
        QDir().mkpath(sounds + QStringLiteral("/not-a-theme"));

        const QVariantList fr = SoundTheme::themesIn({sounds}, QStringLiteral("fr"));
        QCOMPARE(fr.size(), 2);
        QCOMPARE(fr.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Océan"));
        QCOMPARE(fr.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("raye"));
        QCOMPARE(fr.at(1).toMap().value(QStringLiteral("comment")).toString(), QStringLiteral("Les sons de Raye Linux"));
    }

    void userThemeHidesTheSystemOne()
    {
        QTemporaryDir mine;
        QTemporaryDir system;
        write(mine.path() + QStringLiteral("/raye/index.theme"), "[Sound Theme]\nName=My Raye\n");
        write(system.path() + QStringLiteral("/raye/index.theme"), "[Sound Theme]\nName=Raye\n");
        const QVariantList list = SoundTheme::themesIn({mine.path(), system.path()}, QStringLiteral("en"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("My Raye"));
    }

    void findsTheSampleThroughInherits()
    {
        QTemporaryDir dir;
        const QString sounds = dir.path();
        write(sounds + QStringLiteral("/raye/index.theme"),
              "[Sound Theme]\nName=Raye\nInherits=freedesktop\nDirectories=stereo\nExample=theme-demo\n");
        write(sounds + QStringLiteral("/raye/stereo/theme-demo.oga"), "x");
        write(sounds + QStringLiteral("/plain/index.theme"), "[Sound Theme]\nName=Plain\nInherits=freedesktop\nDirectories=stereo\n");
        write(sounds + QStringLiteral("/freedesktop/index.theme"), "[Sound Theme]\nName=Default\nDirectories=stereo\n");
        write(sounds + QStringLiteral("/freedesktop/stereo/bell.oga"), "x");

        QCOMPARE(SoundTheme::exampleOf({sounds}, QStringLiteral("raye")), sounds + QStringLiteral("/raye/stereo/theme-demo.oga"));
        QCOMPARE(SoundTheme::exampleOf({sounds}, QStringLiteral("plain")), sounds + QStringLiteral("/freedesktop/stereo/bell.oga"));
        QCOMPARE(SoundTheme::soundFile({sounds}, QStringLiteral("raye"), QStringLiteral("bell")),
                 sounds + QStringLiteral("/freedesktop/stereo/bell.oga"));
        QVERIFY(SoundTheme::exampleOf({sounds}, QStringLiteral("missing")).isEmpty());
    }

    void inheritsLoopEnds()
    {
        QTemporaryDir dir;
        write(dir.path() + QStringLiteral("/a/index.theme"), "[Sound Theme]\nInherits=b\nDirectories=stereo\n");
        write(dir.path() + QStringLiteral("/b/index.theme"), "[Sound Theme]\nInherits=a\nDirectories=stereo\n");
        QVERIFY(SoundTheme::soundFile({dir.path()}, QStringLiteral("a"), QStringLiteral("bell")).isEmpty());
    }

    void writesKdeglobals()
    {
        QTemporaryDir home;
        qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
        SoundTheme sounds;
        QCOMPARE(sounds.theme(), QStringLiteral("ocean"));
        QVERIFY(sounds.enabled());
        sounds.setTheme(QStringLiteral("raye"));
        sounds.setEnabled(false);
        QFile f(home.path() + QStringLiteral("/kdeglobals"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY2(text.contains("[Sounds]") && text.contains("Theme=raye") && text.contains("Enable=false"), text.constData());
    }

    void volumeChimeGoesToTheShell()
    {
        QTemporaryDir home;
        qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
        SoundTheme sounds;
        QVERIFY(sounds.volumeChime());
        sounds.setVolumeChime(false);
        QFile f(home.path() + QStringLiteral("/rayeshellrc"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY2(text.contains("[Sound]") && text.contains("VolumeChime=false"), text.constData());
        QVERIFY(!sounds.playVolumeChime());
        sounds.setVolumeChime(true);
        QVERIFY(sounds.volumeChime());
    }

    void soundPageHasTheSwitchAndPlaysOnRelease()
    {
        QFile qml(QStringLiteral(SOURCE_DIR "/src/qml/pages/system/SoundPage.qml"));
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QByteArray text = qml.readAll();
        QVERIFY(text.contains("chime.volumeChime = checked"));
        QVERIFY(text.contains("onPressedChanged: if (!pressed"));
        QVERIFY(text.contains("playVolumeChime()"));
    }

    void pageUsesNoDesktopModule()
    {
        QFile qml(QStringLiteral(SOURCE_DIR "/src/qml/pages/personalization/SoundsPage.qml"));
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QByteArray text = qml.readAll();
        QVERIFY(!text.contains("KcmHost"));
        QVERIFY(text.contains("SoundTheme {"));
    }
};

QTEST_GUILESS_MAIN(TestSoundTheme)
#include "test_soundtheme.moc"
