#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# data/libexec/storage-clean, run for real in a folder that stands for
# the computer (--root) with a home inside it. It must free what it
# says and nothing else: every check below looks at the files that
# must stay as much as at those that must go.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
helper=$SOURCE_DIR/data/libexec/storage-clean
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0
checks=0

ok() { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failures=$((failures + 1)); echo "FAIL: $*"; }
check() { if eval "$1"; then ok; else bad "$2"; fi; }

# a file of N KiB, with real blocks behind it
mk() { mkdir -p "$(dirname "$1")"; head -c $(($2 * 1024)) /dev/urandom > "$1"; }
blocks() { stat -c %b "$@" | awk '{ s += $1 * 512 } END { printf "%.0f\n", s }'; }
old() { touch -d "$1 days ago" "$2"; touch -a -d "$1 days ago" "$2"; }

# a fresh computer for each case
setup() {
    rm -rf "$tmp/r" "$tmp/bin" "$tmp/log"
    R=$tmp/r
    H=$R/home/alex
    mkdir -p "$R/etc" "$tmp/bin"
    # the user's things that are never touched
    mk "$H/Documents/report.odt" 4
    mk "$H/.cache/other/data" 4
    mk "$R/victim" 4
    # trash: a file, a folder with files in it, a link out of the trash
    T=$H/.local/share/Trash
    mk "$T/files/photo.jpg" 8
    mk "$T/files/old folder/a" 4
    mk "$T/files/old folder/b/c" 4
    ln -s "$R/victim" "$T/files/-link out"
    mkdir -p "$T/info"
    for name in photo.jpg "old folder" "-link out"; do
        printf '[Trash Info]\nPath=/home/alex/%s\nDeletionDate=2026-10-01T10:00:00\n' "$name" \
            > "$T/info/$name.trashinfo"
    done
    mk "$T/expunged/x" 4
    printf '0 0 x\n' > "$T/directorysizes"
    # thumbnails
    mk "$H/.cache/thumbnails/normal/a.png" 4
    mk "$H/.cache/thumbnails/large/b.png" 4
    # apt
    A=$R/var/cache/apt/archives
    mk "$A/firefox_1_amd64.deb" 16
    mk "$A/libfoo_2_amd64.deb" 8
    mk "$A/partial/half.deb" 4
    mk "$A/sub/deeper.deb" 4
    : > "$A/lock"
    mk "$R/var/cache/apt/pkgcache.bin" 4
    # logs
    L=$R/var/log
    mk "$L/syslog" 4
    mk "$L/syslog.1" 4
    mk "$L/syslog.2.gz" 4
    mk "$L/apt/history.log.1.gz" 4
    mk "$L/dpkg.log.old" 4
    mk "$L/journal/abc/system.journal" 8
    mk "$L/journal/abc/system@0001-old.journal" 8
    mk "$L/journal/abc/user-1000@0002-old.journal~" 8
    mk "$L/journal/abc/system@0003-new.journal" 8
    old 30 "$L/journal/abc/system@0001-old.journal"
    old 20 "$L/journal/abc/user-1000@0002-old.journal~"
    mk "$L/journal/abc/system@0004-week.journal" 8
    old 5 "$L/journal/abc/system@0004-week.journal"
    # helpers that must not run in a test root
    printf '#!/bin/sh\necho "$@" >> %s/apt-get.called\n' "$tmp" > "$tmp/bin/apt-get"
    chmod +x "$tmp/bin/apt-get"
}

run() {
    env -u PKEXEC_UID -u XDG_DATA_HOME -u XDG_CACHE_HOME -u XDG_CONFIG_HOME \
        HOME="$H" PATH="$tmp/bin:$PATH" sh "$helper" --root "$R" "$@"
}
field() { awk -F '\t' -v k="$1" -v i="$2" -v f="$3" '$1 == k && $2 == i { print $f }'; }

# --- wrong use -----------------------------------------------------------
setup
for args in "" "nothing" "--older-than x trash" "--older-than -3 trash" "--older-than 99999 trash" \
            "--root relative trash" "--root /does/not/exist trash" "--sense trash" "--root"; do
    # shellcheck disable=SC2086
    env HOME="$H" sh "$helper" $args >/dev/null 2>&1
    check "[ $? = 1 ]" "wrong use accepted: '$args'"
done
PKEXEC_UID=1000 HOME="$H" sh "$helper" --root "$R" apt >/dev/null 2>&1
check "[ $? = 1 ]" "--root accepted under pkexec"

# --- a dry run deletes nothing -------------------------------------------
setup
before=$(find "$R" | sort)
out=$(run trash thumbnails apt logs); rc=$?
check "[ $rc = 0 ]" "dry run failed"
check "[ \"\$(find \"\$R\" | sort)\" = \"\$before\" ]" "dry run changed files"
for item in trash thumbnails apt logs; do
    check "[ -n \"\$(printf '%s\n' \"\$out\" | field size $item 3)\" ]" "no size for $item"
done
check "[ ! -e \"\$tmp/apt-get.called\" ]" "apt-get ran in a dry run"
want=$(blocks "$A/firefox_1_amd64.deb" "$A/libfoo_2_amd64.deb" "$A/partial/half.deb")
check "[ \"\$(printf '%s\n' \"\$out\" | field size apt 3)\" = \"$want\" ]" "apt size $want, got: $out"
check "[ \"\$(printf '%s\n' \"\$out\" | field size apt 4)\" = 3 ]" "apt count is not 3"
want=$(blocks "$H/.cache/thumbnails/normal/a.png" "$H/.cache/thumbnails/large/b.png")
check "[ \"\$(printf '%s\n' \"\$out\" | field size thumbnails 3)\" = \"$want\" ]" "thumbnails size"
check "[ \"\$(printf '%s\n' \"\$out\" | field size trash 4)\" = 3 ]" "trash count is not 3"
check "[ \"\$(printf '%s\n' \"\$out\" | field size logs 4)\" = 6 ]" "logs count is not 6: $out"

# --- trash: empty it all ---------------------------------------------------
setup
dry=$(run trash | field size trash 3)
out=$(run --apply trash); rc=$?
check "[ $rc = 0 ]" "emptying the trash failed: $out"
check "[ -z \"\$(find \"\$T/files\" \"\$T/info\" \"\$T/expunged\" -mindepth 1)\" ]" "trash not empty"
check "[ -d \"\$T/files\" ] && [ -d \"\$T/info\" ]" "trash folders were removed"
check "[ ! -e \"\$T/directorysizes\" ]" "directorysizes left"
check "[ -f \"\$R/victim\" ]" "the file a trashed link points to was deleted"
check "[ -f \"\$H/Documents/report.odt\" ]" "a document was deleted"
check "[ \"\$(printf '%s\n' \"\$out\" | field freed trash 3)\" = \"$dry\" ]" "freed differs from the dry run"

# --- trash: only what is older than 30 days --------------------------------
setup
old 40 "$T/info/old folder.trashinfo"
old 40 "$T/info/-link out.trashinfo"
out=$(run --apply --older-than 30 trash)
check "[ ! -e \"\$T/files/old folder\" ] && [ ! -e \"\$T/info/old folder.trashinfo\" ]" "old trash entry kept"
check "[ ! -L \"\$T/files/-link out\" ]" "old link entry kept"
check "[ -f \"\$T/files/photo.jpg\" ] && [ -f \"\$T/info/photo.jpg.trashinfo\" ]" "recent trash entry deleted"
check "[ -f \"\$R/victim\" ]" "link target deleted"
check "[ \"\$(printf '%s\n' \"\$out\" | field freed trash 4)\" = 2 ]" "two entries freed: $out"

# --- thumbnails ------------------------------------------------------------
setup
old 45 "$H/.cache/thumbnails/large/b.png"
run --apply --older-than 30 thumbnails >/dev/null
check "[ -f \"\$H/.cache/thumbnails/normal/a.png\" ] && [ ! -e \"\$H/.cache/thumbnails/large/b.png\" ]" "thumbnail age"
run --apply thumbnails >/dev/null
check "[ ! -e \"\$H/.cache/thumbnails/normal/a.png\" ] && [ -d \"\$H/.cache/thumbnails/normal\" ]" "thumbnails not cleared"
check "[ -f \"\$H/.cache/other/data\" ]" "another cache was touched"

# --- apt -------------------------------------------------------------------
setup
out=$(run --apply apt)
check "[ ! -e \"\$A/firefox_1_amd64.deb\" ] && [ ! -e \"\$A/partial/half.deb\" ]" "package files kept"
check "[ -e \"\$A/lock\" ] && [ -d \"\$A/partial\" ]" "apt's lock or folder removed"
check "[ -f \"\$A/sub/deeper.deb\" ] && [ -f \"\$R/var/cache/apt/pkgcache.bin\" ]" "apt deleted outside the archives"
check "[ ! -e \"\$tmp/apt-get.called\" ]" "apt-get ran in a test root"

# --- logs ------------------------------------------------------------------
setup
run --apply logs >/dev/null
check "[ -f \"\$L/syslog\" ] && [ -f \"\$L/journal/abc/system.journal\" ]" "a live log was deleted"
check "[ ! -e \"\$L/syslog.1\" ] && [ ! -e \"\$L/syslog.2.gz\" ] && [ ! -e \"\$L/apt/history.log.1.gz\" ] && [ ! -e \"\$L/dpkg.log.old\" ]" "rotated logs kept"
check "[ ! -e \"\$L/journal/abc/system@0001-old.journal\" ] && [ ! -e \"\$L/journal/abc/user-1000@0002-old.journal~\" ]" "old journals kept"
check "[ -f \"\$L/journal/abc/system@0003-new.journal\" ] && [ -f \"\$L/journal/abc/system@0004-week.journal\" ]" "a recent journal was deleted"

# --- a link where a folder should be is not followed ---------------------
setup
rm -rf "$A"
mkdir -p "$tmp/elsewhere"
mk "$tmp/elsewhere/keep.deb" 4
ln -s "$tmp/elsewhere" "$A"
run --apply apt >/dev/null
check "[ -f \"\$tmp/elsewhere/keep.deb\" ]" "followed a link out of the archives"

# --- snapshots, with a stand-in for snapper ---------------------------------
setup
cat > "$tmp/bin/snapper" <<EOF
#!/bin/sh
echo "\$@" >> "$tmp/snapper.log"
case " \$* " in
    *" list "*)
        printf '0,no,no,\n1,no,no,1000\n2,no,no,2000\n3,yes,yes,3000\n4,no,no,4000\n5,no,no,5000\n' ;;
esac
EOF
chmod +x "$tmp/bin/snapper"
out=$(run snapshots)
check "[ \"\$out\" = \"\$(printf 'error\tsnapshots\tmissing')\" ]" "snapshots without snapper set up: $out"
mkdir -p "$R/etc/snapper/configs"
: > "$R/etc/snapper/configs/root"
out=$(run snapshots)
check "[ \"\$(printf '%s\n' \"\$out\" | field size snapshots 3)\" = 7000 ]" "snapshot bytes: $out"
check "[ \"\$(printf '%s\n' \"\$out\" | field size snapshots 4)\" = 3 ]" "snapshot count: $out"
check "! grep -q delete \"\$tmp/snapper.log\"" "a dry run deleted a snapshot"
out=$(run --apply snapshots)
deleted=$(grep delete "$tmp/snapper.log" | awk '{ print $NF }' | sort -n | tr '\n' ' ')
check "[ \"\$deleted\" = '1 2 4 ' ]" "deleted snapshots '$deleted' (keep 0, the newest and the one in use)"
check "[ \"\$(printf '%s\n' \"\$out\" | field freed snapshots 4)\" = 3 ]" "freed snapshots: $out"

# --- system files need root; a test that pretends to be a user --------------
setup
printf '#!/bin/sh\necho 1000\n' > "$tmp/bin/id"
chmod +x "$tmp/bin/id"
out=$(env -u PKEXEC_UID HOME="$H" PATH="$tmp/bin:$PATH" sh "$helper" --apply apt); rc=$?
check "[ $rc = 2 ]" "a user cleaned system files"
check "[ \"\$out\" = \"\$(printf 'error\tapt\tdenied')\" ]" "user apt: $out"
check "[ ! -e \"\$tmp/apt-get.called\" ]" "apt-get ran for a user"
out=$(env -u PKEXEC_UID HOME="$H" PATH="$tmp/bin:$PATH" sh "$helper" --root "$R" apt)
check "[ -n \"\$(printf '%s\n' \"\$out\" | field size apt 3)\" ]" "a user cannot see the size of package files"

# --- pkexec never cleans a home -------------------------------------------
setup
out=$(PKEXEC_UID=1000 HOME="$H" sh "$helper" trash thumbnails); rc=$?
check "[ $rc = 2 ]" "trash cleaned under pkexec"
check "printf '%s\n' \"\$out\" | grep -q 'error.trash.denied'" "no denial for trash under pkexec"

# --- Storage Sense ---------------------------------------------------------
setup
old 40 "$T/info/photo.jpg.trashinfo"
old 40 "$H/.cache/thumbnails/normal/a.png"
mkdir -p "$H/.config"
printf '[Window]\nTrashDays=1\n\n[StorageSense]\nTrashDays=30\nThumbnails=false\n' > "$H/.config/raye-settingsrc"
run --apply --sense >/dev/null
check "[ ! -e \"\$T/files/photo.jpg\" ] && [ -e \"\$T/files/old folder\" ]" "sense: trash days"
check "[ -f \"\$H/.cache/thumbnails/normal/a.png\" ]" "sense: thumbnails off but cleaned"
printf '[StorageSense]\nTrashDays=0\nThumbnails=true\n' > "$H/.config/raye-settingsrc"
run --apply --sense >/dev/null
check "[ -e \"\$T/files/old folder\" ]" "sense: TrashDays=0 still emptied the trash"
check "[ ! -e \"\$H/.cache/thumbnails/normal/a.png\" ] && [ -f \"\$H/.cache/thumbnails/large/b.png\" ]" "sense: old thumbnails"
printf '[StorageSense]\nTrashDays=$(touch %s/pwned)\n' "$tmp" > "$H/.config/raye-settingsrc"
run --apply --sense >/dev/null
check "[ ! -e \"\$tmp/pwned\" ]" "sense ran text from the config file"
out=$(run --sense)
check "printf '%s\n' \"\$out\" | grep -q '^size'" "sense without --apply must only measure"

# --- Downloads ------------------------------------------------------------
setup
D=$H/Downloads
mk "$D/old.iso" 8
mk "$D/new.zip" 4
mk "$D/folder/old.txt" 4
old 40 "$D/old.iso"
old 40 "$D/folder/old.txt"
mk "$D/opened.pdf" 4
touch -m -d "40 days ago" "$D/opened.pdf"   # changed long ago, opened today
env HOME="$H" sh "$helper" downloads >/dev/null 2>&1
check "[ $? = 1 ]" "downloads without --older-than"
out=$(run --older-than 30 downloads)
check "[ \"\$(printf '%s\n' \"\$out\" | field size downloads 4)\" = 2 ]" "downloads measure: $out"
run --apply --older-than 30 downloads >/dev/null
check "[ ! -e \"\$D/old.iso\" ] && [ ! -e \"\$D/folder/old.txt\" ]" "old downloads kept"
check "[ -f \"\$D/new.zip\" ] && [ -f \"\$D/opened.pdf\" ] && [ -d \"\$D/folder\" ]" "downloads: too much deleted"
# a Downloads folder that is the home itself is never cleaned
setup
mkdir -p "$H/.config"
printf 'XDG_DOWNLOAD_DIR="$HOME/"\n' > "$H/.config/user-dirs.dirs"
old 40 "$H/Documents/report.odt"
out=$(run --apply --older-than 30 downloads)
check "[ -f \"\$H/Documents/report.odt\" ]" "downloads set to the home emptied the home"
check "printf '%s\n' \"\$out\" | grep -q 'error.downloads.missing'" "no error for downloads = home: $out"
printf 'XDG_DOWNLOAD_DIR="$HOME/Téléchargements"\n' > "$H/.config/user-dirs.dirs"
mk "$H/Téléchargements/a" 4
old 40 "$H/Téléchargements/a"
run --apply --older-than 30 downloads >/dev/null
check "[ ! -e \"\$H/Téléchargements/a\" ]" "downloads: user-dirs.dirs not followed"

# --- Storage Sense: Downloads and low disk space --------------------------
setup
mk "$H/Downloads/old.iso" 8
old 40 "$H/Downloads/old.iso"
mkdir -p "$H/.config"
printf '[StorageSense]\nTrashDays=0\nThumbnails=false\nDownloadsDays=30\n' > "$H/.config/raye-settingsrc"
run --apply --sense >/dev/null
check "[ ! -e \"\$H/Downloads/old.iso\" ]" "sense: DownloadsDays"
setup
mk "$H/Downloads/old.iso" 8
old 40 "$H/Downloads/old.iso"
mkdir -p "$H/.config"
printf '[StorageSense]\nTrashDays=0\nThumbnails=false\nDownloadsDays=0\n' > "$H/.config/raye-settingsrc"
run --apply --sense >/dev/null
check "[ -f \"\$H/Downloads/old.iso\" ]" "sense: DownloadsDays=0 deleted downloads"
# only when space is low, and only when the timer started it
printf '#!/bin/sh\nprintf "Filesystem 1024-blocks Used Available Capacity Mounted on\\n/dev/x 1000 %%s %%s x /\\n" "$((1000 - FREE))" "$FREE"\n' > "$tmp/bin/df"
chmod +x "$tmp/bin/df"
printf '[StorageSense]\nTrashDays=0\nThumbnails=false\nDownloadsDays=30\nFrequency=lowspace\n' > "$H/.config/raye-settingsrc"
export FREE=500 TRIGGER_UNIT=raye-storage-sense.timer
run --apply --sense >/dev/null
check "[ -f \"\$H/Downloads/old.iso\" ]" "sense ran with plenty of space"
FREE=50
run --apply --sense >/dev/null
check "[ ! -e \"\$H/Downloads/old.iso\" ]" "sense did not run with little space"
mk "$H/Downloads/old.iso" 8
old 40 "$H/Downloads/old.iso"
FREE=500
unset TRIGGER_UNIT
run --apply --sense >/dev/null
check "[ ! -e \"\$H/Downloads/old.iso\" ]" "Run now waited for low space"
unset FREE

# --- nothing outside the listed places, after everything ------------------
setup
outside=$(find "$R" | grep -v -e '/Trash/' -e '/thumbnails/' -e '/var/cache/apt/archives/' -e '/var/log/' | sort)
run --apply trash thumbnails apt logs >/dev/null
check "[ \"\$(find \"\$R\" | grep -v -e '/Trash/' -e '/thumbnails/' -e '/var/cache/apt/archives/' -e '/var/log/' | sort)\" = \"\$outside\" ]" "a file outside the clean-up places changed"

echo "storage-clean: $checks checks, $failures failed"
[ "$failures" = 0 ]
