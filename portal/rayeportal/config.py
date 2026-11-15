# SPDX-License-Identifier: GPL-3.0-or-later
# Where the portal keeps its things, and the parts of it that Settings
# changes: the sign-in, pairing codes, paired browsers, the connection log.
import hashlib
import hmac
import json
import threading
import os
import secrets
import time

PORT = 50443
SERVICE = "raye-device-portal"
PAIR_CODE_LIFETIME = 10 * 60
PAIRED_LIFETIME = 30 * 24 * 3600
LOG_KEEP = 500

DEFAULTS = {
    "user": "",
    "password": "",
    "allow_screenshot": False,
    "discovery": False,
}


def config_dir():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, SERVICE)


def data_dir():
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, SERVICE)


def _ensure(path):
    os.makedirs(path, mode=0o700, exist_ok=True)
    return path


def _read_json(path, default):
    try:
        with open(path, encoding="utf-8") as f:
            value = json.load(f)
        return value if isinstance(value, type(default)) else default
    except (OSError, ValueError):
        return default


def _write_json(path, value):
    _ensure(os.path.dirname(path))
    tmp = f"{path}.{os.getpid()}.tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(value, f, indent=1)
    os.replace(tmp, path)


def load():
    config = dict(DEFAULTS)
    config.update(_read_json(os.path.join(config_dir(), "config.json"), {}))
    return config


def save(config):
    _write_json(os.path.join(config_dir(), "config.json"), config)


# scrypt: slow enough that a stolen file is no shortcut
def hash_password(password, salt=None):
    salt = salt or secrets.token_bytes(16)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=2**14, r=8, p=1, dklen=32)
    return f"scrypt$16384$8$1${salt.hex()}${digest.hex()}"


def check_password(password, stored):
    try:
        kind, n, r, p, salt, digest = stored.split("$")
        if kind != "scrypt":
            return False
        mine = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt),
                              n=int(n), r=int(r), p=int(p), dklen=32)
    except (ValueError, TypeError):
        return False
    return hmac.compare_digest(mine.hex(), digest)


def set_login(user, password):
    user = user.strip()
    if not user or len(user) > 64 or any(c in user for c in "\r\n\t"):
        raise ValueError("bad user name")
    if len(password) < 8:
        raise ValueError("password too short")
    config = load()
    config["user"] = user
    config["password"] = hash_password(password)
    save(config)


def check_login(user, password):
    config = load()
    if not config["user"] or not config["password"]:
        # still pay for one hash, so timing says nothing
        check_password(password, hash_password("x"))
        return False
    good_user = hmac.compare_digest(user.encode(), config["user"].encode())
    good_password = check_password(password, config["password"])
    return good_user and good_password


def _digest(value):
    return hashlib.sha256(value.encode()).hexdigest()


# A pairing code is shown in Settings and typed in the browser. Six
# digits, ten minutes, and it is gone after five wrong guesses.
def new_pair_code():
    code = f"{secrets.randbelow(10**6):06d}"
    _write_json(os.path.join(data_dir(), "pair.json"),
                {"code": _digest(code), "expires": time.time() + PAIR_CODE_LIFETIME, "misses": 0})
    return code


_pair_lock = threading.Lock()


def _drop(path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass


def use_pair_code(code):
    path = os.path.join(data_dir(), "pair.json")
    with _pair_lock:
        pending = _read_json(path, {})
        if not pending or pending.get("expires", 0) < time.time() \
                or pending.get("expires", 0) > time.time() + PAIR_CODE_LIFETIME:
            return False
        if hmac.compare_digest(pending.get("code", ""), _digest(code.strip())):
            _drop(path)
            return True
        pending["misses"] = pending.get("misses", 0) + 1
        if pending["misses"] >= 5:
            _drop(path)
        else:
            _write_json(path, pending)
        return False


def add_paired(name, where=""):
    """A paired browser, tied to where it paired from (an address, or an
    IPv6 /64): its cookie copied to another machine is no use there."""
    token = secrets.token_urlsafe(32)
    path = os.path.join(data_dir(), "paired.json")
    paired = [p for p in _read_json(path, []) if p.get("expires", 0) > time.time()]
    paired.append({"token": _digest(token), "name": name[:80], "where": where,
                   "added": time.time(), "expires": time.time() + PAIRED_LIFETIME})
    _write_json(path, paired)
    return token


def _paired_entry(digest):
    for p in _read_json(os.path.join(data_dir(), "paired.json"), []):
        if time.time() < p.get("expires", 0) <= time.time() + PAIRED_LIFETIME \
                and hmac.compare_digest(p.get("token", ""), digest):
            return p
    return None


def check_paired(token, where=None):
    """The paired browser this cookie belongs to, or None. A pairing made
    before they were tied to a place takes the place it is first used from."""
    if not token:
        return None
    mine = _digest(token)
    with _pair_lock:
        p = _paired_entry(mine)
        if not p or where is None:
            return p
        if not p.get("where"):
            path = os.path.join(data_dir(), "paired.json")
            paired = _read_json(path, [])
            for entry in paired:
                if entry.get("token") == mine:
                    entry["where"] = where
            _write_json(path, paired)
            return dict(p, where=where)
        return p if p["where"] == where else None


def device_of(token):
    """What a session keeps to know its browser is still paired."""
    return _digest(token) if token else ""


def still_paired(device):
    return _paired_entry(device) is not None


def forget_device(token):
    if not token:
        return
    mine = _digest(token)
    path = os.path.join(data_dir(), "paired.json")
    with _pair_lock:
        paired = _read_json(path, [])
        kept = [p for p in paired if not hmac.compare_digest(p.get("token", ""), mine)]
        if len(kept) != len(paired):
            _write_json(path, kept)


def paired_devices():
    now = time.time()
    return [{"name": p.get("name", ""), "added": p.get("added", 0)}
            for p in _read_json(os.path.join(data_dir(), "paired.json"), []) if p.get("expires", 0) > now]


def forget_paired():
    try:
        os.unlink(os.path.join(data_dir(), "paired.json"))
    except FileNotFoundError:
        pass


def _plain(value, limit):
    return "".join(c if c.isprintable() else " " for c in str(value))[:limit]


def log(event, address="", detail=""):
    path = os.path.join(_ensure(data_dir()), "log.jsonl")
    line = json.dumps({"time": int(time.time()), "event": event, "address": _plain(address, 64),
                       "detail": _plain(detail, 200)})
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    with os.fdopen(fd, "a", encoding="utf-8") as f:
        f.write(line + "\n")
    try:
        if os.path.getsize(path) > 256 * 1024:
            entries = read_log(LOG_KEEP)
            with open(path, "w", encoding="utf-8") as f:
                f.writelines(json.dumps(e) + "\n" for e in reversed(entries))
    except OSError:
        pass


def read_log(count=50):
    try:
        with open(os.path.join(data_dir(), "log.jsonl"), encoding="utf-8") as f:
            lines = f.readlines()
    except OSError:
        return []
    out = []
    for line in reversed(lines[-count:]):
        try:
            out.append(json.loads(line))
        except ValueError:
            continue
    return out
