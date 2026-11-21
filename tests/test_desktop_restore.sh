#!/bin/sh
# The apps of the other desktop open again after a switch, once, on the
# desktop the list was written for, only soon after, and only apps that
# have a .desktop file. The list's format is the one Settings writes
# (DesktopEnvironment::restoreList, checked in test_desktopenvironment).
set -u
helper=$SOURCE_DIR/data/libexec/desktop-restore
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
failed=0
check() {
    what=$1; shift
    if "$@"; then echo "ok $what"; else echo "FAILED $what"; failed=1; fi
}
export HOME=$dir/home XDG_STATE_HOME=$dir/state XDG_DATA_HOME=$dir/data XDG_DATA_DIRS=$dir/sys
mkdir -p "$dir/state/raye" "$dir/data/applications" "$dir/sys/applications"
for id in org.kde.kate org.kde.dolphin org.raye.settings; do touch "$dir/sys/applications/$id.desktop"; done
printf '#!/bin/sh\necho "$1 $2" >> %s/started\n' "$dir" > "$dir/launch"; chmod +x "$dir/launch"
export RAYE_LAUNCHER=$dir/launch
# the wallet step (raye-settings --wallet-open): written down in the same log
printf '#!/bin/sh\necho "wallet $1" >> %s/started\n' "$dir" > "$dir/wallet"; chmod +x "$dir/wallet"
export RAYE_WALLET_OPEN=$dir/wallet
list=$dir/state/raye/desktop-switch
write() { # to, seconds ago
    printf '# Written by Settings\nto=%s\nat=%s\napp=org.kde.kate\napp=org.kde.dolphin\napp=../../bin/sh\napp=not.installed\napp=org.raye.settings\n' \
        "$1" "$(( $(date +%s) - $2 ))" > "$list"
}
started() { cat "$dir/started" 2>/dev/null; }

write hyprland 5
XDG_CURRENT_DESKTOP=Hyprland sh "$helper"
check "the saved passwords open first, then the apps" sh -c "[ \"\$(cut -d' ' -f1 '$dir/started' | tr '\n' ' ')\" = 'wallet org.kde.kate org.kde.dolphin org.raye.settings ' ]"
check "through the wallet step of Settings" grep -qx "wallet --wallet-open" "$dir/started"
check "from their own .desktop files" grep -qx "org.kde.kate $dir/sys/applications/org.kde.kate.desktop" "$dir/started"
check "the list is read once" test ! -e "$list"
rm -f "$dir/started"
XDG_CURRENT_DESKTOP=Hyprland sh "$helper"
check "a second sign-in opens nothing" test ! -e "$dir/started"

write plasma 5
XDG_CURRENT_DESKTOP=Hyprland sh "$helper"
check "not on the other desktop" test ! -e "$dir/started"
check "and the list is gone anyway" test ! -e "$list"
write plasma 5
XDG_CURRENT_DESKTOP=KDE sh "$helper"
check "Plasma opens a list written for Plasma" grep -q '^org.kde.dolphin ' "$dir/started"
rm -f "$dir/started"
write hyprland 900
XDG_CURRENT_DESKTOP=Hyprland sh "$helper"
check "not long after the switch" test ! -e "$dir/started"
ln -s "$dir/elsewhere" "$list"; printf 'to=hyprland\nat=%s\napp=org.kde.kate\n' "$(date +%s)" > "$dir/elsewhere"
XDG_CURRENT_DESKTOP=Hyprland sh "$helper"
check "not through a link" test ! -e "$dir/started"
rm -f "$list"
XDG_CURRENT_DESKTOP=Hyprland sh "$helper"
check "no list: nothing, quietly" test $? -eq 0
check "it starts with both desktops" grep -qx 'OnlyShowIn=KDE;Hyprland;' "$SOURCE_DIR/data/autostart/org.raye.settings.desktop-restore.desktop"
check "the autostart runs the helper" grep -qx 'Exec=/usr/libexec/raye-settings/desktop-restore' "$SOURCE_DIR/data/autostart/org.raye.settings.desktop-restore.desktop"
exit $failed
