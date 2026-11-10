/* (Beat It) SCOPE VIEW and SCOPE COLOUR ride in spare bits of the settings object's MIDI part (project.c midi_part):
 * stored and read back, the defaults (FULL SCREEN, SOUND) as zero bits so older settings read as the defaults, FX CH and
 * the filter's resonance untouched. Built on editor_host.c (the real panel.c / ui*.c / project.c on a RAM image). */
#include "../firmware/hal/fm1_flash_id.h"   /* (editor_host.c leaves it out: project.c uses it) */
#include "editor_host.c"

static int bad;
static void ck(int ok, const char *what)
{
    printf("%-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    uint8_t b[MIDI_PART];
    uint32_t v, c;
    int all = 1;
    scope_view = SV_FULL; scope_col = 7u;
    midi_part_fill(b);
    ck((b[2u * ML_N + 6u] >> 6) == 0u && (b[2u * ML_N + 5u] >> 5) == 0u, "defaults (FULL SCREEN, SOUND) store as zero bits");
    ck(midi_part_default(b), "all defaults: the MIDI part still reads as default (not written)");
    for (v = 0; v < SCOPE_VIEW_N; v++)
        for (c = 0; c < SCOPE_NCOL; c++) {
            uint8_t fx = (uint8_t)(c * 2u % 17u), reso = (uint8_t)(v * 3u + c % 4u);
            scope_view = (uint8_t)v; scope_col = (uint8_t)c; midi_set[6] = fx; flt_reso = reso;
            midi_part_fill(b);
            scope_view = 1u; scope_col = 1u; midi_set[6] = 0; flt_reso = 0;
            midi_part_read(b, MIDI_PART);
            if (scope_view != v || scope_col != c || midi_set[6] != fx || flt_reso != reso)
                all = 0;
        }
    ck(all, "every SCOPE VIEW x SCOPE COLOUR round-trips, with FX CH and RESONANCE untouched");
    scope_view = SV_OFF; scope_col = 2u;
    midi_part_fill(b);
    ck(!midi_part_default(b), "a changed scope setting is saved (the MIDI part no longer default)");
    b[2u * ML_N + 5u] &= 31u; b[2u * ML_N + 6u] &= 63u;   /* settings written by SLOOP: the bits clear */
    midi_part_read(b, MIDI_PART);
    ck(scope_view == SV_FULL && scope_col == 7u, "settings from SLOOP (bits clear): FULL SCREEN and SOUND");
    {   /* (Beat It 1.5) BT MIDI RECEIVE / SEND saved in REMOTE's byte, bits 5-6 */
        uint32_t r, t;
        int ok = 1;
        for (r = 0; r < 2u; r++)
            for (t = 0; t < 2u; t++) {
                bt_rx = (uint8_t)r; bt_tx = (uint8_t)t; midi_set[8] = 11u; midi_set[9] = 2u;
                midi_part_fill(b);
                bt_rx = (uint8_t)!r; bt_tx = (uint8_t)!t; midi_set[8] = 0;
                midi_part_read(b, MIDI_PART);
                if (bt_rx != r || bt_tx != t || midi_set[8] != 11u)
                    ok = 0;
            }
        ck(ok, "(1.5) BT MIDI RECEIVE x SEND round-trip, the REMOTE channel untouched");
        bt_rx = 1; bt_tx = 1;
        midi_part_fill(b);
        b[2u * ML_N + 7u] &= (uint8_t)~0x60u;            /* settings written by Beat It 1.4 or SLOOP */
        midi_part_read(b, MIDI_PART);
        ck(!bt_rx && !bt_tx, "(1.5) settings from 1.4 / SLOOP: Bluetooth OFF at power-on");
    }
    printf("beatit_settings_test: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
