#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# What Settings shows in English is American English: color, center,
# canceled, license, 50%. Search keywords may keep the British words,
# people type them.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
cd "$SOURCE_DIR" || exit 1
fail=0
say() {
    [ -n "$2" ] || return 0
    echo "$2" | sed "s/^/FAIL: $1: /" | cut -c1-220
    fail=1
}
british='\b(colou?rs?|colou?red|behaviours?|greys?|greyed|greyscale|centres?|centred|cancell(ed|ing)|licences?|programmes?|favourites?|optimis(e|ed|ing|er)|synchronis(e|ed|ation|er)|neighbours?|enrolment|customis(e|ed|ing)|organis(e|ed)|recognis(e|ed)|catalogues?|films?)\b'
# colou?r matches color too: only the British forms count
filter() { grep -iE "$british" | grep -iE 'colour|behaviour|grey|centr[ed]|cancell|licence|programme|favourite|optimis|synchronis|neighbour|enrolment|customis|organis|recognis|catalogue|\bfilms?\b'; }
# every string literal inside an i18n call, calls over several lines too
shown() {
    python3 -I - "$@" <<'EOF'
import re, sys
call = re.compile(r"\bi18n[cp]{0,2}\s*\(")
for path in sys.argv[1:]:
    src = open(path, encoding="utf-8").read()
    for m in call.finditer(src):
        j, depth = m.end(), 1
        while j < len(src) and depth:
            c = src[j]
            if c in "\"'":
                k = j + 1
                while k < len(src) and src[k] != c:
                    k += 2 if src[k] == "\\" else 1
                line = src.count("\n", 0, j) + 1
                print(f"{path}:{line}: {src[j:k + 1]}")
                j = k + 1
                continue
            depth += c == "("
            depth -= c == ")"
            j += 1
EOF
}
say "shown text" "$(shown $(find src -name '*.qml' -o -name '*.js' -o -name '*.cpp') | filter)"
say "French catalogue msgid" "$(grep -nE '^msgid' po/fr/raye-settings.po | filter)"
say "search label" "$(grep -ohE '"label": *"[^"]*"' data/search/*.json | filter)"
say "page name or summary" "$(grep -ohE '"(name|summary)": *"[^"]*"' data/layout.json | filter)"
say "polkit message" "$(grep -hE '<(message|description)>' data/polkit/*.policy | filter)"
# no space before the percent sign in English (French keeps it)
say "percent with a space" "$(grep -nE '^msgid .*%[0-9] %([^0-9]|$)' po/fr/raye-settings.po)"
say "percent with a space" "$(shown $(find src -name '*.qml' -o -name '*.js' -o -name '*.cpp') | grep -E '%[0-9] %([^0-9]|$)')"
# the check reads what it should: one American string it must find
grep -q 'msgid "Accent color"' po/fr/raye-settings.po || { echo "FAIL: the catalogue lost Accent color"; fail=1; }
[ $fail -eq 0 ] && echo "US spelling: ok"
exit $fail
