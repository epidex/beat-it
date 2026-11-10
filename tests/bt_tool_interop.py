#!/usr/bin/env python3
"""(1.4) The Linux test tool against the firmware's own BLE-MIDI code: tools/beatit_bt_test.py encodes notes, the
clock and BT STATUS questions; the firmware (midi_ble.c through the host build of the BLE stack) decodes them, counts
them and answers; the tool decodes the answers. Usage: bt_tool_interop.py <beatit_ble_test binary>"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import beatit_bt_test as bt  # noqa: E402

fails = 0


def check(c, what):
    global fails
    print("bt_tool_interop: %-86s %s" % (what, "ok" if c else "FAIL"))
    fails += 0 if c else 1


def run(binary, packets):
    with tempfile.TemporaryDirectory() as d:
        fi, fo = os.path.join(d, "in"), os.path.join(d, "out")
        with open(fi, "w") as f:
            for p in packets:
                f.write(p.hex().upper() + "\n")
        r = subprocess.run([binary, "--interop", fi, fo], capture_output=True, text=True)
        if r.returncode:
            return None, None
        out = open(fo).read().splitlines()
    pk = [bytes.fromhex(l) for l in out if l and not l.startswith("#")]
    tail = [l for l in out if l.startswith("#")][0].split()
    return pk, {"in": int(tail[2]), "lost": int(tail[4]), "replies": int(tail[6])}


def main():
    binary = sys.argv[1]
    for cap in (20, 23, 64):
        notes = []
        for i in range(8):
            notes += [(bytes([0x90, 60 + i, 100]), 100 + i), (bytes([0x80, 60 + i, 0]), 101 + i)]
        msgs = [(bt.STATUS_Q, 50)] + notes + [(b"\xFA", 300), (b"\xF8", 301), (b"\xF8", 302), (b"\xFC", 303),
                                              (bt.STATUS_Q, 400)]
        packets = bt.encode(msgs, cap)
        pk, n = run(binary, packets)
        check(pk is not None, "packets of %d: the firmware took the tool's %d packets" % (cap, len(packets)))
        if pk is None:
            continue
        d = bt.Decoder()
        evs = [e for p in pk for e in d.feed(p)]
        st = [bt.parse_status(m) for m, _ in evs if m.startswith(bt.STATUS_HDR)]
        check(n["in"] == 20 and n["lost"] == 0, "... the firmware counted IN %d (16 note on/off + START, 2 clocks, STOP = 20)" % n["in"])
        check(n["replies"] == 2 and len(st) == 2 and all(st) and not d.errors,
              "... both BT STATUS questions answered, the tool decoded %d replies from %d notifications" % (len(st), len(pk)))
        if len(st) == 2 and all(st):
            a, b = st
            check(a["version"] == "1.5" and a["radio_cal"] == "BACKUP" and a["clock_in"] == "BT" and a["receive"] and a["send"],
                  "... reply fields: Beat It 1.5, RADIO CAL BACKUP, CLOCK IN BT, RECEIVE + SEND on")
            check(a["in"] < b["in"] == 20, "... IN %d in the first reply (what shared its write), 20 in the last (asked after everything)" % a["in"])
            check(all(len(p) <= 20 for p in pk), "... every notification within 20 bytes (the default ATT MTU)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
