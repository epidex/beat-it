#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Beat It Bluetooth MIDI test for Linux: checks a FM-1 running Beat It over a real Bluetooth radio, step by step,
and writes everything it saw to a log file you can send back.

  tools/beatit-bt-test.sh                 (the easy way: makes its own Python environment, then runs this)
  python3 tools/beatit_bt_test.py [--steps env,scan,connect,status,receive,send,clockout,clockin,stress]
                                  [--address AA:BB:CC:DD:EE:FF] [--channel 1] [--bpm 100] [--yes] [--raw]
  python3 tools/beatit_bt_test.py --selftest      (no Bluetooth: the codec and every step against a simulated FM-1)

What it needs: Linux with BlueZ (the normal Bluetooth stack of Ubuntu, Debian, Fedora, Mint, Raspberry Pi OS), a
Bluetooth 4.0 or newer adapter, Python 3.8+, and the free `bleak` package (pip install bleak). No account, no
subscription, nothing paid. It never pairs, never installs firmware, never writes anything to the FM-1 but MIDI:
notes, the clock, and Beat It 1.4's BT STATUS question (SysEx F0 7D 42 49 01 F7), which the FM-1 only answers.

Steps (all by default, in this order):
  env       the computer: kernel, BlueZ version, the adapter powered, rfkill (read only)
  scan      finds "BEAT IT XXXX" (or anything with the BLE-MIDI service) and its signal strength
  connect   connects, checks the BLE-MIDI service and characteristic, turns notifications on
  status    (1.4) asks the FM-1 for BT STATUS: RADIO CAL, the counts, the connection interval, MTU, encryption
  receive   the computer plays 8 notes to the FM-1; with 1.4, checks IN went up by exactly 16 (on + off each)
  send      you play pads for 10 s; the computer shows each note; with 1.4, checks OUT matches. Then the stuck-
            note check: hold a pad, turn BT MIDI SEND off, every note must get its note-off
  clockout  you set CLOCK OUT to BT and press PLAY: measures the FM-1's tempo and jitter from its timestamps
  clockin   you set CLOCK IN to BT: the computer sends START, a clock at --bpm, STOP; with 1.4 checks each arrived
  stress    600 notes as fast as the link takes them; with 1.4, IN and LOST show what got through

Exit codes: 0 every step that ran passed, 1 a step failed, 2 no FM-1 found / no connection, 3 the computer is not
ready (no adapter, Bluetooth off, bleak missing), 130 stopped with Ctrl-C.
"""
import argparse
import asyncio
import datetime
import json
import os
import platform
import random
import shutil
import statistics
import subprocess
import sys
import time

TOOL_VERSION = "1.4"
MIDI_SERVICE = "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
MIDI_CHAR = "7772e5db-3868-4112-a1a9-f2669d106bf3"
NAME_PREFIX = "BEAT IT"
STATUS_Q = bytes([0xF0, 0x7D, 0x42, 0x49, 0x01, 0xF7])
STATUS_HDR = bytes([0xF0, 0x7D, 0x42, 0x49, 0x02])
STATUS_LEN = 38
RADIO_CAL = {0: "MISSING", 1: "STOCK", 2: "BACKUP"}
CLOCK_IN = {0: "INT", 1: "USB", 2: "TRS", 3: "BT"}          # G_SYNC (src 3 = BT, midi_ble.c)
CLOCK_OUT = {0: "OFF", 1: "USB", 2: "BT", 3: "USB+BT"}     # G_CLKOUT
STEP_NAMES = {3: "RF INIT", 4: "BB INIT"}                    # the ones the menu names; others by number
ALL_STEPS = ["env", "scan", "connect", "status", "receive", "send", "clockout", "clockin", "stress"]


# ---------------------------------------------------------------------------------------------------- BLE-MIDI codec
def data_len(st):
    if st < 0xF0:
        return 1 if (st & 0xE0) == 0xC0 else 2
    return 2 if st == 0xF2 else 1 if st in (0xF1, 0xF3) else 0


def now_ts():
    """the 13-bit millisecond timestamp BLE-MIDI carries"""
    return int(time.monotonic() * 1000) & 0x1FFF


def encode(msgs, maxlen=20):
    """[(message bytes, 13-bit ms timestamp)] -> BLE-MIDI packets (MIDI over BLE 1.0): each message with its own
    timestamp byte, no running status; a SysEx longer than a packet continues in the next (header, then data)"""
    out = []
    cur = None
    hi = None

    def flush():
        nonlocal cur
        if cur is not None and len(cur) > 1:
            out.append(bytes(cur))
        cur = None

    def begin(ts):
        nonlocal cur, hi
        flush()
        hi = (ts >> 7) & 0x3F
        cur = bytearray([0x80 | hi])

    for msg, ts in msgs:
        ts &= 0x1FFF
        msg = bytes(msg)
        if cur is None or ((ts >> 7) & 0x3F) != hi:
            begin(ts)
        if msg[0] == 0xF0:
            if len(cur) + 2 > maxlen:
                begin(ts)
            cur.append(0x80 | (ts & 0x7F))
            for b in msg[:-1]:
                if len(cur) >= maxlen:
                    begin(ts)                       # a continuation: header, then the SysEx data straight on
                cur.append(b)
            if len(cur) + 2 > maxlen:
                begin(ts)
            cur += bytes([0x80 | (ts & 0x7F), 0xF7])
        else:
            if len(cur) + 1 + len(msg) > maxlen:
                begin(ts)
            cur.append(0x80 | (ts & 0x7F))
            cur += msg
    flush()
    return out


class Decoder:
    """BLE-MIDI packets -> (message bytes, 13-bit timestamp). Running status, a timestamp per message or not,
    realtime anywhere (inside a SysEx too), a SysEx over several packets, the timestamp's low part wrapping"""

    def __init__(self):
        self.rs = 0
        self.sx = None
        self.errors = 0

    def feed(self, p):
        ev = []
        p = bytes(p)
        if len(p) < 2 or not (p[0] & 0x80) or (p[0] & 0x40):
            self.errors += 1
            return ev
        hi = p[0] & 0x3F
        lo_prev = None
        ts = None
        i = 1
        n = len(p)
        while i < n:
            b = p[i]
            if self.sx is not None and b < 0x80:
                self.sx.append(b)
                i += 1
                continue
            if b & 0x80:                                     # a timestamp octet
                lo = b & 0x7F
                if lo_prev is not None and lo < lo_prev:
                    hi = (hi + 1) & 0x3F
                lo_prev = lo
                ts = (hi << 7) | lo
                i += 1
                if i >= n:
                    self.errors += 1
                    break
                b = p[i]
                if b & 0x80:
                    i += 1
                    if b >= 0xF8:
                        ev.append((bytes([b]), ts))
                        continue
                    if b == 0xF7:
                        if self.sx is not None:
                            self.sx.append(0xF7)
                            ev.append((bytes(self.sx), ts))
                            self.sx = None
                        else:
                            self.errors += 1
                        continue
                    if self.sx is not None:
                        self.errors += 1                    # a status cuts an unfinished SysEx: dropped
                        self.sx = None
                    if b == 0xF0:
                        self.sx = bytearray([0xF0])
                        continue
                    st = b
                    self.rs = st if st < 0xF0 else 0
                else:
                    st = self.rs
            else:
                st = self.rs                                 # running status without a timestamp
            k = data_len(st) if st else 0
            if not st or i + k > n or any(x & 0x80 for x in p[i:i + k]):
                self.errors += 1
                break
            ev.append((bytes([st]) + p[i:i + k], ts if ts is not None else hi << 7))
            i += k
        return ev


def parse_status(m):
    """Beat It 1.4 / 1.5's BT STATUS reply (midi_ble.c ble_diag_reply) -> dict, or None"""
    m = bytes(m)
    if len(m) != STATUS_LEN or not m.startswith(STATUS_HDR) or m[-1] != 0xF7:
        return None
    d = m[5:-1]

    def n28(o):
        return d[o] | d[o + 1] << 7 | d[o + 2] << 14 | d[o + 3] << 21

    def n14(o):
        return d[o] | d[o + 1] << 7

    f = d[4]
    return {
        "layout": d[0], "version": "%d.%d" % (d[1], d[2]),
        "radio_cal": RADIO_CAL.get(d[3], "?%d" % d[3]),
        "receive": bool(f & 1), "send": bool(f & 2), "listening": bool(f & 4), "encrypted": bool(f & 8),
        "core2_stopped": bool(f & 16), "draining": bool(f & 32), "boot_start": bool(f & 64),
        "hung_step": d[5], "clock_in": CLOCK_IN.get(d[6], str(d[6])), "clock_out": CLOCK_OUT.get(d[7], str(d[7])),
        "in": n28(8), "out": n28(12), "lost_in": n28(16), "lost_out": n28(20),
        "interval_ms": n14(24) * 1.25, "timeout_ms": n14(26) * 10, "mtu": n14(28), "data_len": n14(30),
    }


def status_bytes(s):
    """the reverse of parse_status (the simulated FM-1 and the tests)"""
    inv = lambda tab, v: [k for k, x in tab.items() if x == v][0]
    d = bytearray(32)
    d[0] = 1
    d[1], d[2] = [int(x) for x in s["version"].split(".")]
    d[3] = inv(RADIO_CAL, s["radio_cal"])
    d[4] = (s["receive"] and 1) | (s["send"] and 2) | (s["listening"] and 4) | (s["encrypted"] and 8) | \
        (s["core2_stopped"] and 16) | (s["draining"] and 32) | (s.get("boot_start") and 64)
    d[5] = s["hung_step"]
    d[6] = inv(CLOCK_IN, s["clock_in"])
    d[7] = inv(CLOCK_OUT, s["clock_out"])
    for o, k in ((8, "in"), (12, "out"), (16, "lost_in"), (20, "lost_out")):
        for j in range(4):
            d[o + j] = (s[k] >> (7 * j)) & 0x7F
    for o, v in ((24, int(s["interval_ms"] / 1.25)), (26, s["timeout_ms"] // 10), (28, s["mtu"]), (30, s["data_len"])):
        d[o], d[o + 1] = v & 0x7F, (v >> 7) & 0x7F
    return STATUS_HDR + bytes(d) + b"\xF7"


def unwrap(ts_list):
    """13-bit ms timestamps (wrapping every 8.192 s) -> a rising ms count"""
    out, base, prev = [], 0, None
    for t in ts_list:
        if prev is not None and t < prev - 4096:
            base += 8192
        out.append(base + t)
        prev = t
    return out


def note_name(n):
    return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12] + str(n // 12 - 1)


def describe(msg):
    st = msg[0]
    if st >= 0xF8:
        return {0xF8: "clock", 0xFA: "START", 0xFB: "CONTINUE", 0xFC: "STOP"}.get(st, "realtime %02X" % st)
    if st == 0xF0:
        return "SysEx " + msg.hex(" ").upper()
    ch = (st & 15) + 1
    kind = st & 0xF0
    if kind == 0x90 and msg[2]:
        return "note on  %-4s vel %3d ch %d" % (note_name(msg[1]), msg[2], ch)
    if kind in (0x80, 0x90):
        return "note off %-4s         ch %d" % (note_name(msg[1]), ch)
    if kind == 0xB0:
        return "CC %d = %d ch %d" % (msg[1], msg[2], ch)
    return msg.hex(" ").upper()


# ---------------------------------------------------------------------------------------------------- the log
class Report:
    def __init__(self, path, raw, mute=False):
        self.mute = mute
        self.path = path
        self.raw = raw
        self.f = open(path, "w", encoding="utf-8") if path else None
        self.results = []
        self.data = {"tool": TOOL_VERSION, "started": datetime.datetime.now().isoformat(timespec="seconds"), "steps": {}}
        self.t0 = time.monotonic()

    def line(self, s="", quiet=False):
        stamp = "%8.3f " % (time.monotonic() - self.t0)
        if not quiet and not self.mute:
            print(s, flush=True)
        if self.f:
            self.f.write(stamp + s + "\n")
            self.f.flush()

    def rawline(self, s):
        if self.f:
            self.f.write("%8.3f   %s\n" % (time.monotonic() - self.t0, s))
        if self.raw:
            print("   " + s)

    def head(self, s):
        self.line("")
        self.line("== " + s)

    def result(self, step, verdict, why):
        self.results.append((step, verdict, why))
        self.line("   -> %s: %s" % (verdict, why))

    def close(self):
        self.data["results"] = [{"step": s, "result": v, "why": w} for s, v, w in self.results]
        if self.f:
            self.f.write("\nJSON " + json.dumps(self.data, default=str) + "\n")
            self.f.close()


# ---------------------------------------------------------------------------------------------------- the link
class Link:
    """what the steps talk to: a real FM-1 through bleak (BleLink), or the simulated one (SimLink)"""

    def __init__(self, rep, maxlen=20):
        self.rep = rep
        self.dec = Decoder()
        self.q = asyncio.Queue()
        self.maxlen = maxlen
        self.notifications = 0

    def on_notify(self, data):                               # (bleak's callback runs in the event loop)
        arr = time.monotonic()
        self.notifications += 1
        self.rep.rawline("<- " + bytes(data).hex(" ").upper())
        for msg, ts in self.dec.feed(data):
            self.q.put_nowait((msg, ts, arr))

    async def send(self, msgs):
        for p in encode(msgs, self.maxlen):
            self.rep.rawline("-> " + p.hex(" ").upper())
            await self.write(p)

    def drain(self):
        out = []
        while not self.q.empty():
            out.append(self.q.get_nowait())
        return out

    async def collect(self, seconds, stop=None):
        """the messages that arrive within seconds (or until stop(msg) is true)"""
        out = []
        end = time.monotonic() + seconds
        while True:
            left = end - time.monotonic()
            if left <= 0:
                break
            try:
                ev = await asyncio.wait_for(self.q.get(), left)
            except asyncio.TimeoutError:
                break
            out.append(ev)
            if stop and stop(ev[0]):
                break
        return out

    async def status(self, tries=3, quiet=False):
        """BT STATUS (Beat It 1.4 and later): the parsed reply, or None (an older Beat It, or the reply lost)"""
        for _ in range(tries):
            await self.send([(STATUS_Q, now_ts())])
            evs = await self.collect(1.5, stop=lambda m: m.startswith(STATUS_HDR))
            for m, ts, arr in evs:
                if m.startswith(STATUS_HDR):
                    s = parse_status(m)
                    if s:
                        return s
                    self.rep.line("   a BT STATUS reply that does not parse: " + m.hex(" ").upper())
            for m, ts, arr in evs:
                if not quiet and not m.startswith(STATUS_HDR):
                    self.rep.line("   (meanwhile: %s)" % describe(m))
        return None


class BleLink(Link):
    def __init__(self, rep, client, char, maxlen):
        super().__init__(rep, maxlen)
        self.client = client
        self.char = char

    async def write(self, p):
        await self.client.write_gatt_char(self.char, p, response=False)


class SimLink(Link):
    """a FM-1 running Beat It 1.4 or 1.5, as midi_ble.c behaves (for --selftest): counts IN per channel / realtime message
    it would queue, answers BT STATUS, plays notes and the clock back with timestamps and a radio's jitter"""

    def __init__(self, rep, version="1.5", lose_every=0):
        super().__init__(rep, 20)
        self.version = version
        self.fdec = Decoder()
        self.st = {"layout": 1, "version": version, "radio_cal": "STOCK", "receive": True, "send": True,
                   "listening": True, "encrypted": False, "core2_stopped": version < "1.5", "draining": False,
                   "boot_start": version >= "1.5", "hung_step": 0, "clock_in": "INT", "clock_out": "OFF", "in": 0, "out": 0, "lost_in": 0, "lost_out": 0,
                   "interval_ms": 15.0, "timeout_ms": 2000, "mtu": 23, "data_len": 27}
        self.lose_every = lose_every
        self.k = 0
        self.tasks = []

    async def write(self, p):
        for msg, ts in self.fdec.feed(p):
            if msg == STATUS_Q:
                if self.version >= "1.4":
                    self.later(0.03, [(status_bytes(self.st), now_ts())])
                continue
            if msg[0] == 0xF0:
                continue
            if msg[0] >= 0xF8 and self.st["clock_in"] != "BT":
                continue
            if not self.st["receive"]:
                continue
            self.k += 1
            if self.lose_every and self.k % self.lose_every == 0:
                self.st["lost_in"] += 1
            else:
                self.st["in"] += 1

    def later(self, delay, msgs):
        async def go():
            await asyncio.sleep(delay)
            for p in encode(msgs, 20):
                self.on_notify(p)
        self.tasks.append(asyncio.ensure_future(go()))

    def play(self, msgs_with_ms):
        """[(msg, seconds from now)] notified as the FM-1 would: each stamped when sent, arriving at the next
        connection event (0..interval later)"""
        for msg, dt in msgs_with_ms:
            ts = (int((time.monotonic() + dt) * 1000)) & 0x1FFF
            jitter = random.uniform(0, self.st["interval_ms"] / 1000)
            if msg[0] < 0xF8:
                self.st["out"] += 1
            self.later(dt + jitter, [(msg, ts)])


# ---------------------------------------------------------------------------------------------------- the steps
class Ctx:
    def __init__(self, args, rep):
        self.args = args
        self.rep = rep
        self.link = None
        self.client = None
        self.device = None
        self.sim = None
        self.answers = list(args.answers or [])

    def ask(self, prompt, default=""):
        """a question for the person at the FM-1 (the simulated run answers from its script)"""
        if self.answers:
            a = self.answers.pop(0)
        elif self.args.yes:
            a = default
        else:
            try:
                a = input("   >> " + prompt + (" [%s] " % default if default else " ")).strip() or default
            except EOFError:
                a = default
        self.rep.line("   >> %s  answer: %s" % (prompt, a or "(enter)"), quiet=True)
        return a

    def wait_enter(self, prompt):
        return self.ask(prompt + " Press Enter when ready (s to skip).")


def run(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=8)
        return (r.stdout + r.stderr).strip()
    except Exception as e:  # noqa: BLE001 (any failure is just reported)
        return "(could not run %s: %s)" % (cmd[0], e)


async def step_env(c):
    r = c.rep
    r.line("   system   %s %s, Python %s" % (platform.system(), platform.release(), platform.python_version()))
    try:
        from importlib.metadata import version
        r.line("   bleak    %s" % version("bleak"))
    except Exception:  # noqa: BLE001
        if not c.sim:
            r.result("env", "FAIL", "the bleak package is missing: run tools/beatit-bt-test.sh, or pip install bleak")
            return 3
    if c.sim:
        r.result("env", "PASS", "simulated run: no adapter needed")
        return 0
    if platform.system() != "Linux":
        r.line("   (not Linux: bleak also runs on Windows and macOS, but this script was written for BlueZ)")
    bad = []
    hci = sorted(os.listdir("/sys/class/bluetooth")) if os.path.isdir("/sys/class/bluetooth") else []
    r.line("   kernel   Bluetooth adapters: %s" % (", ".join(hci) if hci else "none"))
    if platform.system() == "Linux" and not hci:
        bad.append("Linux sees no Bluetooth adapter (nothing in /sys/class/bluetooth): plug in a USB Bluetooth dongle, "
                   "or turn Bluetooth on in the BIOS / with the laptop's wireless key")
    if shutil.which("bluetoothctl"):
        r.line("   bluez    " + run(["bluetoothctl", "--version"]))
        show = run(["bluetoothctl", "show"]) if hci else ""
        for ln in show.splitlines():
            if any(k in ln for k in ("Controller", "Name:", "Powered:", "Discovering:", "PowerState:")):
                r.line("   adapter  " + ln.strip())
        if hci and ("No default controller" in show or "Controller" not in show):
            bad.append("BlueZ shows no adapter (bluetoothctl show): is the bluetooth service running?")
        elif "Powered: no" in show:
            bad.append("the adapter is off: run  bluetoothctl power on")
    else:
        r.line("   bluetoothctl not found (Debian/Ubuntu: sudo apt install bluez)")
    if shutil.which("rfkill"):
        rf = run(["rfkill", "list", "bluetooth"])
        for ln in rf.splitlines():
            r.line("   rfkill   " + ln.strip())
        if "blocked: yes" in rf:
            bad.append("Bluetooth is blocked: run  rfkill unblock bluetooth")
    if shutil.which("systemctl"):
        act = run(["systemctl", "is-active", "bluetooth"]).strip()
        r.line("   service  bluetooth: " + (act.splitlines()[0] if act else "(no answer)"))
        if act in ("inactive", "failed"):
            bad.append("the bluetooth service is not running: run  sudo systemctl start bluetooth")
    if bad:
        r.result("env", "FAIL", "; ".join(bad))
        return 3
    r.result("env", "PASS", "Bluetooth adapter on, BlueZ running")
    return 0


async def step_scan(c):
    r = c.rep
    if c.sim:
        r.line("   found    BEAT IT 5A3C  C4:1F:00:00:5A:3C  RSSI -58 dBm  (simulated)")
        r.data["steps"]["scan"] = {"name": "BEAT IT 5A3C", "rssi": -58}
        r.result("scan", "PASS", "BEAT IT 5A3C advertising (simulated)")
        return 0
    from bleak import BleakScanner
    if c.args.address:
        r.line("   looking for %s ..." % c.args.address)
    else:
        r.line("   scanning %d s for \"%s XXXX\" (BT CONNECTION on the FM-1 should say ADVERTISING) ..."
               % (c.args.scan, NAME_PREFIX))
    try:
        found = await BleakScanner.discover(timeout=c.args.scan, return_adv=True)
    except Exception as e:  # noqa: BLE001 (no D-Bus, no BlueZ, no adapter, adapter busy)
        r.line("   the scan could not start: %s: %s" % (type(e).__name__, e))
        r.line("   bleak talks to BlueZ (bluetoothd) over D-Bus. Check: systemctl status bluetooth ;  bluetoothctl show")
        r.result("scan", "FAIL", "Bluetooth scan could not start (BlueZ / D-Bus / adapter): %s" % type(e).__name__)
        return 3
    hits, midi_others = [], []
    for addr, (dev, adv) in found.items():
        name = adv.local_name or dev.name or ""
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        is_midi = MIDI_SERVICE in uuids
        if (c.args.address and addr.upper() == c.args.address.upper()) or \
                (not c.args.address and name.upper().startswith(NAME_PREFIX)):
            hits.append((adv.rssi, name, addr, dev, is_midi))
        elif is_midi:
            midi_others.append((adv.rssi, name, addr))
    r.line("   %d Bluetooth LE devices seen in all" % len(found))
    for rssi, name, addr in midi_others:
        r.line("   other BLE-MIDI device: %s %s RSSI %s dBm" % (name or "(no name)", addr, rssi))
    if not hits:
        r.line("   No FM-1 found. Check, in this order:")
        r.line("     - BLUETOOTH > RADIO CAL is STOCK or BACKUP (MISSING: Bluetooth can't start)")
        r.line("     - BT MIDI RECEIVE (or SEND) is ON and BT CONNECTION says ADVERTISING")
        r.line("     - the FM-1 is not connected to a phone or another computer (it takes one link at a time)")
        r.line("     - it is within a couple of metres; then run the scan again")
        r.result("scan", "FAIL", "no BEAT IT device advertising")
        return 2
    hits.sort(key=lambda h: -(h[0] or -999))
    rssi, name, addr, dev, is_midi = hits[0]
    for h in hits:
        r.line("   found    %s  %s  RSSI %s dBm  %s" % (h[1], h[2], h[0], "BLE-MIDI service advertised" if h[4]
                                                          else "(the BLE-MIDI service not in the advert)"))
    c.device = dev
    r.data["steps"]["scan"] = {"name": name, "address": addr, "rssi": rssi, "midi_uuid_advertised": is_midi}
    weak = rssi is not None and rssi < -85
    r.result("scan", "PASS", "%s advertising, RSSI %s dBm%s" % (name, rssi, " (weak: move closer)" if weak else ""))
    return 0


async def step_connect(c):
    r = c.rep
    if c.sim:
        c.link = c.sim
        r.line("   service  BLE-MIDI %s" % MIDI_SERVICE)
        r.line("   char     MIDI I/O %s: read, write-without-response, notify" % MIDI_CHAR)
        r.result("connect", "PASS", "connected, BLE-MIDI characteristic found, notifications on (simulated)")
        return 0
    from bleak import BleakClient
    if c.device is None and not c.args.address:
        r.result("connect", "SKIP", "nothing found to connect to")
        return 2
    lost = {"t": None}

    def gone(_client):
        lost["t"] = time.monotonic()
        r.line("   !! the FM-1 disconnected (%.1f s into the run)" % (lost["t"] - r.t0))

    t = time.monotonic()
    client = BleakClient(c.device or c.args.address, disconnected_callback=gone, timeout=20.0)
    try:
        await client.connect()
    except Exception as e:  # noqa: BLE001
        r.line("   connect failed: %s: %s" % (type(e).__name__, e))
        r.line("   If BT CONNECTION showed CONNECTED for a moment, the link starts but drops: that is a radio-timing")
        r.line("   clue, note it. If it never left ADVERTISING, the FM-1 never heard the connection request.")
        r.line("   Try once more after: bluetoothctl remove <address>  (clears what BlueZ cached about it)")
        r.result("connect", "FAIL", "could not connect: %s" % type(e).__name__)
        return 2
    c.client = client
    r.line("   connected in %.1f s" % (time.monotonic() - t))
    svc = client.services.get_service(MIDI_SERVICE)
    ch = client.services.get_characteristic(MIDI_CHAR)
    for s in client.services:
        r.line("   service  %s  %s" % (s.uuid, s.description))
        for k in s.characteristics:
            r.line("     char   %s  %s" % (k.uuid, ",".join(k.properties)))
    if svc is None or ch is None:
        r.result("connect", "FAIL", "connected, but the BLE-MIDI service or its characteristic is missing")
        return 1
    props = set(ch.properties)
    need = {"read", "write-without-response", "notify"}
    try:
        mtu = client.mtu_size
    except Exception:  # noqa: BLE001
        mtu = 23
    maxlen = max(20, min(c.args.max_packet, mtu - 3)) if c.args.max_packet else 20
    c.link = BleLink(r, client, ch, maxlen)
    try:
        v = await client.read_gatt_char(ch)
        r.line("   read     %d bytes %s (BLE-MIDI asks for an empty read)" % (len(v), bytes(v).hex(" ").upper()))
    except Exception as e:  # noqa: BLE001
        r.line("   read failed: %s" % e)
    try:
        await client.start_notify(ch, lambda _s, data: c.link.on_notify(data))
    except Exception as e:  # noqa: BLE001
        r.result("connect", "FAIL", "notifications could not be turned on: %s" % e)
        return 1
    r.line("   MTU      %s as BlueZ reports it (packets of %d bytes used)" % (mtu, maxlen))
    r.data["steps"]["connect"] = {"properties": sorted(props), "mtu_bluez": mtu}
    if not need <= props:
        r.result("connect", "FAIL", "MIDI characteristic lacks %s" % ",".join(sorted(need - props)))
        return 1
    r.result("connect", "PASS", "connected, BLE-MIDI service found, notifications on")
    return 0


def show_status(r, s, label=""):
    r.line("   %sBeat It %s  RADIO CAL %s  RECEIVE %s  SEND %s  CLOCK IN %s  CLOCK OUT %s" % (
        label, s["version"], s["radio_cal"], "ON" if s["receive"] else "OFF", "ON" if s["send"] else "OFF",
        s["clock_in"], s["clock_out"]))
    r.line("   %sIN %d  OUT %d  LOST in %d  LOST out %d" % (label, s["in"], s["out"], s["lost_in"], s["lost_out"]))
    r.line("   %slink: interval %.2f ms, timeout %d ms, MTU %d, data length %d, %s, second core %s" % (
        label, s["interval_ms"], s["timeout_ms"], s["mtu"], s["data_len"],
        "encrypted" if s["encrypted"] else "not encrypted", "stopped" if s["core2_stopped"] else "running"))
    if s.get("boot_start"):
        r.line("   %sthe radio started at power-on (Beat It 1.5: BT was saved ON)" % label)
    if s["hung_step"]:
        r.line("   %slast run reset during start-up step %d %s" % (label, s["hung_step"], STEP_NAMES.get(s["hung_step"], "")))


async def step_status(c):
    r = c.rep
    if not c.link:
        r.result("status", "SKIP", "not connected")
        return 2
    s = await c.link.status()
    if not s:
        c.has_status = False
        r.line("   No BT STATUS reply. Beat It 1.3 and older don't answer it; the other steps still run, and you read")
        r.line("   IN / OUT / LOST off the BT CONNECTION screen instead. On 1.4, a missing reply means the FM-1's")
        r.line("   notifications are not reaching this computer: the send step will tell.")
        r.result("status", "INFO", "no reply (Beat It older than 1.4, or nothing comes back from the FM-1)")
        return 0
    c.has_status = True
    show_status(r, s)
    r.data["steps"]["status"] = s
    issues = []
    if s["radio_cal"] == "MISSING":
        issues.append("RADIO CAL MISSING (yet the radio runs?)")
    if not s["listening"]:
        issues.append("the FM-1 says nobody has its notifications on")
    if s["version"] >= "1.5" and s["core2_stopped"]:
        r.line("   Beat It 1.5 keeps both cores running with Bluetooth on. A stopped second core means the FM-1 fell")
        r.line("   back to one core after a fault since power-on: write down what ABOUT shows and send this log.")
        issues.append("second core stopped (1.5 should keep it running)")
    if s["interval_ms"] and s["interval_ms"] > 50:
        r.line("   (a long connection interval: notes may come late in bunches; a laptop usually asks for 15-30 ms)")
    if issues:
        r.result("status", "FAIL", "; ".join(issues))
        return 1
    r.result("status", "PASS", "BT STATUS answered: Beat It %s, RADIO CAL %s, interval %.2f ms" % (
        s["version"], s["radio_cal"], s["interval_ms"]))
    return 0


async def step_receive(c):
    r = c.rep
    if not c.link:
        r.result("receive", "SKIP", "not connected")
        return 2
    ch = c.args.channel - 1
    notes = [60, 64, 67, 72, 67, 64, 60, 48]
    if c.wait_enter("Turn BT MIDI RECEIVE ON, pick a synth sound on a track that listens on MIDI channel %d."
                    % c.args.channel) == "s":
        r.result("receive", "SKIP", "skipped")
        return 0
    before = await c.link.status(quiet=True) if c.has_status else None
    if before and not before["receive"]:
        r.line("   (BT STATUS says BT MIDI RECEIVE is OFF: turn it on, then run this step again)")
    r.line("   playing %d notes on channel %d: %s" % (len(notes), c.args.channel, " ".join(note_name(n) for n in notes)))
    for n in notes:
        await c.link.send([(bytes([0x90 | ch, n, 100]), now_ts())])
        await asyncio.sleep(0.25)
        await c.link.send([(bytes([0x80 | ch, n, 0]), now_ts())])
        await asyncio.sleep(0.05)
    await asyncio.sleep(0.5)
    after = await c.link.status(quiet=True) if c.has_status else None
    heard = c.ask("Did the FM-1 play 8 notes (up and down a C chord, then a low C)? y/n", "y").lower().startswith("y")
    want = 2 * len(notes)
    if before and after:
        got = after["in"] - before["in"]
        lost = after["lost_in"] - before["lost_in"]
        r.line("   BT STATUS: IN %d -> %d (+%d of %d sent), LOST in +%d" % (before["in"], after["in"], got, want, lost))
        r.data["steps"]["receive"] = {"sent": want, "in": got, "lost": lost, "heard": heard}
        if got != want:
            r.result("receive", "FAIL", "the FM-1 counted %d of the %d messages sent" % (got, want))
            return 1
        if not heard:
            r.result("receive", "FAIL", "all %d arrived (IN counted them) but no sound: check the track's MIDI channel"
                     % want)
            return 1
        r.result("receive", "PASS", "all %d messages arrived and played" % want)
        return 0
    r.data["steps"]["receive"] = {"sent": want, "heard": heard}
    if heard:
        r.result("receive", "PASS", "you heard the notes (IN on BT CONNECTION should have gone up by %d)" % want)
        return 0
    r.result("receive", "FAIL", "no sound; note what IN on BT CONNECTION shows")
    return 1


async def step_send(c):
    r = c.rep
    if not c.link:
        r.result("send", "SKIP", "not connected")
        return 2
    if c.wait_enter("Turn BT MIDI SEND ON. After Enter, play pads or keys on the FM-1 for %d seconds." % c.args.play) == "s":
        r.result("send", "SKIP", "skipped")
        return 0
    before = await c.link.status(quiet=True) if c.has_status else None
    c.link.drain()
    if c.sim:
        c.sim.play([(bytes([0x99, 36 + i, 100]), 0.3 * i) for i in range(6)] +
                   [(bytes([0x89, 36 + i, 0]), 0.3 * i + 0.15) for i in range(6)])
    r.line("   listening %d s ..." % c.args.play)
    evs = await c.link.collect(c.args.play)
    ons = 0
    chans = set()
    for m, ts, arr in evs:
        if m.startswith(STATUS_HDR):
            continue
        r.line("   %s  (stamped %4d ms)" % (describe(m), ts))
        if (m[0] & 0xF0) == 0x90 and m[2]:
            ons += 1
        if m[0] < 0xF0:
            chans.add((m[0] & 15) + 1)
    msgs = [e for e in evs if e[0][0] < 0xF0]
    after = await c.link.status(quiet=True) if c.has_status else None
    res = {"messages": len(msgs), "note_ons": ons, "channels": sorted(chans)}
    verdict = 0
    if before and after:
        sent = after["out"] - before["out"]
        res.update(out=sent, lost_out=after["lost_out"] - before["lost_out"])
        r.line("   BT STATUS: OUT +%d, this computer got %d channel messages, LOST out +%d" % (sent, len(msgs), res["lost_out"]))
        if sent != len(msgs):
            r.result("send", "FAIL", "the FM-1 sent %d, %d arrived" % (sent, len(msgs)))
            verdict = 1
    if not msgs and verdict == 0:
        r.result("send", "FAIL", "nothing arrived from the FM-1 (is BT MIDI SEND on? did OUT count up?)")
        verdict = 1
    elif verdict == 0:
        r.result("send", "PASS", "%d messages arrived (%d notes) on channel(s) %s" % (
            len(msgs), ons, ",".join(map(str, sorted(chans)))))
    r.data["steps"]["send"] = res

    # the stuck-note check (1.3): SEND OFF ends what the FM-1 left on at the central
    if c.wait_enter("Stuck-note check: hold one or two pads down, then (still holding) turn BT MIDI SEND OFF, "
                    "then let go. You have 15 seconds after Enter.") == "s":
        r.result("stuck", "SKIP", "skipped")
        return verdict
    c.link.drain()
    if c.sim:
        c.sim.play([(bytes([0x99, 40, 100]), 0.5), (bytes([0x99, 43, 100]), 0.7),
                    (bytes([0x89, 40, 0]), 2.0), (bytes([0x89, 43, 0]), 2.0)])
    held = {}
    stuck_evs = await c.link.collect(3 if c.sim else 15)
    for m, ts, arr in stuck_evs:
        if m[0] >= 0xF0:
            continue
        r.line("   %s" % describe(m))
        k = m[0] & 0xF0
        chn = m[0] & 15
        if k == 0x90 and m[2]:
            held[(chn, m[1])] = True
        elif k in (0x80, 0x90):
            held.pop((chn, m[1]), None)
        elif k == 0xB0 and m[1] in (123, 120):
            for key in [x for x in held if x[0] == chn]:
                held.pop(key)
    if not stuck_evs:
        r.result("stuck", "INFO", "no notes arrived, nothing to check")
    elif held:
        r.result("stuck", "FAIL", "left on at this computer: " + ", ".join("%s ch %d" % (note_name(n), ch + 1)
                                                                     for ch, n in held))
        verdict = 1
    else:
        r.result("stuck", "PASS", "every note that went on was ended")
    return verdict


async def step_clockout(c):
    r = c.rep
    if not c.link:
        r.result("clockout", "SKIP", "not connected")
        return 2
    if c.wait_enter("Clock out: BT MIDI SEND ON, Menu > MIDI > CLOCK IN = INT, CLOCK OUT = BT. After Enter, press PLAY "
                    "on the FM-1, let it run, and press STOP after about 10 seconds.") == "s":
        r.result("clockout", "SKIP", "skipped")
        return 0
    tempo = c.ask("What TEMPO does the FM-1 show?", "120")
    try:
        tempo = float(tempo)
    except ValueError:
        tempo = 0.0
    if c.has_status:
        s = await c.link.status(quiet=True)
        if s and s["clock_out"] not in ("BT", "USB+BT"):
            r.line("   (BT STATUS: CLOCK OUT is %s, not BT or USB+BT: the FM-1 will not send its clock)" % s["clock_out"])
    c.link.drain()
    if c.sim:
        bpm = tempo or 120.0
        per = 60.0 / bpm / 24
        c.sim.play([(b"\xFA", 0.2)] + [(b"\xF8", 0.2 + i * per) for i in range(int(c.args.clock_s / per))] +
                   [(b"\xFC", 0.2 + c.args.clock_s)])
    evs = await c.link.collect(25, stop=lambda m: m == b"\xFC")
    f8 = [(ts, arr) for m, ts, arr in evs if m == b"\xF8"]
    starts = sum(1 for m, _, _ in evs if m in (b"\xFA", b"\xFB"))
    stops = sum(1 for m, _, _ in evs if m == b"\xFC")
    r.line("   START %d, clock pulses %d, STOP %d" % (starts, len(f8), stops))
    if len(f8) < 48:
        r.result("clockout", "FAIL", "fewer than 2 beats of clock arrived (%d pulses)" % len(f8))
        r.data["steps"]["clockout"] = {"pulses": len(f8), "start": starts, "stop": stops}
        return 1
    st = unwrap([t for t, _ in f8])
    ar = [a * 1000 for _, a in f8]
    dst = [b - a for a, b in zip(st, st[1:])]
    dar = [b - a for a, b in zip(ar, ar[1:])]
    span = st[-1] - st[0]
    bpm_ts = 60000.0 * (len(st) - 1) / 24 / span if span > 0 else 0
    bpm_ar = 60000.0 * (len(ar) - 1) / 24 / (ar[-1] - ar[0]) if ar[-1] > ar[0] else 0
    jit_ts = statistics.pstdev(dst)
    jit_ar = statistics.pstdev(dar)
    r.line("   tempo from the FM-1's timestamps %.2f BPM (pulse spacing jitter %.2f ms)" % (bpm_ts, jit_ts))
    r.line("   tempo by arrival time            %.2f BPM (arrival jitter %.2f ms: what an app that ignores "
           "timestamps would feel)" % (bpm_ar, jit_ar))
    res = {"pulses": len(f8), "start": starts, "stop": stops, "bpm_timestamps": round(bpm_ts, 3),
           "bpm_arrival": round(bpm_ar, 3), "jitter_ts_ms": round(jit_ts, 3), "jitter_arrival_ms": round(jit_ar, 3),
           "tempo_shown": tempo}
    r.data["steps"]["clockout"] = res
    bad = []
    if tempo and abs(bpm_ts - tempo) > max(0.5, tempo * 0.01):
        bad.append("measured %.2f BPM, the FM-1 shows %g" % (bpm_ts, tempo))
    if not starts:
        bad.append("no START")
    if not stops:
        bad.append("no STOP within 25 s")
    if bad:
        r.result("clockout", "FAIL", "; ".join(bad))
        return 1
    r.result("clockout", "PASS", "START, %d pulses at %.2f BPM, STOP" % (len(f8), bpm_ts))
    return 0


async def step_clockin(c):
    r = c.rep
    if not c.link:
        r.result("clockin", "SKIP", "not connected")
        return 2
    bpm = c.args.bpm
    if c.wait_enter("Clock in: BT MIDI RECEIVE ON, Menu > MIDI > CLOCK IN = BT (CLOCK OUT can stay as it is). "
                    "After Enter this computer sends START, %g BPM for %g seconds, then STOP." % (bpm, c.args.clock_s)) == "s":
        r.result("clockin", "SKIP", "skipped")
        return 0
    before = await c.link.status(quiet=True) if c.has_status else None
    if before and before["clock_in"] != "BT":
        r.line("   (BT STATUS: CLOCK IN is %s, not BT: the FM-1 ignores a Bluetooth clock)" % before["clock_in"])
    if c.sim:
        c.sim.st["clock_in"] = "BT"
    per = 60.0 / bpm / 24
    n = int(c.args.clock_s / per)
    await c.link.send([(b"\xFA", now_ts())])
    t0 = time.monotonic()
    for i in range(n):
        dt = t0 + i * per - time.monotonic()
        if dt > 0:
            await asyncio.sleep(dt)
        await c.link.send([(b"\xF8", now_ts())])
    await asyncio.sleep(per)
    await c.link.send([(b"\xFC", now_ts())])
    took = time.monotonic() - t0
    r.line("   sent START, %d pulses in %.2f s (%.2f BPM as sent), STOP" % (n, took, 60.0 * n / 24 / took))
    await asyncio.sleep(0.5)
    after = await c.link.status(quiet=True) if c.has_status else None
    followed = c.ask("Did the FM-1 start, show TEMPO about %g, and stop? y/n" % bpm, "y").lower().startswith("y")
    res = {"sent": n + 2, "bpm": bpm, "followed": followed}
    if c.sim:
        c.sim.st["clock_in"] = "INT"
    if before and after:
        got = after["in"] - before["in"]
        res.update(arrived=got, lost=after["lost_in"] - before["lost_in"])
        r.line("   BT STATUS: IN +%d of %d sent (START, the pulses, STOP), LOST in +%d" % (got, n + 2, res["lost"]))
        r.data["steps"]["clockin"] = res
        if got != n + 2:
            why = "CLOCK IN is not BT" if got == 0 and after["clock_in"] != "BT" else "some never arrived"
            r.result("clockin", "FAIL", "the FM-1 took %d of %d clock messages (%s)" % (got, n + 2, why))
            return 1
    r.data["steps"]["clockin"] = res
    if not followed:
        r.result("clockin", "FAIL", "the FM-1 did not follow the clock")
        return 1
    r.result("clockin", "PASS", "the FM-1 followed %g BPM, started and stopped" % bpm)
    return 0


async def step_stress(c):
    r = c.rep
    if not c.link:
        r.result("stress", "SKIP", "not connected")
        return 2
    if not c.has_status:
        r.result("stress", "SKIP", "needs Beat It 1.4's BT STATUS to count what arrived")
        return 0
    if c.wait_enter("Stress: BT MIDI RECEIVE ON. The FM-1 gets 600 short notes as fast as the link takes them "
                    "(turn the volume down).") == "s":
        r.result("stress", "SKIP", "skipped")
        return 0
    ch = c.args.channel - 1
    before = await c.link.status(quiet=True)
    t0 = time.monotonic()
    k = 0
    for i in range(300):
        n = 48 + (i * 7) % 36
        await c.link.send([(bytes([0x90 | ch, n, 90]), now_ts()), (bytes([0x80 | ch, n, 0]), now_ts())])
        k += 2
        if i % 10 == 9:
            await asyncio.sleep(0.01)
    took = time.monotonic() - t0
    await asyncio.sleep(1.0)
    after = await c.link.status(quiet=True)
    alive = after is not None
    res = {"sent": k, "seconds": round(took, 3)}
    if before and after:
        got = after["in"] - before["in"]
        lost = after["lost_in"] - before["lost_in"]
        res.update(arrived=got, lost=lost)
        r.line("   sent %d messages in %.2f s; FM-1: IN +%d, LOST in +%d, unaccounted %d" % (k, took, got, lost, k - got - lost))
    r.data["steps"]["stress"] = res
    if not alive:
        r.result("stress", "FAIL", "no BT STATUS after the burst: the link or the FM-1 stopped (note what the screen shows)")
        return 1
    if got + lost != k:
        r.result("stress", "FAIL", "%d messages neither played nor counted as lost (dropped before the FM-1 saw them)"
                 % (k - got - lost))
        return 1
    r.result("stress", "PASS" if lost == 0 else "INFO", "%d of %d arrived, %d dropped by the FM-1 and counted (LOST)"
             % (got, k, lost))
    return 0


STEPS = {"env": step_env, "scan": step_scan, "connect": step_connect, "status": step_status, "receive": step_receive,
         "send": step_send, "clockout": step_clockout, "clockin": step_clockin, "stress": step_stress}
NEEDS_LINK = {"status", "receive", "send", "clockout", "clockin", "stress"}


async def main_async(args, rep):
    c = Ctx(args, rep)
    c.has_status = False
    if args.selftest_run:
        c.sim = SimLink(rep, version=args.sim_version, lose_every=args.sim_lose)
    steps = [s.strip() for s in args.steps.split(",") if s.strip()]
    for s in steps:
        if s not in STEPS:
            print("unknown step %r (steps: %s)" % (s, ",".join(ALL_STEPS)))
            return 2
    if any(s in NEEDS_LINK for s in steps):
        for s in ("scan", "connect"):
            if s not in steps:
                steps.insert(0 if s == "scan" else steps.index("scan") + 1, s)
        if "status" not in steps:
            steps.insert(steps.index("connect") + 1, "status")
    worst = 0
    try:
        for s in steps:
            rep.head("%s" % s)
            try:
                rc = await STEPS[s](c)
            except Exception as e:  # noqa: BLE001 (logged, the summary still written)
                import traceback
                rep.line("   unexpected error: %s: %s" % (type(e).__name__, e))
                for ln in traceback.format_exc().splitlines():
                    rep.line("   | " + ln, quiet=True)
                rep.result(s, "FAIL", "error %s (details in the log)" % type(e).__name__)
                rc = 2 if s in ("scan", "connect") else 1
            worst = max(worst, rc)
            if rc in (2, 3) and s in ("env", "scan", "connect"):
                if s == "env" and not args.force:
                    rep.line("   (stopping: fix the above first, or run with --force)")
                    break
                if s != "env":
                    break
        if c.link and len(steps) > 3 and c.has_status:
            rep.head("final BT STATUS")
            s = await c.link.status(quiet=True)
            if s:
                show_status(rep, s)
                rep.data["final_status"] = s
            else:
                rep.line("   no reply")
        if c.link:
            rep.data["decode_errors"] = c.link.dec.errors
            rep.data["notifications"] = c.link.notifications
            if c.link.dec.errors:
                rep.line("   %d BLE-MIDI packets from the FM-1 did not decode cleanly (see the raw lines)" % c.link.dec.errors)
    finally:
        if c.client is not None:
            try:
                await c.client.disconnect()
            except Exception:  # noqa: BLE001
                pass
        if c.sim:
            for t in c.sim.tasks:
                t.cancel()
    return worst


def summary(rep):
    rep.head("summary")
    for s, v, w in rep.results:
        rep.line("   %-9s %-5s %s" % (s, v, w))
    if rep.path:
        rep.line("")
        rep.line("   The whole run, every packet included, is in %s" % rep.path)
        rep.line("   Send that file back (with a photo of BT CONNECTION if a step failed).")


# ---------------------------------------------------------------------------------------------------- self-test
def selftest_codec():
    ok = True

    def check(c, what):
        nonlocal ok
        print("selftest: %-80s %s" % (what, "ok" if c else "FAIL"))
        ok &= bool(c)

    d = Decoder()
    ev = d.feed(bytes([0x80, 0x80, 0x90, 0x3C, 0x64, 0x81, 0x3E, 0x64]))
    check(ev == [(bytes([0x90, 0x3C, 0x64]), 0), (bytes([0x90, 0x3E, 0x64]), 1)], "two notes, a timestamp each")
    ev = d.feed(bytes([0x80, 0x85, 0x90, 0x3C, 0x64, 0x40, 0x7F]))
    check(ev == [(bytes([0x90, 0x3C, 0x64]), 5), (bytes([0x90, 0x40, 0x7F]), 5)], "running status without a timestamp")
    ev = d.feed(bytes([0x80, 0x85, 0x90, 0x3C, 0x64, 0x86, 0x40, 0x00]))
    check(ev[1] == (bytes([0x90, 0x40, 0x00]), 6), "running status with a timestamp")
    ev = d.feed(bytes([0x81, 0xFF, 0xF8, 0x80, 0xF8]))
    check(ev == [(b"\xF8", 255), (b"\xF8", 256)], "the low timestamp wrapping inside a packet")
    ev = d.feed(bytes([0x80, 0x80, 0xF0, 0x7D, 0x42, 0x81, 0xF8, 0x49, 0x01, 0x82, 0xF7]))
    check(ev == [(b"\xF8", 1), (STATUS_Q, 2)], "a clock inside a SysEx")
    ev = d.feed(bytes([0x80, 0x80, 0xF0, 0x7D])) + d.feed(bytes([0x80, 0x42, 0x49, 0x01, 0x83, 0xF7]))
    check(ev == [(STATUS_Q, 3)], "a SysEx over two packets")
    errs = d.errors
    d.feed(bytes([0x00, 0x90]))
    check(d.errors == errs + 1, "a packet without a header is refused")
    st = {"layout": 1, "version": "1.4", "radio_cal": "BACKUP", "receive": True, "send": False, "listening": True,
          "encrypted": False, "core2_stopped": True, "draining": False, "boot_start": False, "hung_step": 4, "clock_in": "BT",
          "clock_out": "USB+BT", "in": 300000, "out": 2, "lost_in": 0, "lost_out": 9, "interval_ms": 7.5,
          "timeout_ms": 5000, "mtu": 185, "data_len": 251}
    sb = status_bytes(st)
    check(len(sb) == STATUS_LEN and parse_status(sb) == st, "BT STATUS: 38 bytes, every field back")
    st15 = dict(st, version="1.5", core2_stopped=False, boot_start=True)
    check(parse_status(status_bytes(st15)) == st15 and status_bytes(st15)[5 + 4] & 64,
          "BT STATUS 1.5: started at power-on (flag 64), second core running")
    for cap in (20, 23, 64, 182):
        msgs = [(bytes([0x90, 60, 100]), 10), (sb, 11), (b"\xF8", 12), (bytes([0xB0, 64, 0]), 140), (STATUS_Q, 200),
                (bytes([0xC3, 5]), 8191)]
        pk = encode(msgs, cap)
        d2 = Decoder()
        back = [e for p in pk for e in d2.feed(p)]
        check(all(len(p) <= cap for p in pk) and back == [(m, t & 0x1FFF) for m, t in msgs] and not d2.errors,
              "encode -> decode, packets of %d bytes: %d packets, every message and timestamp back" % (cap, len(pk)))
    check(unwrap([8100, 8180, 70, 150]) == [8100, 8180, 8262, 8342], "timestamps unwrapped across 8.192 s")
    return ok


def selftest(args):
    ok = selftest_codec()
    scenarios = [
        ("Beat It 1.5, everything works", "1.5", 0, ["", "y", "", "", "", "120", "", "y", ""], 0),
        ("Beat It 1.4, everything works", "1.4", 0, ["", "y", "", "", "", "120", "", "y", ""], 0),
        ("Beat It 1.3 (no BT STATUS): steps still run on your answers", "1.3", 0, ["", "y", "", "", "", "120", "", "y"], 0),
        ("Beat It 1.4, the FM-1 drops every 7th message: receive, clockin and stress must notice", "1.4", 7,
         ["", "y", "s", "s", "", "y", ""], 1),
        ("Beat It 1.4, the FM-1's TEMPO is not what its clock says: clockout must fail", "1.4", 0,
         ["s", "s", "", "97", "s", "s"], None),
    ]
    for title, ver, lose, answers, want in scenarios:
        print("\nselftest: --- %s" % title)
        a = argparse.Namespace(**vars(args))
        a.selftest_run, a.sim_version, a.sim_lose, a.answers = True, ver, lose, answers
        a.steps, a.play, a.clock_s = ",".join(ALL_STEPS), 2, 4.0
        rep = Report(None, False, mute=not args.verbose)
        if "TEMPO" in title:
            # the simulated FM-1 plays at 120 BPM, the person reads 97 off the screen
            rc = asyncio.run(_tempo_case(a, rep))
            res = dict((s, v) for s, v, _ in rep.results)
            good = res.get("clockout") == "FAIL"
        else:
            rc = asyncio.run(main_async(a, rep))
            res = dict((s, v) for s, v, _ in rep.results)
            if want == 0:
                good = rc == 0 and all(v in ("PASS", "INFO", "SKIP") for v in res.values())
                if ver >= "1.4":
                    good &= all(res.get(s) == "PASS" for s in ("status", "receive", "send", "stuck", "clockout", "clockin", "stress"))
                else:
                    good &= res.get("status") == "INFO" and res.get("stress") == "SKIP"
            else:
                good = rc == 1 and res.get("receive") == "FAIL" and res.get("clockin") == "FAIL" and res.get("stress") == "INFO"
        for st_, v_, w_ in rep.results:
            print("selftest:     %-9s %-5s %s" % (st_, v_, w_))
        print("selftest: %-80s %s" % (title + " -> " + " ".join("%s=%s" % kv for kv in res.items()), "ok" if good else "FAIL"))
        ok &= good
    print("\nselftest: %s" % ("all passed" if ok else "FAILED"))
    return 0 if ok else 1


async def _tempo_case(a, rep):
    """the clock the simulated FM-1 sends is 120 BPM whatever the person answers"""
    orig = SimLink.play

    def play(self, msgs):
        fixed = []
        per120 = 60.0 / 120 / 24
        i = 0
        for m, dt in msgs:
            if m == b"\xF8":
                fixed.append((m, 0.2 + i * per120))
                i += 1
            else:
                fixed.append((m, dt))
        return orig(self, fixed)
    SimLink.play = play
    try:
        return await main_async(a, rep)
    finally:
        SimLink.play = orig


def main():
    ap = argparse.ArgumentParser(description="Beat It Bluetooth MIDI test (Linux, BlueZ, bleak)",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split("\n\n", 1)[1])
    ap.add_argument("--steps", default=",".join(ALL_STEPS), help="comma-separated, from: " + ",".join(ALL_STEPS))
    ap.add_argument("--address", help="the FM-1's Bluetooth address (skips the name search)")
    ap.add_argument("--channel", type=int, default=1, help="MIDI channel the test notes go on (1-16, default 1)")
    ap.add_argument("--bpm", type=float, default=100.0, help="tempo of the clock sent in the clockin step (default 100)")
    ap.add_argument("--clock-seconds", dest="clock_s", type=float, default=8.0, help="how long the clockin step sends "
                    "the clock (default 8)")
    ap.add_argument("--scan", type=float, default=12.0, help="seconds to scan (default 12)")
    ap.add_argument("--play", type=float, default=10.0, help="seconds to listen in the send step (default 10)")
    ap.add_argument("--max-packet", type=int, default=0, help="BLE-MIDI packet size to write (default 20, the size "
                    "every link takes)")
    ap.add_argument("--log", help="log file (default ~/beatit-bt-test-DATE-TIME.log)")
    ap.add_argument("--raw", action="store_true", help="also print every packet sent and received")
    ap.add_argument("--yes", action="store_true", help="take the default answer to every question")
    ap.add_argument("--force", action="store_true", help="go on even if the env step finds a problem")
    ap.add_argument("--selftest", action="store_true", help="no Bluetooth: test this script against a simulated FM-1")
    ap.add_argument("--verbose", action="store_true", help="(--selftest) print every step's output too")
    args = ap.parse_args()
    args.selftest_run = False
    args.answers = None
    args.sim_version, args.sim_lose = "1.5", 0
    if not 1 <= args.channel <= 16:
        ap.error("--channel is 1 to 16")
    if args.selftest:
        return selftest(args)
    path = args.log or os.path.expanduser(datetime.datetime.now().strftime("~/beatit-bt-test-%Y%m%d-%H%M%S.log"))
    rep = Report(path, args.raw)
    rep.line("Beat It Bluetooth test %s, %s" % (TOOL_VERSION, datetime.datetime.now().strftime("%Y-%m-%d %H:%M")))
    rep.line("Back up your projects before you flash anything; this script itself only sends MIDI.")
    try:
        rc = asyncio.run(main_async(args, rep))
    except KeyboardInterrupt:
        rep.line("\nstopped (Ctrl-C)")
        rc = 130
    summary(rep)
    rep.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
