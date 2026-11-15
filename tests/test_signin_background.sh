#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# data/libexec/signin-background, run for real in a folder that stands for
# the computer. It runs as root on what a user hands it, so what it refuses
# matters as much as what it keeps.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
helper=$SOURCE_DIR/data/libexec/signin-background
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0
checks=0

ok() { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failures=$((failures + 1)); echo "FAIL: $*"; }
check() { if eval "$1"; then ok; else bad "$2"; fi; }

R=$tmp/r
theme=$R/usr/share/sddm/themes/raye
conf=$theme/theme.conf.user
store=$R/var/lib/raye-settings/signin
run() { env -u PKEXEC_UID RAYE_ROOT="$R" sh "$helper" "$@"; }

# a few bytes that start the way each kind of picture starts
printf '\211PNG\r\n\032\n0000' > "$tmp/a.png"
printf '\377\330\377\340000000000' > "$tmp/a.jpg"
printf 'RIFF\001\002\003\004WEBPVP8 ' > "$tmp/a.webp"
printf 'root:x:0:0:root:/root:/bin/bash\n' > "$tmp/secret"
: > "$tmp/empty"

# no sign-in theme on this computer
run --set < "$tmp/a.png" 2>/dev/null; code=$?
check '[ "$code" = 4 ]' "no theme: exit 4, got $code"

mkdir -p "$theme"

for kind in png jpg webp; do
    run --set < "$tmp/a.$kind"; code=$?
    check '[ "$code" = 0 ]' "$kind: exit 0, got $code"
    check '[ -f "$store/picture.$kind" ]' "$kind: kept"
    check 'cmp -s "$tmp/a.$kind" "$store/picture.$kind"' "$kind: same bytes"
    # the sign-in screen runs under its own account: it must be able to read it
    check '[ "$(stat -c %a "$store/picture.$kind")" = 644 ]' "$kind: readable by everyone"
    check '[ "$(stat -c %a "$store")" = 755 ]' "$kind: folder readable by everyone"
    check '[ "$(stat -c %a "$conf")" = 644 ]' "$kind: theme file readable by everyone"
    # the path as it will be on the computer, without the test's folder
    check 'grep -qx "background=/var/lib/raye-settings/signin/picture.$kind" "$conf"' "$kind: named in the theme file"
    check 'grep -qx "\[General\]" "$conf"' "$kind: under [General]"
    others=$(ls "$store" | grep -vc "^picture.$kind\$")
    check '[ "$others" = 0 ]' "$kind: nothing else left behind ($others)"
done

before=$(md5sum < "$store/picture.webp")
for bad_input in secret empty; do
    run --set < "$tmp/$bad_input" 2>/dev/null; code=$?
    check '[ "$code" = 3 ]' "$bad_input: exit 3, got $code"
    check '[ "$(md5sum < "$store/picture.webp")" = "$before" ]' "$bad_input: the picture stays"
    check 'grep -qx "background=/var/lib/raye-settings/signin/picture.webp" "$conf"' "$bad_input: the theme file stays"
    check '[ -z "$(ls -A "$store" | grep "^\.new")" ]' "$bad_input: no temporary file left"
done

# too large, and it does not read on for ever
{ printf '\211PNG\r\n\032\n'; head -c $((33 * 1024 * 1024)) /dev/zero; } > "$tmp/big.png"
run --set < "$tmp/big.png" 2>/dev/null; code=$?
check '[ "$code" = 3 ]' "too large: exit 3, got $code"
check '[ "$(md5sum < "$store/picture.webp")" = "$before" ]' "too large: the picture stays"

# a path on the command line is not a way in
run --set /etc/shadow < "$tmp/a.png" 2>/dev/null; code=$?
check '[ "$code" = 2 ]' "extra argument: exit 2, got $code"
run --nonsense 2>/dev/null; code=$?
check '[ "$code" = 2 ]' "unknown option: exit 2, got $code"
run 2>/dev/null; code=$?
check '[ "$code" = 2 ]' "no option: exit 2, got $code"

# under pkexec the test's folder means nothing
mkdir -p "$tmp/fake"
out=$(PKEXEC_UID=1000 RAYE_ROOT=$tmp/fake sh -c '
    # never reaches the real theme: asks for usage first
    sh "$0" --nonsense' "$helper" 2>&1); code=$?
check '[ "$code" = 2 ] || [ "$code" = 4 ]' "under pkexec, exit 2 or 4, got $code"
check 'grep -q "RAYE_ROOT" "$helper" && grep -q "PKEXEC_UID" "$helper"' "the helper ignores RAYE_ROOT under pkexec"
case $(sed -n '/PKEXEC_UID/p' "$helper") in
*'-z "${PKEXEC_UID:-}"'*) ok ;;
*) bad "RAYE_ROOT is only read without PKEXEC_UID" ;;
esac

run --clear; code=$?
check '[ "$code" = 0 ]' "clear: exit 0, got $code"
check '[ ! -e "$conf" ]' "clear: the theme file is gone"
check '[ -z "$(ls -A "$store")" ]' "clear: the picture is gone"
run --clear; code=$?
check '[ "$code" = 0 ]' "clear twice: exit 0, got $code"

# the action names this very helper, and Settings installs it where it says
policy=$SOURCE_DIR/data/polkit/org.raye.settings.signin.policy
check 'grep -q "exec.path\">/usr/libexec/raye-settings/signin-background<" "$policy"' "polkit action names the helper"
check 'grep -q "<allow_active>auth_admin_keep</allow_active>" "$policy"' "polkit asks for an administrator"
check '[ -x "$helper" ]' "the helper is executable"

echo "$checks checks, $failures failed"
[ "$failures" = 0 ]
