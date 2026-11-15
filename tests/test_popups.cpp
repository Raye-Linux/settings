// SPDX-License-Identifier: GPL-3.0-or-later
// Menus, drop-down lists and flyouts open on a solid surface: the page
// under them must not show through. Each one is opened in a window
// over a bright red page and the pixels inside it are read back.
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QGuiApplication>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

#include <memory>

namespace {

const QColor page(255, 0, 0);

// A window filled with red, `popup` built from `qml` inside it.
struct Stage {
    QQmlEngine engine;
    std::unique_ptr<QQuickView> view;
    QObject *popup = nullptr;

    bool build(const QByteArray &qml)
    {
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        view = std::make_unique<QQuickView>(&engine, nullptr);
        view->setColor(page);
        view->resize(800, 600);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QtQuick.Templates as T\nimport org.raye.settings\n"
                          "Item { width: 800; height: 600\n" + qml + "\n}",
                          QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/popuptest.qml")));
        auto *root = qobject_cast<QQuickItem *>(component.create());
        if (!root) {
            qWarning("%s", qPrintable(component.errorString()));
            return false;
        }
        view->setContent(QUrl(), &component, root);
        view->show();
        if (!QTest::qWaitForWindowExposed(view.get()))
            return false;
        popup = root->findChild<QObject *>(QStringLiteral("popup"));
        return popup;
    }

    // the share of pixels inside the open popup that are still the page
    double seeThrough()
    {
        QMetaObject::invokeMethod(popup, "open");
        for (int i = 0; i < 60 && !popup->property("opened").toBool(); ++i)
            QTest::qWait(50);
        QTest::qWait(400); // the opening animation
        auto *item = popup->property("contentItem").value<QQuickItem *>();
        auto *box = item ? item->parentItem() : nullptr; // the popup's own item
        if (!box)
            return 1.0;
        const QRectF r = box->mapRectToScene(QRectF(0, 0, box->width(), box->height()));
        const QImage shot = view->grabWindow();
        int red = 0, all = 0;
        // inset, to stay clear of the rounded corners
        for (int y = int(r.top()) + 6; y < int(r.bottom()) - 6; y += 2) {
            for (int x = int(r.left()) + 6; x < int(r.right()) - 6; x += 2) {
                const QColor c = shot.pixelColor(x, y);
                ++all;
                if (c.red() > 150 && c.green() < 90 && c.blue() < 90)
                    ++red;
            }
        }
        return all ? double(red) / all : 1.0;
    }
};

} // namespace

class TestPopups : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void opaque_data()
    {
        QTest::addColumn<QByteArray>("qml");
        QTest::newRow("app menu") << QByteArray(
            "InstalledAppMenu { objectName: 'popup'; x: 100; y: 100;"
            " actions: [{ id: 'open', text: 'Open' }, { id: 'advanced', text: 'Advanced options' }, { id: 'uninstall', text: 'Uninstall' }] }");
        QTest::newRow("bluetooth menu") << QByteArray(
            "BluetoothDeviceMenu { objectName: 'popup'; x: 100; y: 100;"
            " actions: [{ id: 'remove', text: 'Remove device' }] }");
        QTest::newRow("drop-down list") << QByteArray(
            "ComboBox { x: 100; y: 100; model: ['Name (A to Z)', 'Name (Z to A)', 'Size'];"
            " Component.onCompleted: popup.objectName = 'popup' }");
        QTest::newRow("uninstall flyout") << QByteArray(
            "InstalledAppUninstall { objectName: 'popup'; x: 100; y: 100; appId: 'x'; appName: 'X' }");
        QTest::newRow("flyout with a width only") << QByteArray(
            "Flyout { objectName: 'popup'; x: 100; y: 100; width: 240; contentItem: Text { text: 'Hello'; font: Theme.body } }");
        QTest::newRow("tooltip") << QByteArray(
            "Item { x: 100; y: 100; width: 200; height: 40;"
            " ToolTip { objectName: 'popup'; delay: 0; text: 'Mute all sounds of this app' } }");
    }

    void opaque()
    {
        QFETCH(QByteArray, qml);
        Stage stage;
        QVERIFY(stage.build(qml));
        const double through = stage.seeThrough();
        QVERIFY2(through < 0.01, qPrintable(QStringLiteral("%1% of the popup shows the page under it")
                                                .arg(qRound(through * 100))));
    }

    // Down picks the first row, Enter with nothing picked does nothing
    void menuKeyboard()
    {
        Stage stage;
        QVERIFY(stage.build("ActionMenu { objectName: 'popup'; x: 100; y: 100;"
                            " property string picked; onTriggered: (id) => picked = id;"
                            " actions: [{ id: 'open', text: 'Open' }, { id: 'off', text: 'Off', enabled: false },"
                            " { id: 'uninstall', text: 'Uninstall' }] }"));
        QMetaObject::invokeMethod(stage.popup, "open");
        QTRY_VERIFY(stage.popup->property("opened").toBool());
        QTest::keyClick(stage.view.get(), Qt::Key_Return);
        QCOMPARE(stage.popup->property("picked").toString(), QString());
        QVERIFY(stage.popup->property("opened").toBool());
        QTest::keyClick(stage.view.get(), Qt::Key_Down);
        QTest::keyClick(stage.view.get(), Qt::Key_Return);
        QCOMPARE(stage.popup->property("picked").toString(), QStringLiteral("open"));

        // opened again, it starts from nothing
        QMetaObject::invokeMethod(stage.popup, "open");
        QTRY_VERIFY(stage.popup->property("opened").toBool());
        QTest::keyClick(stage.view.get(), Qt::Key_Down);
        QTest::keyClick(stage.view.get(), Qt::Key_Return);
        QCOMPARE(stage.popup->property("picked").toString(), QStringLiteral("open"));

        QMetaObject::invokeMethod(stage.popup, "open");
        QTRY_VERIFY(stage.popup->property("opened").toBool());
        QTest::keyClick(stage.view.get(), Qt::Key_Up); // from nothing: the last row
        QTest::keyClick(stage.view.get(), Qt::Key_Up); // skips the greyed one
        QTest::keyClick(stage.view.get(), Qt::Key_Up); // and wraps
        QTest::keyClick(stage.view.get(), Qt::Key_Return);
        QCOMPARE(stage.popup->property("picked").toString(), QStringLiteral("uninstall"));
    }

    // Menus, lists, flyouts and tooltips open on the two Raye surfaces:
    // a bare template has no size or background, and the controls of
    // the session's style would bring their own colours.
    void everyPopupIsAFlyout()
    {
        QDirIterator files(QStringLiteral(SOURCE_DIR "/src/qml"), {QStringLiteral("*.qml")}, QDir::Files,
                           QDirIterator::Subdirectories);
        QStringList bare;
        static const QRegularExpression comment(QStringLiteral("//[^\\n]*"));
        static const QRegularExpression popup(QStringLiteral(
            "\\b\\w+\\.(Popup|Menu|ToolTip|Drawer)\\s*\\{|\\b\\w+\\.ToolTip\\.\\w+\\s*:"));
        while (files.hasNext()) {
            const QString path = files.next();
            if (path.endsWith(QLatin1String("/ui/Flyout.qml")) || path.endsWith(QLatin1String("/ui/ToolTip.qml")))
                continue;
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString code = QString::fromUtf8(f.readAll()).remove(comment);
            const QRegularExpressionMatch m = popup.match(code);
            if (m.hasMatch())
                bare << path.mid(QStringLiteral(SOURCE_DIR).size() + 1) + QLatin1Char(' ') + m.captured(0);
        }
        QVERIFY2(bare.isEmpty(), qPrintable(bare.join(QLatin1Char('\n'))));
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("raye-settings");
    TestPopups test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "test_popups.moc"
