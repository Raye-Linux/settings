// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > AutoPlay, as the page itself: the three choices a person can
// make about a plugged-in USB drive or memory card must be ON THE PAGE,
// and choosing one must land in rayeshellrc the way the taskbar's devices
// applet reads it.
//
// WHY THIS FILE EXISTS, next to test_autoplay.cpp. That one drives
// AutoPlaySettings directly and proves the writing and the normalising.
// It says nothing about the page: take the "Memory card" row out of
// AutoPlayPage.qml, or offer "none" where the applet reads "nothing", and
// every one of its checks still passes. That is the shape of the defect
// found on System > Display on 2026-11-06 -- the logic behind the row
// worked, the row was unreachable -- and AutoPlay was one of the 29 pages
// the search advertises with no check of its own that the rows are drawn
// (measured 2026-11-06: 86 qml pages in data/layout.json, 29 with such a
// check, 57 without, 29 of those advertised in data/search).
//
// WHAT THE OTHER SIDE READS. theme/shell/plasmoids/org.raye.devices
// reads the keys this page writes, in devicesapplet.cpp added():
//   138  rayeshellrc [AutoPlay]
//   139  Enabled, default true -- false and nothing happens at all
//   141  RemovableDrive, read with an empty default
//   142  autoPlay(kind == MemoryCard ? MemoryCard, else RemovableDrive)
//   143  "nothing" -> return             146  "open" -> open the files
//   150  anything else -> the toast with "Open folder" / "Take no action"
// and devicelogic.cpp autoPlay() (137) keeps "open" and "nothing" and
// turns everything else into "ask". So an unset or unknown value is
// already "ask" on the reading side, and the page must agree: asking is
// the safe answer, and opening whatever has just been plugged in on its
// own is what gave Windows' AutoRun its reputation. That is checked here
// against the page, not against the backend.
#include "layout.h"

#include <KConfigGroup>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KSharedConfig>
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

const char *pageId = "devices/autoplay";
const char *pageFile = "pages/devices/AutoPlayPage.qml";

QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg
        && (qstrcmp(context.category, "qml") == 0 || qstrncmp(context.category, "qt.qml", 6) == 0
            || message.contains(QLatin1String("qrc:")))) {
        qmlWarnings << message;
    }
    fprintf(stderr, "%s\n", qPrintable(message));
}

QQuickItem *rowWithId(QQuickItem *item, const QString &anchor)
{
    if (!item)
        return nullptr;
    if (item->property("searchId").toString() == anchor)
        return item;
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        if (QQuickItem *found = rowWithId(child, anchor))
            return found;
    return nullptr;
}

// The rows data/search advertises for this page. update-devices.json is
// of the {"version": 1, "entries": [...]} shape and holds the Drivers
// rows too, so the page id is what picks them out.
QStringList indexedAnchors(QString *error)
{
    QFile fragment(QStringLiteral(SOURCE_DIR "/data/search/update-devices.json"));
    if (!fragment.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("data/search/update-devices.json cannot be read");
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(fragment.readAll());
    const QJsonArray entries = doc.object().value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty()) {
        *error = QStringLiteral("data/search/update-devices.json holds no entry");
        return {};
    }
    QStringList anchors;
    for (const QJsonValue &v : entries) {
        const QString id = v.toObject().value(QStringLiteral("id")).toString();
        if (!id.startsWith(QLatin1String(pageId) + QLatin1Char('#')))
            continue;
        if (v.toObject().value(QStringLiteral("label")).toString().isEmpty()) {
            *error = id + QStringLiteral(" has no label");
            return {};
        }
        if (v.toObject().value(QStringLiteral("label_fr")).toString().isEmpty()) {
            *error = id + QStringLiteral(" has no label_fr");
            return {};
        }
        anchors << id.section(QLatin1Char('#'), 1);
    }
    return anchors;
}

} // namespace

class TestAutoPlayPage : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_home;

    KConfigGroup group() const
    {
        KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("rayeshellrc"));
        config->reparseConfiguration();
        return config->group(QStringLiteral("AutoPlay"));
    }

    // the page, loaded as the window loads it
    bool load(QQuickView *view) const
    {
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(1120, 760);
        view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + QLatin1String(pageFile)));
        if (view->status() != QQuickView::Ready)
            return false;
        view->show();
        if (!QTest::qWaitForWindowExposed(view))
            return false;
        QTest::qWait(150);
        return true;
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-settings");
        QVERIFY(m_dir.isValid());
        m_home = m_dir.filePath(QStringLiteral("config"));
        qputenv("XDG_CONFIG_HOME", m_home.toUtf8());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(QStringLiteral("xdg")).toUtf8());
        qputenv("HOME", m_dir.filePath(QStringLiteral("home")).toUtf8());
        // no session: only the files change
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    void init()
    {
        QDir(m_home).removeRecursively();
        QDir().mkpath(m_home);
        KSharedConfig::openConfig(QStringLiteral("rayeshellrc"))->reparseConfiguration();
    }

    // the page is in the layout under the id the search rows use
    void pageIsInTheLayout()
    {
        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        QString component;
        for (const Category &category : layout.categories)
            for (const Page &page : category.pages)
                if (page.id == QLatin1String(pageId))
                    component = page.component;
        QCOMPARE(component, QLatin1String(pageFile));
    }

    void theRowsAreInTheSearchIndex()
    {
        QString error;
        const QStringList indexed = indexedAnchors(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(indexed, QStringList({QStringLiteral("autoplay"),
                                       QStringLiteral("removable-drive"),
                                       QStringLiteral("memory-card")}));
    }

    // The rows the search sends people to must really be drawn, and the
    // page must draw nothing the index does not know.
    void theRowsAreOnThePage()
    {
        QString error;
        const QStringList indexed = indexedAnchors(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!indexed.isEmpty());

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        QStringList drawn;
        {
            QQmlEngine engine;
            engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
            QQuickView view(&engine, nullptr);
            QVERIFY(load(&view));
            QVariant ids;
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "searchIds", Q_RETURN_ARG(QVariant, ids)));
            for (const QString &id : ids.toStringList())
                if (!drawn.contains(id))
                    drawn << id;
        }
        qInstallMessageHandler(previous);

        for (const QString &id : indexed)
            QVERIFY2(drawn.contains(id), qPrintable(QLatin1String(pageId) + QLatin1Char('#') + id
                                                    + QStringLiteral(" is not on the page")));
        for (const QString &id : drawn)
            QVERIFY2(indexed.contains(id), qPrintable(id + QStringLiteral(" is not in data/search/update-devices.json")));
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }

    // The three answers are the point of the page: ask, open, nothing,
    // each with a label of its own, in that order -- asking first, so
    // that the safe answer is the one already selected by default.
    void theThreeChoicesAreOnThePage_data()
    {
        QTest::addColumn<QString>("anchor");
        QTest::newRow("removable drive") << QStringLiteral("removable-drive");
        QTest::newRow("memory card") << QStringLiteral("memory-card");
    }
    void theThreeChoicesAreOnThePage()
    {
        QFETCH(QString, anchor);
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQuickView view(&engine, nullptr);
        QVERIFY(load(&view));

        QQuickItem *row = rowWithId(view.rootObject(), anchor);
        QVERIFY2(row, qPrintable(anchor + QStringLiteral(" is not on the page")));
        QObject *combo = row->property("comboBox").value<QObject *>();
        QVERIFY2(combo, qPrintable(anchor + QStringLiteral(" has no comboBox: is it still a ComboCard?")));

        const QVariantList model = combo->property("model").toList();
        QCOMPARE(model.size(), 3);
        // the combo really offers them, not just the model
        QCOMPARE(combo->property("count").toInt(), 3);

        QStringList values;
        QStringList labels;
        for (const QVariant &choice : model) {
            const QVariantMap entry = choice.toMap();
            values << entry.value(QStringLiteral("value")).toString();
            labels << entry.value(QStringLiteral("text")).toString();
        }
        // exactly the words the applet reads, and in this order
        QCOMPARE(values, QStringList({QStringLiteral("ask"), QStringLiteral("open"), QStringLiteral("nothing")}));
        for (const QString &label : labels)
            QVERIFY2(!label.isEmpty(), qPrintable(anchor + QStringLiteral(" offers a choice with no words on it")));
        QCOMPARE(QSet<QString>(labels.begin(), labels.end()).size(), 3);
    }

    // Choosing on the page, the way a person does: the combo says it was
    // activated, and the value has to be in rayeshellrc afterwards.
    void eachChoiceWritesItsValue_data()
    {
        QTest::addColumn<QString>("anchor");
        QTest::addColumn<QString>("key");
        QTest::newRow("removable drive") << QStringLiteral("removable-drive") << QStringLiteral("RemovableDrive");
        QTest::newRow("memory card") << QStringLiteral("memory-card") << QStringLiteral("MemoryCard");
    }
    void eachChoiceWritesItsValue()
    {
        QFETCH(QString, anchor);
        QFETCH(QString, key);
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQuickView view(&engine, nullptr);
        QVERIFY(load(&view));

        QQuickItem *row = rowWithId(view.rootObject(), anchor);
        QVERIFY(row);
        QObject *combo = row->property("comboBox").value<QObject *>();
        QVERIFY(combo);

        // every choice, including the one that is already selected: going
        // back to "ask" has to be written too, or the page cannot undo
        const QStringList expected{QStringLiteral("open"), QStringLiteral("nothing"), QStringLiteral("ask")};
        for (int index = 0; index < 3; ++index) {
            const QString want = expected.at(index);
            const int position = want == QLatin1String("ask") ? 0 : (want == QLatin1String("open") ? 1 : 2);
            QVERIFY2(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, position)),
                     "the combo has no activated(int) signal");
            QTest::qWait(50);
            QCOMPARE(group().readEntry(key.toUtf8().constData(), QString()), want);
        }
    }

    // The switch at the top: the applet gives up before reading anything
    // else when Enabled is false, so the page has to be able to say it --
    // and true is the default, which leaves no key behind.
    void theSwitchWritesEnabled()
    {
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQuickView view(&engine, nullptr);
        QVERIFY(load(&view));

        QQuickItem *row = rowWithId(view.rootObject(), QStringLiteral("autoplay"));
        QVERIFY2(row, "the AutoPlay switch is not on the page");
        QVERIFY2(row->property("checked").toBool(), "AutoPlay is not on by default");
        // Clicked, not toggle()d. ToggleCard is a Card, which is a
        // T.AbstractButton: toggle() sets `checked` without emitting
        // toggled(), so the page's handler never runs and the check would
        // pass over a switch wired to nothing. The card says "a click
        // anywhere on the card flips it" -- so that is what is done, and
        // it needs the row to be visible and to have a size, which is the
        // other half of being on the page.
        QVERIFY2(row->isVisible(), "the AutoPlay switch is on the page but not visible");
        QVERIFY(row->width() > 0 && row->height() > 0);
        const QPoint centre = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre);
        QTest::qWait(80);
        QVERIFY2(!row->property("checked").toBool(), "the click did not flip the switch");
        QCOMPARE(group().readEntry("Enabled", true), false);
    }

    // An unset or unknown value has to read as "ask" ON THE PAGE. The
    // applet already treats it that way, and a page that showed "open"
    // for a value it does not know would be telling the person that
    // anything plugged in opens itself, when it does not -- and a page
    // that defaulted to "open" would do the opposite and far worse.
    void anUnknownValueShowsAsk_data()
    {
        QTest::addColumn<QString>("stored");
        QTest::newRow("no key at all") << QString();
        QTest::newRow("empty") << QStringLiteral("");
        QTest::newRow("a word nobody writes") << QStringLiteral("format-c");
        QTest::newRow("the right word, wrong case") << QStringLiteral("Open");
        QTest::newRow("a path") << QStringLiteral("/usr/bin/dolphin");
    }
    void anUnknownValueShowsAsk()
    {
        QFETCH(QString, stored);
        if (!stored.isNull()) {
            KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("rayeshellrc"));
            KConfigGroup autoPlay = config->group(QStringLiteral("AutoPlay"));
            autoPlay.writeEntry("RemovableDrive", stored);
            autoPlay.writeEntry("MemoryCard", stored);
            config->sync();
        }

        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQuickView view(&engine, nullptr);
        QVERIFY(load(&view));

        for (const QString &anchor : {QStringLiteral("removable-drive"), QStringLiteral("memory-card")}) {
            QQuickItem *row = rowWithId(view.rootObject(), anchor);
            QVERIFY(row);
            // 0 is "ask": the first choice, the safe one
            QCOMPARE(row->property("currentIndex").toInt(), 0);
            QObject *combo = row->property("comboBox").value<QObject *>();
            QVERIFY(combo);
            QCOMPARE(combo->property("currentValue").toString(), QStringLiteral("ask"));
        }
    }

    // A card with no choice of its own follows the drive, which is what
    // the applet does at devicesapplet.cpp:142 -- shown here through the
    // page, so the two cannot drift apart.
    void aCardWithNoChoiceFollowsTheDrive()
    {
        KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("rayeshellrc"));
        KConfigGroup autoPlay = config->group(QStringLiteral("AutoPlay"));
        autoPlay.writeEntry("RemovableDrive", QStringLiteral("nothing"));
        config->sync();
        QVERIFY(!config->group(QStringLiteral("AutoPlay")).hasKey("MemoryCard"));

        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQuickView view(&engine, nullptr);
        QVERIFY(load(&view));

        QQuickItem *card = rowWithId(view.rootObject(), QStringLiteral("memory-card"));
        QVERIFY(card);
        QObject *combo = card->property("comboBox").value<QObject *>();
        QVERIFY(combo);
        QCOMPARE(combo->property("currentValue").toString(), QStringLiteral("nothing"));
    }
};

QTEST_MAIN(TestAutoPlayPage)
#include "test_autoplay_page.moc"
