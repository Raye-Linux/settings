// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts > Files from Windows, the part in Settings: what it makes of
// the helpers' answers, and a whole copy with the real copying helper.
// The root half (pkexec, windows-import-mount) is stood in for by small
// scripts; tests/windowsimport/test_images.py runs the real one on NTFS.
#include "windowsimport.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace WindowsImportData;

namespace
{
void write(const QString &path, const QByteArray &data, bool program = false)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
    file.close();
    if (program)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
}
} // namespace

class TestWindowsImport : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_drive;
    QString m_home;

    QString path(const QString &name) const { return m_tmp.filePath(name); }

    // a Windows drive the way the person sees it once it is mounted
    void makeDrive()
    {
        m_drive = path(QStringLiteral("c"));
        QDir(m_drive).removeRecursively();
        write(m_drive + QStringLiteral("/Windows/System32/x"), "x");
        write(m_drive + QStringLiteral("/Users/Alex/Documents/report.odt"), QByteArray(5000, 'r'));
        write(m_drive + QStringLiteral("/Users/Alex/Documents/2023/notes.txt"), "notes");
        write(m_drive + QStringLiteral("/Users/Alex/Documents/desktop.ini"), "[x]");
        write(m_drive + QStringLiteral("/Users/Alex/Pictures/cat.png"), QByteArray(8000, 'p'));
        write(m_drive + QStringLiteral("/Users/Sam/Music/song.mp3"), QByteArray(3000, 'm'));
        write(m_drive + QStringLiteral("/Users/Public/Documents/shared.txt"), "shared");
    }

    void setUp(WindowsImport &transfer, const QByteArray &mountScript = {}, int pkexecCode = -1, bool keepHome = false)
    {
        m_home = path(QStringLiteral("home"));
        if (!keepHome)
            QDir(m_home).removeRecursively();
        QDir().mkpath(m_home);
        const QByteArray helper = QByteArrayLiteral("#!/bin/sh\n"
                                                    "if [ \"$1\" = volumes ]; then\n"
                                                    "  cat <<'EOF'\n"
                                                    "{\"volumes\": [\n"
                                                    " {\"device\": \"/dev/nvme0n1p3\", \"fstype\": \"ntfs\", \"label\": \"\", \"uuid\": \"1A\",\n"
                                                    "  \"size\": 214748364800, \"external\": false, \"mountPoint\": \"\", \"state\": \"ok\", \"windows\": true},\n"
                                                    " {\"device\": \"/dev/sda1\", \"fstype\": \"bitlocker\", \"label\": \"Data\", \"uuid\": \"\",\n"
                                                    "  \"size\": 1000, \"external\": false, \"mountPoint\": \"\", \"state\": \"bitlocker\", \"windows\": true}]}\n"
                                                    "EOF\n"
                                                    "  exit 0\n"
                                                    "fi\n"
                                                    "RAYE_WINDOWS_IMPORT_AS_ROOT=1 exec python3 -I " SOURCE_DIR
                                                    "/data/libexec/windows-import \"$@\"\n");
        write(path(QStringLiteral("bin/windows-import")), helper, true);
        write(path(QStringLiteral("bin/pkexec")),
              pkexecCode >= 0 ? QByteArray("#!/bin/sh\nexit ") + QByteArray::number(pkexecCode) + "\n"
                              : QByteArray("#!/bin/sh\nexec \"$@\"\n"),
              true);
        const QByteArray mount = mountScript.isEmpty()
                                     ? QByteArray("#!/bin/sh\necho \"mounted ") + m_drive.toUtf8()
                                           + "\"\ncat >/dev/null\necho done > " + path(QStringLiteral("released")).toUtf8() + "\n"
                                     : mountScript;
        write(path(QStringLiteral("bin/windows-import-mount")), mount, true);
        QFile::remove(path(QStringLiteral("released")));
        transfer.setHelper(path(QStringLiteral("bin/windows-import")));
        transfer.setPkexec(path(QStringLiteral("bin/pkexec")));
        transfer.setMountHelper(path(QStringLiteral("bin/windows-import-mount")));
        transfer.setStateHome(path(QStringLiteral("state")));
        QVariantMap dest;
        for (const char *kind : {"desktop", "documents", "downloads", "music", "pictures", "videos"})
            dest.insert(QLatin1String(kind), QString(m_home + QLatin1Char('/') + QLatin1String(kind)));
        transfer.setDestinations(dest);
    }

    void openWindows(WindowsImport &transfer)
    {
        transfer.refresh();
        QVERIFY(transfer.waitFor([&] { return !transfer.volumes().isEmpty(); }, 10000));
        transfer.openVolume(QStringLiteral("/dev/nvme0n1p3"));
        QVERIFY(transfer.waitFor([&] { return transfer.stage() == QLatin1String("choose")
                                              || transfer.stage() == QLatin1String("refused"); }, 20000));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_tmp.isValid());
        makeDrive();
    }

    void holdLines()
    {
        QCOMPARE(parseHoldLine("mounted /run/raye-windows-import/1000/sda3\n"),
                 qMakePair(QStringLiteral("mounted"), QStringLiteral("/run/raye-windows-import/1000/sda3")));
        QCOMPARE(parseHoldLine("refused hibernated\n").second, QStringLiteral("hibernated"));
        QCOMPARE(parseHoldLine("warning dirty").first, QStringLiteral("warning"));
        QVERIFY(parseHoldLine("mounted").first.isEmpty());
        QVERIFY(parseHoldLine("whatever").first.isEmpty());
    }

    void onlyWhatIsTickedGoesInThePlan()
    {
        const QVariantMap scan = parseScan(R"({"state": "ok", "accounts": [
            {"name": "Alex", "path": "Users/Alex", "wallpaper": 900, "browsers": [{"browser": "edge", "name": "Microsoft Edge", "path": "a/Bookmarks"}],
             "folders": [{"kind": "documents", "path": "Documents", "files": 3, "bytes": 300},
                         {"kind": "pictures", "path": "Pictures", "files": 2, "bytes": 2000}]},
            {"name": "Sam", "path": "Users/Sam", "wallpaper": 0, "browsers": [],
             "folders": [{"kind": "music", "path": "Music", "files": 1, "bytes": 10}]}]})");
        QVariantList accounts = scan.value(QStringLiteral("accounts")).toList();
        // the first account is ticked, the others wait to be
        QCOMPARE(selection(accounts).value(QStringLiteral("files")).toInt(), 3 + 2 + 1);
        QCOMPARE(selection(accounts).value(QStringLiteral("bytes")).toLongLong(), 300 + 2000 + 900);

        QVariantMap alex = accounts[0].toMap();
        QVariantList folders = alex.value(QStringLiteral("folders")).toList();
        QVariantMap pictures = folders[1].toMap();
        pictures.insert(QStringLiteral("checked"), false);
        folders[1] = pictures;
        alex.insert(QStringLiteral("folders"), folders);
        alex.insert(QStringLiteral("wallpaperChecked"), false);
        accounts[0] = alex;
        const QVariantMap dest{{QStringLiteral("documents"), QStringLiteral("/home/a/Documents")},
                               {QStringLiteral("pictures"), QStringLiteral("/home/a/Pictures")},
                               {QStringLiteral("music"), QStringLiteral("/home/a/Music")}};
        const QVariantMap made = plan(QStringLiteral("/mnt/c"), accounts, dest, QStringLiteral("fr"), {});
        const QVariantList chosen = made.value(QStringLiteral("accounts")).toList();
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen[0].toMap().value(QStringLiteral("folders")).toList().size(), 1);
        QCOMPARE(chosen[0].toMap().value(QStringLiteral("browsers")).toList().size(), 1);
        QCOMPARE(made.value(QStringLiteral("dest")).toMap().keys(), QStringList{QStringLiteral("documents")});
        QCOMPARE(made.value(QStringLiteral("lang")).toString(), QStringLiteral("fr"));
    }

    void timeLeft()
    {
        QCOMPARE(secondsLeft(0, 1000, 10000), -1);
        QCOMPARE(secondsLeft(100, 1000, 1000), -1);
        QCOMPARE(secondsLeft(500, 1000, 10000), 11);
        QCOMPARE(secondsLeft(1000, 1000, 10000), 0);
    }

    void firstSignInAsksOnlyAboutWindowsNextToRaye()
    {
        auto volume = [](bool windows, const char *state) {
            return QVariantMap{{QStringLiteral("windows"), windows}, {QStringLiteral("state"), QLatin1String(state)}};
        };
        QVERIFY(offerable({volume(true, "ok")}));
        QVERIFY(!offerable({volume(true, "bitlocker")}));
        QVERIFY(!offerable({volume(false, "ok")}));
        QVERIFY(!offerable({}));
    }

    void bitlockerIsNeverOpened()
    {
        WindowsImport transfer;
        setUp(transfer, "#!/bin/sh\necho mounted /nope\n");
        transfer.refresh();
        QVERIFY(transfer.waitFor([&] { return !transfer.volumes().isEmpty(); }, 10000));
        transfer.openVolume(QStringLiteral("/dev/sda1"));
        QCOMPARE(transfer.stage(), QStringLiteral("refused"));
        QCOMPARE(transfer.refusal(), QStringLiteral("bitlocker"));
    }

    void wholeCopyFromTheWindowsDrive()
    {
        WindowsImport transfer;
        setUp(transfer);
        openWindows(transfer);
        QCOMPARE(transfer.stage(), QStringLiteral("choose"));
        QCOMPARE(transfer.accounts().size(), 2);
        QCOMPARE(transfer.selectedFiles(), 3);
        // untick Pictures
        transfer.setFolderChecked(0, 1, false);
        QCOMPARE(transfer.selectedFiles(), 2);
        QCOMPARE(transfer.selectedBytes(), 5000 + 5);

        transfer.start();
        QVERIFY(transfer.waitFor([&] { return transfer.stage() != QLatin1String("copying"); }, 30000));
        QCOMPARE(transfer.stage(), QStringLiteral("done"));
        QCOMPARE(transfer.report().value(QStringLiteral("files")).toInt(), 2);
        QCOMPARE(transfer.report().value(QStringLiteral("windows")).toInt(), 1);
        QVERIFY(QFile::exists(m_home + QStringLiteral("/documents/2023/notes.txt")));
        QVERIFY(!QFile::exists(m_home + QStringLiteral("/pictures/cat.png")));
        QCOMPARE(transfer.doneBytes(), transfer.totalBytes());
        // the Windows drive is let go of once the copy is done
        QVERIFY(transfer.waitFor([&] { return QFile::exists(path(QStringLiteral("released"))); }, 10000));
    }

    void hibernatedWindowsSaysWhatToDo()
    {
        WindowsImport transfer;
        setUp(transfer, "#!/bin/sh\necho refused hibernated\nexit 2\n");
        openWindows(transfer);
        QCOMPARE(transfer.stage(), QStringLiteral("refused"));
        QCOMPARE(transfer.refusal(), QStringLiteral("hibernated"));
    }

    void closingThePasswordBoxGoesBack()
    {
        WindowsImport transfer;
        setUp(transfer, {}, 126);
        transfer.refresh();
        QVERIFY(transfer.waitFor([&] { return !transfer.volumes().isEmpty(); }, 10000));
        transfer.openVolume(QStringLiteral("/dev/nvme0n1p3"));
        QVERIFY(transfer.waitFor([&] { return transfer.stage() != QLatin1String("opening"); }, 10000));
        QCOMPARE(transfer.stage(), QStringLiteral("pick"));
        QCOMPARE(transfer.refusal(), QStringLiteral("denied"));
    }

    void aFolderOnAStick()
    {
        WindowsImport transfer;
        setUp(transfer);
        transfer.openFolder(QUrl::fromLocalFile(m_drive + QStringLiteral("/Users/Sam")));
        QVERIFY(transfer.waitFor([&] { return transfer.stage() != QLatin1String("scanning"); }, 20000));
        QCOMPARE(transfer.stage(), QStringLiteral("choose"));
        QCOMPARE(transfer.accounts().size(), 1);
        QCOMPARE(transfer.accounts()[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Sam"));
    }

    void aStoppedCopyPicksUp()
    {
        const QString state = path(QStringLiteral("state/raye-settings/windows-import"));
        {
            WindowsImport transfer;
            setUp(transfer);
            openWindows(transfer);
            transfer.start();
            QVERIFY(transfer.waitFor([&] { return transfer.stage() == QLatin1String("done"); }, 30000));
        }
        // as if the power went out after the first files
        QFile::remove(m_home + QStringLiteral("/pictures/cat.png"));
        QFile file(state + QStringLiteral("/state.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"status": "running", "pid": 999999999, "total": 13005, "done": 5000})");
        file.close();

        WindowsImport transfer;
        setUp(transfer, {}, -1, true);
        QVERIFY(transfer.canResume());
        QCOMPARE(transfer.doneBytes(), 5000);
        transfer.resume();
        QVERIFY(transfer.waitFor([&] { return transfer.stage() == QLatin1String("done")
                                              || transfer.stage() == QLatin1String("refused"); }, 30000));
        QCOMPARE(transfer.stage(), QStringLiteral("done"));
        QCOMPARE(transfer.report().value(QStringLiteral("files")).toInt(), 1);
        QCOMPARE(transfer.report().value(QStringLiteral("already")).toInt(), 2);
        QVERIFY(QFile::exists(m_home + QStringLiteral("/pictures/cat.png")));
        QVERIFY(!QFile::exists(m_home + QStringLiteral("/pictures/cat (2).png")));
    }
};

QTEST_GUILESS_MAIN(TestWindowsImport)
#include "test_windowsimport.moc"
