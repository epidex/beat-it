# Beat It 1.2 install guide

Beat It 1.2 is Beat It 1.1 with four fixes and additions to Bluetooth MIDI. It builds cleanly and passes its tests in a simulator, but **it has never run on a real FM-1, and nothing here has ever transmitted over a real radio.** Bluetooth may still not work on the hardware. Treat it as experimental and back up first.

## What's new in 1.2

- **The FM-1 can follow a Bluetooth clock.** Menu > MIDI > CLOCK IN now has a fourth choice, **BT**. With it, a DAW or phone app connected over Bluetooth sets the tempo and starts and stops the sequencer, the same way USB and TRS already did.
  - Bluetooth bunches MIDI messages into bursts every 7.5 to 45 ms, so timing clock pulses by when they arrive would make the tempo wobble. Beat It 1.2 times each pulse by the timestamp the sender puts on it, using the method from Optimist 0.2's Bluetooth code.
  - In the test, with a 15 ms connection interval and packets arriving up to 10 ms late, each pulse was placed within 2 ms of when it was sent. Timing them by arrival would have been up to 25 ms off. With a 45 ms interval it was still within 2 ms, against 65 ms by arrival.
  - When CLOCK IN isn't BT, clock messages from Bluetooth are dropped straight away, so they don't fill the queue that notes go through.
  - If CLOCK IN is BT but BT MIDI RECEIVE is off, the MIDI menu says **BT RECEIVE IS OFF** in amber.
- **BT CONNECTION now says more.** A second line under it shows:
  - **RESET DURING: (step)** in amber, if the FM-1 reset while the radio was starting up or re-advertising. It names the step the radio was in, such as RF INIT or ADVERT. This tells us where a hang happened on your unit. It clears as soon as you turn Bluetooth on again.
  - **IN (n) OUT (n)** once you're connected and messages are moving, so you can see notes are getting through. **LOST (n)** appears in amber if messages came in faster than the FM-1 could take them.
- **The menu updates by itself.** In 1.1, the BLUETOOTH menu didn't redraw when a phone connected or when RADIO CAL finished backing up, until you turned a knob. Now it updates on its own.
- **Fix: turning BT MIDI SEND off.** In 1.1, turning SEND off cleared the outgoing queue from the main loop while the Bluetooth interrupt could be reading it at the same moment. In rare cases that could leave the queue broken and send junk. Now only the Bluetooth side empties it.
- **Version:** the start-up screen and ABOUT say "BEAT IT 1.2 BT".

## What hasn't changed

- Drums, both FM engines (DIGITAL and FM6), the purple-and-black theme, the BEAT IT start-up screen, the full-screen scope, the SCOPE VIEW presets (FULL SCREEN, NORMAL, OFF), SCOPE COLOUR, and the dedicated MIDI and BLUETOOTH menus.
- **Bluetooth is off at every power-on.** It only starts when you turn on BT MIDI RECEIVE or SEND.
- **Turning Bluetooth on stops the second processor core until you restart.** Heavy patches may glitch. ABOUT shows "1 CORE" while this is in effect.
- **The fourth user sample slot (USR4) stays locked.** It sits on the memory that holds your radio calibration.
- **Radio calibration:** Beat It still never writes to the stock calibration area, backs it up to both settings copies when it finds it, uses the backup if it's lost, and never makes calibration up. If **RADIO CAL** says **MISSING**, Bluetooth can't start on this firmware, but everything else works. Your earlier installs may already have erased it, and nothing can bring it back.

## Install it from a Windows PC

You need Chrome or Edge and a USB data cable. You don't need Linux or any build tools.

1. **Back up first.** If your FM-1 is on SLOOP or Beat It, use SLOOP's editor and choose Backup. If it's on the official firmware, use M-VAVE's [FM-1 Workbench](https://open.m-vave.com/fm1-workbench).
2. Save **Beat-It-1.2-installer.html** to your PC and double-click it so it opens in Chrome or Edge.
3. Plug the FM-1 straight into the PC, not through a hub. Close any other app that uses MIDI.
4. Wait for the page to say "Beat It 1.2", then press **INSTALL**. Allow MIDI access when asked, and wait for "Done". The page says SLOOP in places because it's SLOOP's installer with Beat It packed inside.
5. The FM-1 restarts on the BEAT IT screen. Unplug it and plug it back in once so Windows finds its USB audio.
6. Open **Menu > BLUETOOTH** and check **RADIO CAL** first. STOCK or BACKUP means Bluetooth can try to start. On STOCK, leave the FM-1 on for a few seconds until it says BACKED UP.

**Beat-It-1.2.fwsc** is the same firmware on its own, for SLOOP's Python installer or the Firmware Hub.

## First Bluetooth test, step by step

Do these in order. Each one tells us something, even if it fails.

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
6. **To test clock,** set Menu > MIDI > CLOCK IN to **BT**, then press play in a DAW app that sends MIDI clock over Bluetooth. TEMPO should change to match the app, and the sequencer should start and stop with it.

Send me what each step showed: the RADIO CAL value, the BT CONNECTION value, any RESET DURING line, and the IN and OUT counts. A photo of the screen works. That's enough to work out the next fix.

## If something goes wrong

- **The FM-1 freezes or crashes with Bluetooth on:** power it off and on. Bluetooth starts off again, so it should boot normally. Check the RESET DURING line, then leave Bluetooth off.
- **The install stops partway:** open the installer page again and press INSTALL. It picks up where it left off.
- **The FM-1 won't start:** hold **OCT-** while you turn it on to enter USB rescue mode. Then run the installer again (1.2, 1.1, or **Beat-It-1.0-installer.html** for the build without Bluetooth), or use "Back to official" on [The Firmware Hub](https://thefirmwarehub.com/).
- **Going back to the official firmware:** use M-VAVE's **M-UPGRADE** (m-vave.com, Downloads, PC Software) with the official FM-1 firmware file. If M-UPGRADE can't find the FM-1, use "Return to the official firmware" on [Clipper's installer page](https://isod89.github.io/clipper-fm1/webapp/installer/) with that same file.
- **Worst case:** the community [unbricker tool](https://github.com/Quixotic7/MvaveFM1Unbricker) restores the FM-1 over USB.

## What was tested

All of this ran in a simulator on my Linux workspace. None of it ran on a real FM-1, and no radio was used.

- **Firmware build:** built cleanly with no warnings. The firmware is 397,160 bytes, 1,276 more than 1.1, and uses 65,356 of 98,304 bytes of RAM. It still fits.
- **Installer page:** the firmware packed inside it matches the .fwsc file byte for byte, and its fingerprint and FM-1 identity checks pass.
- **Bluetooth and calibration:** 365 checks across eight test groups passed, with no failures. That's 28 more than 1.1. The new ones cover:
  - clock, start and stop from Bluetooth, at 120 BPM with 15 ms connection events and at 133 BPM with 45 ms events, both landing on the right tempo
  - pulse timing within 2 ms, with packets arriving up to 20 ms late
  - clock ignored when CLOCK IN isn't BT, or when RECEIVE is off
  - the SEND OFF queue fix
  - the RESET DURING line, read from a simulated reset
  - the IN, OUT and LOST counts
  - the menu redrawing on its own
  - the three screens drawn and checked (picture included)
- **Existing clock sync:** the USB and TRS clock tests, including steady tempo with jittery pulses, still pass unchanged.
- **The whole suite:** 72 test groups ran (1.1 had 71). The only failures are the same 19 lines that fail on Beat It 1.0 and 1.1, all for sounds Beat It removed: the sample-chop click test, the switch to the SAMPLE engine, and the reference renders of removed presets. Nothing new failed in the drums, the FM engines, the sequencer, MIDI, the menus, the settings or the installer.

## What only your FM-1 can tell us

- whether the radio starts on the real chip and a phone can see "BEAT IT"
- whether notes and clock arrive reliably, and how much delay you can hear
- whether one core is enough for your patches with Bluetooth on
- whether your unit still has its stock radio calibration

The code changes, applied to Beat It 1.1, are in **beat-it-1.2-bluetooth.patch**. **beat-it-1.2-test-log.txt** has the full test output.
