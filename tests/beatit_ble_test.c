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
    memset(&bin, 0, sizeof bin);
    ble_tx_lost = 0;                            /* (1.3) */
    ble_tx_drain = 0;
    memset(ble_sent_held, 0, sizeof ble_sent_held);
    mco_play = 0;
    mco_u = 0;
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

/* ---- (1.2) SYNC = BT: clock / START / STOP from Bluetooth, timed by the packets' own BLE-MIDI timestamps */
static void t_clock_age(void)
{
    fm1_ms = 50000u;
    reset();
    ble_midi_set(1, 0);
    song.g[G_SYNC] = 0;
    WCMD(0x80, 0x80, 0xF8, 0x94, 0xF8);
    check(bmi_w == 0u, "SYNC INT: a central's clock is not queued (notes keep the room)");
    song.g[G_SYNC] = 3;
    WCMD(0x80, 0x80, 0xF8, 0x94, 0xF8, 0x9E, 0xFA);   /* clock at 0 ms, clock at 20, START at 30 */
    check(bmi_w == 3u && (ble_in_q[0] & 0xFFFFu) == 0xF82Fu && ble_in_q[0] >> 16 == 30u &&
              ble_in_q[1] >> 16 == 10u && (ble_in_q[2] & 0xFFFFu) == 0xFA2Fu && ble_in_q[2] >> 16 == 0u,
          "SYNC BT: clock / START queued as cable 2, each with its age in the packet (30, 10, 0 ms)");
    reset();
    ble_midi_set(1, 0);
    song.g[G_SYNC] = 3;
    WCMD(0x80, 0x80, 0xF8, 0xFF, 0xF8, 0xFE, 0xF8, 0xFD, 0xF8);   /* (0, 127, 254, 381 ms: lo wrapping bumps hi) */
    check(bmi_w == 4u && ble_in_q[0] >> 16 == BLE_RT_AGE_MAX && ble_in_q[3] >> 16 == 0u,
          "a pulse 381 ms older than the packet's newest: its age clamped to 120 ms");
    reset();
    ble_midi_set(0, 1);
    song.g[G_SYNC] = 3;
    WCMD(0x80, 0x80, 0xF8);
    check(bmi_w == 0u, "SYNC BT with RECEIVE OFF: the clock is ignored");
    ble_midi_set(0, 0);
}

/* a central's clock at a tempo: its pulses at exact times, carried in a packet at each connection event (every iv ms),
 * each packet arriving 0..jit ms late (radio retries, the stack), each pulse stamped with its sender's millisecond.
 * Returns the worst error (ms) of the time each pulse was given on our clock, after the first 2 s, in *err_new, and
 * of its arrival time (what timing by arrival would give) in *err_arr */
static uint32_t lcg = 12345u;
static void bt_clock_run(uint32_t bpm10, uint32_t iv, uint32_t jit, uint32_t ms, int start, uint32_t *err_new, uint32_t *err_arr)
{
    uint32_t t0 = fm1_ms, pulse_us = 600000000u / (24u * bpm10), next_us = 0, ev, k = 0, en = 0, ea = 0;
    int started = !start;
    for (ev = iv; ev <= ms; ev += iv) {
        uint8_t g[260];
        uint32_t n = 3, first = 1, q0 = bmi_w, arrival, i;
        uint32_t times[64], nt = 0;
        lcg = lcg * 1103515245u + 12345u;
        arrival = t0 + ev + (jit ? (lcg >> 16) % (jit + 1u) : 0u);
        g[0] = 0x52, g[1] = 0x0E, g[2] = 0;
        if (!started) {
            uint32_t ts = (t0 + ev - 1u) & 0x1FFFu;
            g[n++] = (uint8_t)(0x80u | ((ts >> 7) & 0x3Fu));
            g[n++] = (uint8_t)(0x80u | (ts & 0x7Fu));
            g[n++] = 0xFA;
            first = 0;
            started = 1;
            next_us = (ev - 1u) * 1000u + 500u;
            times[nt++] = t0 + ev - 1u;
        }
        while (next_us <= ev * 1000u && n < 250u && nt < 64u) {
            uint32_t ts = (t0 + next_us / 1000u) & 0x1FFFu;
            if (first) {
                g[n++] = (uint8_t)(0x80u | ((ts >> 7) & 0x3Fu));
                first = 0;
            }
            g[n++] = (uint8_t)(0x80u | (ts & 0x7Fu));
            g[n++] = 0xF8;
            times[nt++] = t0 + next_us / 1000u;
            next_us += pulse_us;
        }
        fm1_ms = arrival;
        if (n > 3u) {
            ble_att_rx(g, (uint16_t)n);
            for (i = 0; i < nt && q0 + i != bmi_w; i++) {
                uint32_t est = arrival - (ble_in_q[(q0 + i) % BMQ] >> 16);
                uint32_t e1 = est > times[i] ? est - times[i] : times[i] - est;
                uint32_t e2 = arrival - times[i];
                if (k++ >= 24u && ev >= 2000u) {   /* (after 2 s: the offset has its least delayed packet) */
                    en = e1 > en ? e1 : en;
                    ea = e2 > ea ? e2 : ea;
                }
            }
        }
        tick();
    }
    if (err_new)
        *err_new = en;
    if (err_arr)
        *err_arr = ea;
}

static void t_clock_follow(void)
{
    uint32_t en, ea;
    char m[160];
    reset();
    memset(&mclk, 0, sizeof mclk);
    transport_req = 0;
    song.playing = 0;
    fm1_ms = 100000u;
    ble_midi_set(1, 0);
    song.g[G_SYNC] = 3;
    song.g[G_BPM] = 100;
    bt_clock_run(1200u, 15u, 10u, 6000u, 1, &en, &ea);   /* 120 BPM, a 15 ms connection interval, 0-10 ms late */
    check(song.g[G_BPM] == 120, "SYNC BT: a central's clock at 120 BPM, 15 ms connection events, 0-10 ms late -> TEMPO 120");
    check(song.playing || transport_req == 3u, "  and its START starts the sequencer");
    snprintf(m, sizeof m, "  each pulse timed within %u ms of when it was sent (by arrival it would be up to %u ms off)",
             (unsigned)en, (unsigned)ea);
    check(en <= 2u && ea > 10u, m);
    reset();
    memset(&mclk, 0, sizeof mclk);
    fm1_ms = 300000u;
    ble_midi_set(1, 0);
    song.g[G_SYNC] = 3;
    song.g[G_BPM] = 100;
    bt_clock_run(1330u, 45u, 20u, 8000u, 0, &en, &ea);    /* 133 BPM, 45 ms events: 2-3 pulses a packet */
    snprintf(m, sizeof m, "  133 BPM, 45 ms events, 0-20 ms late -> TEMPO 133; pulses within %u ms (by arrival %u)",
             (unsigned)en, (unsigned)ea);
    check(song.g[G_BPM] == 133 && en <= 2u, m);
    WCMD(0x80, 0x80, 0xFC);
    tick();
    check(transport_req == 2u, "  its STOP stops the sequencer");
    song.g[G_SYNC] = 2;
    memset(&mclk, 0, sizeof mclk);
    song.g[G_BPM] = 90;
    bt_clock_run(1200u, 15u, 0u, 3000u, 0, 0, 0);
    check(song.g[G_BPM] == 90 && !mclk.alive && bmi_w == bmi_r, "SYNC TRS: Bluetooth's clock is not queued or followed");
    song.g[G_SYNC] = 0;
    ble_midi_set(0, 0);
}

/* ---- (1.2) SEND OFF empties the out ring in its one consumer (1.1 reset it from the main loop: a BLE interrupt's
 * pop in between could leave bmo_r past bmo_w, and then the ring read as ~4 billion stale packets) */
static void t_send_off_ring(void)
{
    uint32_t pkt, t;
    reset();
    ble_midi_set(0, 1);
    ble_out_on = 1;
    midi_out_event(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24);
    midi_out_event(0x09u | 0x90u << 8 | 62u << 16 | 100u << 24);
    check(bmo_w - bmo_r == 2u, "two notes waiting for the next connection event");
    ble_midi_set(0, 0);
    check(bmo_w - bmo_r == 2u, "SEND OFF from the menu: the menu no longer touches the BLE interrupts' read index");
    check(!ble_app_midi_peek(&pkt, &t) && bmo_r == bmo_w, "  the next BLE-side peek drops them: nothing sent, the ring empty");
    ble_app_midi_pop();                         /* (a pop the BLE side had begun: harmless now) */
    check(bmo_r == bmo_w + 1u || bmo_r == bmo_w, "  (a pop after it moves at most one)");
    bmo_r = bmo_w;
}

/* ---- (1.2) BT CONNECTION's line: a start-up the last run did not finish (the breadcrumb a reset keeps), the counts */
static void t_link_line(void)
{
    int warn;
    const char *s;
    check(ble_crumb_hung(0xB1000000u | BC_RF_INIT << 16 | 0x0307u) == BC_RF_INIT,
          "breadcrumb: a reset in the radio's RF INIT is a hung start");
    check(!ble_crumb_hung(0xB1000000u | BC_RUNNING << 16) && !ble_crumb_hung(0xB1000000u | BC_SET_OFF << 16) &&
              !ble_crumb_hung(0) && !ble_crumb_hung(0x12345678u),
          "  RUNNING, SET OFF, none (a power cycle) or noise: not a hung start");
    reset();
    ble_midi_set(0, 0);
    ble_up = 0;
    ble_rx_count = ble_tx_count = ble_rx_lost = 0;
    ble_hung_step = BC_BB_INIT;
    s = ble_link_sub(&warn);
    check(!strcmp(s, "RESET DURING: BASEBAND") && warn, "a start that hung last time: BT CONNECTION says where (amber)");
    ble_midi_set(1, 0);
    check(ble_hung_step == 0, "  turning Bluetooth on again clears it (the new start writes its own breadcrumb)");
    s = ble_link_sub(&warn);
    check(!strcmp(s, "") && !warn, "  connected nothing yet: no line");
    WCMD(0x80, 0x80, 0x90, 0x3C, 0x64);
    tick();
    s = ble_link_sub(&warn);
    check(!strcmp(s, "IN 1 OUT 0") && !warn, "  a note in: IN 1 OUT 0");
    ble_rx_lost = 3;
    s = ble_link_sub(&warn);
    check(!strcmp(s, "IN 1 OUT 0 LOST 3") && warn, "  messages lost (queue full) show in amber");
    ble_rx_lost = 0;
    ble_midi_set(0, 0);
}

static void t_menu_sig(void)
{
    uint32_t a, b;
    reset();
    ble_midi_set(1, 0);
    a = ble_menu_sig();
    ble_was_conn = 0;
    b = ble_menu_sig();
    check(a == b, "the menu's BT signature is steady while nothing changes (no redraw every frame)");
    ble_rf_bak_owed = 2;
    b = ble_menu_sig();
    check(a != b, "  RADIO CAL BACKING UP -> BACKED UP changes it (1.1 waited for a knob)");
    ble_rf_bak_owed = 0;
    ble_midi_set(0, 0);
    b = ble_menu_sig();
    check(a != b, "  the radio's state changes it");
}

/* ---- (1.3) CLOCK OUT to Bluetooth: OFF / USB / BT / USB+BT, each destination its own START / STOP */
static uint32_t bt_take(uint32_t *f8, uint32_t *fa, uint32_t *fc, uint32_t *other)   /* drains the BLE out ring */
{
    uint32_t pkt, t, n = 0;
    while (ble_app_midi_peek(&pkt, &t)) {
        uint32_t b = (pkt >> 8) & 0xFFu;
        *f8 += b == 0xF8u, *fa += b == 0xFAu, *fc += b == 0xFCu, *other += b != 0xF8u && b != 0xFAu && b != 0xFCu;
        ble_app_midi_pop();
        n++;
    }
    return n;
}
static uint32_t usb_take(uint32_t *f8, uint32_t *fa, uint32_t *fc)
{
    uint32_t n = 0;
    while (mo_r != mo_w) {
        uint32_t b = (midi_out_q[mo_r++ % MQ] >> 8) & 0xFFu;
        *f8 += b == 0xF8u, *fa += b == 0xFAu, *fc += b == 0xFCu;
        n++;
    }
    return n;
}
static void t_clock_out_bt(void)
{
    uint32_t n = 32, i, beats = 4, nb, f8 = 0, fa = 0, fc = 0, ot = 0, uf8 = 0, ufa = 0, ufc = 0, pkt, t, last = 0, mono = 1;
    reset();
    usb.config = 1;
    check(GP[G_CLKOUT].max == 3 && !strcmp(GP[G_CLKOUT].names[2], "BT") && !strcmp(GP[G_CLKOUT].names[3], "USB+BT"),
          "1.3: GLO > MIDI 3 > CLOCK: OFF / USB / BT / USB+BT");
    ble_midi_set(1, 1);
    ble_out_on = 1;
    song.g[G_CLKOUT] = 2;
    nb = beats * BEAT_U / (n * 120u);
    for (i = 0; i < nb; i++) {
        clkout_block(n);
        bt_take(&f8, &fa, &fc, &ot);
    }
    usb_take(&uf8, &ufa, &ufc);
    check(f8 >= 24u * beats - 1u && f8 <= 24u * beats && !fa && !fc && !ot && !uf8,
          "CLOCK BT, stopped, 4 beats at 120: 24 pulses a beat to Bluetooth only, no START, nothing on USB");
    song.playing = 1;
    f8 = 0;
    fm1_ms = 5000u;
    clkout_block(n);
    check(ble_app_midi_peek(&pkt, &t) && ((pkt >> 8) & 0xFFu) == 0xFAu && t == 5000u, "PLAY: START first, with the block's time");
    ble_app_midi_pop();
    check(ble_app_midi_peek(&pkt, &t) && ((pkt >> 8) & 0xFFu) == 0xF8u, "... then the downbeat's pulse");
    ble_app_midi_pop();
    for (i = 0; i < nb; i++) {
        fm1_ms += 1u;
        clkout_block(n);
        while (ble_app_midi_peek(&pkt, &t)) {
            mono &= t >= last;
            last = t;
            ble_app_midi_pop();
        }
    }
    check(mono && last > 5000u, "... the pulses carry their blocks' times (for the BLE-MIDI timestamps), in order");
    song.playing = 0;
    clkout_block(n);
    f8 = fa = fc = ot = 0;
    bt_take(&f8, &fa, &fc, &ot);
    check(fc == 1u && !fa, "STOP: STOP to Bluetooth");
    usb_take(&uf8, &ufa, &ufc);
    check(!uf8 && !ufa && !ufc, "CLOCK BT: USB got nothing all along");

    /* USB+BT: both, and a phone that comes and goes while the FM-1 plays */
    song.g[G_CLKOUT] = 3;
    ble_out_on = 0;                             /* (no central listening yet) */
    song.playing = 1;
    clkout_block(n);
    clkout_block(n);
    uf8 = ufa = ufc = 0;
    usb_take(&uf8, &ufa, &ufc);
    check(ufa == 1u && uf8 >= 1u && mco_play == CO_USB, "USB+BT, no central: USB gets START and the clock, Bluetooth nothing");
    ble_out_on = 1;                             /* a phone connects and subscribes while it plays */
    clkout_block(n);
    f8 = fa = fc = ot = 0;
    bt_take(&f8, &fa, &fc, &ot);
    uf8 = ufa = ufc = 0;
    usb_take(&uf8, &ufa, &ufc);
    check(fa == 1u && f8 >= 1u && !ufa && mco_play == (CO_USB | CO_BT),
          "... a central that subscribes mid-song: START then (to it only, USB not restarted), the clock follows");
    ble_out_on = 0;                             /* it goes away */
    clkout_block(n);
    check(mco_play == CO_USB && bmo_r == bmo_w, "... it goes away: it is forgotten, no STOP into the void, USB keeps going");
    ble_out_on = 1;
    clkout_block(n);
    f8 = fa = fc = ot = 0;
    bt_take(&f8, &fa, &fc, &ot);
    check(fa == 1u, "... it comes back: START again");
    song.g[G_SYNC] = 3;                         /* following Bluetooth's own clock: nothing back out */
    clkout_block(n);
    f8 = fa = fc = ot = 0;
    bt_take(&f8, &fa, &fc, &ot);
    uf8 = ufa = ufc = 0;
    usb_take(&uf8, &ufa, &ufc);
    check(fc == 1u && ufc == 1u && !mco_play, "SYNC changed to follow a clock while playing: STOP to both, then silence");
    for (i = 0; i < 20u; i++)
        clkout_block(n);
    check(bmo_r == bmo_w && mo_r == mo_w, "... a FM-1 that follows a clock never sends one (no loop through a phone)");
    song.g[G_SYNC] = 0;
    ble_midi_set(1, 0);                         /* SEND OFF: the clock may not go to Bluetooth */
    clkout_block(n);
    clkout_block(n);
    f8 = fa = fc = ot = 0;
    bt_take(&f8, &fa, &fc, &ot);
    check(!f8 && !(mco_play & CO_BT), "BT MIDI SEND OFF: no clock to Bluetooth (CLOCK USB+BT acts as USB)");
    song.playing = 0;
    clkout_block(n);
    ble_midi_set(0, 0);
}

/* ---- (1.3) SEND OFF ends what the FM-1 left sounding at the central; a congested link keeps room for note-offs */
static void t_send_close(void)
{
    uint32_t pkt, t, offs = 0, cc123 = 0, stops = 0, other = 0, i, k;
    reset();
    ble_midi_set(1, 1);
    ble_out_on = 1;
    midi_out_event(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24);   /* two notes on channel 1 */
    midi_out_event(0x09u | 0x90u << 8 | 64u << 16 | 100u << 24);
    midi_out_event(0x08u | 0x80u << 8 | 64u << 16);                /* one ended */
    midi_out_event(0x09u | 0x92u << 8 | 40u << 16 | 90u << 24);    /* one on channel 3 */
    for (k = 0; k < 6u; k++)                                        /* six on channel 10 */
        midi_out_event(0x09u | 0x99u << 8 | (36u + k) << 16 | 100u << 24);
    while (ble_app_midi_peek(&pkt, &t))
        ble_app_midi_pop();
    song.playing = 1;
    song.g[G_CLKOUT] = 2;
    clkout_block(32);                           /* the Bluetooth clock told START */
    while (ble_app_midi_peek(&pkt, &t))
        ble_app_midi_pop();
    ble_midi_set(1, 0);                         /* SEND OFF, RECEIVE still on: the link stays */
    while (ble_app_midi_peek(&pkt, &t)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu;
        if (st == 0x80u && d1 == 60u) offs |= 1;
        else if (st == 0x82u && d1 == 40u) offs |= 2;
        else if (st == 0xB9u && d1 == 123u) cc123++;
        else if (st == 0xFCu) stops++;
        else other++;
        ble_app_midi_pop();
    }
    check(offs == 3u && cc123 == 1u && stops == 1u && !other,
          "1.3: SEND OFF: a note-off for each note left on (C4 ch 1, E2 ch 3), ALL NOTES OFF on ch 10 (6 held), STOP; no more");
    midi_out_event(0x09u | 0x90u << 8 | 62u << 16 | 100u << 24);
    clkout_block(32);
    check(!ble_app_midi_peek(&pkt, &t) && !ble_tx_drain, "... then nothing goes: the ring closed");
    ble_midi_set(1, 1);
    clkout_block(32);
    check(ble_app_midi_peek(&pkt, &t) && ((pkt >> 8) & 0xFFu) == 0xFAu, "SEND back ON while playing: START to the central again");
    while (ble_app_midi_peek(&pkt, &t))
        ble_app_midi_pop();
    song.playing = 0;
    clkout_block(32);
    while (ble_app_midi_peek(&pkt, &t))
        ble_app_midi_pop();

    /* a link that cannot keep up (the central reads nothing): new notes and pulses dropped and counted, note-offs fit */
    midi_out_event(0x09u | 0x90u << 8 | 70u << 16 | 100u << 24);
    for (i = 0; i < BMQ; i++)
        midi_out_event(0x0Bu | 0xB0u << 8 | 1u << 16 | (i & 127u) << 24);   /* a mod wheel sweep floods it */
    check(bmo_w - bmo_r == BMQ - BMQ_KEEP && ble_tx_lost > 0u, "congested: the ring stops taking ordinary messages 16 short of full, LOST counted");
    midi_out_event(0x09u | 0x90u << 8 | 71u << 16 | 100u << 24);
    midi_out_event(0x08u | 0x80u << 8 | 70u << 16);
    midi_out_event(0x09u | 0x90u << 8 | 72u << 16 | 0u << 24);              /* (a note-off as velocity 0) */
    midi_out_event(0x0Bu | 0xB0u << 8 | 64u << 16 | 0u << 24);              /* (sustain up) */
    check(bmo_w - bmo_r == BMQ - BMQ_KEEP + 3u, "... a new note dropped, but the note-offs (8x, 9x vel 0) and the pedal up still go");
    {
        int warn = 0;
        int up = ble_up;
        ble_up = 1;
        check(strstr(ble_link_sub(&warn), "LOST") != 0 && warn, "... BT CONNECTION's line shows the sent messages lost too, in amber");
        ble_up = (uint8_t)up;
    }
    ble_out_on = 0;
    ble_app_state();
    check(bmo_r == bmo_w, "the central goes away: what waited for it is dropped");
    ble_midi_set(0, 0);
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
    t_clock_age();
    t_clock_follow();
    t_clock_out_bt();
    t_send_close();
    t_send_off_ring();
    t_link_line();
    t_menu_sig();
    printf("beatit_ble: %d failed\n", fails);
    return fails;
}
