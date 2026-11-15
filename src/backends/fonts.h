// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Fonts: the font families on this PC, and installing
// new ones by dropping their files.
//
// The list comes from fontconfig (`fc-list`), with each face's file, so
// a family installed by the user can be told from one of the system. A
// dropped file is checked to be a font by its first bytes (TrueType,
// OpenType, collections), copied to $XDG_DATA_HOME/fonts, then
// `fc-cache` is run on that folder. Uninstalling removes only files in
// that folder: a system font cannot be removed from here.
#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

struct FontFamily {
    QString family;
    QStringList styles;
    QStringList files;
    bool user = false; // every file is in the user's fonts folder
};

class FontSettings : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    enum Roles {
        FamilyRole = Qt::UserRole + 1,
        StylesRole,
        FaceCountRole,
        UserRole,
    };
    Q_ENUM(Roles)

    explicit FontSettings(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int total() const { return m_all.size(); }
    QString searchText() const { return m_search; }
    void setSearchText(const QString &text);
    bool loading() const { return m_loading; }

    Q_INVOKABLE void reload();
    // the dropped files or folders; how many fonts were installed
    Q_INVOKABLE int install(const QList<QUrl> &urls);
    // a family's faces: [{ style }], {} when unknown
    Q_INVOKABLE QVariantMap family(const QString &family) const;
    Q_INVOKABLE bool uninstall(const QString &family);

    // --- the parts the tests drive ---
    static QString userFolder();
    // "family<TAB>style<TAB>file" lines of fc-list -> families by name
    static QList<FontFamily> parseList(const QByteArray &output, const QString &userFolder);
    // a font file by its first bytes, whatever its name says
    static bool isFontFile(const QString &path);
    // copy one font into the folder; the file name it got, "" when refused
    static QString copyFont(const QString &path, const QString &folder);
    // the files of a family that may be removed (inside the folder only)
    static QStringList removableFiles(const FontFamily &family, const QString &folder);

Q_SIGNALS:
    void countChanged();
    void searchTextChanged();
    void loadingChanged();
    // after install(): what was refused (not fonts), by file name
    void refused(const QStringList &names);

private:
    void filter();
    void refreshCache();

    QList<FontFamily> m_all;
    QList<FontFamily> m_rows;
    QString m_search;
    bool m_loading = false;
};
