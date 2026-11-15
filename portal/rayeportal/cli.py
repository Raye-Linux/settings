# SPDX-License-Identifier: GPL-3.0-or-later
# What Settings asks of the portal:
#
#   device-portal status            JSON: on or off, addresses, certificate, sign-in, log
#   device-portal enable | disable  start or stop listening (and the mDNS name)
#   device-portal set-login USER    the password comes on stdin
#   device-portal pair              a new pairing code, printed once
#   device-portal forget-devices    paired browsers must pair again
#   device-portal set KEY on|off    allow-screenshot, discovery
#   device-portal serve             the portal itself (systemd starts it)
#   device-portal elevate ARGS      a root action for the portal, outside its sandbox
import hashlib
import json
import os
import socket
import ssl
import subprocess
import sys

from . import config, setting, system

UNITS = ["raye-device-portal.socket"]
DISCOVERY = "raye-device-portal-discovery.service"


def systemctl(*args):
    command = setting("RAYE_PORTAL_SYSTEMCTL", "systemctl")
    return subprocess.run([command, "--user", *args], capture_output=True, text=True, timeout=30)


def cert_paths():
    return os.path.join(config.data_dir(), "cert.pem"), os.path.join(config.data_dir(), "key.pem")


def make_certificate():
    """A certificate of our own, made once; its fingerprint is what the
    person compares in the browser."""
    cert, key = cert_paths()
    if os.path.exists(cert) and os.path.exists(key):
        return
    os.makedirs(config.data_dir(), mode=0o700, exist_ok=True)
    host = socket.gethostname()
    old = os.umask(0o077)
    try:
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256",
                        "-nodes", "-days", "3650", "-subj", f"/CN={host}",
                        "-addext", f"subjectAltName=DNS:{host},DNS:{host}.local",
                        "-keyout", key, "-out", cert], check=True, capture_output=True, timeout=60)
    finally:
        os.umask(old)


def fingerprint():
    cert, _ = cert_paths()
    try:
        with open(cert, encoding="ascii") as f:
            der = ssl.PEM_cert_to_DER_cert(f.read())
    except (OSError, ValueError):
        return ""
    digest = hashlib.sha256(der).hexdigest().upper()
    return ":".join(digest[i:i + 2] for i in range(0, len(digest), 2))


def addresses():
    out = []
    for link in system.interfaces():
        for addr in link["addresses"]:
            ip = addr.split("/")[0]
            if ip.startswith("fe80:"):
                continue
            host = f"[{ip}]" if ":" in ip else ip
            out.append(f"https://{host}:{config.PORT}")
    out.sort(key=lambda a: "[" in a)
    return out


def enabled():
    return systemctl("is-enabled", UNITS[0]).stdout.strip() == "enabled"


def firewall_on():
    try:
        with open("/etc/ufw/ufw.conf", encoding="utf-8") as f:
            return any(line.strip().lower() == "enabled=yes" for line in f)
    except OSError:
        return False


def status():
    cfg = config.load()
    on = enabled()
    return {
        "enabled": on,
        "listening": on and systemctl("is-active", UNITS[0]).stdout.strip() == "active",
        "port": config.PORT,
        "addresses": addresses(),
        "hostname": socket.gethostname(),
        "fingerprint": fingerprint(),
        "user": cfg["user"],
        "allow_screenshot": bool(cfg["allow_screenshot"]),
        "discovery": bool(cfg["discovery"]),
        "paired": len(config.paired_devices()),
        "firewall": firewall_on(),
        "log": config.read_log(30),
    }


def enable():
    make_certificate()
    result = systemctl("enable", "--now", *UNITS)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        return 1
    if config.load()["discovery"]:
        systemctl("start", DISCOVERY)
    config.log("turned-on")
    return 0


def disable():
    systemctl("stop", DISCOVERY)
    result = systemctl("disable", "--now", *UNITS, "raye-device-portal.service")
    config.log("turned-off")
    return 0 if result.returncode == 0 else 1


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    command = argv.pop(0) if argv else "status"
    if command == "status":
        print(json.dumps(status()))
        return 0
    if command == "enable":
        return enable()
    if command == "disable":
        return disable()
    if command == "set-login" and len(argv) == 1:
        password = sys.stdin.readline().rstrip("\n")
        try:
            config.set_login(argv[0], password)
        except ValueError as error:
            sys.stderr.write(f"{error}\n")
            return 2
        config.log("sign-in-changed", "", argv[0].strip())
        return 0
    if command == "pair":
        print(config.new_pair_code())
        config.log("pair-code")
        return 0
    if command == "forget-devices":
        config.forget_paired()
        config.log("devices-forgotten")
        return 0
    if command == "set" and len(argv) == 2 and argv[0] in ("allow-screenshot", "discovery") \
            and argv[1] in ("on", "off"):
        cfg = config.load()
        cfg[argv[0].replace("-", "_")] = argv[1] == "on"
        config.save(cfg)
        if argv[0] == "discovery" and enabled():
            systemctl("start" if argv[1] == "on" else "stop", DISCOVERY)
        return 0
    if command == "elevate" and argv:
        from . import elevate
        return elevate.main(argv)
    if command == "serve":
        from . import web
        web.serve()
        return 0
    sys.stderr.write("usage: device-portal status|enable|disable|set-login USER|pair|forget-devices|"
                     "set allow-screenshot|discovery on|off|serve\n")
    return 2
