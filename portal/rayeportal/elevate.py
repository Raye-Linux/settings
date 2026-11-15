# SPDX-License-Identifier: GPL-3.0-or-later
# Root actions of the portal take the sudo password the person typed in the
# page and give it to sudo on stdin. pkexec is not used: from a unit of the
# user's manager it asked the agent of the graphical session, so the prompt
# showed on the PC and the typed password was never used. Nothing of the
# portal runs as root.
import os
import subprocess
import sys

from . import system

RESULTS = ("ok", "denied", "failed", "unavailable")


def no_new_privileges():
    for line in system._read("/proc/self/status").splitlines():
        if line.startswith("NoNewPrivs:"):
            return line.split()[1] == "1"
    return False


def command(args):
    # -k: never use or leave a cached sudo ticket, every action asks
    return ["sudo", "-S", "-k", "-p", "", "--",
            os.path.join(system.LIBEXEC, "portal-power")] + [str(a) for a in args]


def outside_command(args):
    # The portal's unit forbids setuid programs (NoNewPrivileges), so sudo
    # runs in a unit of the user's manager; only the password and the answer
    # cross over, on pipes.
    return ["systemd-run", "--user", "--pipe", "--wait", "--quiet", "--collect",
            "--service-type=exec", "--", os.path.join(system.LIBEXEC, "device-portal"),
            "elevate"] + [str(a) for a in args]


def run(args, password, timeout=60, argv=None):
    """Run portal-power with these arguments. Returns (result, why): result
    is "ok", "denied" (wrong password or no sudo right), "failed", or
    "unavailable" (the password could not even be asked); why is a short
    line for the connection log."""
    if argv is None and no_new_privileges():
        return run_outside(args, password, timeout)
    return run_here(args, password, timeout, argv)


def run_outside(args, password, timeout=60, argv=None):
    try:
        done = subprocess.run(argv or outside_command(args), input=password + "\n", capture_output=True,
                              text=True, timeout=timeout + 15)
    except FileNotFoundError:
        return "unavailable", "systemd-run is missing"
    except subprocess.TimeoutExpired:
        return "failed", "no answer in time"
    lines = done.stdout.strip().splitlines()
    word, _, why = (lines[-1] if lines else "").partition(" ")
    if word in RESULTS:
        return word, why
    error = done.stderr.strip().splitlines()
    return "unavailable", (error[-1] if error else f"exit {done.returncode}")[:200]


def run_here(args, password, timeout=60, argv=None):
    env = dict(os.environ, LC_ALL="C", LANGUAGE="C")
    try:
        done = subprocess.run(argv or command(args), input=password + "\n", capture_output=True,
                              text=True, env=env, timeout=timeout)
    except OSError as error:
        return "unavailable", f"{error.strerror}: {error.filename}"
    except subprocess.TimeoutExpired:
        return "failed", "no answer in time"
    if done.returncode == 0:
        return "ok", ""
    err = done.stderr
    if "incorrect password" in err or "Sorry, try again" in err:
        return "denied", "wrong password"
    if "not in the sudoers file" in err or "not allowed to execute" in err:
        return "denied", "not allowed"
    return "failed", _last_line(err) or f"exit {done.returncode}"


def _last_line(text):
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1][:200] if lines else ""


def main(args):
    """`device-portal elevate ARGS`, started by run_outside: the password
    on stdin, one answer line on stdout."""
    password = sys.stdin.readline().rstrip("\n")
    result, why = run_here(args, password)
    print(f"{result} {why}".rstrip(), flush=True)
    return 0 if result == "ok" else 1
