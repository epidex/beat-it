#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# (Beat It) the CI build: the firmware (./build.sh, the same plain build as the hand-made releases), then the two files
# a user installs, named after the version on the boot screen (BEATIT_VERSION in firmware/src/ui.c):
#   build/Beat-It-X.Y.fwsc             the firmware package (SLOOP's Python installer, the Firmware Hub)
#   build/Beat-It-X.Y-installer.html   the one-file Windows installer (Chrome or Edge, no build tools)
#   build/Beat-It-X.Y-linux-bt-test.tar.gz   (1.4) the Linux Bluetooth test (docs/BEAT-IT-LINUX-BT-TEST.md)
set -e
cd "$(dirname "$0")/.."
sh build.sh
V="$(sed -n 's/^#define BEATIT_VERSION "BEAT IT \([0-9][0-9.]*\).*/\1/p' firmware/src/ui.c)"
[ -n "$V" ] || { echo "ci_build_beatit.sh: no BEATIT_VERSION in firmware/src/ui.c"; exit 1; }
rm -f build/Beat-It-*
cp build/felucca.fwsc "build/Beat-It-$V.fwsc"
python3 web/make_beatit_installer.py build/felucca.fwsc "$V" "build/Beat-It-$V-installer.html"
T="build/beat-it-$V-bt-test"
rm -rf "$T" && mkdir -p "$T/tools"
cp tools/beatit_bt_test.py tools/beatit-bt-test.sh "$T/tools/" && cp docs/BEAT-IT-LINUX-BT-TEST.md "$T/README.md"
chmod +x "$T/tools/beatit_bt_test.py" "$T/tools/beatit-bt-test.sh"
tar -C build --owner=0 --group=0 --sort=name --mtime='2026-01-01 00:00Z' -czf "build/Beat-It-$V-linux-bt-test.tar.gz" "beat-it-$V-bt-test"
rm -rf "$T"
sha256sum build/Beat-It-*
