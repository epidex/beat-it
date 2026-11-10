#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Beat It Bluetooth test, the easy way, on Linux: sets up its own Python environment the first time (the free
# `bleak` Bluetooth package, in ~/.beatit-bt-venv, nothing installed system-wide), then runs beatit_bt_test.py.
#
#   sh tools/beatit-bt-test.sh                  every step, asks you what to do on the FM-1 as it goes
#   sh tools/beatit-bt-test.sh --steps status   only connect and read BT STATUS
#   sh tools/beatit-bt-test.sh --selftest       no Bluetooth: checks the script itself against a simulated FM-1
#   sh tools/beatit-bt-test.sh --help           every option
#
# No account, no subscription, no sudo for the test itself. It never pairs, flashes or erases anything.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
VENV="${BEATIT_BT_VENV:-$HOME/.beatit-bt-venv}"
BLEAK="bleak>=0.22,<4"

command -v python3 >/dev/null 2>&1 || {
    echo "python3 is missing. Debian/Ubuntu/Mint/Raspberry Pi OS: sudo apt install python3 python3-venv"
    echo "Fedora: sudo dnf install python3    Arch: sudo pacman -S python"
    exit 3
}
if [ ! -x "$VENV/bin/python" ]; then
    echo "First run: making a Python environment in $VENV for the bleak package ..."
    python3 -m venv "$VENV" 2>/dev/null || {
        rm -rf "$VENV"
        echo "python3 can't make an environment. Debian/Ubuntu/Mint/Raspberry Pi OS: sudo apt install python3-venv"
        exit 3
    }
fi
if ! "$VENV/bin/python" -c "import bleak" 2>/dev/null; then
    echo "Installing bleak (the Bluetooth LE package, free, from PyPI) ..."
    "$VENV/bin/python" -m pip install --quiet --upgrade pip >/dev/null 2>&1 || true
    "$VENV/bin/python" -m pip install --quiet "$BLEAK" || {
        echo "Could not install bleak (no internet?). Try again, or: $VENV/bin/python -m pip install '$BLEAK'"
        exit 3
    }
fi
if ! command -v bluetoothctl >/dev/null 2>&1; then
    echo "(bluetoothctl not found: Bluetooth may not be set up. Debian/Ubuntu: sudo apt install bluez)"
fi
exec "$VENV/bin/python" "$HERE/beatit_bt_test.py" "$@"
