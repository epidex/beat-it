/* SPDX-License-Identifier: GPL-3.0-only */
/* (Beat It 1.2) renders the BLUETOOTH and MIDI menus of a Bluetooth build (DIR/bt12-*.ppm) and checks their text.
 * Preamble from tests/beatit_rfcal_test.c. Original header follows.
 * (Beat It) the radio calibration backup end to end, on the real storage.c / project.c / panel.c / ui*.c (editor_host.c:
 * a RAM image of the 1 MiB NOR, the flash ranges as hal/fm1_flash.h) and the real midi_ble.c / ble/ble_vm.c:
 *   stock V15's VM at 0x0E8000 -> ble_midi_init takes it -> the main loop's settings saves write the backup into BOTH
 *   settings copies (after the 80-byte side record) -> the VM is never written -> the VM erased (a later flash) -> the next
 *   boot runs on the backup -> later settings saves keep it -> settings with no backup and no VM: nothing invented.
 * It does not test the radio: nothing here transmits. Exit status: nonzero on a failed check. */
#include <stdint.h>
#define FELUCCA_BLE 1
#define FELUCCA_BLE_STUB 1
#define BLE_API static
#define BLE_DEVICE_NAME "BEAT IT"
#define BLE_NAME_SEP " "
static void ble_midi_out(uint32_t pkt);
static void ble_midi_poll(void);
static void ble_midi_init(void);
static void ble_midi_set(uint8_t rx, uint8_t tx);
static int ble_radio_ok(void);
static const char *ble_link_text(void);
static uint8_t ble_cpu1_stopped;
static const char *ble_cal_text(void);
static const char *ble_cal_sub(void);
static const char *ble_link_sub(int *warn);
static uint32_t ble_menu_sig(void);
#define BLE_RF_COPY_SIZE_ 100u
static uint8_t ble_rf_bak[BLE_RF_COPY_SIZE_];
static uint8_t ble_rf_bak_owed;
static int ble_rf_copy_ok(const uint8_t *copy);
#include "../firmware/hal/fm1_flash_id.h"
#include "editor_host.c"
static void fm1_ble_irqs_hold(int on) { (void)on; }
#include "../firmware/src/midi_ble.c"


static int bad;
static void ck(int ok, const char *what) { printf("bt12: %-90s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static const char *outdir = ".";
static void ppm(const char *name)
{
    char path[512];
    unsigned i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t p = swap16(screen[i]);
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void show(uint32_t sel, const char *name)
{
    ui.menu = 1;
    ui.menu_sel = (uint8_t)sel;
    ui.force = 1;
    draw_menu();
    ui.force = 0;
    ppm(name);
}
int main(int argc, char **argv)
{
    int warn;
    uint32_t s0;
    if (argc > 1)
        outdir = argv[1];
    harness_init();
    ble_midi_init();
    /* a start the last run did not finish (the breadcrumb a watchdog reset keeps) */
    ble_hung_step = BC_RF_INIT;
    show(MI_BTLINK, "bt12-1-reset-during");
    ck(!strcmp(ble_link_sub(&warn), "RESET DURING: RF INIT") && warn, "BT CONNECTION: RESET DURING: RF INIT (amber)");
    /* on, traffic counted */
    ble_midi_set(1, 1);
    bt_rx = bt_tx = 1;
    ble_rx_count = 248, ble_tx_count = 96, ble_rx_lost = 0;
    show(MI_BTLINK, "bt12-2-connection-counts");
    ck(!strcmp(ble_link_sub(&warn), "IN 248 OUT 96") && !warn, "BT CONNECTION: IN 248 OUT 96");
    s0 = ui.menu_sig;
    ble_rx_count = 249;
    {
        uint32_t t = fm1_ms;
        fm1_ms += 600u;
        draw_menu();
        fm1_ms = t;
    }
    ck(ui.menu_sig != s0, "the menu redraws itself when the counts move (no knob turned)");
    /* MIDI: CLOCK IN = BT with RECEIVE OFF */
    song.g[G_SYNC] = 3;
    ble_midi_set(0, 1);
    bt_rx = 0;
    show(MI_MSYNC, "bt12-3-midi-clock-in-bt");
    ck(!strcmp(mi_value(MI_MSYNC, &(uint16_t){0}), "BT"), "MIDI > CLOCK IN shows BT");
    /* (1.3) MIDI: CLOCK OUT = USB+BT (the FM-1 the clock master: SYNC INT) */
    song.g[G_SYNC] = 0;
    song.g[G_CLKOUT] = 3;
    ble_midi_set(1, 1);
    bt_rx = bt_tx = 1;
    show(MI_MCLK, "bt13-4-midi-clock-out-usb-bt");
    ck(!strcmp(mi_value(MI_MCLK, &(uint16_t){0}), "USB+BT"), "1.3: MIDI > CLOCK OUT shows USB+BT");
    song.g[G_CLKOUT] = 2;
    show(MI_MCLK, "bt13-5-midi-clock-out-bt");
    ck(!strcmp(mi_value(MI_MCLK, &(uint16_t){0}), "BT"), "1.3: MIDI > CLOCK OUT shows BT");
    printf("beatit_bt_screens: %d failed\n", bad);
    return bad;
}
