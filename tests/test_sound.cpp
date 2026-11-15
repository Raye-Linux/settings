// SPDX-License-Identifier: GPL-3.0-or-later
// System › Sound without a sound server: the names, icons, volume and
// balance arithmetic, which streams the mixer leaves out, the profile
// a card comes back on with, and the microphone level meter. The models
// themselves are tried against a private PipeWire when one is running
// (RAYE_TEST_PULSE=1, set by the render harness).
#include "sound.h"

#include <KLocalizedString>
#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

using namespace SoundText;

class TestSound : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
    }

    void volume()
    {
        QCOMPARE(percentOf(normalVolume), 100);
        QCOMPARE(percentOf(0), 0);
        QCOMPARE(percentOf(normalVolume / 2), 50);
        // above 100 % (set by another app) still reads 100 on the slider
        QCOMPARE(percentOf(normalVolume * 3 / 2), 100);
        QCOMPARE(volumeOf(100), normalVolume);
        QCOMPARE(volumeOf(0), qint64(0));
        QCOMPARE(volumeOf(150), normalVolume);
        QCOMPARE(volumeOf(-5), qint64(0));
        for (int p : {1, 7, 33, 50, 99})
            QCOMPARE(percentOf(volumeOf(p)), p);
    }

    void labels()
    {
        auto l = deviceLabel(QStringLiteral("Built-in Audio Analog Stereo"), QStringLiteral("Headphones"),
                             QStringLiteral("Built-in Audio"));
        QCOMPARE(l.title, QStringLiteral("Headphones"));
        QCOMPARE(l.subtitle, QStringLiteral("Built-in Audio"));
        // no port: the device's own name, nothing under it
        l = deviceLabel(QStringLiteral("USB Headset"), QString(), QString());
        QCOMPARE(l.title, QStringLiteral("USB Headset"));
        QCOMPARE(l.subtitle, QString());
        // a port named like the device says nothing new
        l = deviceLabel(QStringLiteral("HDMI / DisplayPort"), QStringLiteral("HDMI / DisplayPort"), QStringLiteral("HDA Intel PCH"));
        QCOMPARE(l.title, QStringLiteral("HDMI / DisplayPort"));
        QCOMPARE(l.subtitle, QStringLiteral("HDA Intel PCH"));
        // no card: the device under the port
        l = deviceLabel(QStringLiteral("Jabra Evolve"), QStringLiteral("Headset"), QString());
        QCOMPARE(l.title, QStringLiteral("Headset"));
        QCOMPARE(l.subtitle, QStringLiteral("Jabra Evolve"));
    }

    void icons()
    {
        QCOMPARE(deviceIcon(QStringLiteral("headphones"), QString(), false), QStringLiteral("audio-headphones"));
        QCOMPARE(deviceIcon(QStringLiteral("headset"), QString(), true), QStringLiteral("audio-headset"));
        QCOMPARE(deviceIcon(QStringLiteral("hdmi"), QString(), false), QStringLiteral("video-display"));
        QCOMPARE(deviceIcon(QString(), QStringLiteral("webcam"), true), QStringLiteral("camera-web"));
        QCOMPARE(deviceIcon(QString(), QStringLiteral("headphone"), false), QStringLiteral("audio-headphones"));
        QCOMPARE(deviceIcon(QStringLiteral("bluetooth"), QString(), false), QStringLiteral("preferences-system-bluetooth"));
        QCOMPARE(deviceIcon(QString(), QString(), false), QStringLiteral("audio-speakers-symbolic"));
        QCOMPARE(deviceIcon(QString(), QString(), true), QStringLiteral("audio-input-microphone"));
    }

    void balance()
    {
        const QStringList stereo{QStringLiteral("front-left"), QStringLiteral("front-right")};
        QCOMPARE(balanceOf(stereo, {normalVolume, normalVolume}), 0);
        QCOMPARE(balanceOf(stereo, {normalVolume, 0}), -100);
        QCOMPARE(balanceOf(stereo, {0, normalVolume}), 100);
        QCOMPARE(balanceOf(stereo, {normalVolume, normalVolume / 2}), -50);
        QCOMPARE(balanceOf({QStringLiteral("mono")}, {normalVolume}), 0);
        QCOMPARE(balanceOf(stereo, {}), 0);

        QCOMPARE(volumesFor(stereo, normalVolume, 0), (QList<qint64>{normalVolume, normalVolume}));
        QCOMPARE(volumesFor(stereo, normalVolume, -100), (QList<qint64>{normalVolume, 0}));
        QCOMPARE(volumesFor(stereo, 1000, 50), (QList<qint64>{500, 1000}));
        // round trip
        for (int b : {-80, -25, 0, 40, 100})
            QCOMPARE(balanceOf(stereo, volumesFor(stereo, normalVolume, b)), b);
        // 5.1: the left side goes down with the left channels
        const QStringList surround{QStringLiteral("front-left"), QStringLiteral("front-right"), QStringLiteral("front-center"),
                                   QStringLiteral("lfe"), QStringLiteral("rear-left"), QStringLiteral("rear-right")};
        QCOMPARE(volumesFor(surround, 1000, 50), (QList<qint64>{500, 1000, 1000, 1000, 500, 1000}));
        QCOMPARE(balanceOf(surround, volumesFor(surround, 1000, 50)), 50);
    }

    void streams()
    {
        QVERIFY(isMonitor({{QStringLiteral("device.class"), QStringLiteral("monitor")}}));
        QVERIFY(!isMonitor({{QStringLiteral("device.class"), QStringLiteral("sound")}}));
        QCOMPARE(appName({{QStringLiteral("application.name"), QStringLiteral("Firefox")}}, QStringLiteral("AudioStream")),
                 QStringLiteral("Firefox"));
        QCOMPARE(appName({{QStringLiteral("application.process.binary"), QStringLiteral("mpv")}}, QStringLiteral("x")),
                 QStringLiteral("mpv"));
        QCOMPARE(appName({}, QStringLiteral("Playback")), QStringLiteral("Playback"));
        QCOMPARE(appIcon({{QStringLiteral("application.icon_name"), QStringLiteral("firefox")}}), QStringLiteral("firefox"));
        QCOMPARE(appIcon({{QStringLiteral("application.process.binary"), QStringLiteral("vlc")}}), QStringLiteral("vlc"));
        QCOMPARE(appIcon({}), QStringLiteral("applications-multimedia"));
        QVERIFY(isHiddenStream({{QStringLiteral("media.role"), QStringLiteral("event")}}));
        QVERIFY(isHiddenStream({{QStringLiteral("application.id"), QStringLiteral("org.kde.kmixd")}}));
        QVERIFY(isHiddenStream({{QStringLiteral("application.id"), QStringLiteral("org.raye.settings")}}));
        QVERIFY(!isHiddenStream({{QStringLiteral("media.role"), QStringLiteral("music")}}));
    }

    void profiles()
    {
        QCOMPARE(profileTitle(QStringLiteral("off"), QStringLiteral("Off")), QStringLiteral("Off"));
        QCOMPARE(profileTitle(QStringLiteral("output:analog-stereo"), QStringLiteral("Analog Stereo Output")),
                 QStringLiteral("Analog Stereo Output"));
        QCOMPARE(profileTitle(QStringLiteral("pro-audio"), QString()), QStringLiteral("pro-audio"));

        const QList<SoundCards::ProfileChoice> list = {
            {QStringLiteral("off"), 0, true},
            {QStringLiteral("output:hdmi-stereo"), 5900, false},
            {QStringLiteral("output:analog-stereo+input:analog-stereo"), 6565, true},
            {QStringLiteral("output:analog-stereo"), 6500, true},
        };
        QCOMPARE(SoundCards::profileToRestore(list, QStringLiteral("output:analog-stereo")), QStringLiteral("output:analog-stereo"));
        QCOMPARE(SoundCards::profileToRestore(list, QString()), QStringLiteral("output:analog-stereo+input:analog-stereo"));
        // what was remembered is gone, or was "off": the best one
        QCOMPARE(SoundCards::profileToRestore(list, QStringLiteral("gone")), QStringLiteral("output:analog-stereo+input:analog-stereo"));
        QCOMPARE(SoundCards::profileToRestore(list, QStringLiteral("off")), QStringLiteral("output:analog-stereo+input:analog-stereo"));
        QCOMPARE(SoundCards::profileToRestore({{QStringLiteral("off"), 0, true}}, QString()), QString());
    }

    void sorting()
    {
        QVERIFY(deviceLessThan(true, QStringLiteral("Zeta"), false, QStringLiteral("Alpha")));
        QVERIFY(!deviceLessThan(false, QStringLiteral("Alpha"), true, QStringLiteral("Zeta")));
        QVERIFY(deviceLessThan(false, QStringLiteral("alpha"), false, QStringLiteral("Beta")));
    }

    void meter()
    {
        auto samples = [](std::initializer_list<qint16> values) {
            QByteArray bytes;
            for (qint16 v : values) {
                char raw[2];
                qToLittleEndian(v, raw);
                bytes.append(raw, 2);
            }
            return bytes;
        };
        QCOMPARE(LevelMeter::peakOf(QByteArray()), 0.0);
        QCOMPARE(LevelMeter::peakOf(samples({0, 0, 0})), 0.0);
        QCOMPARE(LevelMeter::peakOf(samples({100, -32768, 50})), 1.0);
        QVERIFY(qAbs(LevelMeter::peakOf(samples({16384, -100})) - 0.5) < 0.001);
        // an odd byte at the end is not half a sample
        QVERIFY(qAbs(LevelMeter::peakOf(samples({16384}) + QByteArray(1, '\x7f')) - 0.5) < 0.001);
        // rises at once, falls slowly
        QCOMPARE(LevelMeter::smoothed(0.1, 0.9), 0.9);
        QVERIFY(qAbs(LevelMeter::smoothed(0.9, 0.0) - 0.82) < 0.0001);
        QCOMPARE(LevelMeter::smoothed(0.05, 0.0), 0.0);

        // off by default: nothing records until the page asks
        LevelMeter meter;
        QVERIFY(!meter.active());
        QCOMPARE(meter.level(), 0.0);
        meter.setDevice(QStringLiteral("no-such-device"));
        meter.setActive(true);
        meter.setActive(false);
        QCOMPARE(meter.level(), 0.0);
    }

    // With a server (the render harness starts a private PipeWire with
    // two outputs and a microphone): the models fill and react.
    void liveServer()
    {
        if (qEnvironmentVariable("RAYE_TEST_PULSE") != QLatin1String("1"))
            QSKIP("no private sound server for this run");
        SoundDevices outputs;
        QTRY_VERIFY_WITH_TIMEOUT(outputs.rowCount() >= 2, 5000);
        SoundDevices inputs;
        inputs.setInput(true);
        QTRY_VERIFY_WITH_TIMEOUT(inputs.rowCount() >= 1, 5000);
        // no monitor in the inputs
        for (int i = 0; i < inputs.rowCount(); ++i)
            QVERIFY(!inputs.get(i).value(QStringLiteral("name")).toString().endsWith(QLatin1String(".monitor")));
        // the default itself is picked by the session manager, which the
        // harness does not run: only the volume is checked here
        QVERIFY(outputs.defaultRow() >= -1);
        const int row = outputs.defaultRow() >= 0 ? outputs.defaultRow() : 0;
        const QString name = outputs.get(row).value(QStringLiteral("name")).toString();
        auto rowOf = [&outputs](const QString &n) {
            for (int i = 0; i < outputs.rowCount(); ++i) {
                if (outputs.get(i).value(QStringLiteral("name")).toString() == n)
                    return i;
            }
            return -1;
        };
        outputs.setVolume(row, 40);
        QTRY_COMPARE_WITH_TIMEOUT(outputs.get(rowOf(name)).value(QStringLiteral("volume")).toInt(), 40, 5000);
        outputs.setMuted(rowOf(name), true);
        QTRY_VERIFY_WITH_TIMEOUT(outputs.get(rowOf(name)).value(QStringLiteral("muted")).toBool(), 5000);
        // moving the volume up brings the sound back
        outputs.setVolume(rowOf(name), 60);
        QTRY_VERIFY_WITH_TIMEOUT(!outputs.get(rowOf(name)).value(QStringLiteral("muted")).toBool(), 5000);
        outputs.setBalance(rowOf(name), -50);
        QTRY_COMPARE_WITH_TIMEOUT(outputs.get(rowOf(name)).value(QStringLiteral("balance")).toInt(), -50, 5000);
        outputs.setBalance(rowOf(name), 0);

        // the two fake apps of the harness are in the mixer
        SoundStreams streams;
        QTRY_VERIFY_WITH_TIMEOUT(streams.rowCount() >= 2, 5000);
        QStringList apps;
        for (int i = 0; i < streams.rowCount(); ++i)
            apps.append(streams.data(streams.index(i), SoundStreams::AppNameRole).toString());
        QVERIFY(apps.contains(QStringLiteral("Music")));
        QVERIFY(apps.contains(QStringLiteral("Firefox")));
        streams.setVolume(0, 30);
        QTRY_COMPARE_WITH_TIMEOUT(streams.data(streams.index(0), SoundStreams::VolumeRole).toInt(), 30, 5000);
    }
};

QTEST_GUILESS_MAIN(TestSound)
#include "test_sound.moc"
