#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# data/libexec/developer and portal-power, run against a folder that
# stands for the computer, with systemctl, ufw and kill recorded instead
# of run. What they refuse matters as much as what they do.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
dev=$SOURCE_DIR/data/libexec/developer
power=$SOURCE_DIR/data/libexec/portal-power
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failures=0
checks=0
ok() { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failures=$((failures + 1)); echo "FAIL: $*"; }
check() { if eval "$1"; then ok; else bad "$2"; fi; }

R=$tmp/r
log=$tmp/log
mkdir -p "$tmp/bin"
for tool in systemctl ufw systemd-run; do
    printf '#!/bin/sh\necho "%s $*" >> "%s"\n' "$tool" "$log" > "$tmp/bin/$tool"
    chmod +x "$tmp/bin/$tool"
done
setup() {
    rm -rf "$R" "$log"
    mkdir -p "$R/usr/sbin" "$R/etc/ufw"
    : > "$log"
    printf '#!/bin/sh\n' > "$R/usr/sbin/sshd"
    chmod +x "$R/usr/sbin/sshd"
}
run() { env -u PKEXEC_UID RAYE_ROOT="$R" PATH="$tmp/bin:$PATH" sh "$dev" "$@"; }
keys=etc/ssh/sshd_config.d/01-raye-keys-only.conf

setup
run ssh on; check '[ $? -eq 0 ]' "ssh on failed"
check 'grep -qx "PasswordAuthentication no" "$R/$keys"' "ssh on is not keys only by default"
check 'grep -qx "KbdInteractiveAuthentication no" "$R/$keys"' "keyboard-interactive left on"
check 'grep -q "systemctl enable --now ssh" "$log"' "sshd not started"
check '! grep -q "^ufw" "$log"' "ufw touched while the firewall is off"

setup
printf 'ENABLED=yes\n' > "$R/etc/ufw/ufw.conf"
run ssh on passwords; check '[ $? -eq 0 ]' "ssh on passwords failed"
check '[ ! -e "$R/$keys" ]' "passwords asked, keys-only file written"
check 'grep -qx "ufw allow OpenSSH" "$log"' "port 22 not opened with the firewall on"

setup
run ssh keys-only on && check '[ -e "$R/$keys" ]' "keys-only on did nothing"
run ssh keys-only off && check '[ ! -e "$R/$keys" ]' "keys-only off did nothing"

setup
rm "$R/usr/sbin/sshd"
run ssh on 2>/dev/null; check '[ $? -eq 3 ]' "ssh on without sshd did not say so"

setup
run ssh off; check 'grep -q "systemctl disable --now ssh.socket ssh.service" "$log"' "ssh off did not stop both units"

setup
run portal-firewall allow; check '! grep -q "^ufw" "$log"' "firewall off, ufw still called"
printf 'enabled=yes\n' > "$R/etc/ufw/ufw.conf"
run portal-firewall allow; check 'grep -qx "ufw allow from 192.168.0.0/16 to any app raye-device-portal" "$log"' "portal port not opened"
check 'grep -qx "ufw allow from fe80::/10 to any app raye-device-portal" "$log"' "portal port not opened to link-local IPv6"
check '! grep -qx "ufw allow raye-device-portal" "$log"' "portal port opened to anywhere"
check '[ "$(grep -c "^ufw allow from" "$log")" -eq 6 ]' "portal port opened to other networks"
check 'grep -qx "ufw delete allow raye-device-portal" "$log"' "the old anywhere rule was kept"
: > "$log"
run portal-firewall deny; check 'grep -qx "ufw delete allow from 10.0.0.0/8 to any app raye-device-portal" "$log"' "portal port not closed"

for args in "" "ssh" "ssh maybe" "ssh on root" "ssh keys-only" "portal-firewall open" "rm -rf /"; do
    setup
    # shellcheck disable=SC2086
    run $args 2>/dev/null; check '[ $? -eq 2 ]' "developer accepted: $args"
    check '[ ! -s "$log" ]' "developer ran something for: $args"
done

# under pkexec RAYE_ROOT is ignored: the real /etc, never a folder handed in
# (only when this machine's own firewall is off, so nothing real is read as on)
if ! grep -qix 'enabled=yes' /etc/ufw/ufw.conf 2>/dev/null; then
    setup
    printf 'ENABLED=yes\n' > "$R/etc/ufw/ufw.conf"
    env PKEXEC_UID=1000 RAYE_ROOT="$R" PATH="$tmp/bin:$PATH" sh "$dev" portal-firewall allow
    check '[ ! -s "$log" ]' "RAYE_ROOT followed under pkexec"
fi

# portal-power
prun() { env PATH="$tmp/bin:$PATH" sh "$power" "$@"; }
setup
prun reboot --delay; check 'grep -q "systemd-run .*--on-active=3 .*systemctl reboot" "$log"' "delayed reboot not scheduled"
prun poweroff; check 'grep -qx "systemctl poweroff" "$log"' "poweroff not run"
sleep 30 &
victim=$!
prun kill "$victim"; check '! kill -0 $victim 2>/dev/null' "kill did not end the process"
wait $victim 2>/dev/null
for args in "" "reboot now" "kill" "kill 1" "kill 0" "kill -9" "kill 12x" "kill 007" "kill 5 6" "halt"; do
    : > "$log"
    # shellcheck disable=SC2086
    prun $args 2>/dev/null; check '[ $? -eq 2 ]' "portal-power accepted: $args"
    check '[ ! -s "$log" ]' "portal-power ran something for: $args"
done

echo "developer helpers: $checks checks, $failures failed"
[ $failures -eq 0 ]
