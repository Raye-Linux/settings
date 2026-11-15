// SPDX-License-Identifier: GPL-3.0-or-later
// System > Clipboard: the history switch reads and writes the clipboard
// service's settings, and Clear says when the service did not answer.
#include "clipboard.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestClipboard : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString file() const { return m_dir.filePath(QStringLiteral("config/klipperrc")); }

    void write(const QByteArray &text)
    {
        QDir().mkpath(m_dir.filePath(QStringLiteral("config")));
        QFile f(file());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        f.close();
        KSharedConfig::openConfig(QStringLiteral("klipperrc"))->reparseConfiguration();
    }

    QString entry(const QString &key) const
    {
        KConfig config(file(), KConfig::SimpleConfig);
        return config.group(QStringLiteral("General")).readEntry(key, QStringLiteral("<unset>"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(QStringLiteral("xdg")).toUtf8());
        // no clipboard service to talk to
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void defaultIsOn()
    {
        write(QByteArray());
        ClipboardSettings c;
        QVERIFY(c.history());
    }

    void readsTheService()
    {
        write("[General]\nMaxClipItems=1\n");
        QVERIFY(!ClipboardSettings().history());
        write("[General]\nMaxClipItems=7\n");
        QVERIFY(ClipboardSettings().history());
    }

    void writes()
    {
        write("[General]\nMaxClipItems=7\nKeepClipboardContents=true\nIgnoreImages=true\n");
        ClipboardSettings c;
        QSignalSpy changed(&c, &ClipboardSettings::changed);
        c.setHistory(false);
        QVERIFY(!c.history());
        QCOMPARE(entry(QStringLiteral("MaxClipItems")), QStringLiteral("1"));
        QCOMPARE(entry(QStringLiteral("KeepClipboardContents")), QStringLiteral("false"));
        // the other settings of the service are left alone
        QCOMPARE(entry(QStringLiteral("IgnoreImages")), QStringLiteral("true"));
        QCOMPARE(changed.count(), 1);
        c.setHistory(false);
        QCOMPARE(changed.count(), 1);

        c.setHistory(true);
        QVERIFY(c.history());
        QCOMPARE(entry(QStringLiteral("MaxClipItems")), QString::number(ClipboardSettings::historySize));
        QCOMPARE(entry(QStringLiteral("KeepClipboardContents")), QStringLiteral("true"));
    }

    void clearWithoutTheService()
    {
        ClipboardSettings c;
        QSignalSpy cleared(&c, &ClipboardSettings::cleared);
        c.clear();
        QVERIFY(c.clearing());
        QTRY_COMPARE(cleared.count(), 1);
        QCOMPARE(cleared.first().first().toBool(), false);
        QVERIFY(!c.clearing());
    }
};

QTEST_GUILESS_MAIN(TestClipboard)
#include "test_clipboard.moc"
