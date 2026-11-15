// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Optional features: the packs and what a pack is from what the
// package manager says. The install itself needs PackageKit and the
// administrator: that is tried on a real system, not here.
#include "optionalfeatures.h"

#include <QSet>
#include <QTest>

class TestOptionalFeatures : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void packsAreSound()
    {
        QSet<QString> ids;
        for (const FeaturePack &pack : OptionalFeatures::packs()) {
            QVERIFY2(!ids.contains(pack.id), qPrintable(pack.id));
            ids.insert(pack.id);
            // a pack either installs packages or leads to a page
            QVERIFY2(pack.packages.isEmpty() != pack.route.isEmpty(), qPrintable(pack.id));
            QVERIFY(!pack.iconName.isEmpty());
        }
        QVERIFY(ids.contains(QStringLiteral("gaming")));
        QVERIFY(ids.contains(QStringLiteral("windows")));
        QVERIFY(ids.contains(QStringLiteral("kernel")));
        QVERIFY(ids.contains(QStringLiteral("lang-fr")));
    }

    void states()
    {
        const FeaturePack lang{QStringLiteral("lang-fr"),
                               {QStringLiteral("language-pack-fr"), QStringLiteral("language-pack-kde-fr"), QStringLiteral("hunspell-fr")},
                               {},
                               QStringLiteral("x")};
        QCOMPARE(OptionalFeatures::stateOf(lang, {}), QString());
        QCOMPARE(OptionalFeatures::stateOf(lang, {{QStringLiteral("language-pack-fr"), false}, {QStringLiteral("hunspell-fr"), false}}),
                 QStringLiteral("available"));
        QCOMPARE(OptionalFeatures::stateOf(lang, {{QStringLiteral("language-pack-fr"), true}, {QStringLiteral("hunspell-fr"), false}}),
                 QStringLiteral("partial"));
        // a package the archive does not have is not asked for
        QCOMPARE(OptionalFeatures::stateOf(lang, {{QStringLiteral("language-pack-fr"), true}, {QStringLiteral("hunspell-fr"), true}}),
                 QStringLiteral("installed"));
        // without its first package the pack is not offered
        QCOMPARE(OptionalFeatures::stateOf(lang, {{QStringLiteral("hunspell-fr"), true}}), QString());
        const FeaturePack kernel{QStringLiteral("kernel"), {}, QStringLiteral("update/advanced"), QStringLiteral("x")};
        QCOMPARE(OptionalFeatures::stateOf(kernel, {}), QStringLiteral("link"));
    }

    void packageNames()
    {
        QCOMPARE(OptionalFeatures::packageName(QStringLiteral("raye-gaming;0.3;all;installed:raye")), QStringLiteral("raye-gaming"));
        QCOMPARE(OptionalFeatures::packageName(QStringLiteral("plain")), QStringLiteral("plain"));
    }
};

QTEST_GUILESS_MAIN(TestOptionalFeatures)
#include "test_optionalfeatures.moc"
