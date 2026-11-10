#!/usr/bin/env python3
"""(1.4) The Linux test tool's real Bluetooth path (step_scan / step_connect / BleLink) through bleak's own API
objects: BleakScanner.discover and BleakClient are replaced by fakes built from bleak's real BLEDevice,
AdvertisementData and GATT classes, and the FM-1 behind them is the tool's simulated Beat It 1.4. Catches a wrong
attribute, a wrong callback shape, a wrong call against the installed bleak. Needs bleak (pip install bleak);
without it, says so and passes (the CI's Python has none). No radio: this is not a Bluetooth test."""
import asyncio
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import beatit_bt_test as bt  # noqa: E402

try:
    import bleak
    from bleak.backends.characteristic import BleakGATTCharacteristic
    from bleak.backends.device import BLEDevice
    from bleak.backends.scanner import AdvertisementData
    from bleak.backends.service import BleakGATTService, BleakGATTServiceCollection
except ImportError:
    print("bt_tool_fake_bleak: bleak not installed here, skipped (pip install bleak to run it)")
    sys.exit(0)

ADDR = "C4:1F:00:00:5A:3C"


def adv(name, uuids, rssi):
    try:
        return AdvertisementData(local_name=name, manufacturer_data={}, service_data={}, service_uuids=uuids,
                                 tx_power=None, rssi=rssi, platform_data=())
    except TypeError:                                 # (older bleak: the same fields, positional)
        return AdvertisementData(name, {}, {}, uuids, None, rssi, ())


class FakeScanner:
    @staticmethod
    async def discover(timeout=5.0, *, return_adv=False, **kw):
        assert return_adv, "the tool reads advertisements"
        devs = {
            ADDR: (BLEDevice(ADDR, "BEAT IT 5A3C", None), adv("BEAT IT 5A3C", [bt.MIDI_SERVICE], -61)),
            "11:22:33:44:55:66": (BLEDevice("11:22:33:44:55:66", "Some Keyboard", None),
                                  adv("Some Keyboard", [bt.MIDI_SERVICE], -70)),
            "AA:AA:AA:AA:AA:AA": (BLEDevice("AA:AA:AA:AA:AA:AA", None, None), adv(None, [], -90)),
        }
        return devs


class FakeClient:
    instances = []

    def __init__(self, address_or_ble_device, disconnected_callback=None, services=None, *, timeout=30, **kw):
        self.dev = address_or_ble_device
        self.connected = False
        self.cb = None
        self.writes = []
        col = BleakGATTServiceCollection()
        svc = BleakGATTService(None, 10, bt.MIDI_SERVICE)
        ch = BleakGATTCharacteristic(None, 12, bt.MIDI_CHAR, ["read", "write-without-response", "notify"],
                                     lambda: 20, svc)
        col.add_service(svc)
        col.add_characteristic(ch)
        self.services = col
        self.ch = ch
        self.fw = bt.SimLink(bt.Report(None, False))
        self.fw.on_notify = self._notify
        self.fw.st["clock_in"] = "BT"                 # (the person set CLOCK IN to BT, as the step asks)
        FakeClient.instances.append(self)

    def _notify(self, data):
        if self.cb:
            self.cb(self.ch, bytearray(data))

    @property
    def mtu_size(self):
        return 23

    async def connect(self, **kw):
        assert isinstance(self.dev, BLEDevice) and self.dev.address == ADDR, "connects to the FM-1 found"
        self.connected = True

    async def disconnect(self):
        self.connected = False

    async def read_gatt_char(self, c, **kw):
        assert c is self.ch
        return bytearray()

    async def start_notify(self, c, callback, **kw):
        assert c is self.ch
        self.cb = callback

    async def write_gatt_char(self, c, data, response=None):
        assert c is self.ch and response is False and len(data) <= 20, "write without response, 20 bytes at most"
        self.writes.append(bytes(data))
        await self.fw.write(bytes(data))


def clock_not_bt():
    import argparse
    orig = FakeClient.__init__

    def init(self, *a, **k):
        orig(self, *a, **k)
        self.fw.st["clock_in"] = "INT"
    FakeClient.__init__ = init
    try:
        a = argparse.Namespace(steps="clockin", address=None, channel=1, bpm=120.0, clock_s=1.0, scan=1.0, play=1.0,
                               max_packet=0, log=None, raw=False, yes=True, force=True, selftest=False,
                               selftest_run=False, answers=None, sim_version="1.4", sim_lose=0)
        rep = bt.Report(None, False, mute=True)
        rc = asyncio.run(bt.main_async(a, rep))
        why = [w for s, v, w in rep.results if s == "clockin" and v == "FAIL"]
        return rc == 1 and why and "CLOCK IN is not BT" in why[0]
    finally:
        FakeClient.__init__ = orig


def main():
    bleak.BleakScanner = FakeScanner
    bleak.BleakClient = FakeClient
    fails = 0
    import argparse
    a = argparse.Namespace(steps="scan,connect,status,receive,clockin,stress", address=None, channel=1, bpm=120.0,
                           clock_s=2.0, scan=1.0, play=1.0, max_packet=0, log=None, raw=False, yes=True, force=True,
                           selftest=False, selftest_run=False, answers=None, sim_version="1.4", sim_lose=0)
    rep = bt.Report(None, False, mute=True)
    rc = asyncio.run(bt.main_async(a, rep))
    res = dict((s, v) for s, v, _ in rep.results)
    cl = FakeClient.instances[-1] if FakeClient.instances else None
    for what, ok in [
        ("scan picks BEAT IT 5A3C out of 3 devices (not the other BLE-MIDI keyboard)", res.get("scan") == "PASS" and
         rep.data["steps"]["scan"].get("address") == ADDR),
        ("connect: the BLE-MIDI service and characteristic found through bleak's collection", res.get("connect") == "PASS"),
        ("status: BT STATUS asked by write-without-response, answered by notification", res.get("status") == "PASS"),
        ("receive / clockin / stress counted right by the FM-1", all(res.get(s) == "PASS" for s in ("receive", "clockin", "stress"))),
        ("every write 20 bytes or less, disconnected at the end", cl is not None and cl.writes and not cl.connected),
        ("exit code 0", rc == 0),
        ("a FM-1 with CLOCK IN not BT: clockin fails and says why", clock_not_bt()),
    ]:
        print("bt_tool_fake_bleak: %-84s %s" % (what, "ok" if ok else "FAIL"))
        fails += 0 if ok else 1
    if fails:
        print(res)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
