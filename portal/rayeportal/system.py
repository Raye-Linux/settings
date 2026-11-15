# SPDX-License-Identifier: GPL-3.0-or-later
# What the pages show about this PC, read from /proc and /sys, and the
# few things they can do to it.
import configparser
import glob
import json
import os
import pwd
import signal
import socket
import subprocess
import threading
import time

from . import setting

HERE = os.path.dirname(os.path.abspath(__file__))
LIBEXEC = setting("RAYE_PORTAL_LIBEXEC", "/usr/libexec/raye-settings")
PROC = setting("RAYE_PORTAL_PROC", "/proc")
CLOCK = os.sysconf("SC_CLK_TCK")
PAGE = os.sysconf("SC_PAGE_SIZE")


def _read(path, default=""):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return default


def raye_version():
    release = {}
    for line in _read("/etc/os-release").splitlines():
        key, _, value = line.partition("=")
        release[key] = value.strip().strip('"')
    return release.get("PRETTY_NAME") or release.get("NAME", "Raye")


def uptime():
    try:
        return int(float(_read(f"{PROC}/uptime", "0").split()[0]))
    except (ValueError, IndexError):
        return 0


def memory():
    info = {}
    for line in _read(f"{PROC}/meminfo").splitlines():
        key, _, value = line.partition(":")
        try:
            info[key] = int(value.split()[0]) * 1024
        except (ValueError, IndexError):
            continue
    total = info.get("MemTotal", 0)
    return {"total": total, "used": total - info.get("MemAvailable", total)}


def disks():
    seen = set()
    out = []
    for line in _read(f"{PROC}/mounts").splitlines():
        parts = line.split()
        if len(parts) < 3 or not parts[0].startswith("/dev/") or parts[0] in seen:
            continue
        if parts[2] in ("squashfs", "iso9660") or parts[1].startswith("/snap"):
            continue
        seen.add(parts[0])
        try:
            st = os.statvfs(parts[1].replace("\\040", " "))
        except OSError:
            continue
        total = st.f_blocks * st.f_frsize
        if total == 0:
            continue
        out.append({"mount": parts[1].replace("\\040", " "), "device": parts[0],
                    "total": total, "used": total - st.f_bavail * st.f_frsize})
    return out


CPU_SENSORS = ("k10temp", "coretemp", "zenpower", "cpu_thermal")


def temperature():
    """The processor's temperature when a sensor says which it is, the
    warmest sensor otherwise."""
    cpu, other = [], []
    for hwmon in glob.glob("/sys/class/hwmon/hwmon*"):
        name = _read(f"{hwmon}/name").strip()
        for sensor in glob.glob(f"{hwmon}/temp*_input"):
            try:
                value = int(_read(sensor, "0")) / 1000
            except ValueError:
                continue
            if 0 < value < 150:
                (cpu if name in CPU_SENSORS else other).append(value)
    if not cpu and not other:
        for zone in glob.glob("/sys/class/thermal/thermal_zone*/temp"):
            try:
                value = int(_read(zone, "0")) / 1000
            except ValueError:
                continue
            if 0 < value < 150:
                other.append(value)
    values = cpu or other
    return round(max(values), 1) if values else None


def interfaces():
    try:
        links = json.loads(subprocess.run(["ip", "-j", "addr"], capture_output=True, text=True,
                                          timeout=5).stdout or "[]")
    except (OSError, ValueError, subprocess.SubprocessError):
        links = []
    out = []
    for link in links:
        if link.get("ifname") == "lo":
            continue
        out.append({"name": link.get("ifname", ""), "state": link.get("operstate", ""),
                    "mac": link.get("address", ""),
                    "addresses": [f"{a['local']}/{a['prefixlen']}" for a in link.get("addr_info", [])
                                  if a.get("scope") != "host" and "local" in a]})
    return out


class Sampler:
    """CPU, network and per-process use are rates: each call compares
    with the call before it."""

    def __init__(self):
        self._lock = threading.Lock()
        self._cpu = None
        self._net = None
        self._procs = {}
        self._procs_total = None

    @staticmethod
    def _cpu_times():
        first = _read(f"{PROC}/stat").splitlines()[:1]
        if not first:
            return 0, 0
        values = [int(v) for v in first[0].split()[1:]]
        idle = values[3] + (values[4] if len(values) > 4 else 0)
        return sum(values), idle

    @staticmethod
    def _net_bytes():
        rx = tx = 0
        for line in _read(f"{PROC}/net/dev").splitlines()[2:]:
            name, _, rest = line.partition(":")
            if name.strip() == "lo":
                continue
            fields = rest.split()
            if len(fields) >= 9:
                rx += int(fields[0])
                tx += int(fields[8])
        return rx, tx

    def stats(self):
        now = time.monotonic()
        total, idle = self._cpu_times()
        rx, tx = self._net_bytes()
        with self._lock:
            cpu = 0.0
            if self._cpu and total > self._cpu[0]:
                cpu = 100.0 * (1 - (idle - self._cpu[1]) / (total - self._cpu[0]))
            down = up = 0.0
            if self._net and now > self._net[0]:
                span = now - self._net[0]
                down = max(0, rx - self._net[1]) / span
                up = max(0, tx - self._net[2]) / span
            self._cpu = (total, idle)
            self._net = (now, rx, tx)
        return {"cpu": round(max(0.0, cpu), 1), "memory": memory(), "disks": disks(),
                "network": {"down": int(down), "up": int(up)}, "temperature": temperature(),
                "uptime": uptime(), "cores": os.cpu_count() or 1}

    def processes(self):
        total, _ = self._cpu_times()
        rows = []
        times = {}
        names = {}
        unmapped = _unmapped_uid()
        for entry in os.listdir(PROC):
            if not entry.isdigit():
                continue
            pid = int(entry)
            stat = _read(f"{PROC}/{pid}/stat")
            if not stat:
                continue
            try:
                name = stat[stat.index("(") + 1:stat.rindex(")")]
                fields = stat[stat.rindex(")") + 2:].split()
                used = int(fields[11]) + int(fields[12])
                rss = int(fields[21]) * PAGE
                state = fields[0]
                parent = int(fields[1])
            except (ValueError, IndexError):
                continue
            try:
                uid = os.stat(f"{PROC}/{pid}").st_uid
            except OSError:
                continue
            if uid == unmapped:
                names[uid] = None
            elif uid not in names:
                try:
                    names[uid] = pwd.getpwuid(uid).pw_name
                except KeyError:
                    names[uid] = str(uid)
            times[pid] = used
            command = _read(f"{PROC}/{pid}/cmdline").replace("\0", " ").strip()
            rows.append({"pid": pid, "name": name, "user": names[uid], "uid": uid, "memory": rss,
                         "state": state, "command": command[:300],
                         "endable": pid > 2 and parent != 2 and pid != os.getpid()})
        with self._lock:
            span = (total - self._procs_total) if self._procs_total else 0
            cores = os.cpu_count() or 1
            for row in rows:
                before = self._procs.get(row["pid"])
                row["cpu"] = round(100.0 * cores * (times[row["pid"]] - before) / span, 1) \
                    if span > 0 and before is not None else 0.0
            self._procs = times
            self._procs_total = total
        rows.sort(key=lambda r: (-r["cpu"], -r["memory"]))
        return rows


def _unmapped_uid():
    """Inside the service's sandbox, root and the other accounts all show
    as the overflow uid: None when there is no such sandbox."""
    first = _read(f"{PROC}/self/uid_map").split()
    if first[:3] == ["0", "0", "4294967295"]:
        return None
    try:
        return int(_read(f"{PROC}/sys/kernel/overflowuid", "65534").strip())
    except ValueError:
        return 65534


def _parent(pid):
    stat = _read(f"{PROC}/{pid}/stat")
    try:
        return int(stat[stat.rindex(")") + 2:].split()[1])
    except (ValueError, IndexError):
        return None


def endable(pid):
    """Not PID 1, not the kernel's own threads."""
    if pid <= 2:
        return False
    parent = _parent(pid)
    return parent is not None and parent != 2


def _desktop_dirs():
    home = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    dirs = [home] + (os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share").split(":")
    for extra in ("/var/lib/flatpak/exports/share", os.path.expanduser("~/.local/share/flatpak/exports/share")):
        if extra not in dirs:
            dirs.append(extra)
    return [os.path.join(d, "applications") for d in dirs if d]


def apps(language="en"):
    seen = set()
    out = []
    for folder in _desktop_dirs():
        for path in sorted(glob.glob(os.path.join(folder, "**", "*.desktop"), recursive=True)):
            app_id = os.path.relpath(path, folder).replace("/", "-")
            if app_id in seen:
                continue
            seen.add(app_id)
            parser = configparser.RawConfigParser(strict=False, interpolation=None)
            parser.optionxform = str
            try:
                parser.read(path, encoding="utf-8")
                entry = parser["Desktop Entry"]
            except (configparser.Error, KeyError, UnicodeDecodeError):
                continue
            if entry.get("Type") != "Application" or entry.get("NoDisplay", "").lower() == "true" \
                    or entry.get("Hidden", "").lower() == "true":
                continue
            name = entry.get(f"Name[{language}]") or entry.get("Name")
            if not name:
                continue
            kind = "flatpak" if "flatpak" in folder else "system"
            out.append({"id": app_id, "name": name, "comment": entry.get(f"Comment[{language}]") or
                        entry.get("Comment", ""), "kind": kind})
    out.sort(key=lambda a: a["name"].casefold())
    return out


PRIORITIES = {"0": "emerg", "1": "alert", "2": "crit", "3": "err", "4": "warning",
              "5": "notice", "6": "info", "7": "debug"}


def logs(lines=200, priority="", unit="", search=""):
    argv = ["journalctl", "-o", "json", "--no-pager", "-n", str(max(1, min(int(lines), 1000)))]
    if priority in PRIORITIES:
        argv += ["-p", priority]
    if unit:
        argv += ["-u", unit]
    if search:
        argv += ["--case-sensitive=false", "-g", search]
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=15).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    rows = []
    for line in out.splitlines():
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        message = entry.get("MESSAGE", "")
        if isinstance(message, list):
            message = bytes(message).decode("utf-8", "replace")
        rows.append({"time": int(entry.get("__REALTIME_TIMESTAMP", "0")) // 1000000,
                     "priority": entry.get("PRIORITY", "6"),
                     "source": entry.get("_SYSTEMD_UNIT") or entry.get("SYSLOG_IDENTIFIER") or entry.get("_COMM", ""),
                     "message": str(message)[:2000]})
    rows.reverse()
    return rows


def end_own(pid):
    """End a process of this user: ask, then insist after two seconds."""
    os.kill(pid, signal.SIGTERM)
    for _ in range(20):
        time.sleep(0.1)
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    return True


def owner_of(pid):
    try:
        return os.stat(f"{PROC}/{pid}").st_uid
    except OSError:
        return None


def notify(summary, body):
    subprocess.run(["gdbus", "call", "--session", "--dest", "org.freedesktop.Notifications",
                    "--object-path", "/org/freedesktop/Notifications",
                    "--method", "org.freedesktop.Notifications.Notify",
                    "Raye Device Portal", "0", "computer", summary, body, "[]",
                    "{'urgency': <byte 2>}", "8000"],
                   capture_output=True, timeout=5, check=False)


def screenshot(path):
    """A picture of the whole desktop, taken without a window."""
    result = subprocess.run(["spectacle", "--background", "--nonotify", "--fullscreen", "--output", path],
                            capture_output=True, timeout=30, check=False)
    return result.returncode == 0 and os.path.getsize(path) > 0 if os.path.exists(path) else False


def hostname():
    return socket.gethostname()
