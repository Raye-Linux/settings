// SPDX-License-Identifier: GPL-3.0-or-later
// The Devices and Accounts pages as pages. Every page of the two
// categories must be a real one: the router must not fall back to "This
// page is on its way", the file must load without a QML warning, and each
// row the search advertises must be drawn on it.
#include "layout.h"
#include "router.h"

#include <KLocalizedContext>
#include <KLocalizedString>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QStandardPaths>
#include <QTest>

namespace {
QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg && message.contains(QLatin1String("qrc:")))
        qmlWarnings << message;
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

QStringList anchorsFor(const QString &pageId)
{
    QStringList anchors;
    for (const char *name : {"devices", "accounts"}) {
        QFile file(QStringLiteral(SOURCE_DIR "/data/search/%1.json").arg(QLatin1String(name)));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &entry : entries) {
            const QString id = entry.toObject().value(QStringLiteral("id")).toString();
            if (id.section(QLatin1Char('#'), 0, 0) == pageId)
                anchors << id.section(QLatin1Char('#'), 1);
        }
    }
    return anchors;
}
} // namespace

class TestAccountPages : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain("raye-settings");
    }

    void everyPageIsReal_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("component");
        QFile file(QStringLiteral(LAYOUT_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Layout layout = parseLayout(file.readAll(), QStringLiteral("en"));
        int count = 0;
        for (const Category &category : layout.categories) {
            if (category.id != QLatin1String("devices") && category.id != QLatin1String("accounts"))
                continue;
            for (const Page &page : category.pages) {
                // the Raye account pages import a module of their own,
                // and test_pagereach knows about them
                if (page.requirement == QLatin1String("raye-account"))
                    continue;
                QTest::newRow(qPrintable(page.id)) << page.id << page.component;
                ++count;
            }
            QTest::newRow(qPrintable(category.id + QStringLiteral("-landing"))) << category.id << category.landing;
            ++count;
        }
        // devices: 8 and a landing page; accounts: 5 and a landing page
        QCOMPARE(count, 15);
    }

    void everyPageIsReal()
    {
        QFETCH(QString, id);
        QFETCH(QString, component);
        QVERIFY2(QFile::exists(QStringLiteral(SOURCE_DIR "/src/qml/") + component), qPrintable(component + QStringLiteral(" is not written")));

        qmlWarnings.clear();
        const auto previous = qInstallMessageHandler(collect);
        QQuickView view;
        view.engine()->rootContext()->setContextObject(new KLocalizedContext(view.engine()));
        view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/") + component));
        QCoreApplication::processEvents();
        qInstallMessageHandler(previous);
        QVERIFY2(view.status() == QQuickView::Ready, qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));

        for (const QString &anchor : anchorsFor(id))
            QVERIFY2(rowWithId(view.rootObject(), anchor), qPrintable(id + QLatin1Char('#') + anchor + QStringLiteral(" is not on the page")));
    }

    // a page that has no way of acting says so in words
    void pagesSayWhenTheyCannotAct_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("sentence");
        QTest::newRow("mouse") << "devices/MousePage.qml" << "No mouse found";
        QTest::newRow("touchpad") << "devices/TouchpadPage.qml" << "No touchpad found";
        QTest::newRow("pen") << "devices/PenPage.qml" << "No pen or tablet found";
        QTest::newRow("cameras") << "devices/CamerasPage.qml" << "No camera found";
        QTest::newRow("printers") << "devices/PrintersPage.qml" << "Printing is turned off";
        QTest::newRow("phone") << "devices/PhonePage.qml" << "Phone linking didn't start";
        QTest::newRow("fingerprint") << "accounts/SignInPage.qml" << "This PC has no fingerprint reader.";
    }
    void pagesSayWhenTheyCannotAct()
    {
        QFETCH(QString, file);
        QFETCH(QString, sentence);
        QFile page(QStringLiteral(SOURCE_DIR "/src/qml/pages/") + file);
        QVERIFY(page.open(QIODevice::ReadOnly));
        QVERIFY(page.readAll().contains(sentence.toUtf8()));
    }
};

QTEST_MAIN(TestAccountPages)
#include "test_accountpages.moc"
