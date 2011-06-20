/*	babelfish - self-propagating Just-In-Time IOS patcher

Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2008-2011		Haxx Enterprises <bushing@gmail.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

This code lives at http://gitweb.bootmii.org/?p=babelfish.git
*/
.arm

.extern _main
.extern __got_start
.extern __got_end
.extern __bss_start
.extern __bss_end
.extern __stack_addr
.extern delay
.globl _start
.globl debug_output
.globl dc_flush
.globl irq_kill
.globl disable_icache_dcache_mmu
.globl jump_to_r0
.section .init

_start:
	@ Get real address of _start
	sub	r4, pc, #8
	@ Subtract offset to get the address that we were loaded at
	ldr	r0, =_start
	sub	r4, r4, r0
	mov r7, r4

@ here's part of the magic:	
	@ XXX calculate size of code+data  -- or just hardcode it :/
	mov r5, #0x4000

	@ Output 0x42 to the debug port
	mov	r0, #0x42
	bl	debug_output

@   copy ourselves to hiding spot	
	mov r2, r4             @ r2 = original loading address
	add r5, r5, r4         @ stop copying at r5
@	ldr r4, =__hiding_spot @ set new loading address
	mov r4, #0x13000000    @ ugh we really shouldn't hardcode this - hiding spot = 0x13A80000
	add r4, r4, #0xA80000

	mov r1, r4
@ copy ourselves to hiding spot: from r2 to r1	
copy_loop:
	ldr r3, [r2]
	add r2, r2, #4
	str r3, [r1]
	add r1, r1, #4
	cmp r2, r5
	bne copy_loop

@   the copy_loop code plus these next two instructions are 0x60 bytes long
@   jump to our newly-relocated self -- execution will "seamlessly" resume
	add r0, r4, #0x60
	bx r0

@   execution is transferred here

@   the rest of this is just standard ELF loader stuff, except for the step after GOT relocation
	@ Set up a stack
	ldr	sp, =__stack_addr
	add	sp, r4
	
	@ Output 0x43 to the debug port
	mov	r0, #0x43
	bl	debug_output
	
	@ relocate the GOT entries
	ldr	r1, =__got_start
	add	r1, r4
	ldr	r2, =__got_end
	add	r2, r4

	@ subtract out previous reloc value (first entry in GOT) -- this is necessary to prevent
	@ double-fixups of the GOT table, thanks segher
	ldr r0, [r1]
	mov r5, r4
	sub r5, r0
	
got_loop:
	@ check for the end
	cmp	r1, r2
	beq	done_got
	@ read the GOT entry
	ldr	r3, [r1]
	@ add our base address
	add	r3, r5
	str	r3, [r1]
	@ move on
	add	r1, r1, #4
	b	got_loop

done_got:
	@ clear BSS
	ldr	r1, =__bss_start
	add	r1, r4
	ldr	r2, =__bss_end
	add	r2, r4
	mov	r3, #0
bss_loop:
	@ check for the end
	cmp	r1, r2
	beq	done_bss
	@ clear the word and move on
	str	r3, [r1]
	add	r1, r1, #4
	b	bss_loop

done_bss:
	mov	r0, #0x44
	bl	debug_output
	@ take the plunge
	mov	r0, r7
	bl	_main
	@ _main returned! Go to whatever address it returned...
	mov	r1, r0
	mov	r0, r4
	mov	pc, r1

.pool
@ misc low-level funcs used by other parts of the code

debug_output:
	@ load address of port
	mov	r3, #0xd800000
	@ load old value
	ldr	r2, [r3, #0xe0]
	@ clear debug byte
	bic	r2, r2, #0xFF0000
	@ insert new value
	and	r0, r0, #0xFF
	orr	r2, r2, r0, LSL #16
	@ store back
	str	r2, [r3, #0xe0]
	bx lr

dc_flush:
	mrc		p15, 0, pc, c7, c10, 3
	bne		dc_flush
	bx		lr

irq_kill:
	mrs		r1, cpsr
	and		r0, r1, #0xc0
	orr		r1, r1, #0xc0
	msr		cpsr_c, r1
	bx		lr

disable_icache_dcache_mmu:
	mrc		p15, 0, r0, c1, c0
	bic		r0, r0, #0x1000
	bic		r0, r0, #0x5
	mcr		p15, 0, r0, c1, c0
	bx		lr

jump_to_r0:
	mov		pc, r0
