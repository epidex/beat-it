/* SPDX-License-Identifier: GPL-3.0-only */
/* (Beat It 1.5, opt-in test build only: BEATIT_STOCK_SUPPLY=1, the "STOCK SUPPLY" installer) The FM-1's own supply
 * start-up, as the stock M-VAVE application does it, taken from Optimist (hal/fm1_supply.h, fm1_supply_stock and
 * fm1_lvr_stock, GPL-3.0-only), which ports it from Melodee 1.3.6 (keremimo/melodee hal/fm1_power.h, GPL-3.0-only;
 * reverse-engineered from FM-1_015 app_core 0x02004F90..0x0200509A). Optimist's docs/DUAL-CORE.md: the boot loader
 * leaves the 1.4 V rail on the LDO and "CPU1 merely spinning crashed CPU0 there"; with this order every Bluetooth +
 * two-core boot that crashed within 2-60 s before ran 3 x 180 s clean, on Optimist's unit.
 *
 * What it writes (P33 analog registers, JieLi AC79 SDK p33.h names), in this order, interrupts off, core 1 not started:
 *   CON0 LDO13 on, DCDC13 on, LDO13 off (the 1.4 V rail handed from the LDO to the DC-DC)
 *   CON1 = 0x20 (MAIN_VDD_LOAD_EN)
 *   CON5 = 0x74 (VDDIO_HD_SEL 1, VDDIOW 3 = 3.2 V, VDDIOM 4 = 3.2 V: the I/O rail, stock's final value; this is the
 *         write that can raise the pins' voltage if the boot loader left it lower)
 *   CON6 VD14_HD_SEL 1; CON7 = 0xA5, CON8 = 0x96 (the BT DC-DC's bias, duty, oscillator, sense, comparator)
 *   CON9 SYSVDD_VOL_HD_SEL 1; VDC14 up to 7 (1.60 V) a step every 100 us
 *   CON0 RC_250k_EN; P3_ANA_KEEP [5:0] cleared; CON2 VDDOK_DIS; P3_VLD_KEEP bit 5 cleared
 *   SYSVDD up to 15 (1.38 V) last, then the 2.6 V low-voltage detector (P3_VLVD_CON 0x78, then VLVD_EN)
 * VDC14 1.60 V and SYSVDD 1.38 V are what every Beat It and SLOOP 2.7+ already sets (fm1_sys.h fm1_supplies_stock).
 * Levels only go up, never past stock's. Read back: DC-DC on, LDO off, VDC14 7, SYSVDD 15, VDDIOM 4, no P33 timeout;
 * anything else and core 1 is never started this boot (cpu1.c: one core). Optimist: "at your own risk: above the AC79
 * datasheet's table, absolute limits not public, unit variation, heat and battery not measured." */
#pragma once
#include <stdint.h>
#include "fm1_sys.h"
#include "fm1_time.h"

#define FM1_SUP_TIMEOUT 1u
#define FM1_SUP_MODE 2u
#define FM1_SUP_LEVEL 4u
#define FM1_SUP_LVR 8u

static void fm1_sup_rmw(uint32_t reg, uint8_t clr, uint8_t set)
{
    fm1_p33_write(reg, (uint8_t)((fm1_p33_read(reg) & ~clr) | set));
}

/* P3_ANA_CON0 | CON5 << 8 | CON6 << 16 | CON9 << 24 (CON0 bit 2 LDO13_EN, bit 3 DCDC13_EN) */
static uint32_t fm1_sup_regs(void)
{
    return fm1_p33_read(0x00u) | (uint32_t)fm1_p33_read(0x05u) << 8 | (uint32_t)fm1_p33_read(0x06u) << 16 |
           (uint32_t)fm1_p33_read(0x09u) << 24;
}

/* -> 0 when it read back right, else FM1_SUP_* bits */
static uint32_t fm1_supply_stock_seq(void)
{
    static const struct { uint8_t reg, clr, set, wait; } STEP[] = {
        {0x00u, 0x00u, 0x04u, 1u}, {0x00u, 0x00u, 0x08u, 1u}, {0x00u, 0x04u, 0x00u, 1u},
        {0x01u, 0xFFu, 0x20u, 0u}, {0x05u, 0xFFu, 0x74u, 0u}, {0x06u, 0x18u, 0x08u, 0u},
        {0x07u, 0xFFu, 0xA5u, 0u}, {0x08u, 0xFFu, 0x96u, 0u}, {0x09u, 0x00u, 0x10u, 0u},
    };
    static const struct { uint8_t reg, clr, set; } LATE[] = {
        {0x00u, 0x00u, 0x80u}, {0x16u, 0x3Fu, 0x00u}, {0x02u, 0x00u, 0x40u}, {0x17u, 0x20u, 0x00u},
    };
    uint32_t i, r, t0 = fm1_p33_timeouts, err = 0;
    for (i = 0; i < sizeof STEP / sizeof STEP[0]; i++) {
        fm1_sup_rmw(STEP[i].reg, STEP[i].clr, STEP[i].set);
        if (STEP[i].wait)
            fm1_delay_us(100);
        if (fm1_p33_timeouts != t0)
            return FM1_SUP_TIMEOUT;
    }
    fm1_supply_raise(FM1_P3_ANA_CON6, 7u, 7u);
    for (i = 0; i < sizeof LATE / sizeof LATE[0] && fm1_p33_timeouts == t0; i++)
        fm1_sup_rmw(LATE[i].reg, LATE[i].clr, LATE[i].set);
    if (fm1_p33_timeouts != t0)
        return FM1_SUP_TIMEOUT;
    fm1_supply_raise(FM1_P3_ANA_CON9, 15u, 15u);
    fm1_delay_us(1000);
    r = fm1_sup_regs();
    if ((r & 0x0Cu) != 0x08u)
        err |= FM1_SUP_MODE;
    if (((r >> 8) & 7u) != 4u || ((r >> 16) & 7u) != 7u || ((r >> 24) & 15u) != 15u)
        err |= FM1_SUP_LEVEL;
    if (fm1_p33_timeouts != t0)
        err |= FM1_SUP_TIMEOUT;
    if (!err) {
        fm1_p33_write(0x11u, 0x78u);               /* the 2.6 V low-voltage detector, as stock, after the rails */
        fm1_sup_rmw(0x11u, 0x00u, 0x01u);
        if ((fm1_p33_read(0x11u) & 0x39u) != 0x39u)
            err |= FM1_SUP_LVR;
    }
    return err;
}
