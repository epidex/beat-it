# Beat It 2.0

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**Custom firmware for the M-VAVE FM-1**, a fork of [Melodee](https://github.com/keremimo/melodee) 1.6.1 by Kerem Kilic / Ellic Studio (GPL-3.0-only). Beat It 2.0 keeps all of Melodee, including its Bluetooth MIDI and second-core code, unchanged, and adds:

- the Beat It 2.0 name everywhere, with the Bluetooth name **BEAT IT XXXX** (XXXX is unique to your unit)
- a purple-on-black theme, on by default
- five saved screen brightness levels (MENU > BRIGHTNESS: 100, 70, 50, 30, 15%), stored in two spare bytes of Melodee's settings, not in a shared ZOOM slot
- a full-screen oscilloscope as a third HOME page, with colour following pitch: red-magenta for low notes, through purple, to cyan for high ones

> **Status: simulator-tested only. Beat It 2.0 has not been run on a real FM-1 yet.** Install it at your own risk and treat the first install as a test.
>
> - **Bluetooth is unproven on hardware.** The simulator cannot show whether the radio really broadcasts as BEAT IT XXXX, or whether Bluetooth MIDI receive (what crashed Beat It 1.6) stays stable.
> - **Second core:** the simulator cannot reproduce the second-core memory misreads one FM-1 had on Beat It 1.6. Only a real unit can show those.

## Install (Windows, Chrome or Edge)

- **Open the installer straight from the web:** [epidex.github.io/beat-it](https://epidex.github.io/beat-it/), or
- **Download it:** `Beat-It-2.0-installer.html` from the [Beat It 2.0 release](https://github.com/epidex/beat-it/releases/tag/v2.0), then double-click it. The firmware is built into the page, so it works offline once open.

Run it on Windows itself, not inside WSL/Ubuntu. Plug the FM-1 straight into the PC, close other MIDI apps, click **Find FM-1**, allow MIDI access, then **Install Beat It**. The installer backs up the whole FM-1 before it writes anything; keep that backup file safe.

The full steps, the first Bluetooth tests (nRF Connect and midimittr) and how to get back are in the **[Beat It 2.0 install guide](docs/BEAT-IT-2.0-INSTALL.md)**.

## Getting back

- **Rescue:** hold **OCT− on its own** while switching on. Three short key-light flashes mean the update loader is ready; reconnect USB and run the installer again.
- **Back to Bump It 1.1:** in the installer use **Back up and return to official V15**, then install Bump It 1.1 with its own installer.

## How it was tested (on the computer only)

- Clean build: 97,288 of 98,304 bytes of RAM used (1,016 free; plain Melodee leaves 1,112).
- Full test suite run twice: 193 of 194 sections passed both times. The one failure, the "CPU live takes" timing test, fails the same way on plain Melodee and passed 3 of 3 times on its own.
- Stress runs: 29 test programs, 40 runs each, 1,160 runs, 0 failures.
- Voltage: in the FM-1 simulator, the start-up power-supply settings are identical to M-VAVE's stock V15 and plain Melodee. Nothing goes above stock.

## Where things are

- `beat-it-2.0` branch (this one): the Beat It 2.0 source, with Melodee's full history underneath. Beat It 1.0 to 1.5 (SLOOP-based) stay on `main`, with their own releases.
- Build: see [BUILDING.md](BUILDING.md) (`./build.sh`).
- Melodee's original README: [docs/MELODEE-README.md](docs/MELODEE-README.md). Melodee's GitHub workflows (which publish Melodee's own pages and releases) are removed from this branch.

## Licence and credit

Beat It 2.0 is a modified version of Melodee 1.6.1 by Kerem Kilic (Ellic Studio), which is itself a modified version of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments. It is free software under the GNU General Public License, version 3 only: see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md); bundled parts with their own licences are listed there and in `LICENSES/`. Beat It is not affiliated with or endorsed by Melodee or Hügelton Instruments: please report Beat It problems here, not to them.
