# Beat It

Beat It is a custom firmware for the M-VAVE FM-1, built on SLOOP (isod89/sloop-fm1, GPL-3.0-only). It keeps the drums
and the two FM engines, adds a purple-and-black theme, a BEAT IT start-up screen, a full-screen scope with SCOPE VIEW
presets, a dedicated MIDI menu, and (from 1.1) Bluetooth MIDI using Optimist's open-source driver, with a backup of the
unit's radio calibration. 1.2 added following a Bluetooth clock; 1.3 added sending the clock over Bluetooth (CLOCK OUT BT / USB+BT)
and ends held notes when BT MIDI SEND is turned off; 1.4 adds BT STATUS (a SysEx question any connected app can ask)
and a Linux Bluetooth test script, `tools/beatit-bt-test.sh` ([docs/BEAT-IT-LINUX-BT-TEST.md](docs/BEAT-IT-LINUX-BT-TEST.md)); 1.5 keeps the second
core running with Bluetooth on, using Optimist's order for it: the radio starts at power-on when BT MIDI was saved ON,
before the audio; no core 1 job for 400 ms after a radio start; core 1 parked during flash writes; the audio interrupt
entries in RAM. The opt-in STOCK SUPPLY test build (`BEATIT_STOCK_SUPPLY=1`) also runs M-VAVE's own supply start-up.
BT STATUS says 1.5, flags a radio started at power-on (64), and flags 16 if core 1 was stopped after a fault.

**Status:** builds and passes its tests in a simulator. It has not run on a real FM-1, and Bluetooth has never been
tried over a real radio.

## Automatic builds

The install steps, the first Bluetooth test and how to go back are in [docs/BEAT-IT-INSTALL.md](docs/BEAT-IT-INSTALL.md).

Every push runs `.github/workflows/build.yml` on GitHub's Linux machines. When a run finishes, open it from the
**Actions** tab and download **packages** under Artifacts. It holds:

- `Beat-It-X.Y-installer.html`: open it in Chrome or Edge on Windows and press INSTALL (see the install guide)
- `Beat-It-X.Y.fwsc`: the firmware on its own, for SLOOP's Python installer or the Firmware Hub
- `Beat-It-X.Y-linux-bt-test.tar.gz`: the Linux Bluetooth test script and its guide
- `SHA256SUMS`

Pushing a tag such as `v1.2` also puts those files on a release (the **Releases** page).

The build step is `tools/ci_build_beatit.sh` (`build.sh`, then the installer page). The tests run on every build but
do not stop it, because some SLOOP tests cover sounds Beat It removed and are expected to fail. The Bluetooth and
calibration tests (`tools/ci_bt_tests.sh`) run as their own step first, and that step must pass.

## Before installing

Back up your projects first: Beat It can't open projects from the official firmware. To go back, use M-VAVE's
M-UPGRADE with the official FM-1 firmware, or the "Back to official" button on The Firmware Hub.
