# Beat It 1.4 install guide

Beat It 1.4 lets a computer or app ask the FM-1 how its Bluetooth is doing, and comes with a free Linux script that runs the whole Bluetooth test for you and saves a log. It builds cleanly and passes its tests in a simulator, but **it has never run on a real FM-1, and nothing here has ever transmitted over a real radio.** Bluetooth may still not work on the hardware. Treat it as experimental and back up first.

## What's new in 1.4

- **BT STATUS over Bluetooth.** When a connected computer or app sends the SysEx message `F0 7D 42 49 01 F7`, the FM-1 answers with one SysEx message. It holds what BT CONNECTION shows (RADIO CAL, IN, OUT, LOST, the RESET DURING step) plus what the screen can't show: the connection interval, timeout, MTU and data length the link actually agreed on, and whether it's encrypted. It answers only while BT MIDI RECEIVE or SEND is on. It's never played and doesn't count toward IN or OUT. Every other SysEx over Bluetooth is still ignored.
- **A Linux Bluetooth test script:** `tools/beatit-bt-test.sh`. It finds the FM-1, connects, and steps through receive, send, stuck notes, clock out, clock in and a 600-note stress test. With 1.4 it checks the FM-1's own counts against what was sent, and writes everything to a log file you can send back. It's free and needs no account or subscription. See [BEAT-IT-LINUX-BT-TEST.md](BEAT-IT-LINUX-BT-TEST.md).
- **Version:** the start-up screen and ABOUT say "BEAT IT 1.4 BT".
- Nothing else changed from 1.3. Notes, the clock both ways, stuck-note handling, the queue and the calibration backup all work the same.

## From 1.3

- **The FM-1 can send its clock over Bluetooth.** Menu > MIDI > CLOCK OUT used to be OFF or OUT (USB only). It now has four choices: **OFF**, **USB**, **BT** and **USB+BT**.
  - With BT or USB+BT, a phone or DAW app connected over Bluetooth gets the FM-1's tempo (24 pulses a beat), plus START when you press play and STOP when you stop.
  - Each clock pulse carries the time it was made, so the receiving app can time it by when it was sent, not by when Bluetooth delivered it.
  - It only goes out over Bluetooth while **BT MIDI SEND** is on and an app is connected. With SEND off, USB+BT acts as USB.
  - **If an app connects while the FM-1 is already playing,** it gets START straight away, and USB isn't restarted. If the app disconnects, USB carries on as normal.
  - Like before, the FM-1 only sends a clock when it's the master (CLOCK IN set to INT). If it's following a clock, it never sends one back, so there's no loop through a phone.
  - Projects that had CLOCK OUT set to OUT open with it set to USB, so nothing changes for them.
- **Turning BT MIDI SEND off no longer leaves notes stuck on the phone.** In 1.2, switching SEND off just went quiet, so any note the FM-1 was holding stayed on in the phone app, and an app following the Bluetooth clock kept running. Now, before it goes quiet, it sends a note-off for each note still on (ALL NOTES OFF for a channel with more than four), and STOP if the Bluetooth clock had started the app. This happens while BT MIDI RECEIVE stays on. Turning both off closes the link completely.
- **A slow link drops the right messages.** When the phone can't keep up (for example on a long connection interval with the clock going out as well), the outgoing queue keeps its last 16 places for messages that end a sound: note-offs, sustain pedal up, ALL NOTES OFF and the clock's STOP. So a congested link drops a clock pulse or a new note, not the note-off that would leave a note hanging.
- **LOST now counts both ways.** The line under BT CONNECTION shows **LOST (n)** in amber when messages couldn't be sent as well as when they couldn't be received.
- **Fix: CLOCK IN = BT was missing from 1.2 on the FM-1.** In the 1.2 firmware file, the Bluetooth switch was set too late in the build, so the menu was built as if Bluetooth didn't exist. CLOCK IN only offered INT, USB and TRS, and you couldn't choose BT. The simulator tests turned Bluetooth on themselves, so they didn't catch it. 1.3 sets it in the right place, and the build now stops with an error if a Bluetooth build is missing the BT choices for CLOCK IN or CLOCK OUT. Nothing else was affected: Bluetooth notes in and out were built in 1.2 as intended.

## What hasn't changed

- Drums, both FM engines (DIGITAL and FM6), the purple-and-black theme, the BEAT IT start-up screen, the full-screen scope, the SCOPE VIEW presets (FULL SCREEN, NORMAL, OFF), SCOPE COLOUR, and the dedicated MIDI and BLUETOOTH menus.
- Everything from 1.3 and 1.2: following a Bluetooth clock (CLOCK IN = BT, now actually in the menu), the RESET DURING line, the IN and OUT counts, and the menu updating by itself.
- **Bluetooth is off at every power-on.** It only starts when you turn on BT MIDI RECEIVE or SEND.
- **Turning Bluetooth on stops the second processor core until you restart.** Heavy patches may glitch. ABOUT shows "1 CORE" while this is in effect.
- **The fourth user sample slot (USR4) stays locked.** It sits on the memory that holds your radio calibration.
- **Radio calibration:** Beat It still never writes to the stock calibration area, backs it up to both settings copies when it finds it, uses the backup if it's lost, and never makes calibration up. If **RADIO CAL** says **MISSING**, Bluetooth can't start on this firmware, but everything else works. Your earlier installs may already have erased it, and nothing can bring it back.
- **Going back to 1.3:** nothing to do; 1.4 saves settings exactly as 1.3 does.
- **Going back to 1.2:** 1.3 and later store the CLOCK OUT BT choice in a spare bit next to the REMOTE setting. If you install 1.2 again after using CLOCK OUT BT, REMOTE goes back to OFF once. Set it again if you use it.

## Install it from a Windows PC

You need Chrome or Edge and a USB data cable. You don't need Linux or any build tools.

1. **Back up first.** If your FM-1 is on SLOOP or Beat It, use SLOOP's editor and choose Backup. If it's on the official firmware, use M-VAVE's [FM-1 Workbench](https://open.m-vave.com/fm1-workbench).
2. Save **Beat-It-1.4-installer.html** to your PC and double-click it so it opens in Chrome or Edge.
3. Plug the FM-1 straight into the PC, not through a hub. Close any other app that uses MIDI.
4. Wait for the page to say "Beat It 1.4", then press **INSTALL**. Allow MIDI access when asked, and wait for "Done". The page says SLOOP in places because it's SLOOP's installer with Beat It packed inside.
5. The FM-1 restarts on the BEAT IT screen. Unplug it and plug it back in once so Windows finds its USB audio.
6. Open **Menu > BLUETOOTH** and check **RADIO CAL** first. STOCK or BACKUP means Bluetooth can try to start. On STOCK, leave the FM-1 on for a few seconds until it says BACKED UP.

**Beat-It-1.4.fwsc** is the same firmware on its own, for SLOOP's Python installer or the Firmware Hub. GitHub's automatic build also builds 1.4 from the same code, and its copies are on the [v1.4 release](https://github.com/epidex/beat-it/releases/tag/v1.4) of your repository, with **Beat-It-1.4-linux-bt-test.tar.gz**. They aren't byte-for-byte identical to the files here, because the build machine is different. Use either set, not a mix.

## First Bluetooth test, step by step

**If you have a Linux computer with Bluetooth,** the script does all of this for you and saves a log: see [BEAT-IT-LINUX-BT-TEST.md](BEAT-IT-LINUX-BT-TEST.md). Otherwise, with a phone, do these in order. Each one tells us something, even if it fails.

1. **RADIO CAL:** write down what it says (STOCK, BACKUP or MISSING). If it's MISSING, stop here. Bluetooth can't start.
2. **Turn on BT MIDI RECEIVE.** Watch BT CONNECTION for about 10 seconds.
   - **ADVERTISING:** the radio started.
   - **NO RF CAL:** there's no calibration.
   - **The FM-1 freezes or restarts:** power it off and on, open the BLUETOOTH menu, and write down the **RESET DURING** line. That's the step where it hung.
3. **On your phone,** open a Bluetooth MIDI app and look for "BEAT IT" followed by four characters. Connect from inside the app, not from the phone's Bluetooth settings.
   - **If the phone can't see it** while BT CONNECTION says ADVERTISING, the radio is on but not reaching the phone. That's a radio or calibration problem, not a menu problem.
4. **When BT CONNECTION says CONNECTED,** play a few notes from the phone. The line under it should count up (IN 1, IN 2...) and the FM-1 should play them.
   - **If IN counts up but there's no sound,** check the track's MIDI channel in the MIDI menu.
5. **To test sending,** turn on BT MIDI SEND and play pads on the FM-1. OUT should count up and the phone app should show notes arriving.
6. **To test stuck notes:** hold a pad so a note is sounding on the phone, then turn BT MIDI SEND off. The note should stop on the phone.
7. **To test sending the clock:** turn SEND back on, set Menu > MIDI > CLOCK IN to **INT** and CLOCK OUT to **BT**, then open an app that can follow MIDI clock over Bluetooth. Press play on the FM-1: the app should start and match the FM-1's TEMPO. Press stop: it should stop.
8. **To test following a clock:** set CLOCK IN to **BT**, then press play in a DAW app that sends MIDI clock over Bluetooth. TEMPO should change to match the app, and the sequencer should start and stop with it.

Send me what each step showed: the RADIO CAL value, the BT CONNECTION value, any RESET DURING line, the IN and OUT counts, and any LOST count. A photo of the screen works. That's enough to work out the next fix.

## If something goes wrong

- **The FM-1 freezes or crashes with Bluetooth on:** power it off and on. Bluetooth starts off again, so it should boot normally. Check the RESET DURING line, then leave Bluetooth off.
- **The install stops partway:** open the installer page again and press INSTALL. It picks up where it left off.
- **The FM-1 won't start:** hold **OCT-** while you turn it on to enter USB rescue mode. Then run the installer again (1.4, 1.3, or **Beat-It-1.0-installer.html** for the build without Bluetooth), or use "Back to official" on [The Firmware Hub](https://thefirmwarehub.com/).
- **Going back to the official firmware:** use M-VAVE's **M-UPGRADE** (m-vave.com, Downloads, PC Software) with the official FM-1 firmware file. If M-UPGRADE can't find the FM-1, use "Return to the official firmware" on [Clipper's installer page](https://isod89.github.io/clipper-fm1/webapp/installer/) with that same file.
- **Worst case:** the community [unbricker tool](https://github.com/Quixotic7/MvaveFM1Unbricker) restores the FM-1 over USB.

## What was tested

All of this ran in a simulator on my Linux workspace. None of it ran on a real FM-1, and no radio was used.

- **Firmware build:** built cleanly with no warnings. The firmware is 398,016 bytes, 856 more than 1.2, and uses 65,612 of 98,304 bytes of RAM. It still fits.
- **Installer page:** the firmware packed inside it matches the .fwsc file byte for byte.
- **Bluetooth and calibration:** every Bluetooth and calibration check passed, with no failures, including 24 new ones for 1.3. They cover:
  - CLOCK OUT BT: 24 pulses a beat at 120 BPM to Bluetooth only, START then the first pulse when play starts, each pulse stamped with its time and in order, STOP when play stops, nothing on USB
  - CLOCK OUT USB+BT: an app that connects mid-song gets START without USB restarting; one that leaves is dropped without a STOP into nowhere; one that comes back gets START again
  - switching CLOCK IN to follow a clock while playing: STOP to both, then nothing sent
  - SEND off: no clock goes to Bluetooth
  - SEND off while notes are held: the right note-offs, ALL NOTES OFF for a busy channel, STOP, and nothing after; SEND back on while playing sends START again
  - a flooded queue keeps room for note-offs and sustain-pedal-up, counts LOST, and shows it in amber
  - CLOCK OUT saved and loaded back correctly for all four choices, with REMOTE unchanged
  - the CLOCK OUT menu drawn and checked showing BT and USB+BT (picture included)
- **The whole suite:** 72 test groups ran, the same as 1.2. The only failures are the same ones that fail on Beat It 1.0, 1.1 and 1.2, all for sounds Beat It removed: the sample-chop click test, the switch to the SAMPLE engine, and the reference renders of removed presets. Nothing new failed in the drums, the FM engines, the sequencer, MIDI, the menus, the settings or the installer.

## What only your FM-1 can tell us

- whether the radio starts on the real chip and a phone can see "BEAT IT"
- whether notes and clock arrive reliably both ways, and how much delay you can hear
- whether the phone app keeps steady time from the FM-1's Bluetooth clock
- whether one core is enough for your patches with Bluetooth on
- whether your unit still has its stock radio calibration

The code changes, applied to Beat It 1.2, are in **beat-it-1.3-bluetooth.patch**. **beat-it-1.3-test-log.txt** has the full test output.
