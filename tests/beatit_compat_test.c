/* SPDX-License-Identifier: GPL-3.0-only
 * Beat It 2.0 (a modified version of Melodee, Copyright (C) 2026 Kerem Kilic, Ellic Studio): new in Beat It 2.0.
 * BRIGHTNESS compatibility. Built in the Beat It 2.0 tree it writes a settings record at each brightness step
 * (build/compat/bi_step<N>.bin) and checks the steps differ only in favorites row 8 bytes 26..27; built against
 * plain Melodee 1.6.1 (-DPLAIN_MELODEE, the same preamble) it reads those records and checks it imports every one
 * to the very same settings, so going back to plain Melodee carries nothing over from BRIGHTNESS. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../firmware/src/tuning.h"
#define __attribute__(x)
#define NENGINES 20u
#define USER_GENERAL 16u
#define USER_NATIVE_P5 20u
#define ENGI_PROPHET 19u
static int p5_favorite_has(uint32_t slot,int factory){(void)slot;(void)factory;return 0;}
static int p5_favorite_set(uint32_t slot,int on,int factory){(void)slot;(void)on;(void)factory;return 0;}
#define UP_SLOTS 64u
#define USER_NATIVE_FM 17u
#define USER_NATIVE_CZ 18u
#define ENGI_FM6 12u
#define ENGI_CZ 15u
static void fm1_led_key(unsigned k, int on) { (void)k; (void)on; }
static int fm1_enc_take(unsigned k) { (void)k; return 0; }
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
static void settings_save(void) {}
static uint8_t settings_boot, settings_drumch = 10;  /* (params.c) */
#define VOICE_BUDGET_DEFAULT 16u
static int16_t settings_budget=VOICE_BUDGET_DEFAULT;
#define VOICE_KINDS 5u
static int16_t settings_vpoints[VOICE_KINDS], settings_safety;  /* (params.c GLO > WEIGHTS) */
static int native_used(uint32_t e, uint32_t k) { (void)e; (void)k; return 0; }
#if __has_include("../firmware/src/favorites.c")
static int up_favorite(uint32_t k);
static int up_favorite_set(uint32_t k,int on);
#include "../firmware/src/favorites.c"
static int up_favorite(uint32_t k){return k<64u && (((k<32u?favorites.user:favorites_user_hi)>>(k%32u))&1u);}
static int up_favorite_set(uint32_t k,int on){if(k>=64u)return 0;uint32_t *word=k<32u?&favorites.user:&favorites_user_hi;uint32_t old=*word;if(on)*word|=1u<<(k%32u);else *word&=~(1u<<(k%32u));return old!=*word;}
#endif
#include "../firmware/src/settings_persist.c"

#include <stddef.h>
#include <stdlib.h>
static persist_t base(void)
{
    persist_t q = {0};
    q.magic = PERSIST_MAGIC; q.panel = PANEL_DEFAULT;
    assert(settings_import(&q, sizeof q));
    settings_screen = 3;                               /* some non-default settings around it */
    settings_lights = 2;
    settings_export(&q);
    return q;
}
static int rd(const char *path, persist_t *p)
{
    FILE *f = fopen(path, "rb"); size_t n;
    if (!f) return 0;
    n = fread(p, 1, sizeof *p, f); fclose(f);
    return n == sizeof *p;
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/compat";
    char path[512];
    int bad = 0;
#ifndef PLAIN_MELODEE
    const size_t off = offsetof(persist_t, favorites.factory[0][0]) + 8u * 32u + 26u;
    persist_t r0, r;
    r0 = base();
    for (uint32_t s = 0; s < BRIGHT_N; s++) {
        persist_t q = base();
        lcd_bright_set(s);
        settings_export(&q);
        snprintf(path, sizeof path, "%s/bi_step%u.bin", dir, (unsigned)s);
        FILE *f = fopen(path, "wb"); assert(f); fwrite(&q, 1, sizeof q, f); fclose(f);
        for (size_t i = 0; i < sizeof q; i++)
            if (((uint8_t *)&q)[i] != ((uint8_t *)&r0)[i] && !(s && (i == off || i == off + 1u))) {
                printf("  step %u: byte %zu differs outside BRIGHTNESS\n", (unsigned)s, i); bad++;
            }
        if (s && (((uint8_t *)&q)[off] != 'B' || ((uint8_t *)&q)[off + 1] != s)) { printf("  step %u: not tagged\n", (unsigned)s); bad++; }
        lcd_bright_set(0);
        r = q;
        if (!settings_import(&r, sizeof r) || lcd_bright_step != s) { printf("  step %u: not restored\n", (unsigned)s); bad++; }
        printf("  Beat It 2.0 BRIGHTNESS %-4s saved, read back %s\n", BRIGHT_NAME[s], BRIGHT_NAME[lcd_bright_step]);
    }
    {   /* a record plain Melodee saved (its bytes 26..27 zero), and junk there: 100 % */
        persist_t q;
        snprintf(path, sizeof path, "%s/melodee_saved.bin", dir);
        if (rd(path, &q)) {
            lcd_bright_set(3);
            if (!settings_import(&q, sizeof q) || lcd_bright_step) { printf("  plain Melodee's record: not 100%%\n"); bad++; }
            else printf("  a record plain Melodee 1.6.1 saved: read as 100%%\n");
        }
        q = base(); ((uint8_t *)&q)[off] = 0x5A; ((uint8_t *)&q)[off + 1] = 0xFF;
        lcd_bright_set(2);
        if (!settings_import(&q, sizeof q) || lcd_bright_step) { printf("  untagged bytes: not 100%%\n"); bad++; }
        q = base(); ((uint8_t *)&q)[off] = 'B'; ((uint8_t *)&q)[off + 1] = 9;
        lcd_bright_set(2);
        if (!settings_import(&q, sizeof q) || lcd_bright_step) { printf("  out-of-range step: not 100%%\n"); bad++; }
        printf("  untagged / out-of-range bytes: read as 100%%\n");
        q = base(); ((uint8_t *)&q)[off] = 0x5A; ((uint8_t *)&q)[off + 1] = 0xFF;   /* saving at 100 %: not ours, kept */
        assert(settings_import(&q, sizeof q)); lcd_bright_set(0); settings_export(&q);   /* (loaded, then saved) */
        if (((uint8_t *)&q)[off] != 0x5A || ((uint8_t *)&q)[off + 1] != 0xFF) { printf("  100%% save changed bytes it does not own\n"); bad++; }
        else printf("  saving at 100%% leaves bytes it does not own as they were\n");
    }
#else
    persist_t ref, q, out;
    int have = 0;
    for (unsigned s = 0; s < 5u; s++) {
        snprintf(path, sizeof path, "%s/bi_step%u.bin", dir, s);
        if (!rd(path, &q)) { printf("  missing %s\n", path); bad++; continue; }
        if (!settings_owned(&q)) { printf("  step %u: not read as Melodee's own settings\n", s); bad++; }
        if (!settings_import(&q, sizeof q)) { printf("  step %u: rejected\n", s); bad++; continue; }
        settings_export(&out);
        memset(out.favorites.factory[8] + 26, 0, 2);   /* (kept as they were: plain Melodee never reads them) */
        if (!have) { ref = out; have = 1; }
        else if (memcmp(&ref, &out, sizeof out)) { printf("  step %u: plain Melodee's settings differ\n", s); bad++; }
        printf("  plain Melodee 1.6.1 read Beat It's BRIGHTNESS step %u: zoom word %u, screen off %u, lights %u\n",
               s, (unsigned)q.zoom, (unsigned)settings_screen, (unsigned)settings_lights);
    }
    snprintf(path, sizeof path, "%s/melodee_saved.bin", dir);
    { persist_t m = {0}; m.magic = PERSIST_MAGIC; m.panel = PANEL_DEFAULT; settings_import(&m, sizeof m);
      settings_export(&m); FILE *f = fopen(path, "wb"); fwrite(&m, 1, sizeof m, f); fclose(f); }
#endif
    printf(bad ? "BRIGHTNESS compat: %d FAILED\n" : "BRIGHTNESS compat: all checks ok\n", bad);
    return bad != 0;
}
