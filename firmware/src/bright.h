/* SPDX-License-Identifier: GPL-3.0-only
 * Beat It 2.0, a modified version of Melodee (Copyright (C) 2026 Kerem Kilic, Ellic Studio): this file is new in
 * Beat It 2.0. Included by lcd.c and settings_persist.c (whichever comes first). */
#ifndef BEATIT_BRIGHT_H
#define BEATIT_BRIGHT_H
/* (Beat It 2.0, from Beat It 1.6 / Bump It 1.1) BRIGHTNESS: the panel has no backlight control, so below 100 %
 * every colour is darkened as it is sent. 0 = full: the pixels go untouched. Saved in the settings record's
 * favorites row 8 bytes 26..27 ('B', step), a spot no other setting uses (settings_persist.c) */
#define BRIGHT_N 5u
static const uint16_t BRIGHT_Q8[BRIGHT_N] = {256u, 218u, 187u, 148u, 108u};
static const char *const BRIGHT_NAME[BRIGHT_N] = {"100%", "70%", "50%", "30%", "15%"};
static uint8_t lcd_bright_step;                       /* 0 = full */
static uint8_t lcd_l5[32], lcd_l6[64];                /* a 5-bit / 6-bit channel value at this step */

static void lcd_bright_set(uint32_t step)             /* the tables for step (before the next blit) */
{
    uint32_t q, v;
    if (step >= BRIGHT_N)
        step = 0;
    lcd_bright_step = (uint8_t)step;
    q = BRIGHT_Q8[step];
    for (v = 0; v < 32u; v++)
        lcd_l5[v] = (uint8_t)((v * q + 128u) >> 8);
    for (v = 0; v < 64u; v++)
        lcd_l6[v] = (uint8_t)((v * q + 128u) >> 8);
}

static inline uint16_t lcd_dim565(uint32_t c)         /* an RGB565 colour (CPU order) at this step */
{
    return (uint16_t)((uint32_t)lcd_l5[(c >> 11) & 31u] << 11 | (uint32_t)lcd_l6[(c >> 5) & 63u] << 5 |
                      lcd_l5[c & 31u]);
}

#endif
