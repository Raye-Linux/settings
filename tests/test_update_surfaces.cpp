// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update says what it knows in three places: its own page, the card
// on the launch screen and the card on System. When the Raye archive could
// not be read, Ubuntu having nothing to offer looks exactly like a PC that
// is up to date, so none of the three may say so.
//
// tests/update/state-raye-archive-down.json is the file raye-update wrote
// on an installed PC with the archive unreachable: one firmware update
// downloaded and "brokenSources" naming the archive. The other cases are
// that file with a few keys changed, and each surface is loaded as the
// window loads it and read back for the words it draws.
#include "updatestatus.h"

#include <KLocalizedContext>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString notChecked = QStringLiteral("Raye updates couldn't be checked.");
const QString partial = QStringLiteral("We couldn't check everything");
const QString upToDate = QStringLiteral("You're up to date");

void collectWords(QObject *object, QStringList *words)
{
    const auto *item = qobject_cast<QQuickItem *>(object);
    // an item that is not drawn says nothing
    if (item && !item->isVisible())
        return;
    for (const char *property : {"text", "title", "message", "description"}) {
        const QVariant v = object->property(property);
        if (v.isValid() && !v.toString().isEmpty())
            words->append(v.toString());
    }
    if (item) {
        const auto children = item->childItems();
        for (QQuickItem *child : children)
            collectWords(child, words);
    }
}

} // namespace

class TestUpdateSurfaces : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString stateFile(const QJsonObject &changes) const
    {
        QFile source(QStringLiteral(SOURCE_DIR "/tests/update/state-raye-archive-down.json"));
        if (!source.open(QIODevice::ReadOnly))
            qFatal("cannot read the state file");
        QJsonObject state = QJsonDocument::fromJson(source.readAll()).object();
        for (auto it = changes.begin(); it != changes.end(); ++it)
            state.insert(it.key(), it.value());
        const QString path = m_dir.filePath(QStringLiteral("state.json"));
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            qFatal("cannot write the state file");
        out.write(QJsonDocument(state).toJson());
        return path;
    }

    // what one surface draws, with this state file
    QStringList wordsOf(const QString &file, const QJsonObject &changes)
    {
        UpdateStatus status(stateFile(changes));
        status.setRebootRequiredPath(m_dir.filePath(QStringLiteral("no-reboot-required")));
        QQuickView view;
        view.engine()->rootContext()->setContextObject(new KLocalizedContext(view.engine()));
        view.engine()->rootContext()->setContextProperty(QStringLiteral("updates"), &status);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1120, 760);
        view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + file));
        if (view.status() != QQuickView::Ready) {
            for (const QQmlError &error : view.errors())
                qWarning("%s", qPrintable(error.toString()));
            return {};
        }
        view.show();
        if (!QTest::qWaitForWindowExposed(&view))
            return {};
        QTest::qWait(150);
        QStringList words;
        collectWords(view.rootObject(), &words);
        return words;
    }

    static QJsonObject nothingToInstall()
    {
        return QJsonObject{{QStringLiteral("phase"), QStringLiteral("idle")},
                           {QStringLiteral("state"), QStringLiteral("idle")},
                           {QStringLiteral("needsRestart"), false},
                           {QStringLiteral("firmware"), QJsonArray()}};
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
        qputenv("HOME", m_dir.filePath(QStringLiteral("home")).toUtf8());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void surfaces_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("Raye Update") << QStringLiteral("pages/update/UpdatePage.qml");
        QTest::newRow("launch screen") << QStringLiteral("frame/UpdateCard.qml");
        QTest::newRow("System") << QStringLiteral("pages/system/SystemPage.qml");
    }
    void surfaces() { QFETCH(QString, file); QVERIFY(!wordsOf(file, {}).isEmpty()); }

    // what the PC of the state file showed: a firmware update waiting, and
    // not a word about the archive
    void saysSoWithAnUpdateWaiting_data() { surfaces_data(); }
    void saysSoWithAnUpdateWaiting()
    {
        QFETCH(QString, file);
        const QStringList words = wordsOf(file, {});
        QVERIFY(!words.isEmpty());
        QVERIFY2(words.contains(notChecked), qPrintable(words.join(QLatin1String(" | "))));
    }

    // the worst case: nothing to install, and the archive never read
    void neverUpToDateWithTheArchiveUnread_data() { surfaces_data(); }
    void neverUpToDateWithTheArchiveUnread()
    {
        QFETCH(QString, file);
        const QStringList words = wordsOf(file, nothingToInstall());
        QVERIFY(!words.isEmpty());
        const QString all = words.join(QLatin1String(" | "));
        QVERIFY2(!all.contains(upToDate), qPrintable(all));
        QVERIFY2(words.contains(notChecked), qPrintable(all));
    }

    // and when everything was read, the notice goes away
    void quietWhenEverythingWasRead_data() { surfaces_data(); }
    void quietWhenEverythingWasRead()
    {
        QFETCH(QString, file);
        QJsonObject changes = nothingToInstall();
        changes.insert(QStringLiteral("brokenSources"), QJsonArray());
        changes.insert(QStringLiteral("checkedEverything"), true);
        const QStringList words = wordsOf(file, changes);
        QVERIFY(!words.isEmpty());
        const QString all = words.join(QLatin1String(" | "));
        QVERIFY2(!all.contains(notChecked), qPrintable(all));
        QVERIFY2(!all.contains(partial), qPrintable(all));
    }

    // the card on System used to fall back on "You're up to date" for a PC
    // that had never looked
    void systemCardClaimsNothingBeforeTheFirstCheck()
    {
        QJsonObject changes = nothingToInstall();
        changes.insert(QStringLiteral("brokenSources"), QJsonArray());
        changes.insert(QStringLiteral("checkedEverything"), true);
        changes.insert(QStringLiteral("checked"), 0);
        changes.insert(QStringLiteral("lastCheck"), 0);
        const QString all = wordsOf(QStringLiteral("pages/system/SystemPage.qml"), changes).join(QLatin1String(" | "));
        QVERIFY2(!all.contains(upToDate), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("Not checked for updates yet")), qPrintable(all));
    }

    // a firmware update counts: the launch screen called this "up to date"
    void launchCardCountsFirmware()
    {
        const QJsonObject changes{{QStringLiteral("phase"), QStringLiteral("idle")},
                                  {QStringLiteral("state"), QStringLiteral("idle")},
                                  {QStringLiteral("needsRestart"), false},
                                  {QStringLiteral("brokenSources"), QJsonArray()},
                                  {QStringLiteral("checkedEverything"), true}};
        const QString all = wordsOf(QStringLiteral("frame/UpdateCard.qml"), changes).join(QLatin1String(" | "));
        QVERIFY2(!all.contains(upToDate), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("One update ready")), qPrintable(all));
    }
};

QTEST_MAIN(TestUpdateSurfaces)
#include "test_update_surfaces.moc"
