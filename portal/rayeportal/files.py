# SPDX-License-Identifier: GPL-3.0-or-later
# The Files page: the user's home folder and nothing above it.
import os
import stat

MAX_UPLOAD = 4 * 1024**3


class OutsideHome(Exception):
    pass


def resolve(home, relative, closed=()):
    """The real path of `relative` inside `home`. Links are followed
    first, so a link that points out of the home folder is refused too.
    The `closed` folders (the portal's own) are refused whatever the way in."""
    root = os.path.realpath(home)
    relative = (relative or "").replace("\\", "/")
    if "\0" in relative:
        raise OutsideHome(relative)
    path = os.path.realpath(os.path.join(root, relative.lstrip("/")))
    if path != root and not path.startswith(root + os.sep):
        raise OutsideHome(relative)
    for folder in closed:
        folder = os.path.realpath(folder)
        if path == folder or path.startswith(folder + os.sep):
            raise OutsideHome(relative)
    return path


def upload_folder(home, relative, closed=()):
    """Where an upload may land: not in a hidden folder, where files
    like ~/.config/autostart/*.desktop would run at the next sign-in."""
    path = resolve(home, relative, closed)
    rel = relative_to(home, path)
    if any(part.startswith(".") for part in rel.split(os.sep) if part):
        raise OutsideHome(relative)
    return path


def relative_to(home, path):
    rel = os.path.relpath(path, os.path.realpath(home))
    return "" if rel == "." else rel


def listing(home, relative, closed=()):
    folder = resolve(home, relative, closed)
    entries = []
    with os.scandir(folder) as it:
        for entry in it:
            try:
                resolve(home, os.path.join(relative_to(home, folder), entry.name), closed)
                info = entry.stat(follow_symlinks=True)
            except (OSError, OutsideHome):
                continue
            if stat.S_ISDIR(info.st_mode):
                kind = "folder"
            elif stat.S_ISREG(info.st_mode):
                kind = "file"
            else:
                continue
            entries.append({"name": entry.name, "kind": kind, "size": info.st_size,
                            "modified": int(info.st_mtime), "hidden": entry.name.startswith(".")})
    entries.sort(key=lambda e: (e["kind"] != "folder", e["name"].casefold()))
    return {"path": relative_to(home, folder), "entries": entries}


def upload_name(name):
    name = os.path.basename((name or "").replace("\\", "/")).strip()
    if not name or name.startswith(".") or "\0" in name or len(name.encode()) > 255:
        raise ValueError("bad file name")
    return name


def open_new(folder, name):
    """Open a new file in folder for writing, never over an existing
    one: "report.pdf" becomes "report (2).pdf"."""
    stem, dot, ext = name.rpartition(".") if "." in name[1:] else (name, "", "")
    for n in range(1, 1000):
        candidate = name if n == 1 else f"{stem} ({n}){dot}{ext}"
        path = os.path.join(folder, candidate)
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
            return fd, path
        except FileExistsError:
            continue
    raise FileExistsError(name)
