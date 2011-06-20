/*
babelfish - self-propagating Just-In-Time IOS patcher

Copyright (C) 2008-2011		Haxx Enterprises <bushing@gmail.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

This code lives at http://gitweb.bootmii.org/?p=babelfish.git
*/

#include "types.h"
#include "utils.h"
#include "hollywood.h"
#include "gecko.h"

/* some of this code is redundant with that in vectors.s */
static int gecko_active = 0;

static u32 _gecko_command(u32 command)
{
	u32 i;
	// Memory Card Port B (Channel 1, Device 0, Frequency 3 (32Mhz Clock))
	write32(EXI1_CSR, 0xd0);
	write32(EXI1_DATA, command);
	write32(EXI1_CR, 0x19);
	while (read32(EXI1_CR) & 1);
	i = read32(EXI1_DATA);
	write32(EXI1_CSR, 0);
	return i;
}

static u32 _gecko_sendbyte(char sendbyte)
{
	u32 i = 0;
	i = _gecko_command(0xB0000000 | (sendbyte<<20));
	if (i&0x04000000)
		return 1; // Return 1 if byte was sent
	return 0;
}

static u32 _gecko_checksend(void)
{
	u32 i = 0;
	i = _gecko_command(0xC0000000);
	if (i&0x04000000)
		return 1; // Return 1 if safe to send
	return 0;
}

int gecko_isalive(void)
{
	u32 i;

	i = _gecko_command(0x90000000);
	if ((i&0xFFFF0000) != 0x04700000)
		return 0;
	return 1;
}

void gecko_init(void)
{
	write32(EXI0_CSR, 0);
	write32(EXI1_CSR, 0);
	write32(EXI2_CSR, 0);
	write32(EXI0_CSR, 0x2000);
	write32(EXI0_CSR, 3<<10);
	write32(EXI1_CSR, 3<<10);

	gecko_active = 0;
	if(gecko_isalive())
		gecko_active = 1;
}

int gecko_putc(int c) {
	if (!gecko_active) return -1;
	int tries = 10000; // about 200ms of tries; if we fail, fail gracefully instead of just hanging
	// this makes CCC demos less embarassing

	if((read32(HW_EXICTRL) & EXICTRL_ENABLE_EXI) == 0)
		return 0;

	if(_gecko_checksend()) {
		if(!_gecko_sendbyte(c))
			return 0;
	} else {
		// if gecko is hung, time out and disable further attempts
		// only affects gecko users without an active terminal
		if(tries-- == 0) {
			gecko_active = 0;
			return 0;
		}
	}
	return 1;
}

int gecko_puts(const char *s) {
	int bytes_written = 0;

	while(*s) {
		if (!gecko_putc(*s++)) break;
		bytes_written++;
	}
	return bytes_written;
}
