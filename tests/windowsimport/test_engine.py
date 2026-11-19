# SPDX-License-Identifier: GPL-3.0-or-later
"""data/libexec/windows-import on real files in a temporary folder."""
import hashlib
import io
import json
import os
import pathlib
import sqlite3
import sys
import tempfile
import unittest
from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader
from unittest import mock

SOURCE = pathlib.Path(os.environ.get("SOURCE_DIR", pathlib.Path(__file__).parents[2]))
HELPER = SOURCE / "data/libexec/windows-import"


def load():
    loader = SourceFileLoader("windows_import", str(HELPER))
    module = module_from_spec(spec_from_loader("windows_import", loader))
    loader.exec_module(module)
    return module


wi = load()


def tree(base):
    """{relative path: sha256} of every file, links as "-> target"."""
    found = {}
    for here, dirs, files in os.walk(base):
        for name in files + [d for d in dirs if os.path.islink(os.path.join(here, d))]:
            path = os.path.join(here, name)
            rel = os.path.relpath(path, base)
            if os.path.islink(path):
                found[rel] = "-> " + os.readlink(path)
            else:
                found[rel] = hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
    return found


def sample(base):
    (base / "2023/trip").mkdir(parents=True)
    (base / "2023/trip/a.jpg").write_bytes(os.urandom(300_000))
    (base / "notes.txt").write_text("hello")
    (base / "empty.bin").write_bytes(b"")
    (base / "Évènement ✓.pdf").write_bytes(os.urandom(5000))
    os.utime(base / "notes.txt", (1_600_000_000, 1_600_000_000))
    return base


def chrome_bookmarks():
    return json.dumps({"roots": {
        "bookmark_bar": {"type": "folder", "name": "Bookmarks bar", "children": [
            {"type": "url", "name": "Raye <home>", "url": "https://rayelinux.com/"},
            {"type": "url", "name": "Bad", "url": "javascript:alert(1)"}]},
        "other": {"type": "folder", "name": "Other bookmarks", "children": [
            {"type": "folder", "name": "Recipes", "children": [
                {"type": "url", "name": "Bread", "url": "https://example.org/bread?a=1&b=2"}]}]},
        "synced": {"type": "folder", "name": "Mobile", "children": []}}})


def firefox_places(path):
    db = sqlite3.connect(path)
    db.executescript("""
        CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT);
        CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, fk INTEGER,
                                    parent INTEGER, position INTEGER, title TEXT);
        INSERT INTO moz_places VALUES (1, 'https://www.mozilla.org/'), (2, 'place:sort=8');
        INSERT INTO moz_bookmarks VALUES (1, 2, NULL, 0, 0, ''), (2, 2, NULL, 1, 0, 'menu'),
            (3, 2, NULL, 1, 1, 'toolbar'), (4, 1, 1, 3, 0, 'Mozilla'), (5, 1, 2, 3, 1, 'Recent');
    """)
    db.commit()
    db.close()


def windows_drive(base, accounts=("Alex",)):
    """A Windows volume the way ntfs-3g shows it."""
    (base / "Windows/System32").mkdir(parents=True)
    (base / "Program Files").mkdir()
    (base / "Users/Public/Documents").mkdir(parents=True)
    (base / "Users/Public/Documents/shared.txt").write_text("shared")
    (base / "Users/Default/Documents").mkdir(parents=True)
    (base / "Users/Default/Documents/x.txt").write_text("x")
    (base / "Users/desktop.ini").write_text("[.ShellClassInfo]")
    for name in accounts:
        home = base / "Users" / name
        sample(home / "Documents")
        (home / "Documents/desktop.ini").write_text("[.ShellClassInfo]")
        (home / "Documents/Old shortcut.lnk").write_bytes(b"L\0\0\0")
        os.symlink("C:/Users/" + name + "/Music", home / "Documents/My Music")
        (home / "Pictures").mkdir()
        (home / "Pictures/cat.png").write_bytes(b"\x89PNG" + os.urandom(2000))
        (home / "Music").mkdir()
        (home / "Downloads").mkdir()
        (home / "Downloads/setup.exe").write_bytes(os.urandom(1000))
        (home / "OneDrive/Desktop").mkdir(parents=True)
        (home / "OneDrive/Desktop/todo.txt").write_text("todo")
        themes = home / pathlib.Path(wi.WALLPAPER).parent
        themes.mkdir(parents=True)
        (themes / "TranscodedWallpaper").write_bytes(b"\xff\xd8\xff\xe0" + os.urandom(4000))
        chrome = home / "AppData/Local/Google/Chrome/User Data/Default"
        chrome.mkdir(parents=True)
        (chrome / "Bookmarks").write_text(chrome_bookmarks())
        profile = home / "AppData/Roaming/Mozilla/Firefox/Profiles/abcd.default-release"
        profile.mkdir(parents=True)
        firefox_places(str(profile / "places.sqlite"))
    return base


class Temp(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def copy(self, source, target, **kwargs):
        target.mkdir(parents=True, exist_ok=True)
        copier = wi.Copier(**kwargs)
        src = os.open(source, wi.DIR_FLAGS)
        dst = os.open(target, wi.DIR_FLAGS)
        try:
            copier.copy_tree(src, dst)
        finally:
            os.close(src)
            os.close(dst)
        return copier


class Paths(unittest.TestCase):
    def test_clean_refuses_leaving_the_folder(self):
        self.assertEqual(wi.clean("Users/Alex"), ["Users", "Alex"])
        for bad in ("", "/etc", "../x", "a/../b", "a\0b", None):
            self.assertIsNone(wi.clean(bad), bad)

    def test_skipped_names(self):
        for name in ("desktop.ini", "Thumbs.db", "Report.LNK"):
            self.assertTrue(wi.is_skipped_name(name), name)
        self.assertFalse(wi.is_skipped_name("photo.jpg"))


class Scan(Temp):
    def test_drive_lists_each_account_and_its_folders(self):
        drive = windows_drive(self.tmp / "c", ("Alex", "Sam"))
        found = wi.scan(str(drive))
        self.assertEqual(found["state"], "ok")
        self.assertEqual(found["layout"], "drive")
        self.assertEqual([a["name"] for a in found["accounts"]], ["Alex", "Sam"])
        alex = found["accounts"][0]
        self.assertEqual(alex["path"], "Users/Alex")
        folders = {f["path"]: f for f in alex["folders"]}
        self.assertEqual(set(folders), {"Documents", "Pictures", "Downloads", "OneDrive/Desktop"})
        # desktop.ini, the shortcut and the link are not counted as files
        self.assertEqual(folders["Documents"]["files"], 4)
        self.assertEqual(folders["Documents"]["bytes"], 300_000 + 5 + 5000)
        self.assertTrue(folders["OneDrive/Desktop"]["onedrive"])
        self.assertGreater(alex["wallpaper"], 0)
        self.assertEqual({b["browser"] for b in alex["browsers"]}, {"chrome", "firefox"})

    def test_hibernated_windows_is_refused(self):
        drive = windows_drive(self.tmp / "c")
        (drive / "hiberfil.sys").write_bytes(b"HIBR" + b"\0" * 60)
        self.assertEqual(wi.scan(str(drive))["state"], "hibernated")
        (drive / "hiberfil.sys").write_bytes(b"wake" + b"\0" * 60)
        self.assertEqual(wi.scan(str(drive))["state"], "ok")

    def test_links_on_the_way_are_not_followed(self):
        drive = windows_drive(self.tmp / "c")
        outside = sample(self.tmp / "outside")
        (drive / "Users/Eve").mkdir()
        os.symlink(outside, drive / "Users/Eve/Documents")
        os.symlink(self.tmp / "outside", drive / "Users/Mallory")
        names = [a["name"] for a in wi.scan(str(drive))["accounts"]]
        self.assertEqual(names, ["Alex"])

    def test_one_account_folder_on_a_stick(self):
        stick = self.tmp / "stick/Alex backup"
        sample(stick / "Documents")
        (stick / "Music").mkdir()
        (stick / "Music/song.mp3").write_bytes(os.urandom(100))
        found = wi.scan(str(stick))
        self.assertEqual(found["layout"], "account")
        self.assertEqual(found["accounts"][0]["name"], "Alex backup")
        self.assertEqual(found["accounts"][0]["path"], "")
        self.assertEqual({f["kind"] for f in found["accounts"][0]["folders"]}, {"documents", "music"})

    def test_a_users_folder_copied_to_a_stick(self):
        users = self.tmp / "stick/Users"
        sample(users / "Alex/Documents")
        found = wi.scan(str(users))
        self.assertEqual(found["layout"], "users")
        self.assertEqual(found["accounts"][0]["path"], "Alex")

    def test_nothing_to_bring(self):
        (self.tmp / "empty/Users/Alex/Documents").mkdir(parents=True)
        (self.tmp / "empty/Windows").mkdir()
        self.assertEqual(wi.scan(str(self.tmp / "empty"))["state"], "nothing")
        self.assertEqual(wi.scan(str(self.tmp / "missing"))["state"], "unreadable")


LSBLK = {"blockdevices": [
    {"path": "/dev/nvme0n1", "type": "disk", "size": 512 * wi.GIB, "rm": False, "hotplug": False,
     "tran": "nvme", "children": [
         {"path": "/dev/nvme0n1p1", "type": "part", "fstype": "vfat", "size": 100 << 20,
          "parttype": "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", "mountpoints": ["/boot/efi"]},
         {"path": "/dev/nvme0n1p2", "type": "part", "fstype": None, "size": 16 << 20,
          "parttype": "e3c9e316-0b5c-4db8-817d-f92df00215ae", "mountpoints": [None]},
         {"path": "/dev/nvme0n1p3", "type": "part", "fstype": "ntfs", "size": 200 * wi.GIB,
          "label": "", "uuid": "1A2B", "parttype": "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7",
          "mountpoints": [None]},
         {"path": "/dev/nvme0n1p4", "type": "part", "fstype": "ntfs", "size": 700 << 20,
          "parttype": "de94bba4-06d1-4d40-a16a-bfd50179d6ac", "mountpoints": [None]},
         {"path": "/dev/nvme0n1p5", "type": "part", "fstype": "ext4", "size": 100 * wi.GIB,
          "mountpoints": ["/"]}]},
    {"path": "/dev/sda", "type": "disk", "size": 1000 * wi.GIB, "tran": "sata", "children": [
        {"path": "/dev/sda1", "type": "part", "fstype": "BitLocker", "size": 900 * wi.GIB,
         "parttype": "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", "mountpoints": [None]}]},
    {"path": "/dev/sdb", "type": "disk", "size": 64 * wi.GIB, "rm": True, "tran": "usb", "children": [
        {"path": "/dev/sdb1", "type": "part", "fstype": "exfat", "size": 64 * wi.GIB,
         "label": "STICK", "mountpoints": ["/media/alex/STICK"]}]},
    {"path": "/dev/sdc", "type": "disk", "size": 2000 * wi.GIB, "hotplug": "1", "tran": "usb",
     "children": [{"path": "/dev/sdc1", "type": "part", "fstype": "ntfs", "size": 2000 * wi.GIB,
                   "label": "Backup", "mountpoints": [None]}]},
    {"path": "/dev/sdd", "type": "disk", "size": 8 * wi.GIB, "tran": "usb", "rm": True, "children": [
        {"path": "/dev/sdd1", "type": "part", "fstype": "vfat", "size": 8 * wi.GIB,
         "mountpoints": [None]}]},
    {"path": "/dev/loop0", "type": "loop", "fstype": "squashfs", "mountpoints": ["/snap/x"]}]}


class Volumes(unittest.TestCase):
    def test_the_drives_worth_looking_at(self):
        found = {v["device"]: v for v in wi.volumes(LSBLK)}
        self.assertEqual(set(found), {"/dev/nvme0n1p3", "/dev/sda1", "/dev/sdb1", "/dev/sdc1"})
        self.assertTrue(found["/dev/nvme0n1p3"]["windows"])
        self.assertEqual(found["/dev/nvme0n1p3"]["state"], "ok")
        self.assertEqual(found["/dev/sda1"]["state"], "bitlocker")
        self.assertEqual(found["/dev/sdb1"]["mountPoint"], "/media/alex/STICK")
        self.assertTrue(found["/dev/sdb1"]["external"])
        self.assertFalse(found["/dev/sdc1"]["windows"])
        self.assertTrue(found["/dev/sdc1"]["external"])


class Copy(Temp):
    def test_every_file_arrives_the_same(self):
        source = sample(self.tmp / "src")
        copier = self.copy(source, self.tmp / "dst/Documents")
        self.assertEqual(tree(source), tree(self.tmp / "dst/Documents"))
        report = copier.report()
        self.assertEqual((report["files"], report["failed"], report["already"]), (4, 0, 0))
        self.assertEqual(int(os.stat(self.tmp / "dst/Documents/notes.txt").st_mtime), 1_600_000_000)
        self.assertEqual(os.stat(self.tmp / "dst/Documents/notes.txt").st_mode & 0o777, 0o644)

    def test_windows_links_and_files_are_left_out(self):
        source = sample(self.tmp / "src")
        os.symlink("notes.txt", source / "link-to-notes")
        os.symlink("/etc", source / "junction")
        (source / "desktop.ini").write_text("x")
        (source / "Word.lnk").write_bytes(b"L")
        copier = self.copy(source, self.tmp / "dst")
        self.assertFalse(os.path.lexists(self.tmp / "dst/link-to-notes"))
        self.assertFalse(os.path.lexists(self.tmp / "dst/junction"))
        self.assertFalse(os.path.lexists(self.tmp / "dst/desktop.ini"))
        self.assertEqual((copier.links, copier.windows), (2, 2))

    def test_a_name_already_there_gets_a_number(self):
        (self.tmp / "src").mkdir()
        (self.tmp / "src/cv.odt").write_text("new")
        (self.tmp / "dst").mkdir()
        (self.tmp / "dst/cv.odt").write_text("old")
        (self.tmp / "dst/cv (2).odt").write_text("older")
        self.copy(self.tmp / "src", self.tmp / "dst")
        self.assertEqual((self.tmp / "dst/cv.odt").read_text(), "old")
        self.assertEqual((self.tmp / "dst/cv (3).odt").read_text(), "new")

    def test_copying_again_adds_nothing(self):
        source = sample(self.tmp / "src")
        self.copy(source, self.tmp / "dst")
        again = self.copy(source, self.tmp / "dst")
        self.assertEqual((again.files, again.already), (0, 4))
        self.assertEqual(tree(source), tree(self.tmp / "dst"))

    def test_a_file_that_reads_back_wrong_is_a_failure(self):
        source = sample(self.tmp / "src")
        real = wi._hash_of
        with mock.patch.object(wi, "_hash_of", lambda fd: ("0" * 64, real(fd)[1])):
            copier = self.copy(source, self.tmp / "dst")
        self.assertEqual((copier.report()["failed"], copier.report()["files"]), (4, 0))
        # nothing half-copied keeps a good name, and no part is left
        self.assertEqual([p.name for p in (self.tmp / "dst").rglob("*") if p.is_file()], [])

    def test_a_file_that_cannot_be_read_is_a_failure(self):
        source = sample(self.tmp / "src")
        real_open = os.open

        def refusing(path, flags, *args, **kwargs):
            if str(path).endswith("a.jpg") and not flags & os.O_WRONLY:
                raise PermissionError(13, "no")
            return real_open(path, flags, *args, **kwargs)
        with mock.patch.object(os, "open", refusing):
            copier = self.copy(source, self.tmp / "dst")
        self.assertEqual(copier.report()["failed"], 1)
        self.assertEqual(copier.report()["examples"], ["2023/trip/a.jpg"])
        self.assertEqual(copier.report()["files"], 3)

    def test_special_files_are_skipped_and_counted(self):
        (self.tmp / "src").mkdir()
        os.mkfifo(self.tmp / "src/pipe")
        copier = self.copy(self.tmp / "src", self.tmp / "dst")
        self.assertEqual((copier.special, copier.files, copier.failed), (1, 0, []))

    def test_stopping_leaves_no_half_file(self):
        (self.tmp / "src").mkdir()
        (self.tmp / "src/big.bin").write_bytes(os.urandom(3 * wi.CHUNK))
        copier = wi.Copier()
        copier.progress = lambda done: copier.stop()
        (self.tmp / "dst").mkdir()
        src = os.open(self.tmp / "src", wi.DIR_FLAGS)
        dst = os.open(self.tmp / "dst", wi.DIR_FLAGS)
        with self.assertRaises(wi.Stopped):
            copier.copy_tree(src, dst)
        os.close(src)
        os.close(dst)
        self.assertEqual(list((self.tmp / "dst").iterdir()), [])

    def test_a_part_left_by_a_crash_is_replaced(self):
        source = sample(self.tmp / "src")
        (self.tmp / "dst").mkdir()
        (self.tmp / "dst" / wi.PART_NAME).write_bytes(b"half")
        self.copy(source, self.tmp / "dst")
        self.assertEqual(tree(source), tree(self.tmp / "dst"))

    def test_a_link_in_the_receiving_folder_is_not_written_through(self):
        source = sample(self.tmp / "src")
        (self.tmp / "dst").mkdir()
        (self.tmp / "elsewhere").mkdir()
        os.symlink(self.tmp / "elsewhere", self.tmp / "dst/2023")
        copier = self.copy(source, self.tmp / "dst")
        self.assertEqual(list((self.tmp / "elsewhere").iterdir()), [])
        self.assertEqual(copier.report()["examples"], ["2023"])


class Plan(Temp):
    def plan(self, **changes):
        drive = self.tmp / "c"
        if not drive.exists():
            windows_drive(drive)
        home = self.tmp / "home"
        home.mkdir(exist_ok=True)
        plan = {"root": str(drive), "lang": "en",
                "dest": {k: str(home / wi.ENGLISH[k]) for k in wi.KINDS},
                "accounts": [{"name": "Alex", "path": "Users/Alex",
                              "folders": [{"kind": "documents", "path": "Documents"}]}]}
        plan.update(changes)
        return plan

    def test_a_good_plan(self):
        checked = wi.check_plan(self.plan(), str(self.tmp / "home"))
        self.assertEqual(checked["accounts"][0]["folders"], [{"kind": "documents", "path": "Documents"}])

    def test_bad_plans_are_refused(self):
        home = str(self.tmp / "home")
        bad = [
            {"accounts": [{"name": "A", "path": "../..", "folders": [{"kind": "documents", "path": "Documents"}]}]},
            {"accounts": [{"name": "A", "path": "Users/Alex", "folders": [{"kind": "documents", "path": "AppData"}]}]},
            {"accounts": [{"name": "A", "path": "Users/Alex", "folders": [{"kind": "system", "path": "Windows"}]}]},
            {"accounts": [{"name": "A", "path": "Users/Alex", "folders": []}]},
            {"root": "relative"},
            {"accounts": [{"name": "A", "path": "Users/Alex", "folders": [],
                           "browsers": [{"path": "AppData/../../etc/shadow"}]}]},
        ]
        for change in bad:
            with self.assertRaises(wi.BadPlan, msg=change):
                wi.check_plan(self.plan(**change), home)

    def test_receiving_folder_inside_the_source_is_refused(self):
        plan = self.plan()
        plan["dest"]["documents"] = plan["root"] + "/Users/Alex/Documents"
        with self.assertRaises(wi.BadPlan):
            wi.check_plan(plan, str(self.tmp / "home"))


class Run(Plan):
    def run_plan(self, plan):
        (self.tmp / "plan.json").write_text(json.dumps(plan))
        out = io.StringIO()
        with mock.patch.dict(os.environ, {"XDG_STATE_HOME": str(self.tmp / "state"),
                                          "HOME": str(self.tmp / "home")}):
            code = wi.run_copy(str(self.tmp / "plan.json"), out)
        events = [json.loads(line) for line in out.getvalue().splitlines()]
        return code, events

    def full(self):
        return self.plan(accounts=[{
            "name": "Alex", "path": "Users/Alex", "wallpaper": True,
            "browsers": [{"browser": "chrome", "name": "Google Chrome",
                          "path": "AppData/Local/Google/Chrome/User Data/Default/Bookmarks"},
                         {"browser": "firefox", "name": "Firefox",
                          "path": "AppData/Roaming/Mozilla/Firefox/Profiles/abcd.default-release/places.sqlite"}],
            "folders": [{"kind": "documents", "path": "Documents"},
                        {"kind": "desktop", "path": "OneDrive/Desktop"},
                        {"kind": "pictures", "path": "Pictures"}]}])

    def test_the_whole_copy(self):
        code, events = self.run_plan(self.full())
        self.assertEqual(code, 0, events)
        kinds = [e["event"] for e in events]
        self.assertEqual(kinds[0], "total")
        self.assertEqual(kinds[-1], "done")
        home = self.tmp / "home"
        drive = self.tmp / "c/Users/Alex"
        copied = tree(home / "Documents")
        self.assertEqual(copied.pop("Bookmarks from Windows.html")[:0], "")
        self.assertEqual(copied, {k: v for k, v in tree(drive / "Documents").items()
                                  if not k.endswith((".ini", ".lnk")) and k != "My Music"})
        self.assertEqual((home / "Desktop/todo.txt").read_text(), "todo")
        report = events[-1]["report"]
        self.assertEqual(report["files"], 4 + 1 + 1 + 1)
        self.assertEqual(report["windows"], 2)
        self.assertEqual(report["links"], 1)
        self.assertEqual(report["wallpaper"], str(home / "Pictures/Windows wallpaper.jpg"))
        html = (home / "Documents/Bookmarks from Windows.html").read_text()
        self.assertIn('HREF="https://rayelinux.com/"', html)
        self.assertIn("Raye &lt;home&gt;", html)
        self.assertIn("https://example.org/bread?a=1&amp;b=2", html)
        self.assertIn("https://www.mozilla.org/", html)
        self.assertNotIn("javascript:", html)
        self.assertNotIn("place:", html)
        state = json.loads((self.tmp / "state/raye-settings/windows-import/state.json").read_text())
        self.assertEqual(state["status"], "done")
        self.assertTrue((self.tmp / "state/raye-settings/windows-import/plan.json").exists())
        # the source is never written
        self.assertFalse(any(p.name == wi.PART_NAME for p in (self.tmp / "c").rglob("*")))

    def test_picking_up_after_a_stop(self):
        plan = self.full()
        calls = {"n": 0}
        real = wi.Copier.copy_file

        def stop_after_two(copier, *args, **kwargs):
            calls["n"] += 1
            if calls["n"] == 3:
                copier.stop()
            return real(copier, *args, **kwargs)
        with mock.patch.object(wi.Copier, "copy_file", stop_after_two):
            code, events = self.run_plan(plan)
        self.assertEqual(code, 3)
        self.assertEqual(events[-1]["event"], "stopped")
        state = self.tmp / "state/raye-settings/windows-import"
        self.assertEqual(json.loads((state / "state.json").read_text())["status"], "stopped")
        resumed = json.loads((state / "plan.json").read_text())
        resumed["resume"] = True
        code, events = self.run_plan(resumed)
        self.assertEqual(code, 0)
        report = events[-1]["report"]
        self.assertEqual(report["already"], 2)
        self.assertEqual(report["files"], 5)
        names = sorted(p.name for p in (self.tmp / "home").rglob("*") if p.is_file())
        self.assertEqual(len(names), len(set(names)) + 0, names)
        self.assertNotIn(wi.PART_NAME, names)

    def test_two_accounts_get_a_folder_each(self):
        windows_drive(self.tmp / "c", ("Alex", "Sam"))
        plan = self.plan(accounts=[
            {"name": "Alex", "path": "Users/Alex", "folders": [{"kind": "music", "path": "Music"},
                                                                {"kind": "pictures", "path": "Pictures"}]},
            {"name": "Sam", "path": "Users/Sam", "folders": [{"kind": "pictures", "path": "Pictures"}]}])
        code, _ = self.run_plan(plan)
        self.assertEqual(code, 0)
        self.assertTrue((self.tmp / "home/Pictures/Alex/cat.png").is_file())
        self.assertTrue((self.tmp / "home/Pictures/Sam/cat.png").is_file())

    def test_not_enough_room(self):
        with mock.patch.object(wi, "room_on", lambda paths: 1000):
            code, events = self.run_plan(self.full())
        self.assertEqual(code, 2)
        self.assertEqual(events[-1]["event"], "refused")
        self.assertEqual(events[-1]["reason"], "room")
        self.assertFalse((self.tmp / "home/Documents").exists())

    def test_hibernated_at_copy_time(self):
        plan = self.full()
        (pathlib.Path(plan["root"]) / "hiberfil.sys").write_bytes(b"hibr" + b"\0" * 12)
        code, events = self.run_plan(plan)
        self.assertEqual((code, events[-1]["reason"]), (2, "hibernated"))

    def test_a_bookmarks_file_nested_too_deep_is_only_said(self):
        plan = self.full()
        folder = '{"type": "folder", "name": "f", "children": ['
        deep = folder * 5000 + '{"type": "url", "name": "x", "url": "https://example.org/"}' + "]}" * 5000
        chrome = pathlib.Path(plan["root"]) / "Users/Alex/AppData/Local/Google/Chrome/User Data/Default/Bookmarks"
        chrome.write_text('{"roots": {"bookmark_bar": %s}}' % deep)
        code, events = self.run_plan(plan)
        self.assertEqual(code, 0)
        self.assertIn("Google Chrome", events[-1]["report"]["bookmarksFailed"])
        self.assertTrue((self.tmp / "home/Documents/notes.txt").exists())

    def test_a_broken_plan_says_so(self):
        code, events = self.run_plan({"root": "nope"})
        self.assertEqual((code, events[-1]["event"], events[-1]["reason"]), (1, "error", "plan"))


if __name__ == "__main__":
    unittest.main()
