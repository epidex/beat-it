# Beat It

Beat It is a custom firmware for the M-VAVE FM-1, built on SLOOP (isod89/sloop-fm1, GPL-3.0-only). It keeps the drums
and the two FM engines, adds a purple-and-black theme, a BEAT IT start-up screen, a full-screen scope with SCOPE VIEW
presets, a dedicated MIDI menu, and (from 1.1) Bluetooth MIDI using Optimist's open-source driver, with a backup of the
unit's radio calibration.

**Status:** builds and passes its tests in a simulator. It has not run on a real FM-1, and Bluetooth has never been
tried over a real radio.

## Automatic builds

The install steps, the first Bluetooth test and how to go back are in [docs/BEAT-IT-INSTALL.md](docs/BEAT-IT-INSTALL.md).

Every push runs `.github/workflows/build.yml` on GitHub's Linux machines. When a run finishes, open it from the
**Actions** tab and download **packages** under Artifacts. It holds:

- `Beat-It-X.Y-installer.html`: open it in Chrome or Edge on Windows and press INSTALL (see the install guide)
- `Beat-It-X.Y.fwsc`: the firmware on its own, for SLOOP's Python installer or the Firmware Hub
- `SHA256SUMS`

Pushing a tag such as `v1.2` also puts those files on a release (the **Releases** page).

The build step is `tools/ci_build_beatit.sh` (`build.sh`, then the installer page). The tests run on every build but
do not stop it, because some SLOOP tests cover sounds Beat It removed and are expected to fail.

## Before installing

Back up your projects first: Beat It can't open projects from the official firmware. To go back, use M-VAVE's
M-UPGRADE with the official FM-1 firmware, or the "Back to official" button on The Firmware Hub.
