// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Text input: the on-screen keyboard. KWin starts the
// program named in kwinrc [Wayland] InputMethod, and shows it when a text
// field needs it on a touch screen.
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class TextInputSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // the keyboard is installed on this PC
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool onScreenKeyboard READ onScreenKeyboard WRITE setOnScreenKeyboard NOTIFY changed)

public:
    explicit TextInputSettings(QObject *parent = nullptr);
    // for the tests: the keyboard's file somewhere of their own
    explicit TextInputSettings(const QString &keyboardFile, QObject *parent = nullptr);

    bool available() const;
    bool onScreenKeyboard() const;
    void setOnScreenKeyboard(bool on);

Q_SIGNALS:
    void changed();

private:
    KSharedConfigPtr m_config;
    QString m_keyboardFile;
};
