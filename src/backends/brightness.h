// SPDX-License-Identifier: GPL-3.0-or-later
// The brightness of each screen that has one to set: the built-in
// panel of a laptop, and external screens that take it over their
// cable. PowerDevil keeps the list and the levels on the session bus
// (org.kde.ScreenBrightness); the brightness keys go through it too, so
// the slider and the keys always agree.
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class ScreenBrightness : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // PowerDevil answers and at least one screen has a brightness
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // [{ name, label, builtIn, percent }], the built-in screen first
    Q_PROPERTY(QVariantList displays READ displays NOTIFY changed)

public:
    explicit ScreenBrightness(QObject *parent = nullptr);
    ScreenBrightness(const QDBusConnection &bus, QObject *parent = nullptr);

    bool available() const { return !m_displays.isEmpty(); }
    QVariantList displays() const;

    // percent 0..100; the built-in screen never goes fully dark (1 %)
    Q_INVOKABLE void setBrightness(const QString &name, int percent);
    Q_INVOKABLE void reload();

    static int percentOf(int brightness, int maximum);
    static int brightnessOf(int percent, int maximum, bool builtIn);
    // A display object path is /org/kde/ScreenBrightness/<name>: plain names only.
    static bool isDisplayName(const QString &name);
    // What to call each display: its label, and when two share one (two
    // screens of the same model), "<label> (Display N)" with N the number
    // the Display page gives it. `screens` is DisplayConfig::outputs.
    Q_INVOKABLE static QStringList titles(const QStringList &labels, const QVariantList &screens);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void brightnessChanged(const QString &name, int brightness);
    void rangeChanged(const QString &name, int maximum, int brightness);
    void displaysChanged();

private:
    struct Display {
        QString name;
        QString label;
        bool builtIn = false;
        int brightness = 0;
        int maximum = 0;
    };

    QDBusConnection m_bus;
    QList<Display> m_displays;
};
