// SPDX-License-Identifier: GPL-3.0-or-later
#include "categorymodel.h"
#include "layout.h"
#include "router.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

namespace {

Layout shipped(const QString &lang)
{
    QFile f(QStringLiteral(LAYOUT_FILE));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parseLayout(f.readAll(), lang);
}

} // namespace

class TestLayout : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void picksTheLanguage()
    {
        const QByteArray json = R"({"categories":[{"id":"s","name":"System","name_fr":"Système",
            "summary":"Sound","summary_fr":"Son",
            "pages":[{"id":"s/a","name":"Sound","name_fr":"Son"}]}]})";
        const Layout fr = parseLayout(json, QStringLiteral("fr"));
        QCOMPARE(fr.categories.first().name, QStringLiteral("Système"));
        QCOMPARE(fr.categories.first().summary, QStringLiteral("Son"));
        QCOMPARE(fr.categories.first().pages.first().name, QStringLiteral("Son"));
    }

    void fallsBackToEnglish()
    {
        const QByteArray json = R"({"categories":[{"id":"s","name":"System","pages":[]}]})";
        QCOMPARE(parseLayout(json, QStringLiteral("de")).categories.first().name, QStringLiteral("System"));
    }

    void skipsBrokenItems()
    {
        const QByteArray json = R"({"categories":[{"name":"no id"},
            {"id":"ok","name":"Ok","pages":[{"name":"no id"},{"id":"ok/b","name":"B"}]}]})";
        const Layout layout = parseLayout(json, QStringLiteral("en"));
        QCOMPARE(layout.categories.size(), 1);
        QCOMPARE(layout.categories.first().pages.size(), 1);
        QCOMPARE(layout.categories.first().pages.first().id, QStringLiteral("ok/b"));
    }

    void reportsBadJson()
    {
        QString error;
        QVERIFY(parseLayout("{not json", QStringLiteral("en"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(parseLayout(R"({"categories":[]})", QStringLiteral("en"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    // the type is written down, or read from what the page carries
    void knowsHowAPageIsDrawn()
    {
        const Layout layout = parseLayout(R"({"categories":[{"id":"s","name":"S","pages":[
            {"id":"s/a","name":"A","component":"pages/s/APage.qml"},
            {"id":"s/b","name":"B","kcm":"kcm_b"},
            {"id":"s/c","name":"C","type":"action","component":"pages/s/CPage.qml"}]}]})",
                                          QStringLiteral("en"));
        const QList<Page> &pages = layout.categories.first().pages;
        QCOMPARE(pages.at(0).type, QStringLiteral("qml"));
        QCOMPARE(pages.at(1).type, QStringLiteral("kcm"));
        QCOMPARE(pages.at(2).type, QStringLiteral("action"));
    }

    // the old window asks each card for the module it opens
    void everyPageHasSomethingToOpen()
    {
        const Layout layout = parseLayout(R"({"categories":[{"id":"s","name":"S","pages":[
            {"id":"s/a","name":"A","component":"x.qml","aliases":["kcm_a","kcm_a2"]},
            {"id":"s/b","name":"B","kcm":"kcm_b"},
            {"id":"s/c","name":"C","component":"x.qml"}]}]})",
                                          QStringLiteral("en"));
        const QList<Page> &pages = layout.categories.first().pages;
        QCOMPARE(pages.at(0).target(), QStringLiteral("kcm_a"));
        QCOMPARE(pages.at(1).target(), QStringLiteral("kcm_b"));
        QCOMPARE(pages.at(2).target(), QStringLiteral("raye:s/c"));
    }

    // a phone module is a link target only: the page never shows it
    void phoneModulesAreNotWhatAPageOpens()
    {
        const Layout layout = parseLayout(R"({"categories":[{"id":"n","name":"N","pages":[
            {"id":"n/wifi","name":"Wi-Fi","component":"x.qml","aliases":["kcm_mobile_wifi"]},
            {"id":"n/power","name":"Power","component":"x.qml","aliases":["kcm_mobile_power","kcm_power"]},
            {"id":"n/gaming","name":"Gaming","component":"x.qml","kcm":"raye:gaming"}]}]})",
                                          QStringLiteral("en"));
        const QList<Page> &pages = layout.categories.first().pages;
        QCOMPARE(pages.at(0).target(), QStringLiteral("raye:n/wifi"));
        QVERIFY(!pages.at(0).opensInWindow());
        QCOMPARE(pages.at(1).target(), QStringLiteral("kcm_power"));
        QVERIFY(pages.at(1).opensInWindow());
        QVERIFY(pages.at(2).opensInWindow());
        // a link to the phone module still lands on the page
        QCOMPARE(layout.pageForModule(QStringLiteral("kcm_mobile_wifi"))->id, QStringLiteral("n/wifi"));
    }

    // the window of today lists what it can open; the new one gets every
    // page
    void entriesLeaveOutWhatTheWindowCannotOpen()
    {
        CategoryModel model;
        QVERIFY(model.load(R"({"categories":[{"id":"s","name":"S","pages":[
            {"id":"s/a","name":"A","component":"x.qml","aliases":["kcm_a"]},
            {"id":"s/b","name":"B","component":"x.qml"},
            {"id":"s/c","name":"C","component":"x.qml","aliases":["kcm_mobile_c"]}]}]})",
                           QStringLiteral("en")));
        const QModelIndex row = model.index(0);
        const QVariantList entries = model.data(row, CategoryModel::EntriesRole).toList();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().toMap().value(QStringLiteral("kcm")).toString(), QStringLiteral("kcm_a"));
        QCOMPARE(model.data(row, CategoryModel::PagesRole).toList().size(), 3);
        QVERIFY(model.roleNames().values().contains("pages"));
    }

    void findsThingsById()
    {
        const Layout layout = parseLayout(R"({"categories":[
            {"id":"system","name":"System","pages":[{"id":"system/display","name":"Display",
             "component":"x.qml","aliases":["kcm_kscreen"]}]},
            {"id":"update","name":"Update","pages":[{"id":"update/raye-update","name":"Update",
             "kcm":"raye:update","component":"x.qml"}]}]})",
                                          QStringLiteral("en"));
        QVERIFY(layout.category(QStringLiteral("system")) != nullptr);
        QVERIFY(layout.page(QStringLiteral("system/display")) != nullptr);
        QVERIFY(layout.page(QStringLiteral("system")) == nullptr);
        QCOMPARE(layout.pageForModule(QStringLiteral("kcm_kscreen"))->id, QStringLiteral("system/display"));
        QCOMPARE(layout.pageForModule(QStringLiteral("raye:update"))->id, QStringLiteral("update/raye-update"));
        QVERIFY(layout.pageForModule(QStringLiteral("kcm_nothing")) == nullptr);
        QCOMPARE(layout.categoryIndexOf(QStringLiteral("raye:update")), 1);
        QCOMPARE(layout.categoryIndexOf(QStringLiteral("kcm_kscreen")), 0);
        QCOMPARE(layout.categoryIndexOf(QStringLiteral("update")), 1);
        QCOMPARE(layout.categoryIndexOf(QStringLiteral("kcm_nothing")), -1);
    }

    void dropsATileThatOpensNothing()
    {
        const Layout layout = parseLayout(R"({"tiles":[{"target":"system"},{"target":"nowhere"},{}],
            "categories":[{"id":"system","name":"System","pages":[]}]})",
                                          QStringLiteral("en"));
        QCOMPARE(layout.tiles.size(), 1);
        QCOMPARE(layout.tiles.first().name, QStringLiteral("System"));
    }

    // --- the layout we ship ---

    void shippedLayoutIsWhole()
    {
        const Layout en = shipped(QStringLiteral("en"));
        const Layout fr = shipped(QStringLiteral("fr"));
        QVERIFY(!en.isEmpty());
        QCOMPARE(en.categories.size(), fr.categories.size());

        static const QRegularExpression pageId(QStringLiteral("^[a-z0-9]+(/[a-z0-9-]+)+$"));
        QSet<QString> ids;
        for (int c = 0; c < en.categories.size(); ++c) {
            const Category &category = en.categories.at(c);
            const Category &french = fr.categories.at(c);
            QVERIFY2(!ids.contains(category.id), qPrintable(category.id));
            ids.insert(category.id);
            QVERIFY2(!category.pages.isEmpty(), qPrintable(category.id));
            QVERIFY2(!category.icon.isEmpty(), qPrintable(category.id));
            QVERIFY2(!category.iconFallback.isEmpty(), qPrintable(category.id));
            QVERIFY2(!category.summary.isEmpty(), qPrintable(category.id));
            QVERIFY2(!french.name.isEmpty(), qPrintable(category.id));
            QVERIFY2(!french.summary.isEmpty(), qPrintable(category.id));
            QCOMPARE(french.pages.size(), category.pages.size());

            for (int p = 0; p < category.pages.size(); ++p) {
                const Page &page = category.pages.at(p);
                const Page &frenchPage = french.pages.at(p);
                QVERIFY2(!ids.contains(page.id), qPrintable(page.id));
                ids.insert(page.id);
                QVERIFY2(pageId.match(page.id).hasMatch(), qPrintable(page.id));
                // a page lives in the category it is named after
                QCOMPARE(page.id.section(QLatin1Char('/'), 0, 0), category.id);
                QVERIFY2(!page.icon.isEmpty(), qPrintable(page.id));
                QVERIFY2(!page.iconFallback.isEmpty(), qPrintable(page.id));
                QVERIFY2(!page.summary.isEmpty(), qPrintable(page.id));
                QVERIFY2(!frenchPage.name.isEmpty(), qPrintable(page.id));
                QVERIFY2(!frenchPage.summary.isEmpty(), qPrintable(page.id));
                // and says in one way or another what to put on screen
                if (page.type == QLatin1String("kcm")) {
                    QVERIFY2(page.kcm.startsWith(QStringLiteral("kcm_")), qPrintable(page.id));
                    QVERIFY2(page.component.isEmpty(), qPrintable(page.id));
                } else {
                    QVERIFY2(page.component.startsWith(QStringLiteral("pages/")), qPrintable(page.id));
                    QVERIFY2(page.component.endsWith(QStringLiteral(".qml")), qPrintable(page.id));
                    QVERIFY2(page.component.section(QLatin1Char('/'), 1, 1) == category.id,
                             qPrintable(page.component));
                }
            }
        }
    }

    // two pages answering for the same module id would make the link
    // open one of them at random
    void everyModuleIdLandsOnOnePage()
    {
        const Layout layout = shipped(QStringLiteral("en"));
        QSet<QString> modules;
        for (const Category &c : layout.categories) {
            for (const Page &p : c.pages) {
                QStringList answered = p.aliases;
                if (p.kcm.startsWith(QStringLiteral("kcm_")))
                    answered.append(p.kcm);
                for (const QString &module : answered) {
                    QVERIFY2(module.startsWith(QStringLiteral("kcm_")), qPrintable(module));
                    QVERIFY2(!modules.contains(module), qPrintable(module + QStringLiteral(" twice")));
                    modules.insert(module);
                }
            }
        }
        QVERIFY(modules.size() > 40);
    }

    void everyRequirementIsOneWeCanProbe()
    {
        static const QStringList known = {QStringLiteral("bluetooth"),  QStringLiteral("battery"),
                                          QStringLiteral("touchpad"),   QStringLiteral("tablet"),
                                          QStringLiteral("two-gpus"),   QStringLiteral("windows"),
                                          QStringLiteral("btrfs"),      QStringLiteral("fingerprint"),
                                          QStringLiteral("raye-account"), QStringLiteral("wifi"),
                                          QStringLiteral("online-accounts")};
        for (const Category &c : shipped(QStringLiteral("en")).categories) {
            for (const Page &p : c.pages) {
                if (p.requirement.isEmpty())
                    continue;
                for (const QString &one : p.requirement.split(QLatin1Char('|')))
                    QVERIFY2(known.contains(one), qPrintable(p.id + QLatin1Char(' ') + p.requirement));
            }
        }
    }

    // the launch screen: twelve cards, in the order of the categories,
    // and Windows programs last
    void shippedLayoutHasTheLaunchScreenTiles()
    {
        const Layout layout = shipped(QStringLiteral("en"));
        QCOMPARE(layout.tiles.size(), 12);
        for (int i = 0; i < layout.categories.size(); ++i)
            QCOMPARE(layout.tiles.at(i).target, layout.categories.at(i).id);
        QCOMPARE(layout.tiles.last().target, QStringLiteral("gaming/windows-programs"));
        // a card is on every computer: what it opens needs no hardware
        for (const Tile &t : layout.tiles) {
            if (const Page *p = layout.page(t.target))
                QVERIFY2(p->requirement.isEmpty(), qPrintable(t.target));
        }
        for (const Tile &t : layout.tiles) {
            QVERIFY2(!t.name.isEmpty(), qPrintable(t.target));
            QVERIFY2(!t.summary.isEmpty(), qPrintable(t.target));
            QVERIFY2(!t.icon.isEmpty(), qPrintable(t.target));
        }
    }

    // every page is drawn by Raye: no desktop module hosted in the frame,
    // no page that is only a placeholder, no row saying "not on Raye"
    void everyPageIsARayePage()
    {
        const Layout layout = shipped(QStringLiteral("en"));
        QStringList wrong;
        for (const Category &c : layout.categories) {
            for (const Page &p : c.pages) {
                if (p.type != QLatin1String("qml")) {
                    wrong << p.id + QStringLiteral(" is a ") + p.type;
                    continue;
                }
                QFile qml(QStringLiteral(SOURCE_DIR "/src/qml/") + p.component);
                if (!qml.open(QIODevice::ReadOnly)) {
                    wrong << p.id + QStringLiteral(" has no file");
                    continue;
                }
                if (qml.readAll().contains("KcmHost"))
                    wrong << p.id + QStringLiteral(" hosts a module");
            }
        }
        QDirIterator files(QStringLiteral(SOURCE_DIR "/src/qml/pages"), {QStringLiteral("*.qml")}, QDir::Files,
                           QDirIterator::Subdirectories);
        static const QRegularExpression notHere(QStringLiteral("Not available on Raye|on its way"),
                                                QRegularExpression::CaseInsensitiveOption);
        while (files.hasNext()) {
            QFile f(files.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            if (notHere.match(QString::fromUtf8(f.readAll())).hasMatch())
                wrong << f.fileName();
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral(" | "))));
    }

    // a PC without Wi-Fi is not told about Wi-Fi on the launch screen
    void networkTileWithoutWifi()
    {
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray json = f.readAll();
        for (const QString &lang : {QStringLiteral("en"), QStringLiteral("fr")}) {
            CategoryModel model;
            QVERIFY(model.load(json, lang));
            Router router;
            router.setLayout(model.layout());
            model.follow(&router);
            const auto networkTile = [&] {
                for (const QVariant &t : model.tiles()) {
                    const QVariantMap tile = t.toMap();
                    if (tile.value(QStringLiteral("target")) == QLatin1String("network"))
                        return tile.value(QStringLiteral("summary")).toString();
                }
                return QString();
            };
            const int row = model.categoryOf(QStringLiteral("network"));
            QVERIFY(row >= 0);
            router.setRequirement(QStringLiteral("wifi"), true);
            QSignalSpy tiles(&model, &CategoryModel::tilesChanged);

            router.setRequirement(QStringLiteral("wifi"), false);
            QVERIFY2(!networkTile().contains(QLatin1String("Wi-Fi")), qPrintable(networkTile()));
            QVERIFY(networkTile().contains(QLatin1String("Ethernet")));
            QVERIFY(!model.data(model.index(row), CategoryModel::SummaryRole).toString().contains(QLatin1String("Wi-Fi")));

            router.setRequirement(QStringLiteral("wifi"), true);
            QVERIFY(networkTile().contains(QLatin1String("Wi-Fi")));
            QVERIFY(model.data(model.index(row), CategoryModel::SummaryRole).toString().contains(QLatin1String("Wi-Fi")));
            QCOMPARE(tiles.count(), 2);
        }
    }

    // A launch screen tile has one line for its summary. French runs longer
    // than English and four of them ended in "..." at the usual window size.
    void frenchSummariesFitTheirTile()
    {
        QFile f(QStringLiteral(LAYOUT_FILE));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const int widest = 44;
        QStringList tooLong;
        const auto check = [&](const QJsonValue &item) {
            const QString text = item.toObject().value(QStringLiteral("summary_fr")).toString();
            if (text.size() > widest)
                tooLong << text;
        };
        for (const QJsonValue &tile : root.value(QStringLiteral("tiles")).toArray())
            check(tile);
        for (const QJsonValue &category : root.value(QStringLiteral("categories")).toArray()) {
            check(category);
            check(category.toObject().value(QStringLiteral("summary_without")));
        }
        QVERIFY2(tooLong.isEmpty(), qPrintable(tooLong.join(QStringLiteral(" | "))));
    }

    // the screen lock is set in one place now: how long before the
    // screen turns off, and whether to ask for the password again
    void shippedLayoutHasNoScreenLockerModule()
    {
        const Layout layout = shipped(QStringLiteral("en"));
        QVERIFY(layout.pageForModule(QStringLiteral("kcm_screenlocker")) == nullptr);
        for (const Category &c : layout.categories) {
            for (const Page &p : c.pages)
                QVERIFY2(p.target() != QStringLiteral("raye:advanced"), qPrintable(p.id));
        }
    }

    // every page of the launch screen is in the layout, and the
    // categories are the twelve of the design
    void shippedLayoutCoversTheDesign()
    {
        const Layout layout = shipped(QStringLiteral("en"));
        static const QStringList order = {QStringLiteral("system"),     QStringLiteral("devices"),
                                          QStringLiteral("network"),    QStringLiteral("personalization"),
                                          QStringLiteral("apps"),       QStringLiteral("accounts"),
                                          QStringLiteral("time"),       QStringLiteral("gaming"),
                                          QStringLiteral("accessibility"), QStringLiteral("privacy"),
                                          QStringLiteral("update")};
        QStringList found;
        for (const Category &c : layout.categories)
            found.append(c.id);
        QCOMPARE(found, order);
        for (const QString &id : {QStringLiteral("system/recovery"), QStringLiteral("system/about"),
                                  QStringLiteral("network/wifi"), QStringLiteral("accounts/sign-in"),
                                  QStringLiteral("personalization/taskbar"),
                                  QStringLiteral("gaming/windows-programs"),
                                  QStringLiteral("update/raye-update"), QStringLiteral("system/sound-devices"),
                                  QStringLiteral("network/known-networks"),
                                  QStringLiteral("personalization/start-folders")})
            QVERIFY2(layout.page(id) != nullptr, qPrintable(id));
    }

    // Plasma's own links name these modules; each lands on its Raye page
    void shippedLayoutAnswersPlasmaLinks()
    {
        const Layout layout = shipped(QStringLiteral("en"));
        const QList<QPair<QString, QString>> links = {
            {QStringLiteral("kcm_kscreen"), QStringLiteral("system/display")},
            {QStringLiteral("kcm_nighttime"), QStringLiteral("system/night-light")},
            {QStringLiteral("kcm_pulseaudio"), QStringLiteral("system/sound")},
            {QStringLiteral("kcm_networkmanagement"), QStringLiteral("network/advanced")},
            {QStringLiteral("kcm_mobile_wifi"), QStringLiteral("network/wifi")},
            {QStringLiteral("kcm_bluetooth"), QStringLiteral("devices/bluetooth")},
            {QStringLiteral("kcm_about-distro"), QStringLiteral("system/about")},
            {QStringLiteral("kcm_wallpaper"), QStringLiteral("personalization/background")},
        };
        for (const auto &[module, page] : links) {
            const Page *found = layout.pageForModule(module);
            QVERIFY2(found, qPrintable(module));
            QCOMPARE(found->id, page);
        }
        // window rules and the like have no Raye page: KDE's command shows them
        QVERIFY(layout.pageForModule(QStringLiteral("kcm_kwinrules")) == nullptr);
        QVERIFY(layout.pageForModule(QStringLiteral("kcm_keys")) == nullptr);
    }
};

QTEST_GUILESS_MAIN(TestLayout)
#include "test_layout.moc"
