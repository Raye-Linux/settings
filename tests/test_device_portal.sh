#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Device Portal's own tests (portal/tests): sign-in, pairing, the local
# network rule, CSRF, throttling, sessions, paths out of the home folder,
# over real HTTPS on 127.0.0.1.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
cd "$SOURCE_DIR/portal" || exit 1
export PYTHONDONTWRITEBYTECODE=1
exec python3 -m unittest discover -s tests -v
