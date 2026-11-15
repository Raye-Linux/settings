#!/bin/sh
# Recovery › Reset your settings, tried in a temp home: what moves, what
# stays, the sign-in script, keeping the wallpaper, undo and the 30 days.
set -u
helper=${SOURCE_DIR:?}/data/libexec/reset-settings
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fails=0

ok() { printf 'ok - %s\n' "$1"; }
fail() { printf 'FAIL - %s\n' "$1"; fails=$((fails + 1)); }
check() { # description, command...
    what=$1; shift
    if "$@"; then ok "$what"; else fail "$what"; fi
}

export HOME="$work/home"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
export RAYE_RESET_AS_ROOT=yes
cfg=$XDG_CONFIG_HOME
state=$XDG_DATA_HOME/raye-settings
run() { RAYE_RESET_NOW=$now sh "$helper" "$@"; }

make_home() {
    rm -rf "$HOME"
    mkdir -p "$cfg/gtk-3.0" "$cfg/kdedefaults" "$cfg/autostart" "$cfg/plasma-workspace/env" \
        "$cfg/mozilla/firefox" "$HOME/Documents" "$HOME/Pictures"
    echo "desktop picture" > "$HOME/Pictures/sea.png"
    cat > "$cfg/plasma-org.kde.plasma.desktop-appletsrc" <<EOF
[Containments][1]
plugin=org.kde.plasma.folder

[Containments][1][Wallpaper][org.kde.image][General]
Image=file://$HOME/Pictures/sea.png
EOF
    cat > "$cfg/kscreenlockerrc" <<'EOF'
[Daemon]
Timeout=7

[Greeter]
WallpaperPlugin=org.kde.image

[Greeter][Wallpaper][org.kde.image][General]
Image=file:///usr/share/wallpapers/Raye/lock.png
EOF
    for f in kwinrc kwinrulesrc plasmashellrc plasmarc kdeglobals kglobalshortcutsrc \
        dolphinrc konsolerc rayeshellrc plasma-localerc kxkbrc ktimezonedrc kwalletrc \
        mimeapps.list katerc; do
        echo "[General]" > "$cfg/$f"
        echo "mine=$f" >> "$cfg/$f"
    done
    echo "gtk-theme-name=Mine" > "$cfg/gtk-3.0/settings.ini"
    echo "bookmark" > "$cfg/gtk-3.0/bookmarks"
    echo "LookAndFeelPackage=mine" > "$cfg/kdedefaults/package"
    echo "[Desktop Entry]" > "$cfg/autostart/myapp.desktop"
    echo "export MINE=1" > "$cfg/plasma-workspace/env/mine.sh"
    echo "profile" > "$cfg/mozilla/firefox/profiles.ini"
    echo "letter" > "$HOME/Documents/letter.txt"
}

now=20261031142200
make_home

# nothing planned, nothing to undo
out=$(run status)
check "status: nothing planned" [ "$out" = "pending none" ]

# schedule: nothing moves yet, the sign-in script and the plan are written
run schedule
check "schedule: plan written" [ "$(cat "$state/reset-pending")" = reset ]
check "schedule: sign-in script written" [ -f "$cfg/plasma-workspace/env/raye-reset-settings.sh" ]
check "schedule: nothing moved yet" [ -f "$cfg/kwinrc" ]
check "schedule: status says so" sh -c "RAYE_RESET_NOW=$now sh '$helper' status | grep -qx 'pending reset'"
grep -q "apply-pending" "$cfg/plasma-workspace/env/raye-reset-settings.sh"
check "schedule: script calls the helper" [ $? -eq 0 ]
# the script is sourced by sh at sign-in: it must parse
check "schedule: script is valid sh" sh -n "$cfg/plasma-workspace/env/raye-reset-settings.sh"

# cancel
run cancel
check "cancel: plan gone" [ ! -e "$state/reset-pending" ]
check "cancel: script gone" [ ! -e "$cfg/plasma-workspace/env/raye-reset-settings.sh" ]

# a plan for undo without any reset is refused
run schedule-undo 2>/dev/null
check "schedule-undo: refused with nothing to undo" [ $? -ne 0 ]
check "schedule-undo: no plan left behind" [ ! -e "$state/reset-pending" ]

# bad option
run schedule --everything 2>/dev/null
check "schedule: unknown option refused" [ $? -eq 2 ]

# the reset itself, as the sign-in script runs it
run schedule
(RAYE_RESET_NOW=$now; export RAYE_RESET_NOW; . "$cfg/plasma-workspace/env/raye-reset-settings.sh")
saved=$state/reset-20261031-142200/config
for f in kwinrc kwinrulesrc plasmashellrc plasmarc kdeglobals kglobalshortcutsrc dolphinrc \
    konsolerc rayeshellrc kscreenlockerrc plasma-org.kde.plasma.desktop-appletsrc \
    gtk-3.0/settings.ini kdedefaults/package; do
    check "reset: $f moved aside" sh -c "[ ! -e '$cfg/$f' ] && [ -e '$saved/$f' ]"
done
for f in plasma-localerc kxkbrc ktimezonedrc kwalletrc mimeapps.list katerc gtk-3.0/bookmarks \
    autostart/myapp.desktop plasma-workspace/env/mine.sh mozilla/firefox/profiles.ini; do
    check "reset: $f stays" [ -e "$cfg/$f" ]
done
check "reset: files stay" [ -e "$HOME/Documents/letter.txt" ]
check "reset: content kept intact" grep -qx "mine=kwinrc" "$saved/kwinrc"
check "reset: plan done" [ ! -e "$state/reset-pending" ]
check "reset: sign-in script gone" [ ! -e "$cfg/plasma-workspace/env/raye-reset-settings.sh" ]
check "reset: no wallpaper kept unless asked" [ ! -e "$cfg/autostart/raye-keep-wallpaper.desktop" ]
out=$(run status)
check "reset: undo offered for 30 days" sh -c "echo '$out' | grep -qx 'undo 20261031142200 30'"

# a second sign-in runs nothing
RAYE_RESET_NOW=$now sh "$helper" apply-pending
check "apply-pending: nothing planned, nothing done" [ -d "$saved" ]

# Plasma writes fresh settings, then the user undoes, 10 days later
echo "fresh" > "$cfg/kwinrc"
mkdir -p "$cfg/kdedefaults" && echo fresh > "$cfg/kdedefaults/package"
now=20261110142200
out=$(run status)
check "undo: 20 days left after 10" sh -c "echo '$out' | grep -qx 'undo 20261031142200 20'"
run schedule-undo
check "undo: planned" [ "$(cat "$state/reset-pending")" = undo ]
run apply-pending
check "undo: kwinrc back" grep -qx "mine=kwinrc" "$cfg/kwinrc"
check "undo: nested file back" grep -qx "gtk-theme-name=Mine" "$cfg/gtk-3.0/settings.ini"
check "undo: folder back" grep -qx "LookAndFeelPackage=mine" "$cfg/kdedefaults/package"
check "undo: other gtk file untouched" [ -e "$cfg/gtk-3.0/bookmarks" ]
check "undo: what Plasma wrote is kept aside" grep -qx fresh "$state/undone-20261110-142200/config/kwinrc"
check "undo: saved reset gone" [ ! -e "$state/reset-20261031-142200" ]
check "undo: nothing more to undo" [ "$(run status)" = "pending none" ]

# keep the wallpaper
now=20261201080000
make_home
run schedule --keep-wallpaper
check "keep wallpaper: planned" [ "$(cat "$state/reset-pending")" = reset-keep-wallpaper ]
run apply-pending
check "keep wallpaper: one-off start entry" [ -f "$cfg/autostart/raye-keep-wallpaper.desktop" ]
check "keep wallpaper: entry calls the helper" grep -q "restore-wallpaper" "$cfg/autostart/raye-keep-wallpaper.desktop"
check "keep wallpaper: picture remembered" [ "$(cat "$state/keep-wallpaper")" = "$HOME/Pictures/sea.png" ]
check "keep wallpaper: lock screen picture stays" grep -q "lock.png" "$cfg/kscreenlockerrc"
check "keep wallpaper: lock screen timeout reset" sh -c "! grep -q Timeout '$cfg/kscreenlockerrc'"
check "keep wallpaper: lock screen plugin stays" grep -qx "WallpaperPlugin=org.kde.image" "$cfg/kscreenlockerrc"

# the one-off entry applies the picture once, then goes
mkdir -p "$work/bin"
cat > "$work/bin/plasma-apply-wallpaperimage" <<EOF
#!/bin/sh
echo "\$1" > "$work/applied"
EOF
chmod +x "$work/bin/plasma-apply-wallpaperimage"
PATH="$work/bin:$PATH" RAYE_RESET_NOW=$now sh "$helper" restore-wallpaper
check "restore wallpaper: applied" [ "$(cat "$work/applied" 2>/dev/null)" = "$HOME/Pictures/sea.png" ]
check "restore wallpaper: entry gone" [ ! -e "$cfg/autostart/raye-keep-wallpaper.desktop" ]
check "restore wallpaper: memory gone" [ ! -e "$state/keep-wallpaper" ]

# after 30 days the saved settings go for good
now=20270105080000
out=$(run status)
check "30 days: undo no longer offered" [ "$out" = "pending none" ]
check "30 days: saved settings removed" [ ! -e "$state/reset-20261201-080000" ]

# quotes and spaces in the paths do not break the sign-in script
now=20270201080000
odd="$work/Raye's helpers"
mkdir -p "$odd"
cp "$helper" "$odd/reset-settings"
chmod +x "$odd/reset-settings"
helper="$odd/reset-settings"
export HOME="$work/it's my home"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
cfg=$XDG_CONFIG_HOME
state=$XDG_DATA_HOME/raye-settings
make_home
run schedule
check "odd paths: script parses" sh -n "$cfg/plasma-workspace/env/raye-reset-settings.sh"
(RAYE_RESET_NOW=$now; export RAYE_RESET_NOW; . "$cfg/plasma-workspace/env/raye-reset-settings.sh")
check "odd paths: reset done" [ -e "$state/reset-20270201-080000/config/kwinrc" ]

# never as root without saying so
unset RAYE_RESET_AS_ROOT
if [ "$(id -u)" -eq 0 ]; then
    run status >/dev/null 2>&1
    check "root: refused" [ $? -eq 2 ]
fi

if [ "$fails" -ne 0 ]; then
    echo "$fails check(s) failed"
    exit 1
fi
echo "all checks passed"
