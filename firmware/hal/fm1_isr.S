/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* IRQ entry wrappers: save what a C call may clobber, call the C body,
 * restore, csync, rti. clang does not emit the csync needed before rti,
 * so ISRs are not written in C directly. */
	.macro FM1_ISR name, body
	.section .text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	call \body
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	FM1_ISR isr_timer5, fm1_timer5_irq
	FM1_ISR isr_alnk0, fm1_alnk0_irq

/* (Beat It 1.5, from Optimist's fm1_isr.S, GPL-3.0-only) the same two entries in RAM, their C bodies called through a
 * register (RAM to the flash: a direct call does not reach). Optimist (docs/DUAL-CORE.md item 6) runs TIMER5 and ALNK0
 * from these in every second-core build: on its unit, with core 1 running, TIMER5 was dispatched to its XIP stub with
 * one address bit wrong three times (2026-10-15). Beat It has no .ram_hot: they sit in .c1_text, the RAM code window
 * the boot copies and the PC guard already allows (app.ld) */
	.macro FM1_ISR_RAM name, body
	.section .c1_text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	r3 = \body
	call r3
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	FM1_ISR_RAM isr_timer5_ram, fm1_timer5_irq
	FM1_ISR_RAM isr_alnk0_ram, fm1_alnk0_irq
