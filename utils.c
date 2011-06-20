/* babelfish -- misc (C) utility functions */

#include "types.h"
#include "utils.h"
#include "hollywood.h"
#include <stdarg.h>

extern void debug_output(u8 byte);  // from start.S

static inline void delay(u32 d)
{
	write32(HW_TIMER, 0);
	while(read32(HW_TIMER) < d);
}

void panic(u8 v)
{
	while(1) {
		debug_output(v);
		delay(1000000);
		debug_output(0);
		delay(1000000);
	}
}

/*
void *memcpy(void *dst, const void *src, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		((unsigned char *)dst)[i] = ((unsigned char *)src)[i];

	return dst;
}
*/
void *memcpyr(void *dst, const void *src, size_t len)
{
	size_t i;

	for (i = len; i > 0; i--)
		((unsigned char *)dst)[i-1] = ((unsigned char *)src)[i-1];

	return dst;
}

int memcmp(const void *s1, const void *s2, size_t len)
{
	size_t i;
	const unsigned char * p1 = (const unsigned char *) s1;
	const unsigned char * p2 = (const unsigned char *) s2;

	for (i = 0; i < len; i++)
		if (p1[i] != p2[i]) return p1[i] - p2[i];
	
	return 0;
}

/*size_t strlcat(char *dest, const char *src, size_t maxlen)
{
        size_t len;
	maxlen--;
	for (len = 0; len < maxlen; len++) if (!dest[len]) break;
	for (; len < maxlen && *src; len++) dest[len] = *src++;
	dest[len] = '\0';
	return len;
}
*/
s32 printf (const char* str, ...) {
	va_list arp;
	u8 c, f, r;
	u32 val, pos;
	char s[16];
	s32 i, w, res, cc;

	va_start(arp, str);

	for (cc = pos = 0;; ) {
		c = *str++;
		if (c == 0) break;			/* End of string */
		if (c != '%') {				/* Non escape cahracter */
			gecko_putc(c);
			pos++;
			continue;
		}
		w = f = 0;
		c = *str++;
		if (c == '0') {				/* Flag: '0' padding */
			f = 1; c = *str++;
		}
		while (c >= '0' && c <= '9') {	/* Precision */
			w = w * 10 + (c - '0');
			c = *str++;
		}
		if (c == 's') {				/* Type is string */
			char *param = va_arg(arp, char*);
			for (i=0; param[i]; i++) {
				gecko_putc(param[i]);
				pos++;
			}
			continue;
		}
		r = 0;
		if (c == 'd') r = 10;		/* Type is signed decimal */
		if (c == 'u') r = 10;		/* Type is unsigned decimal */
		if (c == 'X' || c == 'x') r = 16; /* Type is unsigned hexdecimal */
		if (r == 0) {
			break;			/* Unknown type */
		}
		val = (c == 'd') ? (u32)(long)va_arg(arp, int) : 
				(u32)va_arg(arp, unsigned int);
		/* Put numeral string */
		if (c == 'd') {
			if (val & 0x80000000) {
				val = 0 - val;
				f |= 4;
			}
		}
//		if ((maxlen - pos) <= sizeof(s)) continue;
 		i = sizeof(s) - 1; s[i] = 0;
		do {
//			c = (u8)(val % r + '0');
			c = (u8)((val & 15) + '0');
			if (c > '9') c += 7;
			s[--i] = c;
//			val /= r;
			val >>= 4;
		} while (i && val);
		if (i && (f & 4)) s[--i] = '-';
		w = sizeof(s) - 1 - w;
		while (i && i > w) s[--i] = (f & 1) ? '0' : ' ';
		for (; s[i] ; i++) {
			gecko_putc(s[i]);
			pos++;
		}
	}
	va_end(arp);
//	gecko_puts(output);
	return pos;
}

int puts(const char *s) {
	gecko_puts(s);
	return 0;
}
