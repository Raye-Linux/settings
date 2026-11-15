#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Accounts and password prompts speak Linux: a sudo user, root access.
# "Administrator" stays only where it is not shown: search keywords
# (people coming from Windows type it), property names, comments.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
cd "$SOURCE_DIR" || exit 1
fail=0
say() {
    [ -n "$2" ] || return 0
    echo "$2" | sed "s/^/FAIL: $1: /" | cut -c1-220
    fail=1
}
# the strings handed to i18n, in QML, JavaScript and C++
say "shown text says administrator" "$(grep -rnE 'i18n[cp]?\(' src --include=*.qml --include=*.js --include=*.cpp \
    | grep -iE '"[^"]*\badmin(istrat[a-z]*)?\b[^"]*"')"
say "French text says administrateur" "$(grep -n '^msgstr' po/fr/raye-settings.po | grep -iE 'administrat|\badmin\b')"
# what the password prompt shows for each action
say "polkit message says administrator" "$(grep -n '<message' data/polkit/*.policy | grep -iE 'administrat|\badmin\b')"
# page and result names in the search (the keywords may keep the word)
say "search label says administrator" "$(grep -nE '"label(_fr)?":' data/search/*.json | grep -iE 'administrat|\badmin\b')"
# and the new words are really there, so the check is not empty
grep -q '"Sudo user"' src/currentuser.cpp || { echo "FAIL: the account line lost its sudo label"; fail=1; }
grep -q 'msgstr "Utilisateur sudo"' po/fr/raye-settings.po || { echo "FAIL: no French for sudo user"; fail=1; }
grep -q 'needs root access</message>' data/polkit/org.raye.settings.repair.policy \
    || { echo "FAIL: the repair prompt lost its root wording"; fail=1; }
[ $fail -eq 0 ] && echo "root terms: ok"
exit $fail
