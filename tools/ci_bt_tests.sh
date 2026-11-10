#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# (Beat It 1.4) the Bluetooth tests alone, for the CI step that must pass (tests/run_tests.sh as a whole carries
# SLOOP's known failures for the sounds Beat It removed, so it can't block a build; these have no known failures).
# Needs build/gen (./build.sh or tools/ci_build_beatit.sh first). Simulator and host tests only: no radio.
set -e
cd "$(dirname "$0")/.."
OUT=build/bt-tests
mkdir -p "$OUT"
BLEC="cc -std=c99 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover=all"
fail=0
t() { echo "== $1"; shift; "$@" || fail=1; }
$BLEC -o "$OUT/ble_prim_test" tests/ble_prim_test.c && t "BLE primitives" "$OUT/ble_prim_test"
$BLEC -o "$OUT/ble_stack_test" tests/ble_stack_test.c && t "BLE stack against a simulated central" "$OUT/ble_stack_test"
$BLEC -o "$OUT/ble_midi_test" tests/ble_midi_test.c && t "BLE-MIDI codec" "$OUT/ble_midi_test"
$BLEC -Itests/ble_fake -o "$OUT/ble_driver_test" tests/ble_driver_test.c && t "BLE radio driver (register double)" "$OUT/ble_driver_test"
$BLEC -o "$OUT/ble_vm_test" tests/ble_vm_test.c && t "radio calibration parser" sh -c "cd tests && '$(pwd)/$OUT/ble_vm_test'"
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover=all -w -Ibuild/gen -Ifirmware/src \
    -o "$OUT/beatit_ble_test" tests/beatit_ble_test.c -lm && t "Beat It Bluetooth MIDI, BT STATUS" "$OUT/beatit_ble_test"
mkdir -p "$OUT/gen"
{ echo '#define SMP_DATA (smp_data_v)'; sed 's/^static const uint8_t SMP_DATA\[/static const uint8_t SMP_DATA_ARR[/' build/gen/felucca_samples.h;
  echo 'static const uint8_t *volatile smp_data_v = SMP_DATA_ARR;'; } > "$OUT/gen/felucca_samples.h"
EHC="cc -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-sanitize-recover=all -I$OUT/gen -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
$EHC -o "$OUT/beatit_rfcal_test" tests/beatit_rfcal_test.c -lm && t "radio calibration backup" "$OUT/beatit_rfcal_test"
t "Linux test tool against the firmware's BLE-MIDI code" python3 tests/bt_tool_interop.py "$OUT/beatit_ble_test"
t "Linux test tool self-test" python3 tools/beatit_bt_test.py --selftest
t "Linux test tool through bleak's API" python3 tests/bt_tool_fake_bleak.py
[ "$fail" = 0 ] && echo "ci_bt_tests: all passed" || echo "ci_bt_tests: FAILED"
exit $fail
