/*
babelfish - self-propagating Just-In-Time IOS patcher

Copyright (C) 2008-2011		Haxx Enterprises <bushing@gmail.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

This code lives at http://gitweb.bootmii.org/?p=babelfish.git
*/

#ifndef __GECKO_H__
#define __GECKO_H__

#include "types.h"

int gecko_isalive(void);
int gecko_putc(int c);
int gecko_puts(const char *s);
void gecko_init(void);
#endif
