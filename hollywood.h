/*
	babelfish - self-propagating Just-In-Time IOS patcher

Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2008-2011		Haxx Enterprises <bushing@gmail.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

This code lives at http://gitweb.bootmii.org/?p=babelfish.git
*/
#ifndef __HOLLYWOOD_H__
#define __HOLLYWOOD_H__

#define		HW_REG_BASE		0xd800000
#define		HW_TIMER		(HW_REG_BASE + 0x010)
#define		HW_ARMIRQMASK		(HW_REG_BASE + 0x03c)

#define		HW_MEMMIRR		(HW_REG_BASE + 0x060)
#define		HW_BOOT0		(HW_REG_BASE + 0x18c)

#define		HW_EXICTRL		(HW_REG_BASE + 0x070)
#define		EXICTRL_ENABLE_EXI	1

#define		HW_VERSION		(HW_REG_BASE + 0x214)
#define		MEM_REG_BASE		0xd8b4000

#define		MEM_FLUSHREQ		(MEM_REG_BASE + 0x228)
#define		MEM_FLUSHACK		(MEM_REG_BASE + 0x22a)

#define		EXI_REG_BASE		0xd806800
#define		EXI0_REG_BASE		(EXI_REG_BASE + 0x000)
#define		EXI2_REG_BASE		(EXI_REG_BASE + 0x028)

#define		EXI0_CSR		(EXI0_REG_BASE + 0x000)
#define		EXI2_CSR		(EXI2_REG_BASE + 0x000)

#define		EXI1_REG_BASE		(EXI_REG_BASE + 0x014)
#define		EXI1_CSR		(EXI1_REG_BASE + 0x000)
#define		EXI1_MAR		(EXI1_REG_BASE + 0x004)
#define		EXI1_LENGTH		(EXI1_REG_BASE + 0x008)
#define		EXI1_CR			(EXI1_REG_BASE + 0x00c)
#define		EXI1_DATA		(EXI1_REG_BASE + 0x010)
#endif

