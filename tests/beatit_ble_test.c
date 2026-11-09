/* SPDX-License-Identifier: GPL-3.0-only */
/* Beat It's Bluetooth MIDI glue (firmware/src/midi_ble.c) on the host, with the real BLE stack (ble/, BLE_HW_WL82=0:
 * no radio) and SLOOP's real MIDI path (usb.c's midi_in_q, seq.c's events, the synth's voices), adapted from
 * Optimist's tests/ble_midi_in_test.c. A central's ATT Write Commands on the BLE-MIDI I/O value (handle 0x000E)
 * -> ble_att.c -> the BLE-MIDI decoder -> ble_app_midi_in -> ble_in_q -> the TIMER5 tick (ble_midi_poll) -> midi_in_q
 * -> events_block -> a gated voice. Also: BT MIDI RECEIVE / SEND gating, MIDI out to a listening central, the notes of a
 * vanished central ended, the advertised name, and the radio refused without stored calibration.
 * It does not test the radio: nothing here transmits. Exit status: the number of failed checks. */
#include <stdint.h>
#define FELUCCA_BLE 1
#define FELUCCA_BLE_STUB 1      /* (ble/ble_stack.c: ble_hw_stub.c, no radio) */
#define BLE_API static
#define BLE_DEVICE_NAME "BEAT IT"
#define BLE_NAME_SEP " "
static void ble_midi_out(uint32_t pkt);
#define main hostsim_main
#include "hostsim.c"
#undef main
static uint8_t ble_cpu1_stopped;
static void fm1_ble_irqs_hold(int on) { (void)on; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
#define FELUCCA_FLASH 0
#include "../firmware/src/midi_ble.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("beatit_ble: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void blocks(uint32_t n)
{
    int32_t out[CTL * 2];
    while (n--) {
        mix_block(out, CTL);
        mo_r = mo_w;
    }
}

static void wcmd(uint16_t h, const uint8_t *p, uint32_t n)
{
    uint8_t f[260];
    f[0] = 0x52;
    f[1] = (uint8_t)h;
    f[2] = (uint8_t)(h >> 8);
    memcpy(f + 3, p, n);
    ble_att_rx(f, (uint16_t)(3u + n));
}
#define WCMD(...)                                                                                                  \
    do {                                                                                                           \
        static const uint8_t pk_[] = {__VA_ARGS__};                                                                \
        wcmd(0x000E, pk_, sizeof pk_);                                                                             \
    } while (0)

static void tick(void)
{
    ble_midi_poll();
    blocks(1);
}

static int gated(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static uint32_t ngated(void)
{
    uint32_t i, k, n = 0;
    for (k = 0; k < NPART; k++)
        for (i = 0; i < NVOICE; i++)
            n += trk[k].v[i].active && trk[k].v[i].gate;
    return n;
}

static void reset(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    mi_w = mi_r = mo_w = mo_r = 0;
    bmi_w = bmi_r = bmo_w = bmo_r = 0;
    memset(ble_held, 0, sizeof ble_held);
    ble_release = 0;
    song.sel = 0;
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_SUS] = 127;
    }
    song.g[G_BPM] = 120;
    ble_att_reset();
}

static void t_off_at_boot(void)
{
    check(!ble_on && !ble_up && !ble_rx_on && !ble_tx_on, "power-on: the radio off, receive and send off (nothing saved)");
    reset();
    WCMD(0x80, 0x80, 0x90, 0x3C, 0x64);
    tick();
    check(!gated(&trk[0], 60) && bmi_w == 0u, "a BLE-MIDI note while Bluetooth is off: not queued, not played");
}

static void t_receive(void)
{
    reset();
    ble_midi_set(1, 0);
    check(ble_up && ble_on && ble_rx_on && !ble_tx_on, "BT MIDI RECEIVE ON: the stack started, advertising");
    check(strcmp(ble_link_text(), "ADVERTISING") == 0, "BT CONNECTION shows ADVERTISING");
    WCMD(0xA8, 0xC5, 0x90, 0x3C, 0x64);
    tick();
    check(gated(&trk[0], 60), "a central's note on (header, timestamp, 90 3C 64) plays the selected track");
    check(midi_in_q[0] >> 4 == ((0x2u << 4 | 0x9u | 0x90u << 8 | 0x3Cu << 16 | 0x64u << 24) >> 4),
          "... it reached usb.c's midi_in_q as cable 2 (Bluetooth), the same ring USB and TRS MIDI use");
    WCMD(0xA8, 0xC6, 0xF8, 0xC7, 0x90, 0x40, 0x64, 0xC8, 0x43, 0x64);
    tick();
    check(gated(&trk[0], 64) && gated(&trk[0], 67), "clock skipped, two notes, the second by running status");
    WCMD(0xA8, 0xC9, 0x80, 0x3C, 0x00, 0x40, 0x00, 0xCA, 0x90, 0x43, 0x00);
    tick();
    check(ngated() == 0u, "note offs (80 with running status, 9x velocity 0) end them");
    check(bmi_w == 6u && bmi_r == 6u && ble_rx_lost == 0u, "6 channel messages queued and drained, the clock left out, none lost");
}

static void t_channels(void)
{
    reset();
    ble_midi_set(1, 0);
    WCMD(0x80, 0x80, 0x90, 0x3E, 0x64);
    tick();
    check(gated(&trk[0], 62), "channel 1 plays track 1");
    song.sel = 1;
    WCMD(0x80, 0x81, 0x93, 0x3C, 0x64);
    tick();
    check(ngated() == 2u, "a second channel's note plays as well");
    WCMD(0x80, 0x82, 0x83, 0x3C, 0x00, 0x80, 0x80, 0x3E, 0x00);
    tick();
    check(ngated() == 0u, "... and both note offs end them");
}

static void t_vanished(void)
{
    reset();
    ble_midi_set(1, 0);
    WCMD(0x80, 0x80, 0x90, 0x30, 0x64, 0x80, 0x34, 0x64);
    tick();
    check(gated(&trk[0], 48) && gated(&trk[0], 52), "a central holds two notes");
    ble_was_conn = 1;                           /* (it was connected: the stack now reports it gone) */
    ble_app_state();
    tick();
    tick();
    check(ngated() == 0u, "the link gone: the notes it left on are ended (no stuck notes)");
    reset();
    ble_midi_set(1, 0);
    WCMD(0x80, 0x80, 0x90, 0x30, 0x64);
    tick();
    ble_midi_set(0, 0);
    tick();
    tick();
    check(ngated() == 0u && !ble_on, "RECEIVE and SEND OFF: the radio stops, the held note ends");
    WCMD(0x80, 0x81, 0x90, 0x32, 0x64);
    tick();
    check(!gated(&trk[0], 50), "... and later notes are ignored");
}

static void t_send(void)
{
    uint32_t pkt = 0, t = 0;
    reset();
    ble_midi_set(0, 1);
    check(ble_on && !ble_rx_on && ble_tx_on, "BT MIDI SEND ON alone: the radio on, receive off");
    WCMD(0x80, 0x80, 0x90, 0x3C, 0x64);
    tick();
    check(!gated(&trk[0], 60) && bmi_w == 0u, "receive OFF: a central's note is not played");
    ble_out_on = 0;
    midi_out_event(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24);
    check(!ble_app_midi_peek(&pkt, &t), "no central listening (CCCD off): nothing queued for Bluetooth");
    ble_out_on = 1;
    fm1_ms = 1234u;
    midi_out_event(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24);
    midi_out_event(0x0Fu | 0xF8u << 8);         /* (a clock: not a channel message, USB only) */
    check(ble_app_midi_peek(&pkt, &t) && pkt == (0x09u | 0x90u << 8 | 60u << 16 | 100u << 24) && t == 1234u,
          "a central listening: the synth's note on (midi_out_event, as USB gets it) queued with its time");
    ble_app_midi_pop();
    check(!ble_app_midi_peek(&pkt, &t), "... the clock not sent to Bluetooth, the queue empty after the pop");
    ble_midi_set(0, 0);
    midi_out_event(0x08u | 0x80u << 8 | 60u << 16);
    check(!ble_app_midi_peek(&pkt, &t), "SEND OFF: nothing queued");
}

static void t_name(void)
{
    uint8_t a[6] = {0xCD, 0xAB, 0x33, 0x44, 0x55, 0xC6};
    gap_name(a);
    check(bgap.n == 12u && memcmp(bgap.s, "BEAT IT ABCD", 12) == 0,
          "the advertised name: \"BEAT IT XXXX\" from the address (..:AB:CD -> ABCD)");
}

static void t_no_cal(void)
{
    /* (the device build: BLE_HW_WL82=1 and no stored calibration -> ble_radio_ok() 0; here its logic directly) */
    check(BLE_HW_WL82 == 0 && ble_radio_ok(), "(host: no radio, so no calibration needed; the device build refuses without it)");
}

/* ---- the radio calibration and its backup (midi_ble.c ble_midi_init, ble/ble_vm.c): a flash image with stock V15's
 * VM at 0x0E8000 (records 106, 107, 108, 187, as tests/ble_vm_test.c builds them), the backup in ble_rf_bak as the
 * settings side area would hold it */
static uint8_t vmflash[0x100000];
static uint32_t vput;
static int vm_rd(uint32_t off, uint8_t *dst, uint32_t n)
{
    if (off >= sizeof vmflash || n > sizeof vmflash - off)
        return -1;
    memcpy(dst, vmflash + off, n);
    return 0;
}
static void vrec(uint32_t id, const uint8_t *d, uint32_t n)
{
    uint8_t *p = vmflash + vput;
    p[0] = (uint8_t)ble_crc16_xmodem(0, d, n);
    p[1] = (uint8_t)id;
    p[2] = (uint8_t)((id >> 8) & 15u) | (uint8_t)((n & 15u) << 4);
    p[3] = (uint8_t)(n >> 4);
    memcpy(p + 4, d, n);
    vput += 4 + n;
}
static struct ble_rf_trims vm_want;
static void vm_make(int with187, uint8_t fill)       /* a stock VM at 0x0E8000 (with187 0: a record missing) */
{
    uint32_t i, c;
    memset(vmflash, 0xFF, sizeof vmflash);
    vmflash[BLE_VM_HW] = 0x55, vmflash[BLE_VM_HW + 1] = 0xAA, vmflash[BLE_VM_HW + 2] = 0xAA, vmflash[BLE_VM_HW + 3] = 0x55;
    vput = BLE_VM_HW + 4;
    vm_want.x106[0] = 0x0B, vm_want.x106[1] = fill;
    for (i = 0; i < 7; i++)
        vm_want.x107[i] = (uint8_t)(fill + 3 * i);
    for (i = 0; i < 20; i++)
        vm_want.x108[i] = (uint8_t)(fill ^ (i * 11u));
    for (i = 0; i < 64; i++)
        vm_want.x187[i] = (uint8_t)(fill + i);
    c = ble_crc16_xmodem(0, vm_want.x187, 64);
    vm_want.x187[64] = (uint8_t)c, vm_want.x187[65] = (uint8_t)(c >> 8), vm_want.x187[66] = 0, vm_want.x187[67] = 0;
    vrec(106, vm_want.x106, 2);
    vrec(107, vm_want.x107, 7);
    vrec(108, vm_want.x108, 20);
    if (with187)
        vrec(187, vm_want.x187, 68);
}
static void vm_erase(void) { memset(vmflash, 0xFF, sizeof vmflash); }

static void t_cal_backup(void)
{
    uint8_t keep[BLE_RF_COPY_SIZE];
    ble_vm_rd_host = vm_rd;

    vm_erase();                                      /* nothing: no VM, no backup */
    memset(ble_rf_bak, 0xFF, sizeof ble_rf_bak);
    ble_rf_bak_owed = 0;
    ble_midi_init();
    check(ble_rf_src == BLE_RF_NONE && !strcmp(ble_cal_text(), "MISSING") && !strcmp(ble_cal_sub(), "BT CANNOT START"),
          "no stock calibration and no backup: RADIO CAL MISSING (the device build: BT CONNECTION NO RF CAL, radio off)");
    check(!ble_rf_copy_ok(ble_rf_bak) && ble_rf_bak_owed == 0, "  and no backup is invented or saved");

    vm_make(1, 0x40);                                /* first boot with the stock VM there */
    ble_midi_init();
    check(ble_rf_src == BLE_RF_FROM_VM && !strcmp(ble_cal_text(), "STOCK"), "a complete stock VM: the radio uses it (RADIO CAL STOCK)");
    check(!memcmp(&ble_trims, &vm_want, sizeof vm_want), "  the radio gets exactly the VM's four records");
    check(ble_rf_copy_ok(ble_rf_bak) && ble_rf_copy_area(ble_rf_bak[0]) == BLE_VM_HW && !memcmp(ble_rf_bak + 1, &vm_want, BLE_RF_TRIMS_SIZE),
          "  a backup is made from it (CRC-checked copy, marked as taken from 0x0E8000)");
    check(ble_rf_bak_owed == 2 && !strcmp(ble_cal_sub(), "BACKING UP"), "  and owed to two settings saves (copies A and B): BACKING UP");
    memcpy(keep, ble_rf_bak, sizeof keep);

    ble_rf_bak_owed = 0;                             /* (both saves done) the next boot, VM unchanged */
    ble_midi_init();
    check(ble_rf_src == BLE_RF_FROM_VM && ble_rf_bak_owed == 0 && !strcmp(ble_cal_sub(), "BACKED UP"),
          "VM and backup the same: nothing to write (BACKED UP, no extra flash erase)");

    vm_erase();                                      /* a later flash erased the stock VM; the settings kept the backup */
    memset(&ble_trims, 0, sizeof ble_trims);
    ble_midi_init();
    check(ble_rf_src == BLE_RF_FROM_COPY && !strcmp(ble_cal_text(), "BACKUP") && !strcmp(ble_cal_sub(), "STOCK DATA GONE"),
          "stock VM erased, backup there: the radio uses the backup (RADIO CAL BACKUP)");
    check(!memcmp(&ble_trims, &vm_want, sizeof vm_want) && ble_rf_bak_owed == 0 && !memcmp(ble_rf_bak, keep, sizeof keep),
          "  the same calibration as the stock VM had, and the backup left as it was");

    vm_make(0, 0x40);                                /* a damaged / incomplete VM (187 missing): the backup, not the VM */
    ble_midi_init();
    check(ble_rf_src == BLE_RF_FROM_COPY && !memcmp(ble_rf_bak, keep, sizeof keep),
          "an incomplete VM never overwrites a good backup (backup used)");

    vm_erase();                                      /* the backup damaged (one byte): refused, nothing made up */
    ble_rf_bak[40] ^= 0x10;
    ble_midi_init();
    check(ble_rf_src == BLE_RF_NONE && !strcmp(ble_cal_text(), "MISSING"), "a damaged backup is refused (CRC): MISSING");

    vm_make(1, 0x71);                                /* the stock VM back (e.g. after a stock reflash): it wins, backup redone */
    ble_midi_init();
    check(ble_rf_src == BLE_RF_FROM_VM && ble_rf_copy_ok(ble_rf_bak) && !memcmp(ble_rf_bak + 1, &vm_want, BLE_RF_TRIMS_SIZE) &&
          ble_rf_bak_owed == 2, "a complete VM always wins and rewrites a stale or damaged backup");

    ble_vm_rd_host = 0;
    vm_erase();
    memset(ble_rf_bak, 0, sizeof ble_rf_bak);
    ble_rf_bak_owed = 0;
    ble_midi_init();
}

int main(void)
{
    t_off_at_boot();
    t_receive();
    t_channels();
    t_vanished();
    t_send();
    t_name();
    t_no_cal();
    t_cal_backup();
    printf("beatit_ble: %d failed\n", fails);
    return fails;
}
