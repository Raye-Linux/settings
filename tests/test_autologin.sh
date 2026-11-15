#!/bin/sh
# The automatic sign-in helper as it ships: it writes for the person who
# asked and no one else, comes after the files of the packages, and takes
# it back without leaving anything.
set -u
helper=$SOURCE_DIR/data/libexec/autologin
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
failed=0
check() { # description, then a command that must succeed
    what=$1; shift
    if "$@"; then echo "ok $what"; else echo "FAILED $what"; failed=1; fi
}
export RAYE_SDDM_CONF_DIR=$dir/conf.d
export RAYE_TEST_UID=$(id -u)
me=$(id -un)
[ "$RAYE_TEST_UID" != 0 ] || { me=$(getent passwd 1 | cut -d: -f1); export RAYE_TEST_UID=1; }

check "on writes my login" sh "$helper" on
file=$dir/conf.d/zz-raye-autologin.conf
check "the file is there" test -f "$file"
check "it names me" grep -qx "User=$me" "$file"
check "it is in the section SDDM reads" grep -qx '\[Autologin\]' "$file"
check "it picks the desktop session" grep -qx 'Session=plasma' "$file"
check "everyone can read it (SDDM, Settings)" sh -c "[ \"\$(stat -c %a '$file')\" = 644 ]"
check "--check says who" sh -c "[ \"\$(sh '$helper' --check)\" = '$me' ]"
# read after sddm-raye.conf and sddm-theme-raye.conf
check "it sorts after the packages' files" sh -c "[ \"\$(printf 'sddm-raye.conf\nsddm-theme-raye.conf\nzz-raye-autologin.conf\n' | LC_ALL=C sort | tail -1)\" = zz-raye-autologin.conf ]"
check "off takes it back" sh "$helper" off
check "nothing is left" test ! -e "$file"
check "off twice is fine" sh "$helper" off
check "--check says nobody" sh -c "[ -z \"\$(sh '$helper' --check)\" ]"

# only the caller: no way to name somebody else
RAYE_TEST_UID=0 sh "$helper" on 2>/dev/null
check "never the root account" test $? -eq 2
sh "$helper" on someoneelse >/dev/null 2>&1
check "an extra word changes nothing about who" grep -qx "User=$me" "$file"
sh "$helper" off
sh "$helper" nonsense 2>/dev/null
check "anything else is refused" test $? -eq 2
# outside pkexec the test folder is honoured; under it, never
mkdir -p "$dir/other"
printf '[Autologin]\nUser=fromtestfolder\n' > "$dir/other/zz-raye-autologin.conf"
check "the test folder is read outside pkexec" sh -c "RAYE_SDDM_CONF_DIR='$dir/other' sh '$helper' --check | grep -q fromtestfolder"
check "pkexec ignores the test folder" sh -c "! PKEXEC_UID=$RAYE_TEST_UID RAYE_SDDM_CONF_DIR='$dir/other' sh '$helper' --check | grep -q fromtestfolder"

# what SDDM ends up with counts, not only our own file: the system folder,
# the other files of /etc/sddm.conf.d and /etc/sddm.conf all take part
export RAYE_SDDM_SYSTEM_DIR=$dir/system.d RAYE_SDDM_MAIN=$dir/sddm.conf
mkdir -p "$dir/system.d"
printf '[Autologin]\nUser=%s\n' "$me" > "$dir/conf.d/kde_settings.conf"
check "--check says what SDDM reads" sh -c "[ \"\$(sh '$helper' --check)\" = '$me' ]"
check "off with another file still signing me in" sh "$helper" off
check "off really turns it off" sh -c "[ -z \"\$(sh '$helper' --check)\" ]"
check "the other file is left as it was" grep -qx "User=$me" "$dir/conf.d/kde_settings.conf"
rm -f "$dir/conf.d/kde_settings.conf"
check "on after that" sh "$helper" on
check "on is on again" sh -c "[ \"\$(sh '$helper' --check)\" = '$me' ]"
sh "$helper" off
check "and off again leaves nothing" test ! -e "$file"
printf '[Autologin]\nUser=%s\n' "$me" > "$dir/system.d/autologin.conf"
check "off over the system folder" sh "$helper" off
check "the system folder is overruled" sh -c "[ -z \"\$(sh '$helper' --check)\" ]"
rm -f "$dir/system.d/autologin.conf" "$file"
# /etc/sddm.conf is read last and is not ours to rewrite: say it failed
printf '[Autologin]\nUser=someoneelse\n' > "$dir/sddm.conf"
sh "$helper" on 2>/dev/null
check "on loses to sddm.conf and says so" test $? -ne 0
check "--check says who really signs in" sh -c "[ \"\$(sh '$helper' --check)\" = someoneelse ]"
check "nothing of ours is left behind" test ! -e "$file"
printf '[Autologin]\nUser=%s\n' "$me" > "$dir/sddm.conf"
sh "$helper" off 2>/dev/null
check "off loses to sddm.conf and says so" test $? -ne 0
rm -f "$dir/sddm.conf" "$file"
check "pkexec ignores the test system folder" sh -c "printf '[Autologin]\nUser=fromtest\n' > '$dir/system.d/t.conf'; ! PKEXEC_UID=$RAYE_TEST_UID sh '$helper' --check | grep -q fromtest"
rm -f "$dir/system.d/t.conf"
unset RAYE_SDDM_SYSTEM_DIR RAYE_SDDM_MAIN

for file in autologin printing fingerprint-signin; do
    check "$file has a policy that runs it" grep -qs "/usr/libexec/raye-settings/$file<" "$SOURCE_DIR"/data/polkit/*.policy
done
exit $failed
