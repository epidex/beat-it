/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP boot splash: the logo (tools/gen_logo.py -> sloop_logo.h: 16 colours, RLE runs of
 * (run - 1) << 4 | colour), drawn into the canvas in bands of at most 120 rows. */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
/* (Beat It) the boot screen: "BEAT IT" in large purple letters on black (FONT_L drawn small, then three times as big,
 * in place), a purple bar, the version and where it comes from. No logo image: 4.6 KB of flash less than SLOOP's. */
UI_SMALL static void beatit_title_draw(uint32_t y0)
{
    static const char T[] = "BEAT IT";
    uint32_t w = text_w(&FONT_L, T), k = w * 3u <= 232u ? 3u : 2u, h = 32u, off, y, x, i;
    int32_t px = 0;
    char c[2] = {0, 0};
    cv_begin(240, h * k, C_BLACK);
    for (i = 0; T[i]; i++) {                            /* small, at the top left: the letters in the purple tracks' colours */
        c[0] = T[i];
        px = cv_text(px, 0, &FONT_L, c, T[i] == ' ' ? C_BLACK : TE_COL_PUR[i & 3u]);
    }
    off = (240u - w * k) / 2u;
    for (y = h * k; y-- > 0;)                           /* then k times as big, in place (from the end: nothing read is
                                                         * overwritten before) */
        for (x = w * k; x-- > 0;)
            cv_px[y * 240u + off + x] = cv_px[(y / k) * 240u + x / k];
    for (y = 0; y < h * k; y++)                         /* (the small one's left-over columns) */
        for (x = 0; x < off; x++)
            cv_px[y * 240u + x] = swap16(C_BLACK);
    cv_blit(0, y0);
}

/* the boot screen */
UI_SMALL static void sloop_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    beatit_title_draw(64);
    lcd_fill(40, 172, 160, 3, TE_COL_PUR[0]);           /* a purple bar under the title */
    cv_begin(240, 36, C_BLACK);
    cv_text(120 - text_w(&FONT_S, BEATIT_VERSION) / 2, 0, &FONT_S, BEATIT_VERSION, RGB(196, 160, 255));
    cv_text(120 - text_w(&FONT_S, "based on SLOOP and felucca") / 2, 18, &FONT_S, "based on SLOOP and felucca", RGB(110, 90, 140));
    cv_blit(0, 196);
}
