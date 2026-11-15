# SPDX-License-Identifier: GPL-3.0-or-later
# The portal's locks, tried for real: a server on 127.0.0.1 with its own
# certificate, a home folder and a config of its own.
import http.client
import json
import os
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from rayeportal import cli, config, elevate, files, security, web  # noqa: E402


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.env = {k: os.environ.get(k) for k in ("XDG_CONFIG_HOME", "XDG_DATA_HOME")}
        os.environ["XDG_CONFIG_HOME"] = os.path.join(self.tmp, "config")
        os.environ["XDG_DATA_HOME"] = os.path.join(self.tmp, "data")

    def tearDown(self):
        for key, value in self.env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        shutil.rmtree(self.tmp, ignore_errors=True)


class Passwords(Base):
    def test_hash_is_not_the_password(self):
        stored = config.hash_password("correct horse")
        self.assertNotIn("correct horse", stored)
        self.assertTrue(config.check_password("correct horse", stored))
        self.assertFalse(config.check_password("correct hors", stored))
        self.assertFalse(config.check_password("x", "plain"))

    def test_login(self):
        self.assertFalse(config.check_login("", ""))
        config.set_login("alex", "s3cret-pass")
        self.assertTrue(config.check_login("alex", "s3cret-pass"))
        self.assertFalse(config.check_login("alex", "s3cret-pas"))
        self.assertFalse(config.check_login("bob", "s3cret-pass"))
        saved = open(os.path.join(config.config_dir(), "config.json")).read()
        self.assertNotIn("s3cret-pass", saved)
        self.assertEqual(os.stat(os.path.join(config.config_dir(), "config.json")).st_mode & 0o777, 0o600)

    def test_short_password_refused(self):
        with self.assertRaises(ValueError):
            config.set_login("alex", "short")
        with self.assertRaises(ValueError):
            config.set_login("a\nb", "long enough")


class PairCodes(Base):
    def test_code_works_once(self):
        code = config.new_pair_code()
        self.assertRegex(code, r"^\d{6}$")
        self.assertTrue(config.use_pair_code(code))
        self.assertFalse(config.use_pair_code(code))

    def test_five_misses_burn_the_code(self):
        code = config.new_pair_code()
        wrong = "000000" if code != "000000" else "111111"
        for _ in range(5):
            self.assertFalse(config.use_pair_code(wrong))
        self.assertFalse(config.use_pair_code(code))

    def test_parallel_guesses_burn_the_code(self):
        config.new_pair_code()
        errors = []

        def guess():
            try:
                config.use_pair_code("000000")
            except Exception as error:  # noqa: BLE001
                errors.append(error)
        threads = [threading.Thread(target=guess) for _ in range(20)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        self.assertFalse(os.path.exists(os.path.join(config.data_dir(), "pair.json")))

    def test_code_expires(self):
        code = config.new_pair_code()
        path = os.path.join(config.data_dir(), "pair.json")
        data = json.load(open(path))
        data["expires"] = time.time() - 1
        json.dump(data, open(path, "w"))
        self.assertFalse(config.use_pair_code(code))

    def test_paired_token(self):
        token = config.add_paired("Firefox")
        self.assertTrue(config.check_paired(token))
        self.assertIsNone(config.check_paired(token + "x"))
        self.assertNotIn(token, open(os.path.join(config.data_dir(), "paired.json")).read())
        config.forget_paired()
        self.assertIsNone(config.check_paired(token))


class PairedPlace(Base):
    def test_cookie_stays_with_its_place(self):
        token = config.add_paired("Firefox", "192.168.1.5")
        self.assertTrue(config.check_paired(token, "192.168.1.5"))
        self.assertIsNone(config.check_paired(token, "192.168.1.6"))

    def test_old_pairing_takes_its_first_place(self):
        token = config.add_paired("Firefox")
        self.assertTrue(config.check_paired(token, "192.168.1.5"))
        self.assertIsNone(config.check_paired(token, "192.168.1.6"))

    def test_forget_one(self):
        one, two = config.add_paired("a"), config.add_paired("b")
        config.forget_device(one)
        self.assertIsNone(config.check_paired(one))
        self.assertTrue(config.check_paired(two))


class Log(Base):
    def test_log_lines_are_plain(self):
        config.log("refused-path", "192.168.50.9", "../<img src=x>\u0000\n<b>")
        detail = config.read_log(1)[0]["detail"]
        self.assertNotIn("\0", detail)
        self.assertNotIn("\n", detail)

    def test_discovery_starts_off(self):
        self.assertFalse(config.load()["discovery"])

    def test_note_on_the_pc_uses_its_language(self):
        names = ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG")
        saved = {k: os.environ.pop(k, None) for k in names}
        os.environ["LANG"] = "fr_CA.UTF-8"
        try:
            self.assertEqual(web.note_title(), "Portail d'appareil")
        finally:
            for key, value in saved.items():
                os.environ.pop(key, None)
                if value is not None:
                    os.environ[key] = value


class Network(unittest.TestCase):
    def setUp(self):
        import ipaddress
        nets = [ipaddress.ip_network("192.168.50.0/24"), ipaddress.ip_network("fd00:1::/64")]
        self.hood = security.Neighbourhood(lambda: nets)

    def test_same_network(self):
        self.assertTrue(self.hood.allows("192.168.50.213"))
        self.assertTrue(self.hood.allows("::ffff:192.168.50.213"))
        self.assertTrue(self.hood.allows("fd00:1::5"))
        self.assertTrue(self.hood.allows("127.0.0.1"))

    def test_other_networks_refused(self):
        for address in ("192.168.9.1", "10.1.1.5", "8.8.8.8", "::ffff:8.8.8.8", "2001:db8::1", "nonsense"):
            self.assertFalse(self.hood.allows(address), address)


class Throttling(Base):
    def test_blocks_after_five_and_survives_restart(self):
        throttle = security.Throttle()
        for i in range(4):
            self.assertFalse(throttle.fail("192.168.50.9"))
        self.assertFalse(throttle.blocked("192.168.50.9"))
        self.assertTrue(throttle.fail("192.168.50.9"))
        self.assertTrue(security.Throttle().blocked("192.168.50.9"))
        self.assertFalse(security.Throttle().blocked("192.168.50.10"))
        throttle.clear("192.168.50.9")
        self.assertFalse(throttle.blocked("192.168.50.9"))

    def test_parallel_tries_count(self):
        throttle = security.Throttle()
        allowed = []
        lock = threading.Lock()

        def guess():
            if throttle.attempt("192.168.50.9"):
                with lock:
                    allowed.append(1)
        threads = [threading.Thread(target=guess) for _ in range(40)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(len(allowed), security.MAX_FAILURES)

    def test_one_limit_for_all_addresses(self):
        throttle = security.Throttle()
        for n in range(security.MAX_FAILURES_ALL):
            self.assertTrue(throttle.attempt(f"192.168.50.{n + 10}"))
        self.assertFalse(throttle.attempt("192.168.50.200"))

    def test_ipv6_counts_per_64(self):
        throttle = security.Throttle()
        for n in range(security.MAX_FAILURES):
            self.assertTrue(throttle.attempt(f"fd00:1:2:3::{n + 1:x}"))
        self.assertFalse(throttle.attempt("fd00:1:2:3::ffff"))
        self.assertTrue(throttle.attempt("fd00:1:2:4::1"))

    def test_window_passes(self):
        throttle = security.Throttle(window=0.2)
        for _ in range(5):
            throttle.fail("a")
        self.assertTrue(throttle.blocked("a"))
        time.sleep(0.3)
        self.assertFalse(throttle.blocked("a"))


class SessionRules(unittest.TestCase):
    def test_idle_session_ends(self):
        sessions = security.Sessions(idle=0.2)
        token = sessions.create("1.2.3.4", "password")
        self.assertIsNotNone(sessions.get(token, "1.2.3.4"))
        time.sleep(0.3)
        self.assertIsNone(sessions.get(token, "1.2.3.4"))

    def test_session_stays_with_its_address(self):
        sessions = security.Sessions()
        token = sessions.create("1.2.3.4", "password")
        self.assertIsNone(sessions.get(token, "1.2.3.5"))
        self.assertIsNone(sessions.get(token, "1.2.3.4"))

    def test_longest_session(self):
        sessions = security.Sessions(longest=0.2)
        token = sessions.create("a", "password")
        time.sleep(0.1)
        self.assertIsNotNone(sessions.get(token, "a"))
        time.sleep(0.15)
        self.assertIsNone(sessions.get(token, "a"))


class Paths(Base):
    def setUp(self):
        super().setUp()
        self.home = os.path.join(self.tmp, "home")
        os.makedirs(os.path.join(self.home, "Documents"))
        open(os.path.join(self.tmp, "secret"), "w").write("no")
        os.symlink(os.path.join(self.tmp, "secret"), os.path.join(self.home, "link-out"))
        os.symlink(os.path.join(self.tmp), os.path.join(self.home, "dir-out"))

    def test_inside(self):
        self.assertEqual(files.resolve(self.home, "Documents"), os.path.realpath(os.path.join(self.home, "Documents")))
        self.assertEqual(files.resolve(self.home, ""), os.path.realpath(self.home))
        self.assertEqual(files.resolve(self.home, "/Documents"), os.path.realpath(os.path.join(self.home, "Documents")))

    def test_outside_refused(self):
        for path in ("..", "../secret", "Documents/../../secret", "link-out", "dir-out/secret", "a\0b",
                     "..\\secret"):
            with self.assertRaises(files.OutsideHome, msg=path):
                files.resolve(self.home, path)

    def test_upload_names(self):
        self.assertEqual(files.upload_name("../../etc/passwd"), "passwd")
        self.assertEqual(files.upload_name("C:\\x\\report.pdf"), "report.pdf")
        for bad in ("", "..", ".", "a\0b", "x" * 300):
            with self.assertRaises(ValueError):
                files.upload_name(bad)

    def test_hidden_places_refused_for_upload(self):
        os.makedirs(os.path.join(self.home, ".config", "autostart"))
        for folder in (".config/autostart", ".config", ".local/share/raye-device-portal"):
            with self.assertRaises(files.OutsideHome, msg=folder):
                files.upload_folder(self.home, folder)
        self.assertEqual(files.upload_folder(self.home, "Documents"),
                         os.path.realpath(os.path.join(self.home, "Documents")))
        for bad in (".bash_aliases", ".profile"):
            with self.assertRaises(ValueError):
                files.upload_name(bad)

    def test_portal_folders_closed(self):
        private = os.path.join(self.home, ".local", "share", "raye-device-portal")
        os.makedirs(private)
        open(os.path.join(private, "key.pem"), "w").write("key")
        with self.assertRaises(files.OutsideHome):
            files.resolve(self.home, ".local/share/raye-device-portal/key.pem", closed=[private])
        with self.assertRaises(files.OutsideHome):
            files.resolve(self.home, ".local/share/raye-device-portal", closed=[private])

    def test_never_overwrites(self):
        folder = os.path.join(self.home, "Documents")
        fd, first = files.open_new(folder, "report.pdf")
        os.close(fd)
        fd, second = files.open_new(folder, "report.pdf")
        os.close(fd)
        self.assertTrue(second.endswith("report (2).pdf"))


class Elevation(unittest.TestCase):
    def fake(self, script):
        self.tmp = tempfile.mkdtemp()
        path = os.path.join(self.tmp, "fake")
        with open(path, "w") as f:
            f.write("#!/bin/sh\n" + script)
        os.chmod(path, 0o755)
        self.addCleanup(shutil.rmtree, self.tmp)
        return [path]

    # what sudo -S says on stderr, as sudo 1.9 writes it
    SUDO = ('read -r p; [ "$p" = "right pass" ] && exit 0; echo "Sorry, try again." >&2; '
            'echo "sudo: no password was provided" >&2; echo "sudo: 1 incorrect password attempt" >&2; exit 1\n')

    def test_right_password(self):
        self.assertEqual(elevate.run([], "right pass", argv=self.fake(self.SUDO))[0], "ok")

    def test_wrong_password(self):
        self.assertEqual(elevate.run([], "wrong", argv=self.fake(self.SUDO)), ("denied", "wrong password"))

    def test_not_a_sudo_user(self):
        script = 'read -r p; echo "testn is not in the sudoers file." >&2; exit 1\n'
        self.assertEqual(elevate.run([], "x", argv=self.fake(script)), ("denied", "not allowed"))

    def test_action_failing_after_the_password(self):
        script = 'read -r p; echo "no such process" >&2; exit 3\n'
        self.assertEqual(elevate.run([], "right pass", argv=self.fake(script)), ("failed", "no such process"))

    def test_no_sudo_is_not_a_wrong_password(self):
        result, why = elevate.run([], "x", argv=["/nonexistent/sudo"])
        self.assertEqual(result, "unavailable")

    def test_password_never_reaches_a_prompt_on_the_pc(self):
        # pkexec handed the question to the agent of the graphical session,
        # so the prompt showed on the PC instead of using the typed password
        argv = elevate.command(["reboot"])
        self.assertEqual(argv[0], "sudo")
        self.assertIn("-S", argv)
        self.assertIn("-k", argv)
        self.assertNotIn("pkexec", " ".join(argv))
        self.assertEqual(argv[-2:], [os.path.join(elevate.system.LIBEXEC, "portal-power"), "reboot"])

    def test_outside_the_sandbox_without_a_terminal(self):
        argv = elevate.outside_command(["kill", 77])
        self.assertNotIn("--pty", argv)
        self.assertIn("--pipe", argv)
        self.assertEqual(argv[-3:], ["elevate", "kill", "77"])

    def test_sandboxed_portal_asks_outside(self):
        got = os.path.join(tempfile.mkdtemp(), "got")
        self.addCleanup(shutil.rmtree, os.path.dirname(got))
        fake = self.fake(f'read -r p; printf "%s" "$p" > "{got}"; echo ok\n')
        real = (elevate.no_new_privileges, elevate.outside_command)
        elevate.no_new_privileges, elevate.outside_command = (lambda: True), (lambda args: fake)
        try:
            self.assertEqual(elevate.run(["kill", 77], "sudo pass"), ("ok", ""))
        finally:
            elevate.no_new_privileges, elevate.outside_command = real
        with open(got) as f:
            self.assertEqual(f.read(), "sudo pass")

    def test_outside_unreachable_says_why(self):
        fake = self.fake('echo "Failed to connect to user scope bus" >&2; exit 1\n')
        self.assertEqual(elevate.run_outside(["reboot"], "x", argv=fake),
                         ("unavailable", "Failed to connect to user scope bus"))

    def test_elevate_command(self):
        """device-portal elevate, as the user's service manager runs it:
        password on stdin, sudo reads it, one answer line."""
        bin_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, bin_dir)
        with open(os.path.join(bin_dir, "sudo"), "w") as f:
            f.write('#!/bin/sh\n[ "$1" = -S ] || exit 9\nread -r p\n'
                    '[ "$p" = "sudo pass" ] || { echo "sudo: 1 incorrect password attempt" >&2; exit 1; }\n'
                    f'shift 5; echo "$@" > "{bin_dir}/args"\n')
        os.chmod(os.path.join(bin_dir, "sudo"), 0o755)
        env = dict(os.environ, PATH=bin_dir + ":" + os.environ["PATH"], RAYE_PORTAL_LIBEXEC="/lx")
        script = os.path.join(os.path.dirname(HERE), "device-portal")
        done = subprocess.run([sys.executable, script, "elevate", "kill", "77"], input="sudo pass\n",
                              capture_output=True, text=True, env=env, timeout=30)
        self.assertEqual(done.stdout.strip(), "ok")
        with open(os.path.join(bin_dir, "args")) as f:
            self.assertEqual(f.read().split(), ["/lx/portal-power", "kill", "77"])
        done = subprocess.run([sys.executable, script, "elevate", "kill", "77"], input="wrong\n",
                              capture_output=True, text=True, env=env, timeout=30)
        self.assertEqual(done.stdout.split()[0], "denied")
        self.assertEqual(done.returncode, 1)


class Settings(unittest.TestCase):
    def test_installed_portal_ignores_the_environment(self):
        import rayeportal
        os.environ["RAYE_PORTAL_PROC"] = "/tmp/elsewhere"
        self.addCleanup(os.environ.pop, "RAYE_PORTAL_PROC", None)
        self.assertEqual(rayeportal.setting("RAYE_PORTAL_PROC", "/proc"), "/tmp/elsewhere")
        rayeportal._INSTALLED = True
        self.addCleanup(setattr, rayeportal, "_INSTALLED", False)
        self.assertEqual(rayeportal.setting("RAYE_PORTAL_PROC", "/proc"), "/proc")


class Server(Base):
    """The whole thing over HTTPS."""

    def setUp(self):
        super().setUp()
        self.home = os.path.join(self.tmp, "home")
        os.makedirs(os.path.join(self.home, "Documents"))
        with open(os.path.join(self.home, "Documents", "note.txt"), "w") as f:
            f.write("hello portal")
        with open(os.path.join(self.tmp, "secret"), "w") as f:
            f.write("not for the portal")
        os.symlink(os.path.join(self.tmp, "secret"), os.path.join(self.home, "link-out"))
        cli.make_certificate()
        config.set_login("alex", "portal-pass-1")
        self.allow = True
        hood = security.Neighbourhood(lambda: [])
        hood.allows = lambda address: self.allow
        self.portal = web.Portal(home=self.home, neighbourhood=hood, sessions=security.Sessions(idle=60))
        sock = web.listening_socket(0)
        self.port = sock.getsockname()[1]
        cert, key = cli.cert_paths()
        self.server = web.Server(sock, web.tls_context(cert, key), self.portal)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.context = ssl.create_default_context()
        self.context.check_hostname = False
        self.context.verify_mode = ssl.CERT_NONE
        self.cookies = {}

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        super().tearDown()

    def request(self, method, path, body=None, headers=None, form=None):
        conn = http.client.HTTPSConnection("127.0.0.1", self.port, context=self.context, timeout=15)
        headers = dict(headers or {})
        if self.cookies:
            headers["Cookie"] = "; ".join(f"{k}={v}" for k, v in self.cookies.items())
        if form is not None:
            body = urllib.parse.urlencode(form)
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        conn.request(method, path, body=body, headers=headers)
        reply = conn.getresponse()
        data = reply.read()
        for value in reply.headers.get_all("Set-Cookie") or []:
            name, _, rest = value.partition("=")
            self.cookies[name] = rest.split(";")[0]
        conn.close()
        return reply, data

    def sign_in(self, password="portal-pass-1"):
        self.request("GET", "/login")
        csrf = self.cookies[web.PRELOGIN]
        return self.request("POST", "/login", form={"csrf": csrf, "user": "alex", "password": password})

    def csrf(self):
        reply, data = self.request("GET", "/")
        text = data.decode()
        start = text.index('name="csrf" content="') + len('name="csrf" content="')
        return text[start:text.index('"', start)]

    def test_headers(self):
        reply, _ = self.request("GET", "/login")
        self.assertEqual(reply.status, 200)
        self.assertIn("frame-ancestors 'none'", reply.headers["Content-Security-Policy"])
        self.assertEqual(reply.headers["X-Frame-Options"], "DENY")
        self.assertEqual(reply.headers["X-Content-Type-Options"], "nosniff")
        self.assertEqual(reply.headers["Referrer-Policy"], "same-origin")
        self.assertEqual(reply.headers["Server"], "Raye")
        cookie = reply.headers["Set-Cookie"]
        for part in ("Secure", "HttpOnly", "SameSite=Strict", "Path=/"):
            self.assertIn(part, cookie)

    def test_no_hsts_on_a_certificate_of_its_own(self):
        reply, _ = self.request("GET", "/login")
        self.assertIsNone(reply.headers["Strict-Transport-Security"])

    def raw(self, data, wait=15):
        raw = socket.create_connection(("127.0.0.1", self.port), timeout=wait)
        with self.context.wrap_socket(raw) as tls:
            tls.sendall(data)
            out = b""
            while True:
                chunk = tls.recv(4096)
                if not chunk:
                    return out
                out += chunk

    def test_other_hosts_refused(self):
        for host in ("evil.example", "evil.example:%d" % self.port, "127.0.0.1:1", "[::1]:1", ""):
            reply, _ = self.request("GET", "/login", headers={"Host": host})
            self.assertEqual(reply.status, 421, host)
        for host in ("127.0.0.1:%d" % self.port, "[::1]:%d" % self.port, "%s:%d" % (socket.gethostname(), self.port),
                     "%s.local:%d" % (socket.gethostname().upper(), self.port), "192.168.50.213"):
            reply, _ = self.request("GET", "/login", headers={"Host": host})
            self.assertEqual(reply.status, 200, host)

    def test_error_answers_have_the_headers(self):
        for request in (b"BREW / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", b"GET /" + b"a" * 70000 + b" HTTP/1.1\r\n\r\n"):
            head = self.raw(request).split(b"\r\n\r\n")[0].decode()
            self.assertRegex(head, r"^HTTP/1\.[01] (414|501) ")
            self.assertIn("X-Frame-Options: DENY", head)
            self.assertIn("Content-Security-Policy:", head)

    def test_slow_headers_are_cut(self):
        web.HEADERS_TIME, before = 0.5, web.HEADERS_TIME
        self.addCleanup(setattr, web, "HEADERS_TIME", before)
        start = time.monotonic()
        self.assertEqual(self.raw(b"GET /login HTTP/1.1\r\n"), b"")
        self.assertLess(time.monotonic() - start, 5)

    def test_silent_connections_are_cut(self):
        web.HANDSHAKE_TIME, before = 0.5, web.HANDSHAKE_TIME
        self.addCleanup(setattr, web, "HANDSHAKE_TIME", before)
        raw = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        start = time.monotonic()
        self.assertEqual(raw.recv(10), b"")
        self.assertLess(time.monotonic() - start, 5)
        raw.close()

    def test_idle_connections_capped_per_machine(self):
        web.CONNECTIONS_EACH, before = 3, web.CONNECTIONS_EACH
        self.addCleanup(setattr, web, "CONNECTIONS_EACH", before)
        idle = [socket.create_connection(("127.0.0.1", self.port), timeout=10) for _ in range(3)]
        time.sleep(0.5)
        extra = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        self.assertEqual(extra.recv(10), b"")
        extra.close()
        for sock in idle:
            sock.close()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                reply, _ = self.request("GET", "/login")
                break
            except (ssl.SSLError, OSError):
                time.sleep(0.2)
        self.assertEqual(reply.status, 200)
        self.assertIn("refused-busy", [e["event"] for e in config.read_log()])

    def test_refusals_logged_once_an_hour(self):
        self.allow = False
        for _ in range(3):
            with self.assertRaises((ssl.SSLError, ConnectionError, OSError)):
                self.request("GET", "/login")
        self.assertEqual([e["event"] for e in config.read_log()].count("refused-network"), 1)

    def pair(self):
        code = config.new_pair_code()
        self.request("GET", "/login")
        self.request("POST", "/pair", form={"csrf": self.cookies[web.PRELOGIN], "code": code})

    def test_forgotten_devices_lose_their_sessions(self):
        self.pair()
        reply, _ = self.request("GET", "/api/stats")
        self.assertEqual(reply.status, 200)
        self.assertEqual(cli.main(["forget-devices"]), 0)
        reply, _ = self.request("GET", "/api/stats")
        self.assertEqual(reply.status, 401)
        reply, _ = self.request("GET", "/login")
        self.assertEqual(reply.status, 200)

    def test_sign_out_unpairs_the_browser(self):
        self.pair()
        device = self.cookies[web.DEVICE]
        self.request("POST", "/logout", form={"csrf": self.csrf()})
        self.cookies = {web.DEVICE: device}
        reply, _ = self.request("GET", "/login")
        self.assertEqual(reply.status, 200)
        self.assertEqual(config.paired_devices(), [])

    def test_pages_need_a_session(self):
        reply, _ = self.request("GET", "/")
        self.assertEqual(reply.status, 303)
        self.assertEqual(reply.headers["Location"], "/login")
        reply, _ = self.request("GET", "/api/stats")
        self.assertEqual(reply.status, 401)

    def test_sign_in(self):
        reply, _ = self.sign_in()
        self.assertEqual(reply.status, 303)
        self.assertIn(web.SESSION, self.cookies)
        reply, data = self.request("GET", "/api/stats")
        self.assertEqual(reply.status, 200)
        self.assertIn("cpu", json.loads(data))
        log = [e["event"] for e in config.read_log()]
        self.assertIn("sign-in", log)

    def test_wrong_password_then_lock(self):
        for _ in range(4):
            reply, _ = self.sign_in("nope")
            self.assertEqual(reply.status, 401)
        reply, _ = self.sign_in("nope")
        self.assertEqual(reply.status, 429)
        # even the right password waits now
        reply, _ = self.sign_in()
        self.assertEqual(reply.status, 429)
        self.assertNotIn(web.SESSION, self.cookies)

    def test_login_needs_its_csrf(self):
        self.request("GET", "/login")
        reply, _ = self.request("POST", "/login", form={"csrf": "made-up", "user": "alex", "password": "portal-pass-1"})
        self.assertEqual(reply.status, 403)
        csrf = self.cookies[web.PRELOGIN]
        reply, _ = self.request("POST", "/login", form={"csrf": csrf, "user": "alex", "password": "portal-pass-1"},
                                headers={"Origin": "https://evil.example"})
        self.assertEqual(reply.status, 403)
        self.assertNotIn(web.SESSION, self.cookies)

    def test_changes_need_the_csrf_token(self):
        self.sign_in()
        child = subprocess.Popen(["sleep", "30"])
        self.addCleanup(child.kill)
        body = json.dumps({"pid": child.pid})
        reply, _ = self.request("POST", "/api/kill", body=body)
        self.assertEqual(reply.status, 403)
        reply, _ = self.request("POST", "/api/kill", body=body, headers={"X-CSRF-Token": "wrong"})
        self.assertEqual(reply.status, 403)
        token = self.csrf()
        reply, _ = self.request("POST", "/api/kill", body=body,
                                headers={"X-CSRF-Token": token, "Sec-Fetch-Site": "cross-site"})
        self.assertEqual(reply.status, 403)
        self.assertIsNone(child.poll())
        reply, data = self.request("POST", "/api/kill", body=body, headers={"X-CSRF-Token": token})
        self.assertEqual(reply.status, 200)
        self.assertEqual(json.loads(data)["result"], "ok")
        self.assertIsNotNone(child.wait(timeout=5))

    def test_files_stay_home(self):
        self.sign_in()
        reply, data = self.request("GET", "/api/files?path=Documents")
        self.assertEqual(reply.status, 200)
        self.assertEqual([e["name"] for e in json.loads(data)["entries"]], ["note.txt"])
        reply, data = self.request("GET", "/api/download?path=Documents/note.txt")
        self.assertEqual((reply.status, data), (200, b"hello portal"))
        for path in ("..", "../secret", "link-out", "Documents/../../secret", "%2e%2e/secret"):
            reply, data = self.request("GET", "/api/download?path=" + path)
            self.assertEqual(reply.status, 403, path)
            self.assertNotIn(b"not for the portal", data)
        reply, _ = self.request("GET", "/api/files?path=../")
        self.assertEqual(reply.status, 403)

    def test_upload(self):
        self.sign_in()
        token = self.csrf()
        reply, data = self.request("POST", "/api/upload?path=Documents&name=../../x.txt", body=b"sent",
                                   headers={"X-CSRF-Token": token})
        self.assertEqual(reply.status, 200)
        with open(os.path.join(self.home, "Documents", "x.txt"), "rb") as f:
            self.assertEqual(f.read(), b"sent")
        reply, _ = self.request("POST", "/api/upload?path=..&name=y.txt", body=b"no", headers={"X-CSRF-Token": token})
        self.assertEqual(reply.status, 403)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "y.txt")))

    def test_session_expires(self):
        self.portal.sessions.idle = 0.2
        self.sign_in()
        time.sleep(0.3)
        reply, _ = self.request("GET", "/api/stats")
        self.assertEqual(reply.status, 401)

    def test_sign_out(self):
        self.sign_in()
        token = self.csrf()
        reply, _ = self.request("POST", "/logout", form={"csrf": token})
        self.assertEqual(reply.status, 303)
        reply, _ = self.request("GET", "/api/stats")
        self.assertEqual(reply.status, 401)

    def test_pairing(self):
        code = config.new_pair_code()
        self.request("GET", "/login")
        reply, _ = self.request("POST", "/pair", form={"csrf": self.cookies[web.PRELOGIN], "code": code})
        self.assertEqual(reply.status, 303)
        self.assertIn(web.DEVICE, self.cookies)
        # the session goes, the pairing stays: back in without a password
        del self.cookies[web.SESSION]
        reply, _ = self.request("GET", "/login")
        self.assertEqual((reply.status, reply.headers["Location"]), (303, "/"))
        reply, _ = self.request("GET", "/api/stats")
        self.assertEqual(reply.status, 200)

    def test_other_networks_refused(self):
        self.allow = False
        with self.assertRaises((ssl.SSLError, ConnectionError, OSError)):
            self.request("GET", "/login")
        self.assertIn("refused-network", [e["event"] for e in config.read_log()])

    def test_no_upload_to_autostart(self):
        os.makedirs(os.path.join(self.home, ".config", "autostart"))
        self.sign_in()
        token = self.csrf()
        reply, _ = self.request("POST", "/api/upload?path=.config/autostart&name=evil.desktop", body=b"x",
                                headers={"X-CSRF-Token": token})
        self.assertEqual(reply.status, 403)
        reply, _ = self.request("POST", "/api/upload?path=&name=.bash_aliases", body=b"x",
                                headers={"X-CSRF-Token": token})
        self.assertEqual(reply.status, 400)
        self.assertEqual(os.listdir(os.path.join(self.home, ".config", "autostart")), [])

    def test_no_download_of_portal_key(self):
        private = os.path.join(self.home, ".local", "share", "raye-device-portal")
        os.makedirs(private)
        with open(os.path.join(private, "key.pem"), "w") as f:
            f.write("PRIVATE KEY")
        self.portal.closed = [private]
        self.sign_in()
        reply, data = self.request("GET", "/api/download?path=.local/share/raye-device-portal/key.pem")
        self.assertEqual(reply.status, 403)
        self.assertNotIn(b"PRIVATE KEY", data)

    def test_signed_out_errors_keep_the_path_out_of_the_log(self):
        raw = socket.create_connection(("127.0.0.1", self.port), timeout=15)
        with self.context.wrap_socket(raw) as tls:
            tls.sendall(b"GET /static/<h1>x\x00.css HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
            while tls.recv(4096):
                pass
        entries = config.read_log(10)
        self.assertTrue(any(e["event"] == "error" for e in entries))
        for entry in entries:
            self.assertNotIn("<h1>", entry.get("detail", ""))

    def test_no_end_task_for_pid_1(self):
        self.sign_in()
        token = self.csrf()
        reply, _ = self.request("POST", "/api/kill", body=json.dumps({"pid": 1, "password": "x"}),
                                headers={"X-CSRF-Token": token, "Content-Type": "application/json"})
        self.assertEqual(reply.status, 400)
        reply, data = self.request("GET", "/api/processes")
        rows = {p["pid"]: p for p in json.loads(data)["processes"]}
        if 1 in rows:
            self.assertFalse(rows[1]["endable"])
        if 2 in rows:
            self.assertFalse(rows[2]["endable"])

    def test_too_many_sudo_passwords_lock_root_actions(self):
        """After five wrong sudo passwords, Restart and End task say
        "locked" in plain terms, never a bare server error."""
        self.sign_in()
        token = self.csrf()
        calls = []
        real = web.elevate.run

        def wrong(args, password):
            calls.append(list(args))
            return "denied", "wrong password"
        web.elevate.run = wrong
        self.addCleanup(setattr, web.elevate, "run", real)
        headers = {"X-CSRF-Token": token, "Content-Type": "application/json"}
        power = json.dumps({"action": "reboot", "password": "nope"})
        for _ in range(security.MAX_FAILURES):
            reply, data = self.request("POST", "/api/power", body=power, headers=headers)
            self.assertEqual((reply.status, json.loads(data)), (403, {"result": "denied"}))
        reply, data = self.request("POST", "/api/power", body=power, headers=headers)
        self.assertEqual((reply.status, json.loads(data)), (403, {"result": "locked"}))

        child = subprocess.Popen(["sleep", "30"])
        self.addCleanup(child.kill)
        real_owner = web.system.owner_of
        other = os.getuid() + 1
        web.system.owner_of = lambda pid: other if pid == child.pid else real_owner(pid)
        self.addCleanup(setattr, web.system, "owner_of", real_owner)
        reply, data = self.request("POST", "/api/kill", headers=headers,
                                   body=json.dumps({"pid": child.pid, "password": "nope"}))
        self.assertEqual((reply.status, json.loads(data)), (403, {"result": "locked"}))
        self.assertIsNone(child.poll())
        # the locked tries never reached pkexec
        self.assertEqual(len(calls), security.MAX_FAILURES)
        events = [e["event"] for e in config.read_log()]
        self.assertIn("reboot-locked", events)
        self.assertIn("end-task-locked", events)
        self.assertNotIn("error", events)

    def test_wrong_password_keeps_the_user_name(self):
        _, data = self.sign_in("not-it")
        self.assertIn(b'value="alex"', data)

    def test_screen_page_knows_screenshots_are_off(self):
        self.sign_in()
        reply, data = self.request("GET", "/api/screen")
        self.assertEqual((reply.status, json.loads(data)), (200, {"allowed": False}))

    def test_screenshot_off_by_default(self):
        self.sign_in()
        reply, _ = self.request("POST", "/api/screenshot", headers={"X-CSRF-Token": self.csrf()})
        self.assertEqual(reply.status, 403)

    def test_pages(self):
        self.sign_in()
        for page in ("", "processes", "apps", "files", "logs", "performance", "network", "screen", "power"):
            reply, data = self.request("GET", "/" + page, headers={"Accept-Language": "fr-CA,fr;q=0.9"})
            self.assertEqual(reply.status, 200, page)
            self.assertIn(b'lang="fr"', data)
        reply, _ = self.request("GET", "/static/portal.js")
        self.assertEqual(reply.status, 200)
        reply, _ = self.request("GET", "/static/../rayeportal/web.py")
        self.assertEqual(reply.status, 404)


if __name__ == "__main__":
    unittest.main()
