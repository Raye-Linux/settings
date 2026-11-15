# SPDX-License-Identifier: GPL-3.0-or-later
# The web side: HTTPS on the local network, a sign-in or a pairing code,
# then pages that read the JSON routes below. Every answer carries the
# same strict headers; every change needs the session's CSRF token.
import html
import ipaddress
import http.cookies
import json
import os
import secrets
import shutil
import socket
import ssl
import tempfile
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import config, elevate, files, security, setting, system, text

STATIC = setting("RAYE_PORTAL_STATIC", os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "static"))
LOGOS = ["/usr/share/icons/hicolor/128x128/apps/raye-eye.png", "/usr/share/pixmaps/raye-eye.png"]
SESSION = "__Host-raye-session"
DEVICE = "__Host-raye-device"
PRELOGIN = "__Host-raye-login"
MAX_FORM = 8 * 1024
# A connection must finish its TLS handshake, then each request line and
# headers, in this long; a few per machine at a time, and a cap for all.
HANDSHAKE_TIME = 10
HEADERS_TIME = 15
CONNECTIONS_EACH = 8
CONNECTIONS_ALL = 40
NOISE_EVERY = 3600
TYPES = {".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".png": "image/png"}

HEADERS = {
    "Content-Security-Policy": "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' blob:; "
                               "connect-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
    # not no-referrer: with it the browser sends "Origin: null" on our own forms
    "Referrer-Policy": "same-origin",
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Resource-Policy": "same-origin",
    "Permissions-Policy": "camera=(), microphone=(), geolocation=(), interest-cohort=()",
    "Cache-Control": "no-store",
}


def logo_tag(size):
    if not any(os.path.isfile(logo) for logo in LOGOS):
        return ""
    return f'<img class="logo" src="/static/logo.png" alt="" width="{size}" height="{size}">'


class Portal:
    """What every request shares."""

    def __init__(self, home=None, neighbourhood=None, throttle=None, sessions=None):
        self.home = home or os.path.expanduser("~")
        self.neighbourhood = neighbourhood or security.Neighbourhood()
        self.throttle = throttle or security.Throttle()
        self.sessions = sessions or security.Sessions()
        self.sampler = system.Sampler()
        self.last_request = time.monotonic()
        self.screenshot_lock = threading.Lock()
        # the portal's keys, sign-in and pairings: out of reach of the Files page
        self.closed = [config.config_dir(), config.data_dir()]
        self._noise = {}
        self._noise_lock = threading.Lock()

    def log_noise(self, event, address):
        """Refusals anyone can cause: one line per machine and hour, so a
        flood of them cannot push the real sign-ins out of the log."""
        key = (event, security.place(address))
        now = time.monotonic()
        with self._noise_lock:
            if now - self._noise.get(key, -NOISE_EVERY) < NOISE_EVERY:
                return
            if len(self._noise) > 1000:
                self._noise = {k: t for k, t in self._noise.items() if now - t < NOISE_EVERY}
            self._noise[key] = now
        config.log(event, address)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def version_string(self):
        return "Raye"
    portal: Portal = None

    # quiet: the portal keeps its own log of what matters
    def log_message(self, *args):
        pass

    # ---- plumbing -------------------------------------------------------

    @property
    def address(self):
        return str(security.normalize(self.client_address[0]))

    def cookies(self):
        jar = http.cookies.SimpleCookie()
        try:
            jar.load(self.headers.get("Cookie", ""))
        except http.cookies.CookieError:
            return {}
        return {k: m.value for k, m in jar.items()}

    def lang(self):
        return text.language(self.headers.get("Accept-Language"))

    def t(self, key, **kw):
        return text.TEXT[self.lang()][key].format(**kw)

    def send(self, status, body=b"", kind="text/html; charset=utf-8", headers=None, cookies=()):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        for key, value in HEADERS.items():
            self.send_header(key, value)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        for cookie in cookies:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_error(self, code, message=None, explain=None):
        # http.server's own answers (400, 414, 501...) get the same headers
        self.close_connection = True
        reason = message or self.responses.get(code, ("Error",))[0]
        try:
            self.send(code, f"{code} {reason}\n", "text/plain; charset=utf-8", {"Connection": "close"})
        except OSError:
            pass

    def handle_one_request(self):
        # slow headers, a byte now and then, would hold a thread for good
        self._deadline = threading.Timer(HEADERS_TIME, self._cut)
        self._deadline.daemon = True
        self._deadline.start()
        try:
            super().handle_one_request()
        finally:
            self._deadline.cancel()

    def parse_request(self):
        parsed = super().parse_request()
        self._deadline.cancel()
        return parsed

    def _cut(self):
        try:
            self.connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

    def good_host(self):
        """Only this PC's names and plain addresses: a page that points
        its own name at this PC (DNS rebinding) is turned away."""
        host = (self.headers.get("Host") or "").strip().lower()
        if host.startswith("["):
            name, _, rest = host[1:].partition("]")
            port = rest[1:] if rest.startswith(":") else ("" if not rest else None)
        else:
            name, _, port = host.partition(":")
        if port is None or (port and port != str(self.server.server_address[1])):
            return False
        name = name.rstrip(".")
        try:
            ipaddress.ip_address(name)
            return True
        except ValueError:
            pass
        me = system.hostname().lower()
        return bool(name) and name in (me, f"{me}.local", "localhost")

    def json(self, value, status=200):
        self.send(status, json.dumps(value), "application/json")

    def redirect(self, where, cookies=()):
        self.send(303, b"", headers={"Location": where}, cookies=cookies)

    @staticmethod
    def cookie(name, value, max_age=None):
        parts = [f"{name}={value}", "Path=/", "Secure", "HttpOnly", "SameSite=Strict"]
        if max_age is not None:
            parts.append(f"Max-Age={max_age}")
        return "; ".join(parts)

    def body(self, limit):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if length < 0 or length > limit:
            return None
        return self.rfile.read(length)

    def form(self):
        raw = self.body(MAX_FORM)
        if raw is None:
            return None
        return {k: v[0] for k, v in urllib.parse.parse_qs(raw.decode("utf-8", "replace")).items()}

    def same_origin(self):
        """A change must come from a page of the portal itself."""
        site = self.headers.get("Sec-Fetch-Site")
        if site and site not in ("same-origin", "none"):
            return False
        origin = self.headers.get("Origin")
        if origin and origin != f"https://{self.headers.get('Host', '')}":
            return False
        return True

    def session(self):
        session = self.portal.sessions.get(self.cookies().get(SESSION), self.address)
        # a browser forgotten in Settings loses the sessions it has open
        if session and session.get("device") and not config.still_paired(session["device"]):
            self.portal.sessions.end(session["token"])
            return None
        return session

    # ---- routing --------------------------------------------------------

    def do_GET(self):
        self.route("GET")

    def do_HEAD(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    query = {}

    def route(self, method):
        self.portal.last_request = time.monotonic()
        url = urllib.parse.urlsplit(self.path)
        self.query = {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
        path = url.path
        if not self.good_host():
            self.close_connection = True
            return self.send(421, self.t("error"), "text/plain; charset=utf-8", {"Connection": "close"})
        try:
            if path.startswith("/static/") and method == "GET":
                return self.static(path[len("/static/"):])
            if path == "/login":
                return self.login_page() if method == "GET" else self.login()
            if path == "/pair" and method == "POST":
                return self.pair()
            session = self.session()
            if path.startswith("/api/"):
                if not session:
                    return self.json({"error": "signed-out"}, 401)
                if method == "POST" and not (self.same_origin() and security.same_token(
                        self.headers.get("X-CSRF-Token"), session["csrf"])):
                    return self.json({"error": "csrf"}, 403)
                handler = getattr(self, f"api_{method.lower()}_{path[5:].replace('/', '_')}", None)
                return handler(session) if handler else self.json({"error": "not-found"}, 404)
            if not session:
                return self.redirect("/login")
            if path == "/logout" and method == "POST":
                return self.logout(session)
            page = path.strip("/") or "overview"
            if method == "GET" and page in text.PAGES:
                return self.send(200, self.shell(page, session))
            return self.send(404, self.t("error"), "text/plain; charset=utf-8")
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError, socket.timeout):
            raise
        except Exception:  # one bad request must not take the portal down
            # a signed-out visitor does not get to write into the log
            config.log("error", self.address, path if self.session() else "")
            try:
                self.json({"error": "failed"}, 500)
            except OSError:
                pass

    def static(self, name):
        if name == "logo.png":
            for logo in LOGOS:
                if os.path.isfile(logo):
                    with open(logo, "rb") as f:
                        return self.send(200, f.read(), "image/png", {"Cache-Control": "max-age=86400"})
            return self.send(404, b"", "text/plain")
        if "/" in name or name.startswith(".") or os.path.splitext(name)[1] not in TYPES:
            return self.send(404, b"", "text/plain")
        try:
            with open(os.path.join(STATIC, name), "rb") as f:
                data = f.read()
        except OSError:
            return self.send(404, b"", "text/plain")
        return self.send(200, data, TYPES[os.path.splitext(name)[1]])

    # ---- signing in -----------------------------------------------------

    def login_page(self, error="", status=200, user=""):
        jar = self.cookies()
        paired = config.check_paired(jar.get(DEVICE), security.place(self.address))
        if paired and not error:
            token = self.portal.sessions.create(self.address, "paired", config.device_of(jar.get(DEVICE)))
            config.log("paired-sign-in", self.address, paired.get("name", ""))
            return self.redirect("/", [self.cookie(SESSION, token)])
        if self.session() and not error:
            return self.redirect("/")
        csrf = jar.get(PRELOGIN) or secrets.token_urlsafe(24)
        has_login = bool(config.load()["user"])
        if not error and self.query.get("expired"):
            error = self.t("expired")
        t = text.TEXT[self.lang()]
        e = html.escape
        body = f"""<!doctype html>
<html lang="{self.lang()}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{e(t['title'])}</title><link rel="stylesheet" href="/static/portal.css"></head>
<body class="sign-in"><main class="sign-in-card">
{logo_tag(56)}
<h1>{e(t['sign_in_to'].format(host=system.hostname()))}</h1>
<p class="hint">{e(t['sign_in_hint'] if has_login else t['no_login'])}</p>
{f'<p class="error" role="alert">{e(error)}</p>' if error else ''}
<form method="post" action="/login" class="stack">
<input type="hidden" name="csrf" value="{e(csrf)}">
<label>{e(t['user'])}<input name="user" autocomplete="username" required maxlength="64" value="{e(user)}"></label>
<label>{e(t['password'])}<input name="password" type="password" autocomplete="current-password" required maxlength="256"></label>
<button class="accent" type="submit">{e(t['sign_in'])}</button>
</form>
<details class="pair"><summary>{e(t['pair_title'])}</summary>
<p class="hint">{e(t['pair_hint'])}</p>
<form method="post" action="/pair" class="stack">
<input type="hidden" name="csrf" value="{e(csrf)}">
<label>{e(t['code'])}<input name="code" inputmode="numeric" autocomplete="one-time-code" required maxlength="6" pattern="[0-9]{{6}}"></label>
<button type="submit">{e(t['pair'])}</button>
</form></details>
</main></body></html>"""
        return self.send(status, body, cookies=[self.cookie(PRELOGIN, csrf)])

    def _checked_form(self):
        form = self.form()
        if form is None or not self.same_origin():
            return None
        if not security.same_token(form.get("csrf"), self.cookies().get(PRELOGIN)):
            return None
        return form

    def login(self):
        form = self._checked_form()
        if form is None:
            return self.send(403, self.t("error"), "text/plain; charset=utf-8")
        if not self.portal.throttle.attempt(self.address):
            self.portal.log_noise("locked", self.address)
            return self.login_page(self.t("locked"), 429)
        user = form.get("user", "")
        if config.check_login(user, form.get("password", "")):
            self.portal.throttle.clear(self.address)
            token = self.portal.sessions.create(self.address, "password")
            config.log("sign-in", self.address, user)
            return self.redirect("/", [self.cookie(SESSION, token)])
        locked = self.portal.throttle.blocked(self.address)
        config.log("sign-in-failed", self.address, user)
        return self.login_page(self.t("locked" if locked else "bad_login"), 429 if locked else 401, user)

    def pair(self):
        form = self._checked_form()
        if form is None:
            return self.send(403, self.t("error"), "text/plain; charset=utf-8")
        if not self.portal.throttle.attempt(self.address):
            self.portal.log_noise("locked", self.address)
            return self.login_page(self.t("locked"), 429)
        if config.use_pair_code(form.get("code", "")):
            self.portal.throttle.clear(self.address)
            name = (self.headers.get("User-Agent") or self.address)[:80]
            device = config.add_paired(name, security.place(self.address))
            token = self.portal.sessions.create(self.address, "paired", config.device_of(device))
            config.log("paired", self.address, name)
            return self.redirect("/", [self.cookie(SESSION, token),
                                       self.cookie(DEVICE, device, config.PAIRED_LIFETIME)])
        locked = self.portal.throttle.blocked(self.address)
        config.log("pair-failed", self.address)
        return self.login_page(self.t("locked" if locked else "bad_code"), 429 if locked else 401)

    def logout(self, session):
        form = self.form() or {}
        if not (self.same_origin() and security.same_token(form.get("csrf"), session["csrf"])):
            return self.send(403, self.t("error"), "text/plain; charset=utf-8")
        self.portal.sessions.end(session["token"])
        # signing out also forgets the pairing of this browser, here too
        config.forget_device(self.cookies().get(DEVICE))
        config.log("sign-out", self.address)
        return self.redirect("/login", [self.cookie(SESSION, "", 0), self.cookie(DEVICE, "", 0)])

    # ---- pages ----------------------------------------------------------

    def shell(self, page, session):
        lang = self.lang()
        t = text.TEXT[lang]
        e = html.escape
        nav = "".join(f'<a href="/{p if p != "overview" else ""}" class="{"current" if p == page else ""}" '
                      f'data-icon="{p}">{e(t[p])}</a>' for p in text.PAGES)
        strings = json.dumps(t).replace("<", "\\u003c")
        return f"""<!doctype html>
<html lang="{lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="csrf" content="{e(session['csrf'])}">
<title>{e(t[page])} - {e(system.hostname())}</title><link rel="stylesheet" href="/static/portal.css"></head>
<body data-page="{page}" data-lang="{lang}">
<script type="application/json" id="strings">{strings}</script>
<header>{logo_tag(28)}
<span class="host">{e(system.hostname())}</span><span class="product">{e(t['title'])}</span>
<form method="post" action="/logout"><input type="hidden" name="csrf" value="{e(session['csrf'])}">
<button type="submit" class="subtle">{e(t['sign_out'])}</button></form></header>
<div class="frame"><nav>{nav}</nav><main id="main" aria-live="polite"><h1>{e(t[page])}</h1><div id="content"></div></main></div>
<script src="/static/portal.js"></script>
</body></html>"""

    # ---- JSON routes ----------------------------------------------------

    def api_get_overview(self, session):
        stats = self.portal.sampler.stats()
        self.json({"host": system.hostname(), "version": system.raye_version(), **stats})

    def api_get_stats(self, session):
        self.json(self.portal.sampler.stats())

    def api_get_processes(self, session):
        self.json({"me": os.getuid(), "processes": self.portal.sampler.processes()})

    def api_get_apps(self, session):
        self.json(system.apps(self.lang()))

    def api_get_network(self, session):
        self.json({"interfaces": system.interfaces(), **self.portal.sampler.stats()["network"]})

    def api_get_logs(self, session):
        q = self.query
        try:
            lines = int(q.get("lines", "200"))
        except ValueError:
            lines = 200
        self.json(system.logs(lines, q.get("priority", ""), q.get("unit", "")[:100], q.get("q", "")[:200]))

    def api_get_files(self, session):
        try:
            self.json(files.listing(self.portal.home, self.query.get("path", ""), self.portal.closed))
        except files.OutsideHome:
            config.log("refused-path", self.address, self.query.get("path", ""))
            self.json({"error": "outside"}, 403)
        except (FileNotFoundError, NotADirectoryError):
            self.json({"error": "missing"}, 404)
        except PermissionError:
            self.json({"error": "denied"}, 403)

    def api_get_download(self, session):
        try:
            path = files.resolve(self.portal.home, self.query.get("path", ""), self.portal.closed)
            handle = open(path, "rb")
        except files.OutsideHome:
            config.log("refused-path", self.address, self.query.get("path", ""))
            return self.json({"error": "outside"}, 403)
        except (IsADirectoryError, FileNotFoundError, PermissionError):
            return self.json({"error": "missing"}, 404)
        with handle:
            size = os.fstat(handle.fileno()).st_size
            name = os.path.basename(path)
            self.send_response(200)
            for key, value in HEADERS.items():
                self.send_header(key, value)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.send_header("Content-Disposition", "attachment; filename*=UTF-8''" + urllib.parse.quote(name))
            self.end_headers()
            if self.command != "HEAD":
                shutil.copyfileobj(handle, self.wfile, 1024 * 1024)
        config.log("download", self.address, files.relative_to(self.portal.home, path))

    def api_post_upload(self, session):
        try:
            folder = files.upload_folder(self.portal.home, self.query.get("path", ""), self.portal.closed)
            name = files.upload_name(self.query.get("name", ""))
            length = int(self.headers.get("Content-Length", "-1"))
        except files.OutsideHome:
            config.log("refused-path", self.address, self.query.get("path", ""))
            return self.json({"error": "outside"}, 403)
        except ValueError:
            return self.json({"error": "bad-name"}, 400)
        if length < 0 or length > files.MAX_UPLOAD or not os.path.isdir(folder):
            return self.json({"error": "bad-size"}, 400)
        try:
            fd, path = files.open_new(folder, name)
        except OSError:
            return self.json({"error": "denied"}, 403)
        left = length
        try:
            with os.fdopen(fd, "wb") as out:
                while left > 0:
                    chunk = self.rfile.read(min(left, 1024 * 1024))
                    if not chunk:
                        raise ConnectionResetError
                    out.write(chunk)
                    left -= len(chunk)
        except BaseException:
            os.unlink(path)
            raise
        config.log("upload", self.address, files.relative_to(self.portal.home, path))
        self.json({"name": os.path.basename(path)})

    def _password(self):
        raw = self.body(MAX_FORM)
        try:
            data = json.loads(raw or b"{}")
        except ValueError:
            return None
        return data if isinstance(data, dict) else None

    def _elevated(self, args, password):
        if not self.portal.throttle.attempt(self.address):
            return "locked", "too many tries"
        result, why = elevate.run(args, password)
        if result == "ok":
            self.portal.throttle.clear(self.address)
        elif result == "unavailable":
            # the password was never checked: not a try
            self.portal.throttle.forgive(self.address)
        return result, why

    def api_post_kill(self, session):
        data = self._password()
        try:
            pid = int((data or {}).get("pid"))
        except (TypeError, ValueError):
            return self.json({"error": "bad-pid"}, 400)
        owner = system.owner_of(pid)
        if not system.endable(pid) or pid == os.getpid() or owner is None:
            return self.json({"error": "bad-pid"}, 400)
        if owner == os.getuid():
            system.end_own(pid)
            config.log("end-task", self.address, str(pid))
            return self.json({"result": "ok"})
        password = data.get("password") or ""
        if not password:
            return self.json({"result": "password"})
        result, why = self._elevated(["kill", pid], password)
        config.log(f"end-task-{result}", self.address, f"{pid} {why}".strip())
        return self.json({"result": result}, 200 if result == "ok" else 403)

    def api_post_power(self, session):
        data = self._password() or {}
        action = data.get("action")
        if action not in ("reboot", "poweroff"):
            return self.json({"error": "bad-action"}, 400)
        result, why = self._elevated([action, "--delay"], data.get("password") or "")
        config.log(f"{action}-{result}", self.address, why)
        return self.json({"result": result}, 200 if result == "ok" else 403)

    def api_get_screen(self, session):
        self.json({"allowed": bool(config.load()["allow_screenshot"])})

    def api_post_screenshot(self, session):
        if not config.load()["allow_screenshot"]:
            return self.json({"error": "off"}, 403)
        if not self.portal.screenshot_lock.acquire(blocking=False):
            return self.json({"error": "busy"}, 429)
        try:
            system.notify(note_title(), text.TEXT[system_lang()]["screen_note_local"].format(
                address=self.address))
            with tempfile.TemporaryDirectory(prefix="raye-portal-") as tmp:
                path = os.path.join(tmp, "screen.png")
                if not system.screenshot(path):
                    return self.json({"error": "failed"}, 500)
                with open(path, "rb") as f:
                    data = f.read()
            config.log("screenshot", self.address)
            return self.send(200, data, "image/png")
        finally:
            self.portal.screenshot_lock.release()


def note_title():
    return text.TEXT[system_lang()]["title"]


def system_lang():
    for var in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
        value = os.environ.get(var, "")
        if value:
            return "fr" if value.lower().startswith("fr") else "en"
    return "en"


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 32

    def __init__(self, sock, context, portal):
        super().__init__(sock.getsockname()[:2], Handler, bind_and_activate=False)
        self.socket.close()
        self.socket = sock
        self.server_address = sock.getsockname()
        self.context = context
        self.portal = portal
        self.handler = type("BoundHandler", (Handler,), {"portal": portal})
        self._open = {}
        self._counted = {}
        self._open_lock = threading.Lock()

    def handle_error(self, request, client_address):
        # failed handshakes and dropped connections are not news
        pass

    def verify_request(self, request, client_address):
        if not self.portal.neighbourhood.allows(client_address[0]):
            self.portal.log_noise("refused-network", str(client_address[0]))
            return False
        where = security.place(client_address[0])
        with self._open_lock:
            if self._open.get(where, 0) >= CONNECTIONS_EACH or len(self._counted) >= CONNECTIONS_ALL:
                busy = True
            else:
                busy = False
                self._open[where] = self._open.get(where, 0) + 1
                self._counted[request] = where
        if busy:
            self.portal.log_noise("refused-busy", str(client_address[0]))
        return not busy

    def shutdown_request(self, request):
        with self._open_lock:
            where = self._counted.pop(request, None)
            if where is not None:
                self._open[where] -= 1
                if not self._open[where]:
                    del self._open[where]
        super().shutdown_request(request)

    def finish_request(self, request, client_address):
        # the TLS handshake runs here, in the request's own thread, so one
        # slow client cannot hold the door for the others
        request.settimeout(30)
        # wrap_socket() takes the descriptor away from request: shut a copy
        cut = threading.Timer(HANDSHAKE_TIME, _shut, (request.fileno(),))
        cut.daemon = True
        cut.start()
        try:
            tls = self.context.wrap_socket(request, server_side=True)
        finally:
            cut.cancel()
        with tls:
            self.handler(tls, client_address, self)


def _shut(fd):
    try:
        with socket.socket(fileno=os.dup(fd)) as sock:
            sock.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass


def tls_context(cert, key):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    return context


def listening_socket(port):
    """The socket systemd opened for us, or one of our own."""
    if os.environ.get("LISTEN_PID") == str(os.getpid()) and os.environ.get("LISTEN_FDS") == "1":
        return socket.socket(fileno=3)
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("::", port))
    sock.listen(32)
    return sock


def serve(port=config.PORT, idle_exit=30 * 60):
    cert, key = os.path.join(config.data_dir(), "cert.pem"), os.path.join(config.data_dir(), "key.pem")
    portal = Portal()
    server = Server(listening_socket(port), tls_context(cert, key), portal)

    def watch():
        # nobody for a while: stop, and systemd starts us again on the next
        # connection, so the portal costs no memory while it is not used
        while True:
            time.sleep(30)
            if time.monotonic() - portal.last_request > idle_exit and portal.sessions.count() == 0:
                server.shutdown()
                return

    if os.environ.get("LISTEN_FDS"):
        threading.Thread(target=watch, daemon=True).start()
    try:
        server.serve_forever()
    finally:
        server.server_close()
