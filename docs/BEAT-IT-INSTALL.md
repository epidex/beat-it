# Beat It 1.5 install guide

Beat It 1.5 keeps both processor cores running while Bluetooth is on. It does this by starting the radio in the order Optimist found stops the Bluetooth plus two-core crash. 1.5 builds cleanly and passes its tests in a simulator, but **it has never run on a real FM-1, and nothing here has ever transmitted over a real radio.** Treat it as experimental, and back up before you install it.

## What's new in 1.5

- **Bluetooth no longer shuts off the second core.** In 1.4, turning Bluetooth on stopped the second core until you restarted, so heavy patches could glitch and ABOUT said 1 CORE. In 1.5, ABOUT should stay at 2 CORES with Bluetooth on.
- **Bluetooth stays on across a restart.** If BT MIDI RECEIVE or SEND is on when you leave the menu, the setting is saved, and the radio starts at the next power-on, before the sound starts. This is the same order Optimist uses. The safest way to use Bluetooth is: turn it on, leave the menu, power off and on, then play.
- **Turning Bluetooth on while the FM-1 is running still works.** The second core keeps running, and it gets no audio work for 400 ms after the radio starts.
- **Saving to flash pauses the second core safely.** It waits in RAM during each flash write, and the audio interrupt code now runs from RAM too.
- **No boot loop.** If the FM-1 reset in the middle of starting the radio, the next power-on skips Bluetooth and saves it as OFF. The RESET DURING line in the BLUETOOTH menu shows the step where it hung.
- **BT STATUS reports 1.5.** The reply now says version 1.5, whether the radio started at power-on, and whether the second core was stopped after a fault since power-on. The Linux test script reads both, and on 1.5 its status step fails if the second core is reported stopped.
- **Screen messages that were cut off now fit.** The message bar holds 23 characters, and four messages were longer, so their ends were cut off: "CAL BACKED UP IN SETTINGS" is now "CAL SAVED IN SETTINGS", "2ND CORE OFF UNTIL RESTART" is now "1 CORE UNTIL RESTART", "NUDGE, LOCKS, FILL CLEARED" is now "NUDGE/LOCK/FILL CLEARED", and the new Bluetooth message reads "BT STAYS ON AT POWER-ON". A new test checks every message in the firmware against that limit.
- **Version:** the start-up screen and ABOUT say "BEAT IT 1.5 BT".

## The STOCK SUPPLY test build (optional)

There's a second installer, **Beat-It-1.5-STOCK-SUPPLY-test-installer.html**. It's the same firmware, plus the FM-1's own power start-up as M-VAVE's firmware does it. It moves the 1.4 V rail from its linear regulator to the switching one, sets up the Bluetooth power stage, and turns on the low-voltage detector. Optimist's notes say the boot loader leaves the rail on the linear regulator, and that the second core crashed the first one there. With the stock order, every Bluetooth plus two-core boot that used to crash within 2 to 60 seconds ran three times for 180 seconds without a crash, on Optimist's unit.

- Optimist marks the voltages "at your own risk": they're above the chip datasheet's table, and heat and battery effects weren't measured.
- It shows "BEAT IT 1.5 BT SUP" so you always know which build is on the synth.
- If the power settings don't read back correctly, it never starts the second core that boot. The FM-1 runs on one core.
- **Try the normal 1.5 first.** Only use this build if the normal one crashes with Bluetooth on.

## What hasn't changed

- **Drums and both FM engines (DIGITAL and FM6) are still in.** I didn't remove the FM engine. Optimist runs all of its engines on two cores with Bluetooth on, so the fix is the start-up order, not fewer engines.
- The purple-and-black theme, the BEAT IT start-up screen, the full-screen scope and SCOPE VIEW, SCOPE COLOUR, and the MIDI and BLUETOOTH menus.
- Everything from 1.2 to 1.4: the Bluetooth clock both ways, stuck-note handling when SEND turns off, the outgoing queue, LOST counts, BT STATUS and the Linux test script.
- **The fourth user sample slot (USR4) stays locked.** It sits on the memory that holds your radio calibration.
- **Radio calibration:** Beat It never writes to the stock calibration area. It backs it up to both settings copies, uses the backup if the original is lost, and never makes one up. If **RADIO CAL** says **MISSING**, Bluetooth can't start, but everything else works.
- **Going back to 1.4 is safe.** 1.5 saves Bluetooth on or off in two spare bits that 1.4 ignores. On 1.4, Bluetooth just starts off at every power-on again.

## Before you install

1. **Back up.** Open SLOOP's [web editor](https://isod89.github.io/sloop-fm1/webapp/editor/) in Chrome or Edge, go to Song & sections, and save a backup. Check that the backup file is actually on your PC. We haven't confirmed a backup of your FM-1 yet, so this step matters.
2. **Write down what ABOUT says** (Menu, then SYSTEM, then ABOUT) and, if you're on Beat It already, what RADIO CAL says.
3. **Give the FM-1 back to Windows.** If it's attached to Ubuntu, run `usbipd detach --busid 1-2` in PowerShell, then `usbipd list`, and check it no longer says Attached.
4. Plug it straight into the PC, not through a hub, and close any other app that uses MIDI.

## Install it from a Windows PC

1. Save **Beat-It-1.5-installer.html** to your PC and double-click it so it opens in Chrome or Edge.
2. Wait for the page to say "Beat It 1.5", then press **INSTALL**. Allow MIDI access when asked, and wait for "Done". The page says SLOOP in places because it's SLOOP's installer with Beat It packed inside.
3. The FM-1 restarts on the BEAT IT screen. Unplug it and plug it back in once so Windows finds its USB audio.
4. Open **Menu > BLUETOOTH** and check **RADIO CAL**. STOCK or BACKUP means Bluetooth can try to start. On STOCK, leave the FM-1 on for a few seconds until it says BACKED UP.

**Beat-It-1.5.fwsc** is the same firmware on its own, for SLOOP's Python installer or the Firmware Hub. All the 1.5 files, the STOCK SUPPLY test build included, are on the [v1.5 release](https://github.com/epidex/beat-it/releases/tag/v1.5). Earlier versions are on the [Releases](https://github.com/epidex/beat-it/releases) page if you need to go back.

## First test, in order

Each step tells us something, even if it fails. Stop at the first crash and write down what you saw.

1. **Without Bluetooth:** play a busy pattern for a minute. Open ABOUT and check it says 2 CORES. This is your baseline.
2. **Turn on BT MIDI RECEIVE and SEND,** then leave the menu. You should see "BT STAYS ON AT POWER-ON".
3. **Power off and on.** It should boot to the BEAT IT screen with sound working, and BT CONNECTION should say ADVERTISING. ABOUT should still say 2 CORES.
4. **Connect from a Bluetooth MIDI app on your phone** (look for "BEAT IT" and four characters, and connect inside the app). Play notes from the phone and check that IN counts up and the FM-1 plays them.
5. **The real test:** with the phone connected, play a heavy pattern for at least 3 minutes. That's the length of Optimist's clean runs. 1.4 needed to stop the second core here; 1.5 shouldn't need to. Check ABOUT again afterwards.
6. **Turn Bluetooth on while it's running:** turn both BT settings off, power off and on, start playing, then turn RECEIVE back on. It should keep playing without a glitch.
7. **If you have the Linux laptop handy,** the test script does the Bluetooth steps for you and saves a log. Its status line should say Beat It 1.5, that the radio started at power-on, and that the second core is running.

Send me what each step showed: ABOUT (2 CORES or 1 CORE), RADIO CAL, BT CONNECTION, any RESET DURING line, the IN and OUT counts, and how long it played before any crash. A photo of the screen works.

## If something goes wrong

- **It freezes or restarts while starting Bluetooth:** power it off and on. 1.5 skips Bluetooth at that power-on and saves it as OFF. Check the RESET DURING line.
- **It crashes while playing with Bluetooth on:** Bluetooth is still saved ON, so turn it off in the menu right after the next boot. If it crashes too fast to reach the menu, hold **OCT-** while you turn it on for USB rescue, and install Beat It 1.4 again: 1.4 always starts with Bluetooth off. Then tell me how long it ran, and we can try the STOCK SUPPLY build.
- **The install stops partway:** open the installer page again and press INSTALL. It picks up where it left off.
- **The FM-1 won't start:** hold **OCT-** while you turn it on (USB rescue), then run an installer again.
- **Back to official firmware:** use M-VAVE's M-UPGRADE with the official FM-1 file. If it can't find the synth, use Return to the official firmware on [Clipper's installer page](https://isod89.github.io/clipper-fm1/webapp/installer/).
- **Worst case:** the community [unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker).

## What was tested

Everything below ran on my computer, in the simulator and host tests. None of it ran on a real FM-1. The full log is in beat-it-1.5-test-log.txt.

- **Both builds compile,** and building again gives the same file byte for byte. The firmware is 399,248 bytes and uses 65,644 of 98,304 bytes of RAM. Both installers carry exactly the firmware file they're named for.
- **2,519 checks passed.**
- **Bluetooth start-up order:** BT saved OFF means no radio at power-on. BT saved ON means it starts at power-on with RECEIVE and SEND on. The second core keeps running and gets no job for 400 ms after a start. A reset during a start means no retry, and Bluetooth is saved OFF.
- **BT STATUS:** it reports version 1.5, flags a radio started at power-on, and still flags a stopped second core. The Linux test script passes all of its simulated runs, including a new "1.5, everything works" run. It still catches dropped messages and a wrong tempo.
- **Second core:** every way of splitting the sound work across the two cores passes, including a slow, erratic second core and one that times out. ThreadSanitizer found no race.
- **Screen messages:** all 135 fixed messages fit the 23-character bar.
- **The installer** passes its test against a simulated FM-1.

Some checks still fail, and they fail exactly the same way as in the 1.1 and 1.4 test runs, so 1.5 didn't cause them: five sample-chop click checks, one track-switch check, four silent GRAIN presets in the stored test renders, and the combined render-split test because of those renders. None of them involve Bluetooth or the second core's start-up.

## What only your FM-1 can show

- Whether Bluetooth plus two cores runs without crashing on your unit
- Whether the radio starts at power-on and your phone can see and connect to it
- What RADIO CAL says after the install
- Whether the STOCK SUPPLY build is needed at all
