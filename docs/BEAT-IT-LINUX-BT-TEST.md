# Testing Beat It's Bluetooth from a Linux computer

`tools/beatit-bt-test.sh` connects to your FM-1 over Bluetooth, walks you through each check, and writes a log file with everything it saw, every packet included. Send that file back and it shows what the radio did, without photos of the screen.

It's free. It doesn't need Claude, an account, or a subscription. It never pairs, never installs firmware, and never erases anything. It only sends MIDI to the FM-1: notes, a clock, and Beat It 1.4's BT STATUS question, which the FM-1 only answers.

**It has only been tested against a simulated FM-1, not a real radio.** See "What was tested" at the end.

## What you need

- A Linux computer with a Bluetooth 4.0 or newer adapter (built in, or a USB dongle). Ubuntu, Debian, Mint, Fedora, Pop!_OS and Raspberry Pi OS all work: they use BlueZ, Linux's normal Bluetooth stack.
- Python 3.8 or newer. It comes with all of those.
- Internet the first time only, to download the free `bleak` Bluetooth package.
- Your FM-1 running **Beat It 1.4**. 1.3 and older work too, but they can't answer BT STATUS, so the script relies on your answers and skips the stress step.

On Debian, Ubuntu, Mint or Raspberry Pi OS, if the script says something is missing:

```
sudo apt install bluez python3 python3-venv
```

## Get the files

Either download the repository (on GitHub: **Code**, then **Download ZIP**, then unzip it), or:

```
git clone https://github.com/epidex/beat-it.git
cd beat-it
```

The build also puts **Beat-It-1.4-linux-bt-test.tar.gz** on the release and in each build's packages. It holds just the script and this guide. Unpack it, then `cd beat-it-1.4-bt-test` and run the same command from there.

## Run it

1. On the FM-1, open **Menu > BLUETOOTH**. **RADIO CAL** must say STOCK or BACKUP. If it says MISSING, Bluetooth can't start, and there's nothing to test.
2. Turn on **BT MIDI RECEIVE** and **BT MIDI SEND**. BT CONNECTION should say **ADVERTISING**.
3. Make sure no phone app is connected to the FM-1. It takes one connection at a time.
4. On the Linux computer, in the beat-it folder:

```
sh tools/beatit-bt-test.sh
```

The first run takes a minute to set up its own Python environment in `~/.beatit-bt-venv`. Nothing is installed system-wide, and you don't need sudo.

Then it runs these steps in order, and tells you what to do on the FM-1 before each one. Press Enter when you're ready, or type `s` and Enter to skip a step.

| Step | What happens | What you do |
|---|---|---|
| env | checks the adapter is on, BlueZ is running, Bluetooth isn't blocked | nothing |
| scan | looks for "BEAT IT" and four characters, shows its signal strength | nothing |
| connect | connects, finds the BLE-MIDI service, turns on notifications | watch BT CONNECTION go to CONNECTED |
| status | asks the FM-1 for BT STATUS: RADIO CAL, the IN/OUT/LOST counts, the connection timing | nothing |
| receive | the computer plays 8 notes; checks IN went up by exactly 16 | listen, then say if you heard them |
| send | shows each note you play for 10 seconds; checks it matches OUT. Then the stuck-note check | play pads; then hold a pad and turn BT MIDI SEND off |
| clockout | measures the FM-1's tempo and timing jitter from its Bluetooth clock | set CLOCK IN to INT and CLOCK OUT to BT, press PLAY, then STOP after about 10 s, and type the TEMPO shown |
| clockin | sends START, a 100 BPM clock for 8 s, then STOP; checks each one arrived | set CLOCK IN to BT, then say if the FM-1 followed |
| stress | 600 short notes as fast as the link takes them; counts what got through | turn the volume down |

At the end it prints a summary, PASS or FAIL for each step with the reason, and the log's name, for example `~/beatit-bt-test-20261110-2130.log`. **Send me that file.** If a step failed, a photo of BT CONNECTION helps too.

## Useful options

```
sh tools/beatit-bt-test.sh --steps status                 just connect and read BT STATUS
sh tools/beatit-bt-test.sh --steps receive,send           only those (scan and connect are added for you)
sh tools/beatit-bt-test.sh --address C4:1F:00:00:5A:3C    connect to that address, skip the name search
sh tools/beatit-bt-test.sh --channel 10 --bpm 90          test notes on channel 10, clock at 90 BPM
sh tools/beatit-bt-test.sh --raw                          print every Bluetooth packet as well
sh tools/beatit-bt-test.sh --selftest                     no Bluetooth: check the script against a simulated FM-1
sh tools/beatit-bt-test.sh --help                         everything
```

Exit codes: 0 everything that ran passed, 1 a step failed, 2 no FM-1 found or no connection, 3 the computer isn't ready.

## If it can't find or connect to the FM-1

- **"the adapter is off"**: run `bluetoothctl power on`.
- **"Bluetooth is blocked"**: run `rfkill unblock bluetooth`.
- **"the bluetooth service is not running"**: run `sudo systemctl start bluetooth`.
- **No BEAT IT found** while BT CONNECTION says ADVERTISING: the FM-1 thinks it's on the air, but the computer doesn't hear it. That's a radio or calibration result, and worth sending. Move closer and try once more first.
- **It connects, then drops:** note whether BT CONNECTION showed CONNECTED for a moment. Then run `bluetoothctl remove` followed by the FM-1's address (the scan step prints it), and try again. That clears what BlueZ remembered about it.
- **The FM-1 freezes or restarts:** power it off and on, open Menu > BLUETOOTH and write down the **RESET DURING** line. With 1.4, BT STATUS also reports it the next time you connect.

## BT STATUS (new in 1.4)

When a connected app or computer sends the SysEx message `F0 7D 42 49 01 F7`, Beat It 1.4 answers with one SysEx `F0 7D 42 49 02 … F7` (38 bytes). It includes:

- the Beat It version, RADIO CAL (MISSING, STOCK or BACKUP), and the RESET DURING step
- whether RECEIVE and SEND are on, whether the app has notifications on, whether the link is encrypted, and whether the second core is stopped
- the CLOCK IN and CLOCK OUT settings
- the IN, OUT, LOST in and LOST out counts
- the connection interval, supervision timeout, MTU and data length that the link actually agreed on

It's answered only while BT MIDI RECEIVE or SEND is on. It's never played, and it doesn't count toward IN or OUT. Any other SysEx over Bluetooth is ignored, as in 1.3. `7D` is the MIDI ID set aside for non-commercial use, so it doesn't clash with real products. The byte layout is in `firmware/src/midi_ble.c` (`ble_diag_reply`).

## What was tested

None of this has run on a real FM-1 or a real Bluetooth radio. My workspace has no Bluetooth hardware.

- `python3 tools/beatit_bt_test.py --selftest`: the script's BLE-MIDI encoder and decoder (running status, timestamps wrapping, a clock inside a SysEx, a SysEx split over packets, packet sizes 20 to 182), BT STATUS's layout, and every step against a simulated Beat It 1.4. Four cases: everything works; Beat It 1.3 with no BT STATUS; an FM-1 that drops every 7th message, which receive, clockin and stress must catch; and a TEMPO on the screen that doesn't match the clock, which clockout must catch.
- `tests/bt_tool_interop.py`: the script's packets fed into the firmware's own Bluetooth MIDI code (compiled for the PC), and the firmware's replies decoded by the script, at packet sizes 20, 23 and 64.
- `tests/bt_tool_fake_bleak.py`: the real Bluetooth code path in the script, run through the installed `bleak` package's own device, advertisement and GATT objects, with a fake radio underneath. This catches wrong calls or attributes against bleak 3.1.

What only your FM-1 and computer can show: whether BlueZ sees and connects to the FM-1, whether notifications arrive, the real connection interval, and real timing and losses.
