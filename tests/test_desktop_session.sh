#!/bin/sh
# The desktop helper as it ships: it makes KDE Plasma or Hyprland the
# desktop the caller's next sign-in opens, in every place the sign-in
# screen reads, for the caller only, and never a desktop that is not
# installed. The file and key it writes are the ones Raye's sign-in
# screen reads (theme: sddm/raye/SessionMemory.qml, SddmAdapter.qml).
set -u
helper=$SOURCE_DIR/data/libexec/desktop-session
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
failed=0
check() { # description, then a command that must succeed
    what=$1; shift
    if "$@"; then echo "ok $what"; else echo "FAILED $what"; failed=1; fi
}
export RAYE_SESSION_DIR=$dir/sessions RAYE_SDDM_HOME=$dir/sddm RAYE_SDDM_CONF_DIR=$dir/conf.d RAYE_SDDM_MAIN=$dir/sddm.conf
export RAYE_TEST_UID=$(id -u)
me=$(id -un)
[ "$RAYE_TEST_UID" != 0 ] || { me=$(getent passwd 1 | cut -d: -f1); export RAYE_TEST_UID=1; }
mkdir -p "$dir/sessions" "$dir/sddm" "$dir/conf.d"
touch "$dir/sessions/plasma.desktop"
memory=$dir/sddm/.config/raye-signin.conf
state=$dir/sddm/state.conf
get() { sh "$helper" --check; }

check "nothing written yet: Plasma" sh -c "[ \"\$(sh '$helper' --check)\" = plasma ]"
sh "$helper" set hyprland 2>/dev/null
check "a desktop that is not installed is refused" test $? -eq 5
check "and nothing is written for it" test ! -e "$memory"

touch "$dir/sessions/raye-hyprland.desktop"
check "set hyprland" sh "$helper" set hyprland
check "the sign-in screen's memory names it for me" sh -c "awk '/^\[sessions\]/{s=1;next} /^\[/{s=0} s' '$memory' | grep -qx '$me=raye-hyprland.desktop'"
# exactly the shape of the theme's fixture sddm/tests/data/raye-signin.conf,
# which its tst_adapter.qml reads through QtCore Settings
check "a new memory is the file the sign-in screen reads" sh -c "[ \"\$(cat '$memory')\" = \"\$(printf '[sessions]\\n%s=raye-hyprland.desktop' '$me')\" ]"
check "the memory is private to the sign-in screen" sh -c "[ \"\$(stat -c %a '$memory')\" = 600 ]"
check "its folder too" sh -c "[ \"\$(stat -c %a '$dir/sddm/.config')\" = 700 ]"
check "SDDM's last session is the session file's path" grep -qx "Session=$dir/sessions/raye-hyprland.desktop" "$state"
check "--check says hyprland" sh -c "[ \"\$(sh '$helper' --check)\" = hyprland ]"
check "set plasma" sh "$helper" set plasma
check "--check says plasma" sh -c "[ \"\$(sh '$helper' --check)\" = plasma ]"
check "one line per account, not a second one" sh -c "[ \$(grep -c '^$me=' '$memory') -eq 1 ]"
check "nothing left next to the files" sh -c "[ -z \"\$(ls -A '$dir/sddm/.config' | grep -v '^raye-signin.conf\$')\" ]"

# other accounts and other settings are kept
printf '[General]\nTheme=raye\n\n[sessions]\nsomeoneelse=plasma.desktop\n\n[other]\nk=v\n' > "$memory"
check "set hyprland next to another account" sh "$helper" set hyprland
check "the other account keeps its desktop" grep -qx 'someoneelse=plasma.desktop' "$memory"
check "mine is in the same group" sh -c "awk '/^\[sessions\]/{s=1;next} /^\[/{s=0} s' '$memory' | grep -qx '$me=raye-hyprland.desktop'"
check "other groups stay" sh -c "grep -qx 'Theme=raye' '$memory' && grep -qx 'k=v' '$memory'"

# SDDM's last session belongs to the last account that signed in: not mine, not touched
printf '[Last]\nUser=someoneelse\nSession=/usr/share/wayland-sessions/plasma.desktop\n' > "$state"
check "set plasma when someone else signed in last" sh "$helper" set plasma
check "their last session is left alone" grep -qx 'Session=/usr/share/wayland-sessions/plasma.desktop' "$state"
check "and their name too" grep -qx 'User=someoneelse' "$state"
printf '[Last]\nUser=%s\nSession=/usr/share/wayland-sessions/plasma.desktop\n' "$me" > "$state"
check "set hyprland when I signed in last" sh "$helper" set hyprland
check "my last session follows" grep -qx "Session=$dir/sessions/raye-hyprland.desktop" "$state"

# automatic sign-in opens the same desktop, only when it signs me in
printf '[Autologin]\nUser=%s\nSession=plasma\nRelogin=false\n' "$me" > "$dir/conf.d/zz-raye-autologin.conf"
check "set hyprland with automatic sign-in" sh "$helper" set hyprland
check "automatic sign-in opens Hyprland" grep -qx 'Session=raye-hyprland.desktop' "$dir/conf.d/zz-raye-autologin.conf"
check "the rest of that file stays" grep -qx 'Relogin=false' "$dir/conf.d/zz-raye-autologin.conf"
printf '[Autologin]\nUser=someoneelse\nSession=plasma\n' > "$dir/conf.d/zz-raye-autologin.conf"
check "set plasma, someone else signs in by themselves" sh "$helper" set plasma
check "their automatic sign-in is not mine to change" grep -qx 'Session=plasma' "$dir/conf.d/zz-raye-autologin.conf"
rm -f "$dir/conf.d/zz-raye-autologin.conf"
printf '[Autologin]\nUser=%s\nSession=plasma.desktop\n\n[Theme]\nCurrent=raye\n' "$me" > "$dir/sddm.conf"
check "set hyprland with Setup's sddm.conf" sh "$helper" set hyprland
check "sddm.conf's automatic sign-in follows" grep -qx 'Session=raye-hyprland.desktop' "$dir/sddm.conf"
check "its other groups stay" grep -qx 'Current=raye' "$dir/sddm.conf"
printf '[Theme]\nCurrent=raye\n' > "$dir/sddm.conf"
check "set plasma, sddm.conf without automatic sign-in" sh "$helper" set plasma
check "no automatic sign-in is added" sh -c "! grep -q Autologin '$dir/sddm.conf'"
rm -f "$dir/sddm.conf"

# only the caller, only these two desktops, never through a link
RAYE_TEST_UID=0 sh "$helper" set plasma 2>/dev/null
check "never the root account" test $? -eq 2
sh "$helper" set gnome 2>/dev/null
check "another desktop name is refused" test $? -eq 2
sh "$helper" set plasma someoneelse 2>/dev/null
check "an extra word is refused" test $? -eq 2
sh "$helper" nonsense 2>/dev/null
check "anything else is refused" test $? -eq 2
check "someoneelse was never written by me" sh -c "! grep -q '^someoneelse=raye' '$memory'"
mv "$memory" "$dir/real.conf"
ln -s "$dir/real.conf" "$memory"
sh "$helper" set hyprland 2>/dev/null
check "a link in place of the memory is refused" test $? -ne 0
check "and what it points to is not changed" sh -c "! grep -q '$me=raye-hyprland' '$dir/real.conf'"
rm -f "$memory"
sh "$helper" set hyprland
check "pkexec ignores the test folders" sh -c "! PKEXEC_UID=$RAYE_TEST_UID RAYE_SDDM_HOME='$dir/sddm' sh '$helper' --check 2>/dev/null | grep -qx hyprland"

# --- switching now: one automatic sign-in, for me, into that desktop ---
mkdir -p "$dir/logind"
{
    printf '#!/bin/sh\n'
    printf '# show-session ID -p ... [--value] / list-sessions --no-legend\n'
    printf 'case $1 in\n'
    printf 'show-session)\n'
    printf '    f=%s/logind/$2; [ -f "$f" ] || exit 1\n' "$dir"
    printf '    case " $* " in *" --value "*) sed -n "s/^State=//p" "$f" ;; *) cat "$f" ;; esac ;;\n'
    printf 'list-sessions) cat %s/logind/list 2>/dev/null ;;\n' "$dir"
    printf 'esac\n'
} > "$dir/loginctl"
printf '#!/bin/sh\necho "$@" >> %s/run-calls\n' "$dir" > "$dir/systemd-run"
chmod +x "$dir/loginctl" "$dir/systemd-run"
mkdir -p "$dir/vt"
export RAYE_LOGINCTL=$dir/loginctl RAYE_SYSTEMD_RUN=$dir/systemd-run RAYE_SELF=$helper RAYE_SWITCH_WINDOW=2
export RAYE_VT_DIR=$dir/vt RAYE_SWITCH_SETTLE=0
mine() { printf 'User=%s\nActive=yes\nClass=user\nType=wayland\nSeat=seat0\nState=active\nVTNr=2\n' "$RAYE_TEST_UID"; }
mine > "$dir/logind/5"
switch=$dir/conf.d/zz-raye-switch.conf
check "switch to Hyprland now" sh "$helper" switch hyprland 5
check "one automatic sign-in, for me" grep -qx "User=$me" "$switch"
check "into Hyprland" grep -qx 'Session=raye-hyprland.desktop' "$switch"
check "after the session ends, not only at boot" grep -qx 'Relogin=true' "$switch"
check "it is read after the automatic sign-in file" sh -c "[ \"\$(printf 'zz-raye-autologin.conf\nzz-raye-switch.conf\n' | LC_ALL=C sort | tail -1)\" = zz-raye-switch.conf ]"
check "the next sign-in is Hyprland too" sh -c "[ \"\$(sh '$helper' --check)\" = hyprland ]"
check "a watcher is started for it" grep -q "watch $RAYE_TEST_UID 5 $me" "$dir/run-calls"
check "the watcher cannot outlive the window" grep -q 'RuntimeMaxSec=32' "$dir/run-calls"
check "cancel takes it back" sh "$helper" cancel
check "nothing is left" test ! -e "$switch"

# only my own session, on the screen now
printf 'User=4242\nActive=yes\nClass=user\nType=wayland\nSeat=seat0\nState=active\n' > "$dir/logind/6"
sh "$helper" switch hyprland 6 2>/dev/null
check "not someone else's session" test $? -eq 6
mine | sed 's/Active=yes/Active=no/' > "$dir/logind/7"
sh "$helper" switch hyprland 7 2>/dev/null
check "not a session in the background" test $? -eq 6
mine | sed 's/Class=user/Class=greeter/' > "$dir/logind/8"
sh "$helper" switch hyprland 8 2>/dev/null
check "not the sign-in screen's" test $? -eq 6
sh "$helper" switch hyprland 99 2>/dev/null
check "not a session that does not exist" test $? -eq 6
sh "$helper" switch hyprland '5;x' 2>/dev/null
check "not a strange session name" test $? -eq 6
check "and nothing was written for any of them" test ! -e "$switch"
# someone else signs in by themselves: no switch, sign out instead
printf '[Autologin]\nUser=someoneelse\nSession=plasma\n' > "$dir/conf.d/kde_settings.conf"
sh "$helper" switch hyprland 5 2>/dev/null
check "not over another account's automatic sign-in" test $? -eq 6
check "nothing written over it" test ! -e "$switch"
rm -f "$dir/conf.d/kde_settings.conf"
# sddm.conf is read last: if it decides otherwise, the switch is taken back
printf '[Autologin]\nUser=%s\nSession=plasma.desktop\nRelogin=false\n' "$me" > "$dir/sddm.conf"
sh "$helper" switch hyprland 5 2>/dev/null
check "sddm.conf would say otherwise: refused" test $? -eq 6
check "and no switch file is left" test ! -e "$switch"
rm -f "$dir/sddm.conf"

# Setup's sddm.conf: "[Autologin] Session=plasma", no User, read last
printf '[Autologin]\nSession=plasma\n\n[Theme]\nCurrent=raye\n' > "$dir/sddm.conf"
check "switch with Setup's sddm.conf" sh "$helper" switch hyprland 5
check "its desktop follows, or SDDM would open Plasma" grep -qx 'Session=raye-hyprland.desktop' "$dir/sddm.conf"
check "and nobody is added to it" sh -c "! grep -q '^User=' '$dir/sddm.conf'"
sh "$helper" cancel
rm -f "$dir/sddm.conf"

# the watcher: gone once the new session is there, or when time is up
sh "$helper" switch hyprland 5
printf '5 %s %s seat0\n' "$RAYE_TEST_UID" "$me" > "$dir/logind/list"
( sh "$helper" watch "$RAYE_TEST_UID" 5 "$me" ) &
watcher=$!
sleep 1
check "the switch stays while my session is open" test -e "$switch"
sed -i 's/State=active/State=closing/' "$dir/logind/5"
sleep 1
check "and while the new one is not there yet" test -e "$switch"
printf '5 %s %s seat0\n9 %s %s seat0\n' "$RAYE_TEST_UID" "$me" "$RAYE_TEST_UID" "$me" > "$dir/logind/list"
wait $watcher
check "it is gone once the new session started" test ! -e "$switch"
mine > "$dir/logind/5"
sh "$helper" switch hyprland 5
rm -f "$dir/logind/list"
sh "$helper" watch "$RAYE_TEST_UID" 5 "$me"
check "time is up: gone without anyone signing in" test ! -e "$switch"
sh "$helper" switch hyprland 5
sh "$helper" watch "$RAYE_TEST_UID" 5 "$me" &
watcher=$!
sleep 1
kill -TERM $watcher; wait $watcher
check "a stopped watcher takes it away too" test ! -e "$switch"
# once the old session is over, the new one has a short while, not the whole window
sh "$helper" switch hyprland 5
sed -i 's/State=active/State=closing/' "$dir/logind/5"
start=$(date +%s)
RAYE_SWITCH_WINDOW=60 RAYE_SWITCH_AFTER=2 sh "$helper" watch "$RAYE_TEST_UID" 5 "$me"
check "no new session after the old one ended: gone soon" sh -c "[ \$(( \$(date +%s) - $start )) -le 5 ] && [ ! -e '$switch' ]"
mine > "$dir/logind/5"

# Owner's Dell, 2026-11-21: a full screen of terminal text for a few
# seconds during the switch. The old session's terminal is what shows once
# its desktop has let go of the screen: it is emptied, painted in the new
# desktop's first colour, no cursor; given back once the new one is there.
: > "$dir/vt/tty2"
sh "$helper" switch hyprland 5
printf '5 %s %s seat0\n9 %s %s seat0\n' "$RAYE_TEST_UID" "$me" "$RAYE_TEST_UID" "$me" > "$dir/logind/list"
sed -i 's/State=active/State=closing/' "$dir/logind/5"
sh "$helper" watch "$RAYE_TEST_UID" 5 "$me"
vt=$(od -An -c "$dir/vt/tty2" | tr -d ' \n')
check "the old session's terminal gets Hyprland's colour" sh -c "printf '%s' '$vt' | grep -q ']P00e0a17'"
check "it is emptied and its cursor hidden" sh -c "printf '%s' '$vt' | grep -q '\[2J' && printf '%s' '$vt' | grep -q '\[?25l'"
check "its colours and cursor come back at the end" sh -c "printf '%s' '$vt' | grep -q ']R' && printf '%s' '$vt' | grep -q '\[?25h'"
check "painted before it is given back" sh -c "[ \$(printf '%s' '$vt' | grep -bo ']P0' | head -1 | cut -d: -f1) -lt \$(printf '%s' '$vt' | grep -bo ']R' | head -1 | cut -d: -f1) ]"
mine > "$dir/logind/5"
: > "$dir/vt/tty2"
sh "$helper" switch plasma 5
sed -i 's/State=active/State=closing/' "$dir/logind/5"
sh "$helper" watch "$RAYE_TEST_UID" 5 "$me"
check "towards Plasma: the colour of its start screen" grep -q ']P0100f13' "$dir/vt/tty2"
mine | sed 's/VTNr=2/VTNr=2;x/' > "$dir/logind/5"
: > "$dir/vt/tty2"
sh "$helper" switch plasma 5 2>/dev/null
sed -i 's/State=active/State=closing/' "$dir/logind/5"
sh "$helper" watch "$RAYE_TEST_UID" 5 "$me"
check "a strange terminal number is not written to" test ! -s "$dir/vt/tty2"
rm -f "$dir/logind/list"
mine > "$dir/logind/5"
sh "$helper" cancel 2>/dev/null

PKEXEC_UID=$RAYE_TEST_UID sh "$helper" watch "$RAYE_TEST_UID" 5 "$me" 2>/dev/null
check "the watcher is never run through pkexec" test $? -eq 2
check "boot takes it away" grep -qs '^r /etc/sddm.conf.d/zz-raye-switch.conf' "$SOURCE_DIR"/data/tmpfiles/raye-desktop-switch.conf
rm -f "$dir/run-calls"

# SDDM keeps what it read: its ConfigBase::load() (sddm 0.21) reads the
# files again in order (system folder, /etc/sddm.conf.d, sddm.conf) over
# the values in memory and never forgets a key no file names any more.
# This models it, to show the one automatic sign-in really ends.
memory=$dir/sddm-memory
: > "$memory"
sddm_reload() {
    set --
    for d in "$RAYE_SDDM_SYSTEM_DIR" "$RAYE_SDDM_CONF_DIR"; do
        for f in "$d"/*.conf; do [ -f "$f" ] && set -- "$@" "$f"; done
    done
    [ -f "$RAYE_SDDM_MAIN" ] && set -- "$@" "$RAYE_SDDM_MAIN"
    awk '
        FILENAME ~ /sddm-memory$/ { n = index($0, "="); if (n) v[substr($0, 1, n - 1)] = substr($0, n + 1); next }
        { sub(/\r$/, ""); line = $0; gsub(/^[ \t]+|[ \t]+$/, "", line) }
        line ~ /^\[/ { section = line; next }
        section == "[Autologin]" { n = index(line, "="); if (n) {
            k = substr(line, 1, n - 1); gsub(/[ \t]+$/, "", k)
            val = substr(line, n + 1); gsub(/^[ \t]+/, "", val)
            if (k == "User" || k == "Session" || k == "Relogin") v[k] = val } }
        END { print "User=" v["User"]; print "Session=" v["Session"]; print "Relogin=" v["Relogin"] }
    ' "$memory" "$@" > "$memory.new"
    mv "$memory.new" "$memory"
}
remembers() { grep -qx "$1" "$memory"; }
export RAYE_SDDM_SYSTEM_DIR=$dir/system.d
mkdir -p "$RAYE_SDDM_SYSTEM_DIR"
cp "$SOURCE_DIR/data/sddm/00-raye-autologin-defaults.conf" "$RAYE_SDDM_SYSTEM_DIR/"
mine > "$dir/logind/5"
printf '[Autologin]\nSession=plasma\n' > "$dir/sddm.conf"
sddm_reload
check "before: SDDM signs nobody in" remembers "User="
sh "$helper" switch hyprland 5
sddm_reload
check "the session ends: SDDM signs me in, into Hyprland" sh -c "grep -qx 'User=$me' '$memory' && grep -qx 'Session=raye-hyprland.desktop' '$memory' && grep -qx 'Relogin=true' '$memory'"
sed -i 's/State=active/State=closing/' "$dir/logind/5"
printf '5 %s %s seat0\n9 %s %s seat0\n' "$RAYE_TEST_UID" "$me" "$RAYE_TEST_UID" "$me" > "$dir/logind/list"
sh "$helper" watch "$RAYE_TEST_UID" 5 "$me"
sddm_reload
check "the next session end: SDDM forgot me (the sign-in screen again)" remembers "User="
check "and does not sign in again by itself" remembers "Relogin=false"
check "the defaults are shipped in SDDM's system folder" grep -qs 'sddm/sddm.conf.d' "$SOURCE_DIR/CMakeLists.txt"
rm -f "$dir/sddm.conf" "$dir/logind/list" "$RAYE_SDDM_SYSTEM_DIR/00-raye-autologin-defaults.conf"
mine > "$dir/logind/5"

check "the helper has a policy that runs it" grep -qs "/usr/libexec/raye-settings/desktop-session<" "$SOURCE_DIR"/data/polkit/*.policy
exit $failed
