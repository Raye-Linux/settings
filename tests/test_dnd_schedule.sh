#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# data/libexec/dnd-schedule with a made-up clock, and kreadconfig6 and
# kwriteconfig6 that keep plasmanotifyrc as a plain file in the test.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
helper=$SOURCE_DIR/data/libexec/dnd-schedule
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0
checks=0

check() {
    checks=$((checks + 1))
    if ! eval "$1"; then failures=$((failures + 1)); echo "FAIL: $2"; fi
}

H=$tmp/home
mkdir -p "$H/.config" "$tmp/bin"
cat > "$tmp/bin/kwriteconfig6" <<'EOF'
#!/bin/sh
echo "$*" >> "$HOME/kwrite.log"
for v; do :; done
printf '[DoNotDisturb]\nUntil=%s\n' "$v" > "$HOME/.config/plasmanotifyrc"
EOF
cat > "$tmp/bin/kreadconfig6" <<'EOF'
#!/bin/sh
sed -n 's/^Until=//p' "$HOME/.config/plasmanotifyrc" 2>/dev/null
EOF
chmod +x "$tmp/bin/kwriteconfig6" "$tmp/bin/kreadconfig6"

conf() { printf '[DoNotDisturbSchedule]\n%s\n' "$1" > "$H/.config/raye-settingsrc"; }
fresh() { rm -f "$H/.config/plasmanotifyrc" "$H/kwrite.log"; }
run() { env -u XDG_CONFIG_HOME HOME="$H" PATH="$tmp/bin:$PATH" sh "$helper" --now "$1"; }
until_is() { [ "$(sed -n 's/^Until=//p' "$H/.config/plasmanotifyrc" 2>/dev/null)" = "$1" ]; }

# 22:00 to 07:00, every day
conf "Enabled=true
Start=22:00
End=07:00
Days=everyday"
fresh; run "2026-11-19 22:00"
check "until_is 2026,11,20,7,0,0" "start of a night window"
check "grep -q -- '--notify' \"\$H/kwrite.log\"" "written without --notify"
fresh; run "2026-11-20 03:15"
check "until_is 2026,11,20,7,0,0" "after midnight, window started yesterday"
fresh; run "2026-11-20 07:00"
check "[ ! -e \"\$H/.config/plasmanotifyrc\" ]" "at the end time it must not start"
fresh; run "2026-11-20 12:00"
check "[ ! -e \"\$H/.config/plasmanotifyrc\" ]" "midday is outside the window"

# a longer do not disturb is kept, a shorter one is made longer
fresh; printf '[DoNotDisturb]\nUntil=9999,12,31,23,59,59\n' > "$H/.config/plasmanotifyrc"
run "2026-11-19 23:00"
check "until_is 9999,12,31,23,59,59" "notifications off was shortened"
fresh; printf '[DoNotDisturb]\nUntil=2026,11,19,23,30,0\n' > "$H/.config/plasmanotifyrc"
run "2026-11-19 23:00"
check "until_is 2026,11,20,7,0,0" "a shorter do not disturb was not extended"

# a day window on weekdays only
conf "Enabled=true
Start=09:30
End=12:00
Days=weekdays"
fresh; run "2026-11-20 10:00"   # Friday
check "until_is 2026,11,20,12,0,0" "weekday window"
fresh; run "2026-11-21 10:00"   # Saturday
check "[ ! -e \"\$H/.config/plasmanotifyrc\" ]" "weekday window on a Saturday"
conf "Enabled=true
Start=22:00
End=07:00
Days=weekends"
fresh; run "2026-11-22 02:00"   # Sunday night, started Saturday
check "until_is 2026,11,22,7,0,0" "weekend night window"
fresh; run "2026-11-24 02:00"   # Tuesday, started Monday
check "[ ! -e \"\$H/.config/plasmanotifyrc\" ]" "weekend window on a Monday night"

# off, or anything odd, does nothing
for bad in "Enabled=false
Start=22:00
End=07:00" "Enabled=true
Start=25:00
End=07:00" "Enabled=true
Start=22:00
End=22:00" "Enabled=true
Start=\$(touch $tmp/pwned)
End=07:00"; do
    conf "$bad"
    fresh; run "2026-11-19 23:00"
    check "[ ! -e \"\$H/.config/plasmanotifyrc\" ]" "acted on: $bad"
done
check "[ ! -e \"\$tmp/pwned\" ]" "ran text from the config file"
env HOME="$H" sh "$helper" --now nonsense >/dev/null 2>&1; rc=$?
check "[ $rc = 1 ]" "a wrong --now was accepted"

echo "dnd-schedule: $checks checks, $failures failed"
[ "$failures" = 0 ]
