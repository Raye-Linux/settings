#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Accounts > Files from Windows: the helper that copies, on real files
# (tests/windowsimport/test_engine.py), and the read-only mount on real
# NTFS images on loop devices (test_images.py, as root; skipped otherwise).
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
export SOURCE_DIR
cd "$SOURCE_DIR/tests/windowsimport" || exit 1
exec python3 -B -m unittest test_engine test_images
