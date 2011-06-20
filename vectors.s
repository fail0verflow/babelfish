@ I believe Sven wrote these?  Thanks sven :)

	.ARM
	.text

swi_vector:

	stmfa sp!, {r0-r4, lr}

	@ check SWI number
	@ should work for ARM and THUMB (yay bigendian):
	@
	@ ARM:
	@  OOnnnnNN BBBBBBBB
	@      |--| ^--LR
    @      ^--halfword retreived, mask last 8 bits to get number

	@ THUMB:
	@  xxxx OONN BBBB CCCC
	@       |--| ^--LR
    @       ^--halfword retreived, mask last 8 bits to get number

	ldrh r3, [lr, #-2]
	and r3, r3, #0xFF
	cmp r3, #0xAB
	bne return

	@ check operation number (4=debug print)
	cmp r0, #4
	bne return

	@ gpio port
	ldr r3, =0x0d806814

loop:
	ldrb r2, [r1]
	bl send
	add r1, #1
	cmp r2, #0x00
	bne loop
	
@ optional code to insert linefeed at the end of each print, some IOS modules seem to need this
@ to get sane output
@	mov r2, #0xa
@	bl send

return:
	ldmfa sp!, {r0-r4, lr}
	movs pc, lr

@ send a string over USBGecko
send:
	mov r0, #0xd0
	str r0, [r3, #0x00]

	mov r0, #0xB0000000
	orr r0, r0, r2, LSL #20
	str r0, [r3, #0x10]

	mov r0, #0x19
	str r0, [r3, #0x0c]

sendloop:
	ldr r0, [r3, #0x0c]
	tst r0, #1
	bne sendloop

	ldr r0, [r3, #0x10]
	tst r0, #0x04000000

	mov r0, #0
	str r0, [r3, #0x00]
	beq send
	
	mov pc, lr

	.POOL
