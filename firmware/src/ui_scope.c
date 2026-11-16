/* SPDX-License-Identifier: GPL-3.0-only
 * Beat It 2.0, a modified version of Melodee (Copyright (C) 2026 Kerem Kilic, Ellic Studio): this file is new in
 * Beat It 2.0. The colour ramp comes from Bump It 1.1's purple scope. */
/* (Beat It 2.0) HOME's third page (HOME tapped on the chord view; HOME again: Stage): the output as a full-screen
 * oscilloscope, purple on black, the trace's colour following the sound's pitch on Bump It's ramp:
 *   ~40 Hz red, ~250 Hz magenta, ~560 Hz purple, ~1 kHz blue, ~3 kHz cyan, ~9 kHz near white; quiet: a dim purple.
 * It reads only the scope snapshot audio.c already keeps (every other output frame, 22.05 kHz, SCOPE_N samples),
 * in the main loop's screen drawing: nothing is added to the audio path. The pitch is estimated from the rising zero
 * crossings in the snapshot (with hysteresis, so a little noise does not count), the colour eased between frames.
 * Stage's knobs drop in over the top as on the other HOME pages (ui_stage.c stage_knobs). Included by ui_draw.c */
#define SF_Y 32                                         /* the scope: rows 32 .. 239, the whole width */
#define SF_H 208
#define SF_W 240
#define SF_RATE 22050                                   /* the snapshot's rate: 44.1 kHz, every other frame */
#define SF_QUIET RGB(70, 30, 110)

static int32_t sf_pos = 12 * 16, sf_amt;                /* the ramp position x 16 (eased), presence 0..32 */
static int32_t sf_hz_shown;                             /* the readout (eased), Hz; 0: none */

static const uint8_t SF_STOP_POS[6] = {0, 5, 12, 19, 25, 31};
static const uint16_t SF_STOP_HZ[6] = {40, 250, 560, 1000, 3000, 9000};
static const uint8_t SF_STOP_RGB[6][3] = {{255, 40, 40}, {255, 40, 210}, {170, 60, 255}, {70, 90, 255},
                                         {0, 220, 255}, {220, 255, 255}};

static uint16_t sf_ramp(int32_t p16)                    /* Bump It 1.1's FREQUENCY ramp: position x 16 -> colour */
{
    int32_t s = 0, k, a, b, c[3];
    p16 = clamp(p16, 0, 31 * 16);
    while (s < 4 && p16 > SF_STOP_POS[s + 1] * 16)
        s++;
    a = SF_STOP_POS[s] * 16;
    b = SF_STOP_POS[s + 1] * 16;
    for (k = 0; k < 3; k++)
        c[k] = SF_STOP_RGB[s][k] + (SF_STOP_RGB[s + 1][k] - SF_STOP_RGB[s][k]) * (p16 - a) / (b - a);
    return RGB(c[0], c[1], c[2]);
}

static int32_t sf_log2_q8(uint32_t v)                   /* log2(v) x 256, the mantissa linear (v >= 1) */
{
    int32_t e = 31;
    while (e && !(v >> e))
        e--;
    return e * 256 + (int32_t)(e >= 8 ? (v >> (e - 8)) & 255u : (v << (8 - e)) & 255u);
}

static int32_t sf_pos_of(int32_t hz)                    /* Hz -> ramp position x 16, log between the stops */
{
    int32_t s = 0, lo, hi;
    if (hz <= SF_STOP_HZ[0])
        return 0;
    if (hz >= SF_STOP_HZ[5])
        return 31 * 16;
    while (s < 4 && hz > SF_STOP_HZ[s + 1])
        s++;
    lo = sf_log2_q8(SF_STOP_HZ[s]);
    hi = sf_log2_q8(SF_STOP_HZ[s + 1]);
    return SF_STOP_POS[s] * 16 + (SF_STOP_POS[s + 1] - SF_STOP_POS[s]) * 16 * (sf_log2_q8((uint32_t)hz) - lo) / (hi - lo);
}

/* the pitch of n samples at SF_RATE from their rising zero crossings (armed below -peak/8); 0: too few */
static int32_t sf_pitch(const int16_t *s, uint32_t n, int32_t peak)
{
    int32_t h = peak / 8, first = -1, last = -1, crossings = 0;
    uint32_t i, armed = 0;
    for (i = 0; i < n; i++) {
        if (s[i] < -h)
            armed = 1;
        else if (armed && s[i] >= 0) {
            if (first < 0)
                first = (int32_t)i;
            last = (int32_t)i;
            crossings++;
            armed = 0;
        }
    }
    if (crossings < 2 || last <= first)
        return 0;
    return SF_RATE * (crossings - 1) / (last - first);
}

static void scope_full_draw(void)
{
    int16_t *snap = scope_snap;
    uint32_t w = scope_w, i, trig = 0;
    int32_t peak = 1, cy = SF_H / 2, py = cy, x, hz, amt;
    uint16_t c, glow, grid;
    char b[16];
    stage_knobs();
    if (!ui.force && (ui.frame & 1u))                   /* (every other frame, as the chord view's pulses) */
        return;
    for (i = 0; i < SCOPE_N; i++) {                     /* one snapshot for the whole picture */
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    for (i = 1; i < SCOPE_N - SF_W; i++)                /* triggered on a rising zero crossing */
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    hz = peak >= 300 ? sf_pitch(snap, SCOPE_N, peak) : 0;
    if (hz) {                                           /* the colour eases toward the pitch, the presence up */
        sf_pos += (sf_pos_of(hz) - sf_pos) / 3;
        sf_hz_shown = sf_hz_shown ? sf_hz_shown + (hz - sf_hz_shown) / 2 : hz;
    }
    amt = hz ? 32 : 0;
    sf_amt += (amt - sf_amt + (amt > sf_amt ? 3 : -3)) / 4;
    sf_amt = clamp(sf_amt, 0, 32);
    if (!sf_amt)
        sf_hz_shown = 0;
    c = mix565(SF_QUIET, sf_ramp(sf_pos), (uint32_t)sf_amt);
    glow = mix565(T_BG, c, 8u);
    grid = mix565(T_BG, SF_QUIET, 10u);
    if (peak < 1500)                                    /* (quiet: not blown up to full height) */
        peak = 1500;
    for (uint32_t band = 0; band < SF_H; band += CV_MAX / SF_W) {
        cv_begin_band(SF_W, SF_H, T_BG, band);
        cv_scroll = stage.out;                          /* (the knobs over its top: its words there cut on purpose) */
        for (x = 30; x < SF_W; x += 30)                 /* the graticule: eight divisions, the centre line brighter */
            cv_rect(x, 0, 1, SF_H, grid);
        for (x = 26; x < SF_H; x += 26)
            cv_rect(0, x, SF_W, 1, x == cy ? mix565(T_BG, SF_QUIET, 20u) : grid);
        py = cy;
        for (x = 0; x < SF_W; x++) {                    /* the glow under the trace, then the trace */
            int32_t y = cy - snap[trig + (uint32_t)x] * (SF_H / 2 - 12) / peak;
            if (x)
                cv_line_t(x - 1, py, x, y, glow, 6);
            py = y;
        }
        py = cy;
        for (x = 0; x < SF_W; x++) {
            int32_t y = cy - snap[trig + (uint32_t)x] * (SF_H / 2 - 12) / peak;
            if (x)
                cv_line_t(x - 1, py, x, y, c, 2);
            py = y;
        }
        if (sf_hz_shown) {                              /* the pitch read, in its own colour, at the foot */
            fmt_int(b, sf_hz_shown);
            str_cpy(b + str_len(b), " Hz", 4);
            cv_text_on(8, ty_y(SF_H - 14, TY_LB), TY_LB, b, c, T_BG);
        }
        cv_scroll = 0;
        if (stage.out)                                  /* (the knobs over its top: the rows under them only) */
            cv_blit_band_from(0, SF_Y, (uint32_t)(Y_LABEL + CARD_H - SF_Y));
        else
            cv_blit_band(0, SF_Y);
    }
}
