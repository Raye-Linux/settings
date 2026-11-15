#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Device Portal must stay off until it is turned on in Settings: the
# package may enable the storage sense timer for everyone, never the portal.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
command -v dh_installsystemduser >/dev/null || { echo "dh_installsystemduser is missing"; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/debian/raye-settings/usr/lib/systemd/user"
cp "$SOURCE_DIR/debian/control" "$SOURCE_DIR/debian/rules" "$work/debian/"
cp "$SOURCE_DIR"/portal/units/* "$SOURCE_DIR"/data/systemd/user/* "$work/debian/raye-settings/usr/lib/systemd/user/"
cd "$work" || exit 1
make -s -f debian/rules override_dh_installsystemduser >/dev/null 2>&1 \
    || { echo "debian/rules has no working override_dh_installsystemduser"; exit 1; }
script=debian/raye-settings.postinst.debhelper
fail=0
if grep -q raye-device-portal "$script" 2>/dev/null; then
    echo "the package would turn Device Portal on for every account:"
    grep raye-device-portal "$script"
    fail=1
fi
grep -q raye-storage-sense.timer "$script" 2>/dev/null || { echo "storage sense timer is no longer enabled"; fail=1; }
[ $fail = 0 ] && echo "ok"
exit $fail
