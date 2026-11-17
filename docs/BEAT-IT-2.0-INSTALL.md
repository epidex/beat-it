# Beat It 2.0: install, first test and getting back

Beat It 2.0 is Melodee 1.6.1 with a new name, a purple-on-black theme, five saved brightness levels and a full-screen oscilloscope whose colour follows the pitch. Melodee's Bluetooth MIDI and second-core code are unchanged. The only Bluetooth change is the name it shows up as: **BEAT IT XXXX**, where XXXX is four characters unique to your unit.

It has only run in tests on the computer. It has never run on a real FM-1. Treat the first install as a test.

## What you need

- A Windows laptop with **Chrome or Edge**. Run the installer on Windows itself, not inside WSL/Ubuntu.
- The FM-1, charged, and a USB-C data cable.
- The file **Beat-It-2.0-installer.html**. Everything is built in, so it works offline once it's open.
- Close anything else that uses MIDI first: DAWs, MIDI Wrench, the Ubuntu USB passthrough (`usbipd detach`), and any other installer tab.

## 1. Back up first

The installer backs up the whole FM-1 to your computer before it writes anything, and asks you to confirm the backup is saved. Don't skip this.

1. Before you start, copy any projects or samples you care about off the FM-1 the way you normally do.
2. During the install, when the browser asks where to save the backup file, keep it somewhere safe, like a `FM-1 backups` folder. That file is your way back.

## 2. Install

1. Plug the FM-1 into the laptop and switch it on.
2. Double-click **Beat-It-2.0-installer.html** so it opens in Chrome or Edge.
3. Click **Find FM-1**. When the browser asks for MIDI access, allow it.
4. The page shows what's on the FM-1 now. Beat It 1.6 may show up as a development build. That's expected: 1.6 and 2.0 both use the same development tag.
5. Click **Install Beat It**, tick the box confirming the backup is saved, and start it.
6. Leave the cable in and don't touch the FM-1 until the page says the install is finished and verified.

If it stops partway through, plug back in and run the same installer again. It picks up the waiting install.

## 3. First test

Do these in order, and write down anything odd, especially a crash screen with its BAD count and address.

**Boot and screen**
- It should start with the Beat It 2.0 name and the purple-on-black theme.
- **MENU > BRIGHTNESS**: Knob 1 right makes it dimmer and left makes it brighter: 100%, 70%, 50%, 30%, 15%. Close the menu, switch off, switch on, and check the level stayed.
- **Scope:** tap **HOME** to go from Stage to the chord view, and tap HOME again for the full-screen scope. Play low and high notes. The trace should go from red-magenta for low notes, through purple, to cyan for high ones. One more tap of HOME goes back to Stage.

**Bluetooth: the FM-1 shows up (nRF Connect on your iPhone)**
1. On the FM-1, hold **HOME** and choose **BLUETOOTH**. Stop playback first. If the page says STOP AUDIO TO START BLE, stop the audio.
2. Turn the radio on (OCT+/SAVE, or Knob 1 right) and set **Mode** to **VISIBLE**. The header should show `VISIBLE` and four characters.
3. In nRF Connect, scan. Look for **BEAT IT** followed by those same four characters.
4. If the page says RADIO CALIBRATION MISSING or FINISH SETUP IN WEB APP, stop and send me a photo.

**Bluetooth: MIDI out and in (midimittr)**
1. With the FM-1 still on VISIBLE, open midimittr, go to the Bluetooth device list and connect to **BEAT IT XXXX**.
2. **Send:** play keys on the FM-1. Notes should show up in midimittr.
3. **Receive:** this is what crashed Beat It 1.6. Send notes from midimittr's keyboard to the FM-1 for a few minutes. Then try it with a pattern playing, and with the scope page open.
4. Switch the FM-1 off and on with Bluetooth left on, a few times. Check it starts cleanly and comes back on Bluetooth every time.

If something crashes, take a photo of the crash screen and send it to me.

## 4. Getting back if something goes wrong

**Reinstall rescue (Beat It 2.0 has this, Beat It 1.6 didn't)**
If Beat It 2.0 won't start properly, hold **OCT− on its own** while you switch the FM-1 on. Three short flashes of the key lights mean it has handed over to the update loader. Reconnect USB, open the same installer, and install again. The port shows up as **Beat It FM-1 Update**. If the loader doesn't come up, the FM-1 drops to its built-in ROM update mode instead.

**Back to Bump It 1.1**
1. Open the Beat It 2.0 installer, scroll to the section for leaving this firmware, and use **Back up and return to official V15**.
2. Once it's on V15, install Bump It 1.1 with its installer, the same way you installed it the first time from stock.

The installer also has **Back up and install TransitFM1**, a stopover firmware for switching. I haven't checked whether Bump It's installer accepts an FM-1 sitting on TransitFM1, so use the V15 route to get back to Bump It.

**Brightness and older firmware.** Beat It 2.0 saves brightness in two spare bytes inside Melodee's own settings and nowhere else. It doesn't use a ZOOM slot the way Beat It 1.6 did. Plain Melodee 1.6.1 was tested reading those saved settings, and its ZOOM and other settings came out untouched.

## How it was tested (all on the computer)

- **Build:** clean. Memory: 97,288 of 98,304 bytes used, which leaves 1,016 bytes free. That's about the same as plain Melodee, which leaves 1,112 free.
- **Full test suite, run twice:** 193 of 194 sections passed both times. The one that failed both times is a timing test called "CPU live takes". It also fails on plain Melodee here, and it passed 3 out of 3 times when run on its own. None of the files it tests were changed for Beat It.
- **Stress runs:** 29 test programs were run 40 times each, 1,160 runs in total, with 0 failures. They covered both cores, the second-core safety shutoff, Bluetooth, the screen and brightness, settings and saving, audio start-up and the audio paths.
- **Voltage:** in the FM-1 simulator, the power-supply settings Beat It 2.0 makes at start-up were identical to M-VAVE's stock V15 and to plain Melodee. Nothing goes above stock.
- **Installer:** opened from disk in Chrome. It ran start to finish in its built-in demo mode, with a pretend FM-1 and nothing written. The firmware files built into it matched their checksums.

**Not tested:** whether the radio really broadcasts, and the second-core memory misreads your unit had on Beat It 1.6. The simulator can't reproduce either one, so only your FM-1 can show those.

## Files

- `Beat-It-2.0-installer.html`: the installer, with the firmware built in.
- `Beat-It-2.0.fwsc` and `Beat-It-2.0-res.bin`: the firmware and its resource file, for the record.
- `SHA256SUMS`: checksums for those two files.

Beat It 2.0 is a modified version of Melodee 1.6.1 by Kerem Kilic / Ellic Studio, GPL-3.0. Report Beat It problems to me, not to Melodee.
