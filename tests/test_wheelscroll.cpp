// SPDX-License-Identifier: GPL-3.0-or-later
// The mouse wheel in Raye Settings: one notch moves a page by the
// "lines per notch" setting, about 100 px, like on Windows. Qt Quick's own
// Flickable turns a notch into a short, uneven flick, which is what made
// the pages feel slow to scroll.
#include <KLocalizedContext>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTest>
#include <QWheelEvent>

class TestWheelScroll : public QObject
{
    Q_OBJECT

    static void notch(QQuickWindow *window, int angle)
    {
        const QPointF at(window->width() / 2.0, window->height() / 2.0);
        QWheelEvent ev(at, window->mapToGlobal(at), QPoint(), QPoint(0, angle), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(window, &ev);
    }

private Q_SLOTS:
    void oneNotchMovesAboutAHundredPixels()
    {
        QCOMPARE(QGuiApplication::styleHints()->wheelScrollLines(), 3);
        QQmlEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import org.raye.settings
            Window {
                width: 400; height: 300; visible: true
                Flickable {
                    id: flick
                    objectName: "flick"
                    anchors.fill: parent
                    contentHeight: 5000
                    WheelScroll { target: flick }
                }
            })", QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *flick = window->findChild<QQuickItem *>(QStringLiteral("flick"));
        QVERIFY(flick);

        notch(window, -120);
        // the step is animated: wait for it to land
        QTRY_VERIFY(qAbs(flick->property("contentY").toReal() - 102) < 2);
        QTest::qWait(400);
        QVERIFY2(qAbs(flick->property("contentY").toReal() - 102) < 2,
                 qPrintable(QString::number(flick->property("contentY").toReal())));

        // and back up by the same step
        notch(window, 120);
        QTRY_VERIFY(flick->property("contentY").toReal() < 2);
    }
};

QTEST_MAIN(TestWheelScroll)
#include "test_wheelscroll.moc"
