#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# data/libexec/firewall against a folder that stands for the computer,
# with ufw recorded instead of run.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
helper=$SOURCE_DIR/data/libexec/firewall
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0
check() { if eval "$1"; then :; else failures=$((failures + 1)); echo "FAIL: $2"; fi; }

R=$tmp/r
log=$tmp/log
mkdir -p "$tmp/bin" "$R"
printf '#!/bin/sh\necho "ufw $*" >> "%s"\n' "$log" > "$tmp/bin/ufw"
chmod +x "$tmp/bin/ufw"
run() { env -u PKEXEC_UID RAYE_ROOT="$R" PATH="$tmp/bin:$PATH" sh "$helper" "$@"; }

run off; check '[ $? -eq 0 ]' "off failed"
check 'grep -qx "ufw disable" "$log"' "ufw not disabled"
check '[ -e "$R/var/lib/raye/firewall-off" ]' "turning it off left no mark"
run on; check '[ $? -eq 0 ]' "on failed"
check 'grep -qx "ufw --force enable" "$log"' "ufw not enabled"
check '[ ! -e "$R/var/lib/raye/firewall-off" ]' "mark kept after turning it on"
run maybe 2>/dev/null; check '[ $? -eq 2 ]' "a wrong word was accepted"
# under pkexec the folder of the tests is never used
: > "$log"
env PKEXEC_UID=1000 RAYE_ROOT="$R" PATH="$tmp/bin:$PATH" sh "$helper" maybe 2>/dev/null
check '[ ! -e "$R/var/lib/raye/firewall-off" ]' "pkexec run wrote into RAYE_ROOT"

[ "$failures" -eq 0 ] && echo "firewall helper: ok"
exit "$failures"
