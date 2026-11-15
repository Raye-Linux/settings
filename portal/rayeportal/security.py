# SPDX-License-Identifier: GPL-3.0-or-later
# Who may talk to the portal: only machines on one of this PC's own
# networks, a few wrong passwords at most, and sessions that end.
import ipaddress
import json
import os
import secrets
import subprocess
import threading
import time

from . import config

SESSION_IDLE = 30 * 60
SESSION_MAX = 8 * 3600
MAX_FAILURES = 5
# all addresses together: a PC can take as many IPv6 addresses as it likes
MAX_FAILURES_ALL = 20
FAILURE_WINDOW = 15 * 60


def normalize(address):
    ip = ipaddress.ip_address(address.split("%")[0])
    if ip.version == 6 and ip.ipv4_mapped:
        ip = ip.ipv4_mapped
    return ip


def local_networks():
    """The networks of this PC's interfaces, as ip_network objects."""
    try:
        out = subprocess.run(["ip", "-j", "addr"], capture_output=True, text=True, timeout=5).stdout
        links = json.loads(out or "[]")
    except (OSError, ValueError, subprocess.SubprocessError):
        return []
    nets = []
    for link in links:
        for addr in link.get("addr_info", []):
            if addr.get("scope") == "host":
                continue
            try:
                nets.append(ipaddress.ip_network(f"{addr['local']}/{addr['prefixlen']}", strict=False))
            except (KeyError, ValueError):
                continue
    return nets


def place(address):
    """One machine, as far as an address tells: IPv4 as is, IPv6 by its /64,
    since a PC takes new IPv6 addresses in it whenever it likes."""
    try:
        ip = normalize(address)
    except ValueError:
        return address
    if ip.version == 6:
        return str(ipaddress.ip_network(f"{ip}/64", strict=False))
    return str(ip)


class Neighbourhood:
    """Is an address on the local network? The answer follows the
    interfaces, re-read at most every 30 seconds."""

    def __init__(self, reader=local_networks):
        self._reader = reader
        self._nets = []
        self._read = 0.0
        self._lock = threading.Lock()

    def allows(self, address):
        try:
            ip = normalize(address)
        except ValueError:
            return False
        if ip.is_loopback:
            return True
        with self._lock:
            if time.monotonic() - self._read > 30:
                self._nets = self._reader()
                self._read = time.monotonic()
            nets = list(self._nets)
        return any(ip.version == net.version and ip in net for net in nets)


class Throttle:
    """Wrong passwords and codes, per address. Kept on disk, so a
    restart of the portal does not wipe the slate."""

    def __init__(self, path=None, limit=MAX_FAILURES, window=FAILURE_WINDOW, limit_all=MAX_FAILURES_ALL):
        self.path = path or os.path.join(config.data_dir(), "failures.json")
        self.limit = limit
        self.limit_all = limit_all
        self.window = window
        self._lock = threading.Lock()

    @staticmethod
    def key(address):
        return place(address)

    def _load(self):
        try:
            with open(self.path, encoding="utf-8") as f:
                data = json.load(f)
            return data if isinstance(data, dict) else {}
        except (OSError, ValueError):
            return {}

    def _recent(self, data, address):
        now = time.time()
        return [t for t in data.get(address, []) if now - t < self.window]

    def _fresh(self):
        data = self._load()
        data = {a: self._recent(data, a) for a in data}
        return {a: t for a, t in data.items() if t}

    def _full(self, data, key):
        return len(data.get(key, [])) >= self.limit or sum(map(len, data.values())) >= self.limit_all

    def blocked(self, address):
        with self._lock:
            return self._full(self._fresh(), self.key(address))

    def attempt(self, address):
        """Count a try before checking it, so tries sent side by side
        cannot all slip in before the first one fails. False: locked."""
        key = self.key(address)
        with self._lock:
            data = self._fresh()
            if self._full(data, key):
                return False
            data.setdefault(key, []).append(time.time())
            config._write_json(self.path, data)
            return True

    def forgive(self, address):
        """Take back the last try of attempt()."""
        key = self.key(address)
        with self._lock:
            data = self._fresh()
            if data.get(key):
                data[key].pop()
                config._write_json(self.path, data)

    def fail(self, address):
        key = self.key(address)
        with self._lock:
            data = self._fresh()
            data.setdefault(key, []).append(time.time())
            config._write_json(self.path, data)
            return self._full(data, key)

    def clear(self, address):
        with self._lock:
            data = self._load()
            if data.pop(self.key(address), None) is not None:
                config._write_json(self.path, data)


class Sessions:
    """Signed-in browsers. In memory only: the portal ends them when it
    stops."""

    def __init__(self, idle=SESSION_IDLE, longest=SESSION_MAX):
        self.idle = idle
        self.longest = longest
        self._sessions = {}
        self._lock = threading.Lock()

    def create(self, address, via, device=""):
        token = secrets.token_urlsafe(32)
        now = time.monotonic()
        with self._lock:
            self._sessions[token] = {"address": address, "via": via, "device": device,
                                     "csrf": secrets.token_urlsafe(32), "created": now, "seen": now}
        return token

    def get(self, token, address):
        if not token:
            return None
        now = time.monotonic()
        with self._lock:
            session = self._sessions.get(token)
            if not session:
                return None
            if (now - session["seen"] > self.idle or now - session["created"] > self.longest
                    or session["address"] != address):
                del self._sessions[token]
                return None
            session["seen"] = now
            return dict(session, token=token)

    def end(self, token):
        with self._lock:
            self._sessions.pop(token, None)

    def count(self):
        now = time.monotonic()
        with self._lock:
            return sum(1 for s in self._sessions.values() if now - s["seen"] <= self.idle)


def same_token(a, b):
    return bool(a) and bool(b) and secrets.compare_digest(a, b)
