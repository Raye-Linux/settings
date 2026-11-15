// SPDX-License-Identifier: GPL-3.0-or-later
// System > Display > Identify, on two screens: one number per screen, in
// the bottom left corner of that screen, the same window every time.
#include <KLocalizedContext>
#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTemporaryFile>
#include <QTest>

namespace {

QList<QQuickWindow *> badges()
{
    QList<QQuickWindow *> found;
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *q = qobject_cast<QQuickWindow *>(w); q && q->title() == QLatin1String("Display number"))
            found << q;
    return found;
}

QList<QQuickWindow *> shown()
{
    QList<QQuickWindow *> found;
    for (QQuickWindow *w : badges())
        if (w->isVisible())
            found << w;
    return found;
}

QScreen *screenNamed(const QString &name)
{
    for (QScreen *s : QGuiApplication::screens())
        if (s->name() == name)
            return s;
    return nullptr;
}

} // namespace

class TestDisplayIdentify : public QObject
{
    Q_OBJECT

public:
    // two screens side by side, like a laptop with a monitor on its right
    static void initMain()
    {
        static QTemporaryFile config;
        config.open();
        config.write(R"({"screens": [
            {"name": "eDP-1", "x": 0, "y": 0, "width": 1366, "height": 768, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1},
            {"name": "DP-1", "x": 1366, "y": 0, "width": 1920, "height": 1080, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}
        ]})");
        config.flush();
        const QByteArray platform = "offscreen:configfile=" + config.fileName().toLocal8Bit();
        qputenv("QT_QPA_PLATFORM", platform);
    }

private Q_SLOTS:
    void oneNumberPerScreen()
    {
        QCOMPARE(QGuiApplication::screens().size(), 2);

        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/org/raye/settings/pages/system/DisplayIdentify.qml")));
        QVariantMap one{{QStringLiteral("connector"), QStringLiteral("eDP-1")}, {QStringLiteral("number"), 1}};
        QVariantMap two{{QStringLiteral("connector"), QStringLiteral("DP-1")}, {QStringLiteral("number"), 2}};
        std::unique_ptr<QObject> identify(component.createWithInitialProperties(
            {{QStringLiteral("outputs"), QVariantList{one, two}}}));
        QVERIFY2(identify, qPrintable(component.errorString()));
        QCOMPARE(identify->property("duration").toInt(), 3000);

        QVERIFY(shown().isEmpty());
        QMetaObject::invokeMethod(identify.get(), "show");
        QTRY_COMPARE(shown().size(), 2);

        const QList<QQuickWindow *> first = shown();
        for (QQuickWindow *w : first) {
            const int number = w->property("output").toMap().value(QStringLiteral("number")).toInt();
            QScreen *screen = screenNamed(number == 1 ? QStringLiteral("eDP-1") : QStringLiteral("DP-1"));
            QVERIFY(screen);
            QCOMPARE(w->screen(), screen);

            // a normal window, not a popup: on Wayland a popup is placed
            // beside its parent, so both numbers landed on one screen
            const auto type = Qt::WindowType(int(w->flags() & Qt::WindowType_Mask));
            QVERIFY(type != Qt::ToolTip && type != Qt::Popup);

            auto *layer = LayerShellQt::Window::get(w);
            QVERIFY(layer);
            // no screen of its own: it takes the window's
            QVERIFY(!layer->wantsToBeOnActiveScreen());
            QVERIFY(!layer->screen() || layer->screen() == screen);
            QCOMPARE(layer->anchors(), LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorLeft
                                                                     | LayerShellQt::Window::AnchorBottom));
            QCOMPARE(layer->layer(), LayerShellQt::Window::LayerOverlay);

            // bottom left corner of its own screen
            const QRect g = w->geometry();
            QVERIFY2(screen->geometry().contains(g), qPrintable(QStringLiteral("%1 outside its screen").arg(number)));
            QCOMPARE(g.left(), screen->geometry().left());
            QCOMPARE(g.bottom(), screen->geometry().bottom());
        }

        // clicking again keeps the same two windows
        QMetaObject::invokeMethod(identify.get(), "show");
        QTest::qWait(100);
        QCOMPARE(shown(), first);
        QCOMPARE(badges().size(), 2);

        // gone after about three seconds
        QTest::qWait(2300);
        QCOMPARE(shown().size(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(shown().isEmpty(), 1500);

        // and the next time, still the same windows
        QMetaObject::invokeMethod(identify.get(), "show");
        QTRY_COMPARE(shown().size(), 2);
        QCOMPARE(shown(), first);
        QCOMPARE(badges().size(), 2);
    }
};

QTEST_MAIN(TestDisplayIdentify)
#include "test_display_identify.moc"
