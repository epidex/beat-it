/* SPDX-License-Identifier: GPL-3.0-only */
/* Beat It: Bluetooth LE MIDI (FELUCCA_BLE). The stack is Optimist's own open BLE stack (ble/, GPL-3.0-only OR
 * MPL-2.0, from github.com/w0ts/optimist; its baseband driver ble/ble_hw_wl82.c and hal/fm1_ble*.h), written for the
 * FM-1's JL AC7916 (WL82) radio from the chip's documented and observed behaviour, not from M-VAVE's closed code.
 * This file is the glue to SLOOP's MIDI rings, adapted from Optimist's io/midi/midi_ble.c:
 *   in   the stack decodes a central's BLE-MIDI writes, in the BLE interrupts, into USB-MIDI event packets. They wait
 *        in ble_in_q (one producer: the BLE interrupts) until the TIMER5 tick (main.c, beside uart_midi_poll) moves
 *        them into usb.c's midi_in_q, whose producers all run in TIMER5. Channel voice messages (cable 2), and
 *        (1.2) clock / START / CONTINUE / STOP for SYNC = BT (seq.c mclk_event, src 3): each realtime message keeps,
 *        in its packet's third byte, how many ms ago it was sent (at most BLE_RT_AGE_MAX), from the sender's own
 *        BLE-MIDI timestamps (ble_in_time, as Optimist times its SYNC BLE), so the clock follower times it as sent,
 *        not by the connection event that brought it.
 *   out  midi_out_event (usb.c: the audio ISR, the stream USB MIDI gets) queues each event in ble_out_q while a
 *        central has the MIDI notifications on; the stack packs them at the next connection event.
 *   BT MIDI RECEIVE / BT MIDI SEND (ui_menu.c) gate in and out; the radio is on while either is ON.
 *   Power-on: always OFF (not saved), so a radio start-up that hangs ends in a watchdog reset into a FM-1 with the
 *   radio off; the menu is the only way it starts. The radio is never started without the unit's own stored radio
 *   calibration (stock V15's VM at 0x0E8000, read only, ble/ble_vm.c), or Beat It's backup copy of it.
 *   Calibration backup: when the VM is complete (every record's check holds), its trims are copied (Optimist's
 *   100-byte copy format, CRC-checked, ble_rf_copy_make) into the settings object's side area, after SLOOP's 80-byte
 *   side record (project.c settings_write), in both settings copies. A later flash that erases the VM then still
 *   leaves Bluetooth its calibration (RADIO CAL: BACKUP). The copy is only ever made from this unit's own complete
 *   VM: never invented, never another unit's. Neither there: BT CONNECTION shows NO RF CAL, the radio stays off.
 *   The device address: the stock VM's provisioned one when it has one, else a random static address made from the
 *   unit's calibration bytes (the same every boot, no flash write). Name: "BEAT IT XXXX". No pairing / bonding. */
#include "ble/ble_stack.c"
#include "ble/ble_vm.c"

#define BMQ 64u
static uint32_t ble_in_q[BMQ], ble_out_q[BMQ], ble_out_ms[BMQ];
static volatile uint32_t bmi_w, bmi_r, bmo_w, bmo_r;
static volatile uint8_t ble_out_on;            /* a central has the MIDI notifications on */
static volatile uint8_t ble_rx_on, ble_tx_on;   /* BT MIDI RECEIVE / SEND (ui_menu.c bt_rx / bt_tx) */
static uint8_t ble_on;                         /* the radio advertises / keeps a link */
static uint8_t ble_held[16][16];               /* the notes a central has on (bit per note, per channel), TIMER5's */
static volatile uint8_t ble_release;           /* the link is gone / off: end those notes (ble_midi_poll) */
static uint8_t ble_was_conn;
static uint8_t ble_up;                         /* the radio and the stack started (once per boot) */
static uint8_t ble_rf_src;                     /* BLE_RF_NONE / BLE_RF_FROM_VM / BLE_RF_FROM_COPY */
static uint8_t ble_rf_bak[BLE_RF_COPY_SIZE];   /* the calibration backup (project.c saves it with the settings) */
static uint8_t ble_rf_bak_owed;                /* settings saves still owed for it (2: both copies A and B) */
#define BLE_RF_BAK_OFF (ST_SIDE_OFF + 80u)     /* in a settings copy's sector: after the 80-byte side record */
static struct ble_vm_info ble_vm_seen;
static struct ble_rf_trims ble_trims;
static volatile uint32_t ble_rx_count, ble_tx_count, ble_rx_lost;   /* (BT CONNECTION's line, the host tests) */
#define BLE_RT_AGE_MAX 120u                    /* ms: a realtime message's age within its packet, clamped */
/* (1.2) what the last run left in the breadcrumb (hal/fm1_ble_rf.h), read once at boot: the step a radio start-up
 * had reached when the FM-1 reset, or 0 when it had finished (RUNNING / SET_OFF) or there is none (a power cycle) */
static uint8_t ble_hung_step;
static uint8_t ble_crumb_hung(uint32_t w);

BLE_API void ble_app_state(void)               /* the BLE interrupts, or the menu with them held */
{
    uint8_t c = (uint8_t)ble_connected();
    ble_out_on = (uint8_t)ble_midi_ready();
    if (!ble_out_on)
        bmo_r = bmo_w;                          /* nothing waits for a central that does not listen */
    if (ble_was_conn && !c)
        ble_release = 1;                        /* its notes are not ended by a note-off that will never come */
    ble_was_conn = c;
}

BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms)
{
    if (!ble_tx_on) {                           /* (1.2) SEND OFF: dropped here, by the ring's one consumer. 1.1 */
        bmo_r = bmo_w;                          /* reset bmo_r from the main loop, which a BLE interrupt between */
        return 0;                               /* its peek and its pop could push past bmo_w: garbage sent */
    }
    if (bmo_r == bmo_w)
        return 0;
    RING_PUBLISH();                             /* the slot read after the index (the audio ISR fills it) */
    *pkt = ble_out_q[bmo_r % BMQ];
    *t_ms = ble_out_ms[bmo_r % BMQ];
    return 1;
}

BLE_API void ble_app_midi_pop(void)
{
    RING_PUBLISH();
    bmo_r++;
}

/* (1.2) a realtime message's time on our clock (fm1_ms), from Optimist 0.2's ble_in_time (io/midi/midi_ble.c, GPL-3.0,
 * its BLE stack's own project), in ms: the sender's millisecond (ts, 13 bits; unwrapped into bin.s) plus the offset o
 * of the least delayed message lately: o follows each earlier one at once and drifts up 1/4096 of the time since (the
 * two clocks' drift). A sender whose times do not move (some send 0) or a gap of 4 s: o from this one (its arrival,
 * less how much older than the packet's newest it is). Only BIN_LATE_N in a row more than BIN_LATE_MS late take o from
 * the last one. So the connection interval's bunching (a few F8s arriving together every 7.5-45 ms) is taken out */
#define BIN_LATE_MS 100u
#define BIN_LATE_N 24u                          /* (a beat of F8s) */
static struct {
    uint32_t s, o, at;                          /* the sender's time unwrapped; the offset; our last arrival (ms) */
    uint16_t ts;
    uint8_t ok, late;
} bin;
static uint32_t ble_in_time(uint16_t ts, uint16_t last_ts)
{
    uint32_t now = fm1_ms, arr, t, rise;
    uint32_t back = (uint32_t)(uint16_t)(last_ts - ts) & 0x1FFFu;   /* ms older than the packet's newest */
    arr = now - back;
    if (bin.ok && now - bin.at < 4000u)
        bin.s += (uint32_t)(uint16_t)(ts - bin.ts) & 0x1FFFu;
    else
        bin.s = 0, bin.o = arr, bin.ok = 1;
    rise = (now - bin.at) >> 12;
    bin.ts = ts;
    bin.at = now;
    t = arr - bin.s;                            /* the offset this message gives */
    if ((int32_t)(t - (bin.o + rise)) < 0)
        bin.o = t;                              /* less delayed: the offset */
    else
        bin.o += rise;
    t = bin.o + bin.s;
    bin.late = (int32_t)(now - t) > (int32_t)BIN_LATE_MS ? (uint8_t)(bin.late + 1u) : 0u;
    if (bin.late >= BIN_LATE_N || (int32_t)(t - now) > 0) {
        bin.s = 0;                              /* late a beat long, or ahead of now: from this one */
        bin.o = arr;
        bin.late = 0;
        t = arr;
    }
    return t;
}

BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts)   /* the BLE interrupts */
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (!ble_on || !ble_rx_on)
        return;
    if (cin == 0xFu && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) {
        uint32_t age;
        if (song.g[G_SYNC] != 3)
            return;                             /* (SYNC not BT: a DAW's clock is not queued, notes keep the room) */
        age = fm1_ms - ble_in_time(ts, last_ts);
        if ((int32_t)age < 0)
            age = 0;
        if (age > BLE_RT_AGE_MAX)
            age = BLE_RT_AGE_MAX;
        pkt = 0x0Fu | st << 8 | age << 16;      /* clock / transport: seq.c mclk_event, SYNC = BT */
    } else if (cin < 8u || cin > 0xEu || (st >> 4) != cin) {
        return;                                 /* channel voice messages (as usb.c midi_in_event checks them) */
    }
    if (bmi_w - bmi_r >= BMQ) {
        ble_rx_lost++;
        return;
    }
    ble_in_q[bmi_w % BMQ] = (pkt & ~0xF0u) | 0x20u;   /* cable 2: Bluetooth (0 USB, 1 TRS) */
    RING_PUBLISH();
    bmi_w++;
}

static void ble_midi_out(uint32_t pkt)          /* the audio ISR, from usb.c midi_out_event */
{
    if (!ble_out_on || !ble_tx_on || bmo_w - bmo_r >= BMQ)
        return;
    ble_out_q[bmo_w % BMQ] = pkt & ~0xF0u;      /* (cable 0 to the encoder) */
    ble_out_ms[bmo_w % BMQ] = fm1_ms;
    RING_PUBLISH();
    bmo_w++;
    ble_tx_count++;
}

static void ble_note_seen(uint32_t pkt)         /* the notes a central has on, to end them if it is gone */
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, n = (pkt >> 16) & 0x7Fu;
    if (st == 0x90u && ((pkt >> 24) & 0x7Fu))
        ble_held[ch][n >> 3] |= (uint8_t)(1u << (n & 7u));
    else if (st == 0x80u || st == 0x90u)
        ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
}

static int ble_in_push(uint32_t pkt)           /* TIMER5: into usb.c's ring, as uart_midi_poll does */
{
    if (mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = pkt;
    RING_PUBLISH();
    mi_w++;
    return 1;
}

static void ble_notes_end(void)                 /* a note-off for each note a central left on (the next tick if full) */
{
    uint32_t ch, n, left = 0;
    for (ch = 0; ch < 16u; ch++)
        for (n = 0; n < 128u; n++) {
            if (!(ble_held[ch][n >> 3] & (1u << (n & 7u))))
                continue;
            if (ble_in_push(0x28u | (0x80u | ch) << 8 | n << 16))
                ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
            else
                left = 1;
        }
    if (!left)
        ble_release = 0;
}

static void ble_midi_poll(void)                 /* the TIMER5 ISR (main.c): BLE in -> the MIDI ring */
{
    while (bmi_r != bmi_w) {
        uint32_t pkt;
        RING_PUBLISH();
        pkt = ble_in_q[bmi_r % BMQ];
        if (!ble_in_push(pkt))
            return;                             /* the ring is full: the next tick */
        ble_note_seen(pkt);
        ble_rx_count++;
        RING_PUBLISH();
        bmi_r++;
    }
    if (ble_release)
        ble_notes_end();                        /* (after what the central had sent: those are in the ring now) */
}

#if FELUCCA_FLASH
static int ble_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)   /* SPI reads, no XIP decryption (felucca.c) */
{
    (void)ctx;
    return flash_ok ? st_read(off, dst, n) : -1;
}
/* the backup, from the current settings copy, else the other one (each checks itself: a mark and a CRC) */
static void ble_rf_bak_read(void)
{
    st_hdr_t a, b;
    uint32_t k, first;
    if (!flash_ok)
        return;
    {   /* the newer committed copy first (headers only: st_buf untouched) */
        int va = st_head(OBJ_SETTINGS, 0, &a) == 0, vb = st_head(OBJ_SETTINGS, 1, &b) == 0;
        first = (vb && (!va || b.seq > a.seq)) ? 1u : 0u;
    }
    for (k = 0; k < 2; k++) {
        uint32_t copy = k ? !first : first;
        if (st_read(st_sector(OBJ_SETTINGS, copy) + BLE_RF_BAK_OFF, ble_rf_bak, BLE_RF_COPY_SIZE) == 0 &&
            ble_rf_copy_ok(ble_rf_bak))
            return;
    }
    memset(ble_rf_bak, 0, sizeof ble_rf_bak);
}
#else
/* host tests: a flash image to read the VM from (0: none), and ble_rf_bak as the test left it */
static int (*ble_vm_rd_host)(uint32_t off, uint8_t *dst, uint32_t n);
static int ble_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    (void)ctx;
    return ble_vm_rd_host ? ble_vm_rd_host(off, dst, n) : -1;
}
static void ble_rf_bak_read(void) {}
#endif

static int ble_radio_ok(void) { return !BLE_HW_WL82 || ble_rf_src != BLE_RF_NONE; }

/* the address: the VM's provisioned public one, else random static from the unit's own calibration bytes */
static uint8_t ble_midi_addr(uint8_t a[6])
{
    const uint8_t *p = (const uint8_t *)&ble_trims;
    uint32_t h = 2166136261u, g = 0x9E3779B9u, i;
    uint8_t rnd = ble_hw_addr(a);
    if (!rnd || ble_rf_src == BLE_RF_NONE)
        return rnd;
    for (i = 0; i < sizeof ble_trims; i++) {
        h = (h ^ p[i]) * 16777619u;
        g = (g ^ p[i]) * 0x01000193u + (g >> 7);
    }
    if ((h == 0u && (g & 0x3FFFu) == 0u) || (h == 0xFFFFFFFFu && (g & 0x3FFFu) == 0x3FFFu))
        g ^= 0x1234u;
    a[0] = (uint8_t)h, a[1] = (uint8_t)(h >> 8), a[2] = (uint8_t)(h >> 16), a[3] = (uint8_t)(h >> 24);
    a[4] = (uint8_t)g, a[5] = (uint8_t)(((g >> 8) & 0x3Fu) | 0xC0u);   /* random static: the top two bits 11 */
    return 1;
}

#if BLE_HW_WL82
#define BLE_STEP(s) fm1_ble_step(FM1_BLE_STEP_##s)   /* the breadcrumb a watchdog reset keeps (hal/fm1_ble_rf.h) */
#else
#define BLE_STEP(s) ((void)0)
#endif

/* The second core stops for the rest of the session before the radio's start-up program runs. Optimist (whose stack this
 * is) found BLE plus a running second core crashed the FM-1 within 2-60 s until it copied stock's start-up order (its
 * docs/DUAL-CORE.md), and it holds the second core while the radio starts at run time, with a rule that keeps it
 * stopped for the boot; Beat It does the safe one of those: held with SLOOP's own fallback (the one cpu1_wait uses after
 * a timeout), not restarted, until the next power-on. Interrupts off: the audio ISR posts and waits for its job inside
 * the ISR, so no job is in flight here. One core renders everything from here (as 2ND CORE OFF). */
static void ble_cpu1_down(void)
{
#if FELUCCA_CPU1
    uint32_t f = irq_save();
    if (cpu1_state == CPU1_OK || fm1_c1_on) {
        fm1_cpu1_hold();
        cpu1_state = CPU1_FAILED;               /* (cpu1_run declines from here; ABOUT: "1 CORE") */
        ble_cpu1_stopped = 1;
    }
    irq_restore(f);
#endif
}

static int ble_radio_start(void)                /* the first time Bluetooth is wanted, BLE interrupts masked */
{
    uint8_t a[6], rnd;
    if (ble_up)
        return 1;
    if (!ble_radio_ok())
        return 0;
    ble_cpu1_down();
#if BLE_HW_WL82
    fm1_ble_crumb_irqs = 0;
#endif
    BLE_STEP(STACK_INIT);
    rnd = ble_midi_addr(a);
    ble_init(a, rnd);
#if BLE_HW_WL82
    ble_hw_wl82_start(&ble_trims);              /* the radio and the baseband (ble/ble_hw_wl82.c) */
#endif
    BLE_STEP(STARTED);
    ble_up = 1;
    return 1;
}

static void ble_midi_init(void)                 /* at boot, before the interrupts are on: flash reads only */
{
    int complete, save = 0;
    struct ble_rf_trims vm;
    ble_rf_bak_read();
    complete = ble_vm_scan(ble_vm_rd, 0, &ble_vm_seen, &vm, 0, 0);
    /* the VM (complete) first, then the backup (valid), else none; a complete VM (re)makes the backup */
    ble_rf_src = (uint8_t)ble_rf_choose(complete, &vm, ble_vm_seen.area, ble_rf_bak, &ble_trims, &save);
    if (save) {
        ble_rf_bak_owed = 2;                    /* written with the next two settings saves (A and B) */
#if FELUCCA_FLASH
        settings_later = 1;                     /* (main loop: saved once the FM-1 is idle, project.c) */
#endif
    }
#if BLE_HW_WL82
    fm1_ble_crumb_boot();
    ble_hung_step = ble_crumb_hung(fm1_ble_bc.prev);
    ble_hw_wl82_attach();                       /* the two vectors, masked: no BLE / RF register written */
#endif
}

/* main loop (ui_menu.c, after BT MIDI RECEIVE / SEND changed): the radio on while either is wanted */
static void ble_midi_set(uint8_t rx, uint8_t tx)
{
    uint8_t on = (uint8_t)(rx || tx);
    ble_rx_on = rx;
    ble_tx_on = tx;                             /* (SEND OFF: ble_app_midi_peek empties the out ring) */
    if (!rx)
        ble_release = 1;                        /* (receive OFF: what a central left on ends now) */
    if (on == ble_on)
        return;
    if (on && !ble_up) {
        ble_hung_step = 0;                      /* (a new start: the last run's breadcrumb is old news) */
        BLE_STEP(SET_ON);
        if (!ble_radio_start())
            return;                             /* no stored calibration: the radio stays off */
    }
    if (!ble_up)
        return;
    ble_on = on;
    fm1_ble_irqs_hold(1);
    BLE_STEP(ENABLE);
    ble_enable(on);
    if (!on)
        ble_release = 1;
    BLE_STEP(IRQS_ON);
    fm1_ble_irqs_hold(0);
    if (on)
        BLE_STEP(RUNNING);
    else
        BLE_STEP(SET_OFF);
}

/* (1.2) the step a breadcrumb word names when the radio's start-up had not finished: the FM-1 reset (a watchdog
 * reset, a crash) in the middle of it. 0: it finished (RUNNING), Bluetooth was turned off (SET_OFF), or no word */
enum { BC_NONE, BC_SET_ON, BC_RF_INIT = 3, BC_BB_INIT, BC_RUNNING = 12, BC_SET_OFF };   /* (FM1_BLE_STEP_*: the host has no hal) */
#if BLE_HW_WL82
_Static_assert(FM1_BLE_STEP_RF_INIT == BC_RF_INIT && FM1_BLE_STEP_BB_INIT == BC_BB_INIT && FM1_BLE_STEP_RUNNING == BC_RUNNING &&
                   FM1_BLE_STEP_SET_OFF == BC_SET_OFF, "breadcrumb steps");
#endif
static uint8_t ble_crumb_hung(uint32_t w)
{
    uint32_t step = (w >> 16) & 0xFFu;
    if (w >> 24 != 0xB1u || step == BC_NONE || step == BC_RUNNING || step == BC_SET_OFF || step > BC_SET_OFF)
        return 0;
    return (uint8_t)step;
}
static const char *ble_step_name(uint32_t step)
{
    static const char *const N[] = {"", "SET ON", "STACK", "RF INIT", "BASEBAND", "STARTED", "ENABLE", "LINK STOP",
                                    "LINK OPEN", "ADVERT", "ADVERT ON", "IRQS ON", "RUNNING", "SET OFF"};
    return step < sizeof N / sizeof N[0] ? N[step] : "?";
}

/* (1.2) BT CONNECTION's line below its value (ui_menu.c): the radio step the last run was in when the FM-1 reset
 * (a watchdog reset: a start-up, or a re-advertise after a disconnect, that never returned), else what went
 * through the link this session (MIDI messages in / out, and any lost), else nothing */
static const char *ble_link_sub(int *warn)
{
    static char b[28];
    uint32_t n;
    *warn = 0;
    if (ble_hung_step && !ble_up) {
        *warn = 1;
        str_cpy(b, "RESET DURING: ", sizeof b);
        str_cpy(b + str_len(b), ble_step_name(ble_hung_step), sizeof b - str_len(b));
        return b;
    }
    if (!ble_up || (!ble_rx_count && !ble_tx_count && !ble_rx_lost))
        return "";
    str_cpy(b, "IN ", sizeof b);
    fmt_int(b + str_len(b), (int32_t)(ble_rx_count % 100000u));
    str_cpy(b + str_len(b), " OUT ", sizeof b - str_len(b));
    fmt_int(b + str_len(b), (int32_t)(ble_tx_count % 100000u));
    n = ble_rx_lost;
    if (n) {
        *warn = 1;
        str_cpy(b + str_len(b), " LOST ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), (int32_t)(n % 10000u));
    }
    return b;
}

/* (1.2) what the menu redraws for (ui_menu.c draw_menu's signature): 1.1 left the link state and RADIO CAL's
 * BACKING UP out of it, so a phone connecting did not change the screen until a knob moved. Counts at most
 * twice a second */
static uint32_t ble_menu_sig(void)
{
    static uint32_t t, cnt;
    uint32_t s = (uint32_t)ble_on + 2u * (uint32_t)ble_connected() + 4u * (uint32_t)ble_midi_ready() +
                 8u * (uint32_t)ble_rf_bak_owed + 32u * (uint32_t)ble_hung_step + 1024u * (uint32_t)ble_up;
    if (fm1_ms - t >= 500u) {
        t = fm1_ms;
        cnt = ble_rx_count * 7u + ble_tx_count * 131u + ble_rx_lost * 8191u;
    }
    return s * 2654435761u + cnt;
}

/* RADIO CAL's value and its line below (ui_menu.c) */
static const char *ble_cal_text(void)
{
    if (ble_rf_src == BLE_RF_FROM_VM)
        return "STOCK";
    if (ble_rf_src == BLE_RF_FROM_COPY)
        return "BACKUP";
    return "MISSING";
}
static const char *ble_cal_sub(void)
{
    if (ble_rf_src == BLE_RF_FROM_VM)
        return ble_rf_bak_owed ? "BACKING UP" : "BACKED UP";
    if (ble_rf_src == BLE_RF_FROM_COPY)
        return "STOCK DATA GONE";
    return "BT CANNOT START";
}

/* BT CONNECTION's value (ui_menu.c) */
static const char *ble_link_text(void)
{
    if (ble_rf_src == BLE_RF_NONE && BLE_HW_WL82)
        return "NO RF CAL";
    if (!ble_on)
        return "OFF";
    if (ble_midi_ready())
        return "CONNECTED";
    if (ble_connected())
        return "LINKING";
    return "ADVERTISING";
}
