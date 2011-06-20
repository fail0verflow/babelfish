#ifndef __UTILS_H__
#define __UTILS_H__

static inline u32 read32(u32 addr)
{
	u32 data;
	__asm__ volatile ("ldr\t%0, %1" : "=l" (data) : "m"(*(u32 *)addr));
	return data;
}

static inline void write32(u32 addr, u32 data)
{
  __asm__ volatile ("str\t%1, %0" : "=m"(*(u32 *)addr) : "l"(data));
}

static inline u32 set32(u32 addr, u32 set)
{
	u32 data;
	__asm__ volatile (
		"ldr\t%0, [%1]\n"
		"\torr\t%0, %2\n"
		"\tstr\t%0, [%1]"
		: "=&r" (data)
		: "r" (addr), "r" (set)
	);
	return data;
}

static inline u32 clear32(u32 addr, u32 clear)
{
	u32 data;
	__asm__ volatile (
		"ldr\t%0, [%1]\n"
		"\tbic\t%0, %2\n"
		"\tstr\t%0, [%1]"
		: "=&r" (data)
		: "r" (addr), "r" (clear)
	);
	return data;
}


static inline u32 mask32(u32 addr, u32 clear, u32 set)
{
	u32 data;
	__asm__ volatile (
		"ldr\t%0, [%1]\n"
		"\tbic\t%0, %3\n"
		"\torr\t%0, %2\n"
		"\tstr\t%0, [%1]"
		: "=&r" (data)
		: "r" (addr), "r" (set), "r" (clear)
	);
	return data;
}

static inline u16 read16(u32 addr)
{
        u32 data;
        __asm__ volatile ("ldrh\t%0, [%1]" : "=l" (data) : "l" (addr));
        return data;
}

static inline void write16(u32 addr, u16 data)
{
        __asm__ volatile ("strh\t%0, [%1]" : : "l" (data), "l" (addr));
}

void panic(u8 v);
size_t strlen(const char *);
void *memset(void *, int, size_t);
//void *memcpy(void *, const void *, size_t);
void *memcpyr(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
s32 printf (const char* format,	...);
int puts(const char *);
#endif

