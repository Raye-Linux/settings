#!/bin/sh
# Where Settings gets the release from.
#
# Settings never writes "0.5 Beta" down. About > Raye Linux specifications
# reads os-release on the running machine, so the number is right on an
# upgraded install without rebuilding anything. ./VERSION only records
# which release this checkout is for; it mirrors iso/VERSION, where the
# number is decided.
#
# Settings' own version is a different thing and stays different: the
# package's, taken from debian/changelog by CMakeLists.txt.
set -u
src=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
fails=0
work_stale=$(mktemp)
trap 'rm -f "$work_stale"' EXIT

ok() { printf 'ok - %s\n' "$1"; }
fail() { printf 'FAIL - %s\n' "$1"; fails=$((fails + 1)); }

version=$(cat "$src/VERSION" 2>/dev/null)
number=${version%% *}

# Comments go first, everywhere below: deviceinfo.h documents the shape of
# os-release in a comment that names "0.5 Beta", and a test that protects
# that documentation must not be the thing that forbids it.
strip_comments() {
    sed -e 's@/\*.*\*/@@g' -e 's@//.*$@@' "$1"
}

# --- the release ---------------------------------------------------------

case $version in
    [0-9]*.[0-9]*' Beta') ok "VERSION is a beta release: $version" ;;
    *) fail "VERSION is '$version', expected something like '0.5 Beta'" ;;
esac

# iso/VERSION decides the number; this repo only follows it. The image is
# built with every repo checked out side by side, so this compares for real
# there. A lone clone says so out loud instead of passing in silence.
source=$src/../iso/VERSION
if [ -f "$source" ]; then
    if [ "$(cat "$source")" = "$(cat "$src/VERSION")" ]; then
        ok "VERSION mirrors iso/VERSION"
    else
        fail "VERSION is '$version', iso/VERSION is '$(cat "$source")'"
    fi
else
    printf 'note - no sibling %s: cross-check skipped, VERSION not compared\n' "$source"
fi

# --- Settings must not carry the release in its code --------------------

spelt=0
for file in $(find "$src/src" "$src/data" -type f \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.qml' -o -name '*.js' -o -name '*.json' \) | sort); do
    if strip_comments "$file" | grep -qiE "(Raye( Linux)?[ _-]+)?${number}[ _-]+Beta"; then
        fail "${file#"$src/"} spells the release out; read os-release instead"
        spelt=1
    fi
done
[ "$spelt" = 0 ] && ok "no source file spells the release out"

# Markdown explains the rule by quoting the number, so the number is in it.
# Checked rather than skipped: documentation left on an older release is how
# people learn the wrong version. French prose writes "Bêta", same release.
stale=0
for file in $(find "$src" -name '*.md' -not -path '*/.git/*' | sort); do
    # one match per line: a release name has a space in it, so word
    # splitting would compare "0.1" and "Beta" separately and always fail
    grep -oE '[0-9]+(\.[0-9]+)+ B[eê]ta' "$file" | sort -u | while IFS= read -r found; do
        [ "$(printf '%s' "$found" | sed 's/Bêta/Beta/')" = "$version" ] && continue
        printf 'FAIL - %s names "%s", this release is "%s"\n' "${file#"$src/"}" "$found" "$version"
    done
done > "$work_stale"
if [ -s "$work_stale" ]; then
    cat "$work_stale"
    stale=$(wc -l < "$work_stale")
    fails=$((fails + stale))
else
    ok "every release named in markdown is $version"
fi

# --- and it must really read it -----------------------------------------

info=$src/src/backends/deviceinfo.cpp
for key in etc/os-release usr/lib/os-release NAME VERSION VERSION_ID; do
    if grep -q "$key" "$info"; then
        ok "deviceinfo.cpp reads $key"
    else
        fail "deviceinfo.cpp no longer reads $key"
    fi
done

about=$src/src/qml/pages/system/AboutPage.qml
for bind in info.edition info.version; do
    if grep -q "value: $bind" "$about"; then
        ok "About shows $bind"
    else
        fail "About no longer shows $bind: the release would stop being displayed"
    fi
done

# The fixture that stands for a real Raye machine must be the real thing,
# or the parser is proven against a release that was never shipped.
fixture=$src/tests/test_deviceinfo.cpp
# the release name, when there is one, follows the version: "0.5 Beta Alpenglow"
for want in 'PRETTY_NAME=\"Raye Linux '"$version" 'VERSION=\"'"$version" 'VERSION_ID=\"'"$number"'\"'; do
    if grep -qF "$want" "$fixture"; then
        ok "the os-release fixture carries $want"
    else
        fail "the os-release fixture is not this release: $want is missing"
    fi
done

# --- Settings' own version is the package's, never a literal ------------

if grep -q 'QStringLiteral(RAYE_SETTINGS_VERSION)' "$src/src/main.cpp"; then
    ok "About takes Settings' own version from the package"
else
    fail "main.cpp no longer builds KAboutData from RAYE_SETTINGS_VERSION"
fi
if grep -q 'debian/changelog' "$src/CMakeLists.txt"; then
    ok "CMakeLists.txt reads the package version from debian/changelog"
else
    fail "CMakeLists.txt no longer reads debian/changelog"
fi
first=$(head -1 "$src/debian/changelog" | sed -n 's/.*(\([^)]*\)).*/\1/p')
case $first in
    "$number"*) ok "the changelog is in the $version series: $first" ;;
    *) fail "the changelog says $first, which is not in the $version series" ;;
esac

printf '\n%s\n' "$([ "$fails" = 0 ] && echo 'all checks passed' || echo "$fails check(s) failed")"
exit $([ "$fails" = 0 ] && echo 0 || echo 1)
