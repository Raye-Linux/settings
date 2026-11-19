# SPDX-License-Identifier: GPL-3.0-or-later
"""windows-import-mount and windows-import on real NTFS volumes, in image
files on loop devices. Root only: they skip elsewhere (the package build),
and run under RAYE_TESTS_STRICT=1 or they fail."""
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import unittest

from test_engine import HELPER, SOURCE, tree, windows_drive

MOUNT = SOURCE / "data/libexec/windows-import-mount"
TOOLS = ("losetup", "mkfs.ntfs", "ntfs-3g", "ntfs-3g.probe", "blkid", "findmnt")
# the account the files are for: one that exists everywhere
PERSON = 65534


def missing():
    if os.geteuid() != 0:
        return "needs root"
    gone = [t for t in TOOLS if shutil.which(t) is None]
    if gone:
        return "missing " + " ".join(gone)
    if not os.path.exists("/dev/fuse") or not os.path.exists("/dev/loop-control"):
        return "no loop devices or FUSE here"
    return ""


def run(*argv, **kwargs):
    return subprocess.run(argv, check=True, capture_output=True, text=True, **kwargs)


def as_person():
    os.setgroups([])
    os.setgid(PERSON)
    os.setuid(PERSON)


@unittest.skipIf(missing() and not os.environ.get("RAYE_TESTS_STRICT"), missing())
class Images(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = pathlib.Path(self._tmp.name)
        os.chmod(self.tmp, 0o755)
        self.loops = []
        self.mounts = []

    def tearDown(self):
        for where in reversed(self.mounts):
            subprocess.run(["umount", where], capture_output=True)
        for loop in self.loops:
            subprocess.run(["losetup", "-d", loop], capture_output=True)
        self._tmp.cleanup()

    def loop(self, name, mib=160):
        image = self.tmp / name
        with open(image, "wb") as f:
            f.truncate(mib << 20)
        loop = run("losetup", "-f", "--show", str(image)).stdout.strip()
        self.loops.append(loop)
        return loop

    def windows(self, hibernated=False):
        """A loop device holding an NTFS volume with a Windows on it."""
        loop = self.loop("c.img")
        run("mkfs.ntfs", "-F", "-Q", "-L", "Windows", loop)
        where = self.tmp / "fill"
        where.mkdir()
        run("ntfs-3g", loop, str(where))
        self.mounts.append(str(where))
        windows_drive(where, ("Alex", "Sam"))
        if hibernated:
            (where / "hiberfil.sys").write_bytes(b"HIBR" + b"\0" * 4092)
        self.expected = tree(where / "Users/Alex/Documents")
        run("umount", str(where))
        self.mounts.pop()
        return loop

    def hold(self, device):
        env = {"PATH": os.environ["PATH"], "PKEXEC_UID": str(PERSON),
               "RAYE_WINDOWS_IMPORT_RUN": str(self.tmp / "run")}
        process = subprocess.Popen(["sh", str(MOUNT), "hold", device], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, text=True, env=env)
        self.addCleanup(process.stdout.close)
        self.addCleanup(lambda: process.stdin.closed or process.stdin.close())
        line = process.stdout.readline().strip()
        while line.startswith("warning "):
            line = process.stdout.readline().strip()
        return process, line

    def release(self, process):
        process.stdin.close()
        tail = process.stdout.read()
        process.wait(timeout=30)
        return tail

    def person_run(self, *argv):
        home = self.tmp / "home"
        home.mkdir(exist_ok=True)
        os.chown(home, PERSON, PERSON)
        env = {"PATH": os.environ["PATH"], "HOME": str(home), "LANG": "C.UTF-8",
               "XDG_STATE_HOME": str(home / ".local/state")}
        return subprocess.run(["python3", "-I", str(HELPER), *argv], capture_output=True, text=True,
                              env=env, preexec_fn=as_person, timeout=120)

    def test_open_read_only_scan_copy_and_close(self):
        process, line = self.hold(self.windows())
        self.assertTrue(line.startswith("mounted "), line)
        where = line.split(" ", 1)[1]
        options = run("findmnt", "-rn", "-o", "OPTIONS", "-M", where).stdout
        self.assertIn("ro", options.split(","))
        self.assertIn("nosuid", options)
        # the folders on the way are root's; inside, the files are the person's only
        self.assertEqual(os.stat(os.path.dirname(where)).st_uid, 0)
        self.assertEqual((os.stat(where).st_uid, os.stat(where).st_mode & 0o777), (PERSON, 0o500))

        scanned = self.person_run("scan", where)
        found = json.loads(scanned.stdout)
        self.assertEqual(found["state"], "ok", scanned.stderr)
        self.assertEqual([a["name"] for a in found["accounts"]], ["Alex", "Sam"])

        home = self.tmp / "home"
        plan = {"root": where, "lang": "fr", "dest": {"documents": str(home / "Documents"),
                                                       "pictures": str(home / "Images")},
                "accounts": [{"name": "Alex", "path": "Users/Alex", "wallpaper": True,
                              "folders": [{"kind": "documents", "path": "Documents"},
                                          {"kind": "pictures", "path": "Pictures"}]}]}
        (self.tmp / "plan.json").write_text(json.dumps(plan))
        copied = self.person_run("copy", str(self.tmp / "plan.json"))
        events = [json.loads(line) for line in copied.stdout.splitlines()]
        self.assertEqual(copied.returncode, 0, copied.stdout + copied.stderr)
        self.assertEqual(events[-1]["event"], "done")
        got = tree(home / "Documents")
        want = {k: v for k, v in self.expected.items()
                if not k.endswith((".ini", ".lnk")) and k != "My Music"}
        self.assertEqual(got, want)
        self.assertEqual(os.stat(home / "Documents/notes.txt").st_uid, PERSON)
        self.assertTrue((home / "Images/Windows wallpaper.jpg").is_file())

        tail = self.release(process)
        self.assertIn("unmounted", tail)
        self.assertNotEqual(subprocess.run(["findmnt", "-M", where], capture_output=True).returncode, 0)
        self.assertFalse(os.path.exists(where))
        self.assertEqual(process.returncode, 0)
        # and the volume itself is as it was
        check = self.tmp / "check"
        check.mkdir()
        run("ntfs-3g", "-o", "ro", self.loops[0], str(check))
        self.mounts.append(str(check))
        self.assertEqual(tree(check / "Users/Alex/Documents"), self.expected)

    def test_a_hibernated_windows_is_never_mounted(self):
        loop = self.windows(hibernated=True)
        process, line = self.hold(loop)
        process.wait(timeout=30)
        self.assertEqual(line, "refused hibernated")
        self.assertNotEqual(subprocess.run(["findmnt", "-S", loop], capture_output=True).returncode, 0)

    def test_a_volume_in_use_is_refused(self):
        loop = self.windows()
        where = self.tmp / "busy"
        where.mkdir()
        run("ntfs-3g", "-o", "ro", loop, str(where))
        self.mounts.append(str(where))
        process, line = self.hold(loop)
        process.wait(timeout=30)
        self.assertEqual(line, "refused busy")

    def test_bitlocker_is_refused(self):
        loop = self.loop("locked.img", 64)
        # what the start of a BitLocker volume carries, as libblkid reads it
        with open(self.tmp / "locked.img", "r+b") as f:
            sector = bytearray(512)
            sector[0:3] = b"\xeb\x58\x90"
            sector[3:11] = b"-FVE-FS-"
            sector[11:13] = (512).to_bytes(2, "little")
            sector[13] = 8
            sector[510:512] = b"\x55\xaa"
            for offset in (0x1000, 0x2000, 0x3000):
                sector[176 + 8 * ((offset // 0x1000) - 1):184 + 8 * ((offset // 0x1000) - 1)] = \
                    offset.to_bytes(8, "little")
            f.write(sector)
            for offset in (0x1000, 0x2000, 0x3000):
                f.seek(offset)
                f.write(b"-FVE-FS-" + (64).to_bytes(2, "little") + (2).to_bytes(2, "little"))
        run("blockdev", "--flushbufs", loop)
        kind = subprocess.run(["blkid", "-p", "-o", "value", "-s", "TYPE", loop],
                              capture_output=True, text=True).stdout.strip()
        if kind != "BitLocker":
            self.skipTest("this blkid does not see the made-up BitLocker header (%r)" % kind)
        process, line = self.hold(loop)
        process.wait(timeout=30)
        self.assertEqual(line, "refused bitlocker")

    def test_something_else_than_a_device_is_refused(self):
        for bad in ("/etc/passwd", "../dev/sda", "/dev/../etc/passwd", "relative"):
            process, line = self.hold(bad)
            process.wait(timeout=30)
            self.assertEqual(line, "refused not-windows", bad)

    def test_settings_going_away_unmounts(self):
        process, line = self.hold(self.windows())
        where = line.split(" ", 1)[1]
        # what happens when Settings is killed: its end of the pipe closes
        process.stdin.close()
        process.wait(timeout=30)
        deadline = time.time() + 10
        while time.time() < deadline and os.path.exists(where):
            time.sleep(0.2)
        self.assertFalse(os.path.exists(where))


if __name__ == "__main__":
    unittest.main()
