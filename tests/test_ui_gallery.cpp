// SPDX-License-Identifier: GPL-3.0-or-later
// The Settings components, measured where they are drawn: tests/ui/
// Gallery.qml is loaded in a window and every check reads the items as
// they sit on screen (a card is 68 px high, its text starts at 58 px,
// a switch is 40 x 20), in Raye Night and Raye Day.
//
// RAYE_GALLERY=night,0 (mode, page, then "dialog" and/or "rtl") shows
// the gallery instead of testing, at RAYE_GALLERY_SIZE (1280x800): the
// way to look at it under Xvfb.
#include "windoweffects.h"

#include <KLocalizedContext>
#include <KLocalizedString>
#include <QAccessible>
#include <QClipboard>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <memory>

namespace {

QStringList qmlWarnings;

void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // anything QML complains about is a broken component
    if (type != QtDebugMsg && type != QtInfoMsg
        && (qstrcmp(context.category, "qml") == 0 || qstrncmp(context.category, "qt.qml", 6) == 0
            || message.contains(QLatin1String("qrc:")) || message.contains(QLatin1String("Gallery.qml")))) {
        qmlWarnings << message;
    }
    fprintf(stderr, "%s\n", qPrintable(message));
}

QUrl galleryUrl()
{
    return QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/tests/ui/Gallery.qml"));
}

// A view on the gallery, with i18n() and the Raye module, the way the
// application window has them.
QQuickView *makeView(QQmlEngine *engine, const QSize &size)
{
    auto *view = new QQuickView(engine, nullptr);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->resize(size);
    view->setSource(galleryUrl());
    return view;
}

QObject *theme(QQmlEngine *engine)
{
    return engine->singletonInstance<QObject *>("org.raye.settings", "Theme");
}

QColor color(QObject *object, const char *name)
{
    return object->property(name).value<QColor>();
}

QPointF topLeftIn(QQuickItem *item, QQuickItem *within)
{
    return item->mapToItem(within, QPointF(0, 0));
}

// Every item drawn under `root`, in the scene's order. A Repeater's
// items are not QObject children of anything, so walk the scene.
QList<QQuickItem *> itemsUnder(QQuickItem *root)
{
    QList<QQuickItem *> items;
    const auto children = root->childItems();
    for (QQuickItem *child : children) {
        items << child;
        items << itemsUnder(child);
    }
    return items;
}

// The descendant Text (or icon...) whose QML type name starts with
// `type` and, for texts, says `text`.
QQuickItem *descendant(QQuickItem *root, const char *type, const QString &text = QString())
{
    if (!root)
        return nullptr;
    const auto items = itemsUnder(root);
    for (QQuickItem *item : items) {
        if (!QByteArray(item->metaObject()->className()).startsWith(type))
            continue;
        if (text.isEmpty() || item->property("text").toString() == text)
            return item;
    }
    return nullptr;
}

QPoint sceneCenter(QQuickItem *item, qreal dx = 0)
{
    const QPointF c = item->mapToScene(QPointF(item->width() / 2 + dx, item->height() / 2));
    return c.toPoint();
}

double luminance(const QColor &c)
{
    auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The compositor as the tests see it: does it blur, and what was asked.
struct FakeBlur : WindowEffects::Backend {
    bool canBlur = true;
    int calls = 0;
    bool lastEnable = false;
    QRect lastRegion;
    bool available() const override { return canBlur; }
    void setBlur(QWindow *, bool enable, const QRect &region) override
    {
        ++calls;
        lastEnable = enable;
        lastRegion = region;
    }
};

} // namespace

class TestUiGallery : public QObject
{
    Q_OBJECT

    QQmlEngine *m_engine = nullptr;
    QQuickView *m_view = nullptr;
    QObject *m_theme = nullptr;

    QQuickItem *root() const { return m_view->rootObject(); }
    QQuickItem *item(const char *name) const
    {
        const auto items = itemsUnder(root());
        for (QQuickItem *found : items) {
            if (found->objectName() == QLatin1String(name))
                return found;
        }
        qFatal("no item named %s in the gallery", name);
        return nullptr;
    }
    void showPage(int page, const char *mode = "night")
    {
        root()->setProperty("mode", QString::fromLatin1(mode));
        root()->setProperty("page", page);
        settle();
    }
    // layouts and bindings catch up (animations are off in the tests)
    void settle()
    {
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
            QTest::qWait(10);
        }
    }

private Q_SLOTS:
    void initTestCase()
    {
        qInstallMessageHandler(collect);
        KLocalizedString::setApplicationDomain("raye-settings");
        // as with a screen reader on from the start
        QAccessible::setActive(true);
        m_engine = new QQmlEngine(this);
        m_engine->rootContext()->setContextObject(new KLocalizedContext(m_engine));
        m_view = makeView(m_engine, QSize(1280, 800));
        QVERIFY2(m_view->status() == QQuickView::Ready, qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
        m_theme = theme(m_engine);
        QVERIFY(m_theme);
        // every state at once: no animation in between
        m_theme->setProperty("motionFactor", 0);
        m_view->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_view));
        settle();
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }

    void cleanupTestCase()
    {
        delete m_view;
        qInstallMessageHandler(nullptr);
    }

    // Each page in each mode loads and lays out without a QML complaint.
    void everyPageLoads_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("page");
        for (const char *mode : {"night", "day"}) {
            for (int page = 0; page < 3; ++page)
                QTest::addRow("%s-%d", mode, page) << QString::fromLatin1(mode) << page;
        }
    }
    void everyPageLoads()
    {
        QFETCH(QString, mode);
        QFETCH(int, page);
        qmlWarnings.clear();
        showPage(page, mode.toLatin1().constData());
        QCOMPARE(m_theme->property("night").toBool(), mode == QLatin1String("night"));
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    }

    // The Raye tokens, Night and Day (theming.md, raye-tokens.json).
    void tokens()
    {
        showPage(0, "night");
        QCOMPARE(color(m_theme, "text"), QColor(QStringLiteral("#f1f0f4")));
        QCOMPARE(color(m_theme, "textSecondary"), QColor(QStringLiteral("#b5b3bc")));
        QCOMPARE(color(m_theme, "accent"), QColor(QStringLiteral("#7b3cff")));
        QCOMPARE(color(m_theme, "accentText"), QColor(QStringLiteral("#b48cff")));
        QCOMPARE(color(m_theme, "backdropSolid"), QColor(QStringLiteral("#141318")));
        QCOMPARE(color(m_theme, "critical"), QColor(QStringLiteral("#ff6b85")));
        showPage(0, "day");
        QCOMPARE(color(m_theme, "text"), QColor(QStringLiteral("#17151c")));
        QCOMPARE(color(m_theme, "textSecondary"), QColor(QStringLiteral("#54525b")));
        QCOMPARE(color(m_theme, "accentText"), QColor(QStringLiteral("#6a2fe6")));
        QCOMPARE(color(m_theme, "backdropSolid"), QColor(QStringLiteral("#f5f4f7")));
        QCOMPARE(color(m_theme, "cardSolid"), QColor(QStringLiteral("#ffffff")));
        // text reads on its layer in both modes
        for (const char *mode : {"night", "day"}) {
            showPage(0, mode);
            const QColor layer = color(m_theme, "layerSolid");
            QVERIFY2(contrast(color(m_theme, "text"), layer) >= 7, mode);
            QVERIFY2(contrast(color(m_theme, "textSecondary"), layer) >= 4.5, mode);
            QVERIFY2(contrast(color(m_theme, "accentText"), layer) >= 4.5, mode);
            // a greyed-out card is dim, but still reads (3:1, as for controls)
            const QColor card = color(m_theme, "card");
            const auto over = [&card](int top, int under) { return int(top * card.alphaF() + under * (1 - card.alphaF())); };
            const QColor onCard(over(card.red(), layer.red()), over(card.green(), layer.green()), over(card.blue(), layer.blue()));
            QVERIFY2(contrast(color(m_theme, "textDisabled"), onCard) >= 3, mode);
        }
        showPage(0, "night");
    }

    // A component reads the same Theme as a page: Day on the module's
    // singleton turns a card in another folder Day too.
    void themeIsShared()
    {
        showPage(0, "day");
        QQuickItem *card = item("cardPlain");
        // background > rounded clip > the card's fill
        QQuickItem *background = card->property("background").value<QQuickItem *>();
        QVERIFY(background && !background->childItems().isEmpty());
        QQuickItem *fill = background->childItems().first()->childItems().value(0);
        QVERIFY(fill);
        QCOMPARE(fill->property("color").value<QColor>(), color(m_theme, "card"));
        QCOMPARE(color(m_theme, "card"), QColor(QStringLiteral("#d9ffffff")));
        showPage(0, "night");
        QCOMPARE(fill->property("color").value<QColor>(), QColor(QStringLiteral("#0dffffff")));
    }

    // A SettingsCard row: 68 high with a description, 64 without, icon at
    // 18, text at 58, 4 px between cards; the same in Day.
    void cardMetrics_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("night") << QStringLiteral("night");
        QTest::newRow("day") << QStringLiteral("day");
    }
    void cardMetrics()
    {
        QFETCH(QString, mode);
        showPage(0, mode.toLatin1().constData());
        QQuickItem *withText = item("cardDesc");
        QQuickItem *plain = item("cardPlain");
        QVERIFY(withText && plain);
        QCOMPARE(withText->height(), 68.0);
        QCOMPARE(plain->height(), 64.0);
        QCOMPARE(plain->y() - (withText->y() + withText->height()), 4.0);

        QQuickItem *title = descendant(withText, "QQuickText", QStringLiteral("Card with a description"));
        QVERIFY(title);
        QCOMPARE(topLeftIn(title, withText).x(), 58.0);
        QQuickItem *icon = descendant(withText, "Icon");
        QVERIFY(icon);
        QCOMPARE(topLeftIn(icon, withText).x(), 18.0);
        QCOMPARE(icon->width(), 20.0);
        // the control sits 16 px from the right edge, centred
        QQuickItem *control = withText->property("controlItem").value<QQuickItem *>();
        QVERIFY(control);
        QCOMPARE(topLeftIn(control, withText).x() + control->width(), withText->width() - 16);
        QCOMPARE(withText->property("wrapped").toBool(), false);

        // a navigating card ends with the 13 px chevron, 16 from the edge
        QQuickItem *nav = item("navCard");
        QQuickItem *chevron = descendant(nav, "Glyph");
        QVERIFY(chevron);
        QCOMPARE(chevron->width(), 13.0);
        QCOMPARE(topLeftIn(chevron, nav).x() + chevron->width(), nav->width() - 16);

        // the switch in a card, and the controls of the second page
        QQuickItem *toggle = item("toggleInCard");
        QCOMPARE(toggle->width(), 40.0);
        QCOMPARE(toggle->height(), 20.0);
        QQuickItem *slider = item("sliderCard")->property("slider").value<QQuickItem *>();
        QVERIFY(slider);
        QCOMPARE(slider->width(), 240.0);

        showPage(1, mode.toLatin1().constData());
        QCOMPARE(item("button")->height(), 32.0);
        QCOMPARE(item("combo")->height(), 32.0);
        QCOMPARE(item("textField")->height(), 32.0);
        QCOMPARE(item("toggleOff")->width(), 40.0);
        QCOMPARE(item("progress")->height(), 3.0);
        showPage(0, "night");
    }

    // Below 476 px the control goes under the text, lined up with it.
    void narrowCardWraps()
    {
        showPage(0);
        QQuickItem *card = item("cardWrapped");
        QCOMPARE(card->width(), 400.0);
        QVERIFY(card->property("wrapped").toBool());
        QQuickItem *control = card->property("controlItem").value<QQuickItem *>();
        QQuickItem *description = descendant(card, "QQuickText", QStringLiteral("Below 476 px the control goes under the text"));
        QVERIFY(control && description);
        QCOMPARE(topLeftIn(control, card).x(), 58.0);
        QVERIFY(topLeftIn(control, card).y() >= topLeftIn(description, card).y() + description->height());
        QVERIFY(card->height() > 68);
    }

    // Right to left: the icon moves to the right edge, the chevron to the left.
    void mirrored()
    {
        QQmlComponent component(m_engine);
        component.setData(R"(import QtQuick
import org.raye.settings
NavCard {
    LayoutMirroring.enabled: true
    LayoutMirroring.childrenInherit: true
    width: 600
    iconName: "preferences-system"
    title: "Display"
})", QUrl(QStringLiteral("file:///mirrored.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *card = qobject_cast<QQuickItem *>(object.get());
        QQuickItem *icon = descendant(card, "Icon");
        QQuickItem *title = descendant(card, "QQuickText", QStringLiteral("Display"));
        QQuickItem *chevron = descendant(card, "Glyph");
        QVERIFY(icon && title && chevron);
        QCOMPARE(topLeftIn(icon, card).x() + icon->width(), 600.0 - 18);
        QCOMPARE(topLeftIn(title, card).x() + title->width(), 600.0 - 58);
        QCOMPARE(topLeftIn(chevron, card).x(), 16.0);
        // the words themselves start at the right
        QCOMPARE(title->property("effectiveHorizontalAlignment").toInt(), int(Qt::AlignRight));
    }

    // A mirrored slider fills from the right edge to its thumb.
    void mirroredSlider()
    {
        QQmlComponent component(m_engine);
        component.setData(R"(import QtQuick
import org.raye.settings
Slider { LayoutMirroring.enabled: true; width: 240; from: 0; to: 100; value: 70 })",
                          QUrl(QStringLiteral("file:///slider.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *slider = qobject_cast<QQuickItem *>(object.get());
        auto *rail = slider->property("background").value<QQuickItem *>();
        auto *handle = slider->property("handle").value<QQuickItem *>();
        QVERIFY(rail && handle && rail->childItems().size() >= 2);
        QQuickItem *fill = rail->childItems().at(1);
        const qreal thumb = handle->mapToItem(rail, QPointF(handle->width() / 2, 0)).x();
        QCOMPARE(fill->x() + fill->width(), rail->width());
        QVERIFY(std::abs(fill->x() - thumb) <= 1);
        QVERIFY(thumb < rail->width() / 2);
    }

    // A toggle card flips from anywhere on the card, from the switch
    // itself, and from Space and Enter; each time it says so once.
    void toggleCard()
    {
        showPage(0);
        QQuickItem *card = item("toggleCard");
        QVERIFY(card->property("checked").toBool());
        QSignalSpy toggled(card, SIGNAL(toggled()));

        // on the title, far from the switch
        QTest::mouseClick(m_view, Qt::LeftButton, {}, card->mapToScene(QPointF(120, card->height() / 2)).toPoint());
        QTRY_COMPARE(card->property("checked").toBool(), false);
        QCOMPARE(toggled.count(), 1);

        QQuickItem *control = card->property("controlItem").value<QQuickItem *>();
        QQuickItem *toggle = descendant(control, "ToggleSwitch");
        QVERIFY(toggle);
        QCOMPARE(toggle->property("checked").toBool(), false);
        QTest::mouseClick(m_view, Qt::LeftButton, {}, sceneCenter(toggle));
        QTRY_COMPARE(card->property("checked").toBool(), true);
        QCOMPARE(toggle->property("checked").toBool(), true);
        QCOMPARE(toggled.count(), 2);

        card->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(m_view, Qt::Key_Space);
        QTRY_COMPARE(card->property("checked").toBool(), false);
        QCOMPARE(toggle->property("checked").toBool(), false);
        QTest::keyClick(m_view, Qt::Key_Return);
        QTRY_COMPARE(card->property("checked").toBool(), true);
        QCOMPARE(toggled.count(), 4);
        // the label beside the switch follows
        QVERIFY(descendant(control, "QQuickText", QStringLiteral("On")));
    }

    // Down and Up walk from card to card.
    void arrowKeysWalk()
    {
        showPage(0);
        QQuickItem *nav = item("navCard");
        nav->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(nav->hasActiveFocus());
        QTest::keyClick(m_view, Qt::Key_Down);
        QQuickItem *next = m_view->activeFocusItem();
        QVERIFY(next && next != nav);
        QTest::keyClick(m_view, Qt::Key_Up);
        QCOMPARE(m_view->activeFocusItem(), nav);
    }

    // The expander opens and closes on a click; its rows line up with
    // the header's text and are at least 52 high.
    void expander()
    {
        showPage(0);
        QQuickItem *expander = item("expander");
        QVERIFY(expander->property("expanded").toBool());
        QQuickItem *row = item("expanderRow");
        QVERIFY(row->height() >= 52);
        QQuickItem *rowText = descendant(row, "QQuickText", QStringLiteral("When plugged in, turn off my screen after"));
        QVERIFY(rowText);
        QCOMPARE(topLeftIn(rowText, expander).x(), 58.0);
        const qreal open = expander->height();

        QSignalSpy toggled(expander, SIGNAL(toggled()));
        QTest::mouseClick(m_view, Qt::LeftButton, {}, expander->mapToScene(QPointF(100, 30)).toPoint());
        QTRY_COMPARE(expander->property("expanded").toBool(), false);
        QCOMPARE(toggled.count(), 1);
        QTRY_VERIFY(expander->height() < open);
        QTRY_COMPARE(expander->height(), 68.0);
        QTest::mouseClick(m_view, Qt::LeftButton, {}, expander->mapToScene(QPointF(100, 30)).toPoint());
        QTRY_COMPARE(expander->height(), open);
    }

    // Fluent durations, scaled by the animation speed, nothing when off.
    void motion()
    {
        auto durations = [this] {
            return QList<int>{m_theme->property("durationFaster").toInt(), m_theme->property("durationFast").toInt(),
                              m_theme->property("durationNormal").toInt()};
        };
        m_theme->setProperty("motionFactor", 1.0);
        QCOMPARE(durations(), (QList<int>{83, 167, 250}));
        m_theme->setProperty("motionFactor", 0.5);
        QCOMPARE(durations(), (QList<int>{42, 84, 125}));
        m_theme->setProperty("motionFactor", 2.0);
        QCOMPARE(durations(), (QList<int>{166, 334, 500}));
        // "no animations": Kirigami's 1 ms
        m_theme->setProperty("motionFactor", 1.0 / 200);
        QVERIFY(m_theme->property("animationsOff").toBool());
        QCOMPARE(durations(), (QList<int>{0, 0, 0}));
        m_theme->setProperty("motionFactor", 0);
    }

    // Any accent gives shades that read: text on it, and accent text on
    // the window, at 4.5:1 at least.
    void accentShades_data()
    {
        QTest::addColumn<QString>("accent");
        for (const char *c : {"#3daee9", "#e93d58", "#ffd700", "#1d99f3", "#2e7d32", "#000080", "#ff8c00"})
            QTest::newRow(c) << QString::fromLatin1(c);
    }
    void accentShades()
    {
        QFETCH(QString, accent);
        for (bool night : {true, false}) {
            QVariant shades;
            QVERIFY(QMetaObject::invokeMethod(m_theme, "deriveAccent", Q_RETURN_ARG(QVariant, shades),
                                              Q_ARG(QVariant, QColor(accent)), Q_ARG(QVariant, night)));
            const QVariantMap map = shades.toMap();
            const QColor base(night ? QStringLiteral("#130e1f") : QStringLiteral("#f6f3fb"));
            const QColor text = map.value(QStringLiteral("text")).value<QColor>();
            const QColor onAccent = map.value(QStringLiteral("onAccent")).value<QColor>();
            QVERIFY2(contrast(text, base) >= 4.5, qPrintable(accent + QLatin1String(night ? " night" : " day")));
            QVERIFY2(contrast(onAccent, QColor(accent)) >= 3, qPrintable(accent));
        }
    }

    // The accent follows the colour scheme; a missing or stock scheme
    // paints Raye Night with the Raye violet.
    void scheme()
    {
        auto read = [this](const char *bg, const char *text, const char *highlight) {
            QVariant result;
            QMetaObject::invokeMethod(m_theme, "readScheme", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QColor(bg)),
                                      Q_ARG(QVariant, QColor(text)), Q_ARG(QVariant, QColor(highlight)));
            return result.toMap();
        };
        QVariantMap s = read("#000000", "#000000", "#000000");
        QVERIFY(s.value(QStringLiteral("night")).toBool());
        QCOMPARE(s.value(QStringLiteral("accent")).value<QColor>(), QColor(QStringLiteral("#7b3cff")));
        s = read("#eff0f1", "#232627", "#3daee9");
        QVERIFY(s.value(QStringLiteral("night")).toBool());
        QCOMPARE(s.value(QStringLiteral("accent")).value<QColor>(), QColor(QStringLiteral("#7b3cff")));
        s = read("#f6f3fb", "#1a1028", "#e93d58");
        QVERIFY(!s.value(QStringLiteral("night")).toBool());
        QCOMPARE(s.value(QStringLiteral("accent")).value<QColor>(), QColor(QStringLiteral("#e93d58")));
        s = read("#130e1f", "#f3eaff", "#7b3cff");
        QVERIFY(s.value(QStringLiteral("night")).toBool());
    }

    // Every control the keyboard reaches has a name a screen reader says.
    void accessibleNames_data()
    {
        QTest::addColumn<int>("page");
        QTest::newRow("cards") << 0;
        QTest::newRow("controls") << 1;
        QTest::newRow("other") << 2;
    }
    void accessibleNames()
    {
        QFETCH(int, page);
        showPage(page);
        QAccessible::setActive(true);
        int checked = 0;
        const auto items = itemsUnder(root());
        for (QQuickItem *control : items) {
            if (!control->isVisible() || !control->activeFocusOnTab() || !control->isEnabled())
                continue;
            QAccessibleInterface *a = QAccessible::queryAccessibleInterface(control);
            QVERIFY(a);
            QString name = a->text(QAccessible::Name);
            // Qt keeps a password field's name to itself: it says what it is for in the description
            if (name.isEmpty() && a->state().passwordEdit)
                name = a->text(QAccessible::Description);
            QVERIFY2(!name.isEmpty(), control->metaObject()->className());
            ++checked;
        }
        QVERIFY(checked > 10);
    }

    // The drop-down opens with the chosen item over the field; Esc closes.
    void comboOpensOnChoice()
    {
        showPage(1);
        QQuickItem *combo = item("combo");
        combo->setProperty("currentIndex", 2);
        QTest::mouseClick(m_view, Qt::LeftButton, {}, sceneCenter(combo));
        QObject *popup = combo->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QQuickItem *list = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(list);
        // the third item's middle is on the field's middle
        const qreal itemMiddle = list->mapToScene(QPointF(0, 2 * 32 + 16)).y();
        const qreal fieldMiddle = combo->mapToScene(QPointF(0, combo->height() / 2)).y();
        QVERIFY2(std::abs(itemMiddle - fieldMiddle) <= 1, qPrintable(QString::number(itemMiddle - fieldMiddle)));
        QTest::keyClick(m_view, Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 2);
        combo->setProperty("currentIndex", 0);
    }

    // The dialog: Esc is the close button, Enter the accent one.
    void dialog()
    {
        QObject *dialog = root()->findChild<QObject *>(QStringLiteral("dialog"));
        QVERIFY(dialog);
        QSignalSpy rejected(dialog, SIGNAL(rejected()));
        QSignalSpy accepted(dialog, SIGNAL(accepted()));
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        const qreal width = dialog->property("width").toReal();
        QVERIFY(width >= 320 && width <= 548);
        // title, then the text, then the buttons, all on the dialog's surface
        auto *header = dialog->property("header").value<QQuickItem *>();
        auto *body = dialog->property("contentItem").value<QQuickItem *>();
        auto *footer = dialog->property("footer").value<QQuickItem *>();
        auto *surface = dialog->property("background").value<QQuickItem *>();
        QVERIFY(header && body && footer && surface);
        const auto top = [](QQuickItem *i) { return i->mapToScene(QPointF(0, 0)).y(); };
        QVERIFY(top(header) + header->height() <= top(body));
        QVERIFY(top(body) + body->height() <= top(footer));
        QCOMPARE(top(surface), top(header));
        QCOMPARE(top(surface) + surface->height(), top(footer) + footer->height());
        QVERIFY(body->width() <= 548 - 2 * 24);
        QTest::keyClick(m_view, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(rejected.count(), 1);

        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(m_view, Qt::Key_Return);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(accepted.count(), 1);
    }

    // A long path folds its first parents into "…".
    void breadcrumbFolds()
    {
        showPage(2);
        QCOMPARE(item("crumbsWide")->property("folded").toInt(), 0);
        // each crumb is drawn as wide as its text
        for (const QString &text : {QStringLiteral("System"), QStringLiteral("Display")}) {
            QQuickItem *label = descendant(item("crumbsWide"), "QQuickText", text);
            QVERIFY(label);
            QVERIFY(label->width() > 20);
            QVERIFY(label->parentItem()->width() >= label->width());
        }
        QVERIFY(item("crumbsNarrow")->property("folded").toInt() >= 1);
    }

    // The page frame finds a row by its searchId and lists them all.
    void scaffoldAnchors()
    {
        showPage(2);
        QQuickItem *scaffold = item("scaffold");
        QVariant found;
        QMetaObject::invokeMethod(scaffold, "scrollToAnchor", Q_RETURN_ARG(QVariant, found),
                                  Q_ARG(QVariant, QStringLiteral("demo-anchor")));
        QVERIFY(found.toBool());
        QMetaObject::invokeMethod(scaffold, "scrollToAnchor", Q_RETURN_ARG(QVariant, found),
                                  Q_ARG(QVariant, QStringLiteral("not-on-this-page")));
        QVERIFY(!found.toBool());
        QVariant ids;
        QMetaObject::invokeMethod(scaffold, "searchIds", Q_RETURN_ARG(QVariant, ids));
        QVERIFY(ids.toStringList().contains(QStringLiteral("demo-anchor")));
    }

    // A password shows as dots until the eye is on, and never leaves
    // the field through the clipboard.
    void passwordField()
    {
        showPage(1);
        QQuickItem *field = item("passwordField");
        QCOMPARE(field->property("echoMode").toInt(), 2); // TextInput.Password
        field->setProperty("revealed", true);
        QCOMPARE(field->property("echoMode").toInt(), 0);
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        field->forceActiveFocus();
        QMetaObject::invokeMethod(field, "selectAll");
        QTest::keyClick(m_view, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(m_view, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("before"));
        QCOMPARE(field->property("text").toString(), QStringLiteral("not a real one"));
        field->setProperty("revealed", false);
    }

    // Copy puts the value on the clipboard and the button says Copied.
    void infoCardCopies()
    {
        showPage(0);
        QQuickItem *card = item("infoCard");
        QSignalSpy copied(card, SIGNAL(copied()));
        QQuickItem *button = descendant(card->property("controlItem").value<QQuickItem *>(), "Button");
        QVERIFY(button);
        QTest::mouseClick(m_view, Qt::LeftButton, {}, sceneCenter(button));
        QTRY_COMPARE(copied.count(), 1);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("raye-laptop"));
        QCOMPARE(button->property("text").toString(), QStringLiteral("Copied"));
    }

    // The window backdrop: blur when the compositor has it and the user
    // wants it, opaque otherwise, and a request to undo it.
    void windowEffects()
    {
        QWindow window;
        WindowEffects effects;
        auto *fake = new FakeBlur;
        effects.setBackend(fake);
        QSignalSpy changed(&effects, &WindowEffects::translucentChanged);
        effects.setWindow(&window);
        QCOMPARE(fake->calls, 0); // nothing before the component is complete
        effects.componentComplete();
        QCOMPARE(fake->calls, 1);
        QVERIFY(fake->lastEnable);
        QVERIFY(effects.translucent());
        QVERIFY(effects.blurAvailable());
        QCOMPARE(changed.count(), 1);

        effects.setRegion(QRect(0, 0, 300, 200));
        QCOMPARE(fake->lastRegion, QRect(0, 0, 300, 200));

        // Transparency effects off
        effects.setBlurBehind(false);
        QVERIFY(!fake->lastEnable);
        QVERIFY(!effects.translucent());
        const int calls = fake->calls;
        effects.refresh();
        QCOMPARE(fake->calls, calls); // nothing more to undo

        // the compositor without blur: never asked, opaque
        effects.setBlurBehind(true);
        fake->canBlur = false;
        effects.refresh();
        QVERIFY(!fake->lastEnable);
        QVERIFY(!effects.translucent());
        QVERIFY(!effects.blurAvailable());
        const int after = fake->calls;
        effects.refresh();
        QCOMPARE(fake->calls, after);

        // blur back on the compositor, then the window goes away
        fake->canBlur = true;
        effects.refresh();
        QVERIFY(effects.translucent());
        effects.setWindow(nullptr);
        QVERIFY(!fake->lastEnable);
        QVERIFY(!effects.translucent());
    }
};

// RAYE_GALLERY=night,1[,dialog][,rtl]: show the gallery until closed.
static int showGallery(const QString &spec)
{
    const QStringList parts = spec.split(QLatin1Char(','));
    const QStringList size = qEnvironmentVariable("RAYE_GALLERY_SIZE", QStringLiteral("1280x800")).split(QLatin1Char('x'));
    KLocalizedString::setApplicationDomain("raye-settings");
    QQmlEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    std::unique_ptr<QQuickView> view(makeView(&engine, QSize(size.value(0).toInt(), size.value(1).toInt())));
    if (view->status() != QQuickView::Ready)
        return 1;
    view->rootObject()->setProperty("mode", parts.value(0, QStringLiteral("night")));
    view->rootObject()->setProperty("page", parts.value(1).toInt());
    view->rootObject()->setProperty("dialogOpen", parts.contains(QLatin1String("dialog")));
    view->rootObject()->setProperty("rtl", parts.contains(QLatin1String("rtl")));
    view->show();
    return QGuiApplication::exec();
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString spec = qEnvironmentVariable("RAYE_GALLERY");
    if (!spec.isEmpty())
        return showGallery(spec);
    TestUiGallery test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ui_gallery.moc"
