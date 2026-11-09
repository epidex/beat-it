#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# (Beat It) the CI build: the firmware (./build.sh, the same plain build as the hand-made releases), then the two files
# a user installs, named after the version on the boot screen (BEATIT_VERSION in firmware/src/ui.c):
#   build/Beat-It-X.Y.fwsc             the firmware package (SLOOP's Python installer, the Firmware Hub)
#   build/Beat-It-X.Y-installer.html   the one-file Windows installer (Chrome or Edge, no build tools)
set -e
cd "$(dirname "$0")/.."
sh build.sh
V="$(sed -n 's/^#define BEATIT_VERSION "BEAT IT \([0-9][0-9.]*\).*/\1/p' firmware/src/ui.c)"
[ -n "$V" ] || { echo "ci_build_beatit.sh: no BEATIT_VERSION in firmware/src/ui.c"; exit 1; }
rm -f build/Beat-It-*
cp build/felucca.fwsc "build/Beat-It-$V.fwsc"
python3 web/make_beatit_installer.py build/felucca.fwsc "$V" "build/Beat-It-$V-installer.html"
sha256sum build/Beat-It-*
