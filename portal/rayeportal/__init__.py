# SPDX-License-Identifier: GPL-3.0-or-later
"""Raye Device Portal: manage this PC from a browser on the same network."""
import os

_INSTALLED = os.path.abspath(__file__).startswith("/usr/")


def setting(name, default):
    """A RAYE_PORTAL_* variable. Only the tests set them: the installed
    portal reads none, so the account's environment cannot move it."""
    return default if _INSTALLED else os.environ.get(name) or default
