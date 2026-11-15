// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsdebug.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const QRegularExpression &debugLine()
{
    // the same lines the launcher takes: anything else is left alone
    static const QRegularExpression line(QStringLiteral("^\\s*debug\\s*=\\s*(on|off)\\s*$"));
    return line;
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

WindowsDebug::WindowsDebug(QObject *parent)
    : WindowsDebug(QStringLiteral("/etc/raye/windows.conf"),
                   QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                       + QStringLiteral("/raye/windows.conf"),
                   parent)
{
}

WindowsDebug::WindowsDebug(const QString &systemFile, const QString &userFile, QObject *parent)
    : QObject(parent)
    , m_systemFile(systemFile)
    , m_userFile(userFile)
{
}

QString WindowsDebug::valueIn(const QString &text)
{
    QString value;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const auto m = debugLine().match(line);
        if (m.hasMatch())
            value = m.captured(1);
    }
    return value;
}

QString WindowsDebug::withValue(const QString &text, bool on)
{
    QStringList kept;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!debugLine().match(line).hasMatch())
            kept << line;
    }
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty())
        kept.removeLast();
    kept << (on ? QStringLiteral("debug=on") : QStringLiteral("debug=off"));
    return kept.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool WindowsDebug::on() const
{
    QString value = valueIn(readAll(m_userFile));
    if (value.isEmpty())
        value = valueIn(readAll(m_systemFile));
    return value == QLatin1String("on");
}

void WindowsDebug::setOn(bool on)
{
    if (on == this->on())
        return;
    QDir().mkpath(QFileInfo(m_userFile).absolutePath());
    QSaveFile f(m_userFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    f.write(withValue(readAll(m_userFile), on).toUtf8());
    f.commit();
    Q_EMIT changed();
}
