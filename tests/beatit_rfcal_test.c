/* SPDX-License-Identifier: GPL-3.0-only */
/* (Beat It) the radio calibration backup end to end, on the real storage.c / project.c / panel.c / ui*.c (editor_host.c:
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
static void ck(int ok, const char *what)
{
    printf("rfcal: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t vput;
static void vrec(uint32_t id, const uint8_t *d, uint32_t n)
{
    uint8_t *p = nor + vput;
    p[0] = (uint8_t)ble_crc16_xmodem(0, d, n);
    p[1] = (uint8_t)id;
    p[2] = (uint8_t)((id >> 8) & 15u) | (uint8_t)((n & 15u) << 4);
    p[3] = (uint8_t)(n >> 4);
    memcpy(p + 4, d, n);
    vput += 4 + n;
}
static struct ble_rf_trims want;
static void vm_make(uint8_t fill)                    /* stock V15's VM at 0x0E8000: the four calibration records */
{
    uint32_t i, c;
    memset(nor + BLE_VM_HW, 0xFF, BLE_VM_HW_SIZE);
    nor[BLE_VM_HW] = 0x55, nor[BLE_VM_HW + 1] = 0xAA, nor[BLE_VM_HW + 2] = 0xAA, nor[BLE_VM_HW + 3] = 0x55;
    vput = BLE_VM_HW + 4;
    want.x106[0] = 0x0B, want.x106[1] = fill;
    for (i = 0; i < 7; i++)
        want.x107[i] = (uint8_t)(fill + 5 * i);
    for (i = 0; i < 20; i++)
        want.x108[i] = (uint8_t)(fill ^ (i * 13u));
    for (i = 0; i < 64; i++)
        want.x187[i] = (uint8_t)(fill * 3u + i);
    c = ble_crc16_xmodem(0, want.x187, 64);
    want.x187[64] = (uint8_t)c, want.x187[65] = (uint8_t)(c >> 8), want.x187[66] = 0, want.x187[67] = 0;
    vrec(106, want.x106, 2);
    vrec(107, want.x107, 7);
    vrec(108, want.x108, 20);
    vrec(187, want.x187, 68);
}

/* a settings copy's backup bytes hold this unit's calibration (and the 80-byte side record before it is intact) */
static int copy_has(uint32_t copy)
{
    const uint8_t *b = nor + st_sector(OBJ_SETTINGS, copy) + BLE_RF_BAK_OFF;
    return ble_rf_copy_ok(b) && !memcmp(b + 1, &want, BLE_RF_TRIMS_SIZE);
}
static int frames_until_saved(uint32_t max)          /* the main loop until the backup is no longer owed */
{
    uint32_t f;
    for (f = 0; f < max && ble_rf_bak_owed; f++)
        ui_frame();
    for (f = 0; f < 200; f++)                        /* (and a little longer: nothing else should be written) */
        ui_frame();
    return !ble_rf_bak_owed;
}
static void reboot(void)                             /* RAM forgets; the flash image stays */
{
    memset(ble_rf_bak, 0, sizeof ble_rf_bak);
    ble_rf_bak_owed = 0;
    ble_rf_src = 0;
    memset(&ble_trims, 0, sizeof ble_trims);
    persist_boot();
    ble_midi_init();
}

int main(void)
{
    uint8_t vm_img[BLE_VM_HW_SIZE], side_before[80];
    st_hdr_t h;
    uint32_t e0;
    side_rec_t r;

    harness_init();                                   /* a blank flash: no settings saved yet */
    vm_make(0x52);
    memcpy(vm_img, nor + BLE_VM_HW, sizeof vm_img);
    ck(flash_ok == 1, "(the flash double answers as the FM-1's part)");

    e0 = fl_erases;
    ble_midi_init();
    ck(ble_rf_src == BLE_RF_FROM_VM && !memcmp(&ble_trims, &want, sizeof want), "boot with the stock VM: Bluetooth uses it (STOCK)");
    ck(ble_rf_bak_owed == 2 && settings_later, "  a backup is owed to the settings (two saves) and a settings save is asked for");
    ck(fl_erases == e0, "  nothing written at boot itself (the save waits for the main loop)");

    ck(frames_until_saved(2000), "the main loop saves the settings until the backup is no longer owed");
    ck(copy_has(0) && copy_has(1), "  both settings copies (A and B) hold the backup after the side record");
    ck(!strcmp(ble_cal_sub(), "BACKED UP"), "  RADIO CAL shows STOCK / BACKED UP");
    ck(st_current(OBJ_SETTINGS, &h) >= 0 && st_side_load(OBJ_SETTINGS, &r, sizeof r) == 0,
       "  the settings object still loads (payload CRC) and its side record reads");
    memcpy(side_before, &r, sizeof side_before);
    ck(!memcmp(nor + BLE_VM_HW, vm_img, sizeof vm_img), "  the stock VM at 0x0E8000 is byte for byte untouched");
    ck(fl_bad == 0, "  no flash access outside the allowed ranges");

    e0 = fl_erases;                                   /* the next boot, nothing changed: no extra erase */
    reboot();
    ck(ble_rf_src == BLE_RF_FROM_VM && ble_rf_bak_owed == 0 && ble_rf_copy_ok(ble_rf_bak),
       "reboot with VM and backup the same: the backup read back, nothing owed");
    frames_until_saved(10);
    ck(fl_erases == e0, "  and no settings save for it (no flash wear)");

    memset(nor + BLE_VM_HW, 0xFF, BLE_VM_HW_SIZE);   /* a later firmware flash erased the stock VM */
    reboot();
    ck(ble_rf_src == BLE_RF_FROM_COPY && !memcmp(&ble_trims, &want, sizeof want),
       "VM erased: the next boot runs Bluetooth on the backup, the same calibration (RADIO CAL BACKUP)");
    ck(!strcmp(ble_cal_text(), "BACKUP") && ble_rf_bak_owed == 0, "  RADIO CAL BACKUP, nothing owed");

    scope_view = (uint8_t)((scope_view + 1u) % SCOPE_VIEW_N);   /* the user changes a setting: two more saves */
    settings_save();
    scope_view = (uint8_t)((scope_view + 1u) % SCOPE_VIEW_N);
    settings_save();
    ck(copy_has(0) && copy_has(1), "later settings saves (VM gone) keep the backup in both copies");
    reboot();
    ck(ble_rf_src == BLE_RF_FROM_COPY && !memcmp(&ble_trims, &want, sizeof want), "  and the boot after still uses it");

    nor[st_sector(OBJ_SETTINGS, 0) + BLE_RF_BAK_OFF + 30] ^= 0x04;   /* copy A's backup damaged: copy B's is used */
    reboot();
    ck(ble_rf_src == BLE_RF_FROM_COPY && !memcmp(&ble_trims, &want, sizeof want), "one copy's backup damaged: the other copy's is used");
    nor[st_sector(OBJ_SETTINGS, 1) + BLE_RF_BAK_OFF + 30] ^= 0x04;   /* both damaged, no VM */
    reboot();
    ck(ble_rf_src == BLE_RF_NONE && !strcmp(ble_cal_text(), "MISSING") && ble_rf_bak_owed == 0,
       "both damaged and no VM: MISSING, Bluetooth cannot start, nothing made up or saved");

    harness_init();                                   /* SLOOP / Beat It 1.0 settings (no backup), the VM already erased */
    settings_save();
    reboot();
    ck(ble_rf_src == BLE_RF_NONE && !ble_rf_copy_ok(ble_rf_bak), "older settings without a backup and no VM: MISSING (NO RF CAL)");

    printf("beatit_rfcal_test: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
