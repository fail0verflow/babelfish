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
#include "elf.h"
#include "gecko.h"
#include "vectors_bin.h"
#define DEBUG 1


/* Known issues:
   MEMHOLE_ADDR was picked by looking at the ELF headers for a few versions of IOS
   some spot that wasn't used; it would be better to programmatically determine
   this while reloading IOS, and move it around as necessary.
   
   PPC patching doesn't work, see below.  Half of the code here is for PPC patching,
   it'd sure be nice if it worked (or we should get rid of it if we can't fix it)
   
   syscall numbering might change between IOS versions, need to check this
   
   Need to hook two different places for patching, depending on the IOS version --
   we should either find a better place, or automatically choose the correct one
   
   We freak out if we try to reload MINI.
   
   Big chunks of this code could be cleaned up and/or refactored, probably.
*/


/* CONFIG OPTIONS */
#define USE_RELOAD_IOS  /* as opposed to hooking xchange_osvers -- only works on newer versions of IOS? */
   
/* XXX this shouldn't be hardcoded, and if you change it it must also change in start.S and babelfish.ld! */
/* This is where we save a copy of ourselves to use when we reload into a new IOS */
#define MEMHOLE_ADDR 0x13A80000

/* We should be able to use this framework to patch PPC code, but I can't get it to work.
   Either it thinks it's patched code (but the PPC doesn't see the updated code), or it hangs the PPC.
   I spent a couple of weeks trying to get this to work, but failed -- 
   I'd really like it if someone fixed this. :)
*/
// #define PPCHAX
/* END CONFIG OPTIONS */

#ifdef PPCHAX // save space if we're not going to try to patch PPC code

/* This is a patch to __fwrite in the Nintendo SDK to redirect all output to USBGecko */
static u32 fwrite_patch[] = {
  0x7c8429d6, 0x39400000, 0x9421fff0, 0x93e1000c, 0x7f8a2000, 0x409c0064,
  0x3d00cd00, 0x3d60cd00, 0x3d20cd00, 0x61086814, 0x616b6824, 0x61296820,
  0x398000d0, 0x38c00019, 0x38e00000, 0x91880000, 0x7c0350ae, 0x5400a016,
  0x6400b000, 0x900b0000, 0x90c90000, 0x80090000, 0x701f0001, 0x4082fff8,
  0x800b0000, 0x90e80000, 0x540037fe, 0x7d4a0214, 0x7f8a2000, 0x419cffc8,
  0x7ca32b78, 0x83e1000c, 0x38210010, 0x4e800020
};

static u32 sig_fwrite[] = {
  0x9421FFD0,
  0x7C0802A6,
  0x90010034,
  0xBF210014,
  0x7C9B2378,
  0x7CDC3378,
  0x7C7A1B78,
  0x7CB92B78
};
#endif
// keep track of how many times we think we patched the code
static u32 fwrite_count = 0;

/* these are the syscalls we want to hook, either for debug spew or to actually change behavior */
// FIXME -- can these change between IOS versions?
#define SYSCALL_NEW_THREAD	0x0
#define SYSCALL_OPEN 		0x1c
#define SYSCALL_PPCBOOT 	0x41
#define SYSCALL_ARMBOOT 	0x42
#define SYSCALL_LOADELF		0x5a

#ifdef DEBUG
#define dprintf printf
#else	
void dprintf(char *fmt, ...) {}
#endif

/* these are defined in start.s */
extern void dc_flush(void);
extern s32 irq_kill(void);
extern void disable_icache_dcache_mmu(void);
extern void jump_to_r0(u32 *target);
extern void debug_output(u8 byte);

void do_kd_patch(u8 *buffer, u32 size);
void * find_stuff_EXI_stub(void);
u32 * find_ppcreset(void);
void find_powerpc_reset(void);
void replace_ios_loader(u32 *buffer);

extern u32 __got_start, __got_end, _start, __bss_end;

// we don't actually use this struct, but we probably should :(
typedef struct {
	u32 hdrsize;
	u32 loadersize;
	u32 elfsize;
	u32 argument;
} ioshdr;

/* these are the function pointers we will use to hook IOS syscalls -
   they will point at the real (old) IOS syscall handlers.  Make a new type
   for each syscall you hook */

typedef u32(*new_thread_func)(u32 (*proc)(void* arg), u8 priority, u32* stack, u32 stacksize, void* arg, u8 autostart);
new_thread_func new_thread = NULL;

typedef s32(*device_open_func)(char *, s32);
device_open_func device_open = NULL;

typedef s32(*ppcboot_func)(char *);
ppcboot_func ppcboot = NULL;

typedef s32(*loadelf_func)(char *);
loadelf_func loadelf = NULL;

typedef s32(*armboot_func)(char *, u32, u32);
armboot_func armboot = NULL;

typedef void(*reload_ios_func)(u32 *, u32);
reload_ios_func reload_ios = NULL;

typedef void(*stuff_EXI_stub_func)(u32, u32 *, u32);
stuff_EXI_stub_func stuff_EXI_stub = NULL;

typedef void(*powerpc_reset_func)(void);
powerpc_reset_func powerpc_reset = NULL;

/* wrapper functions for hooked syscalls */
u32 new_thread_wrapper(u32 (*proc)(void* arg), u8 priority, u32* stack, u32 stacksize, void* arg, u8 autostart) {
	s32 retval;
	char buf[128];

	/* gross hack -- on later modular IOSes, we can't patch KD when it's loaded
	   because only the kernel gets loaded by our ELF loader/patcher -- so instead,
	   we wait until the module is actually started to do our patch */
	if ((((u32)proc) >> 16) == 0x13db) do_kd_patch((u8 *)0x13db0000, 0x57000);
	retval = new_thread(proc, priority, stack, stacksize, arg, autostart);
	printf("new_thread(%x,%d,%x,%x,%x,%x)=%d\n", (u32)proc, priority, (u32)stack, stacksize, (u32)arg, autostart, retval);
	return retval;
}

s32 device_open_wrapper(char *filename, s32 mode) {
	s32 retval;
	char buf[128];
	/* This is really just for debug spew */
	retval = device_open(filename, mode);
	printf("open(%s,%d)=%d\n", filename, mode, retval);
	return retval;
}

/* it would be nice to figure out how to do patching of the loaded PPC content here */
s32 ppcboot_wrapper(char *filename) {
	s32 retval;
	
	printf("ppcboot(%s)\n",filename);
	
	retval = ppcboot(filename);

	printf("ppcboot returned %d fwrite_count=%d\n", retval,fwrite_count);
	return retval;
}

/* This is called by modular IOS's kernel to load each module from NAND -- could probably
   do patching here instead of in new_thread, but you have to figure out whether or not
   this is the right module to patch somehow */
s32 loadelf_wrapper(char *filename) {
	s32 retval;
	
	printf("loadelf(%s)\n", filename);
	
	retval = loadelf(filename);

	printf("loadelf returned %d\n", retval);
	return retval;
}

// mostly just for logging at this point, but maybe would be better to do patches here?
s32 armboot_wrapper(char *filename, u32 r1, u32 filesize) {
	s32 retval;
	
	printf("armboot(%s, %x, %x)\n", filename, r1, filesize);

	retval = armboot(filename, r1, filesize);
	printf("armboot returned %d\n", retval);

	return retval;
}


/* here is where the magic happens -- note that this is really just reimplementing most of reload_ios
   from IOS, because there was no easy way to just hook it */

/* option 1 */
/* here is where the magic happens -- note that this is really just reimplementing most of reload_ios
   from IOS, because there was no easy way to just hook it.  This option is better than option 2,
   because we don't need to hardcode the value of buffer; IIRC, this code is not present on early versions of IOS. */
#ifdef USE_RELOAD_IOS
void reload_ios_wrapper(u32 *buffer, u32 version) {
	printf("reload_ios(%x, %x)\n",(u32)buffer, version);

	irq_kill();
	set32(HW_ARMIRQMASK, 0);
	
    replace_ios_loader(buffer);
	
	printf("Here goes nothing...\n");
/* 	magic pokes from reload_ios() */
	dc_flush();
	disable_icache_dcache_mmu();
	write32(0x3118, 0x04000000);
	write32(0x311C, 0x04000000);
	write32(0x3120, 0x93400000);
	write32(0x3124, 0x90000800);
	write32(0x3128, 0x933E0000);
	write32(0x3130, 0x933E0000);
	write32(0x3134, 0x93400000);
	write32(0x3140, version);
	write32(0x3148, 0x93400000);
	write32(0x314C, 0x93420000);
	write32(0x0d010018, 1);
	jump_to_r0(buffer);
	printf("wtf am I here\n"); // should not be reached
	panic(0x14);
}
#else // !USE_RELOAD_IOS
/* option 2 */
/* we really need to figure out how to find the buffer we need at runtime if we intend on using this */
u32 xchange_osvers_and_patch(u32 version) {
	u32 *buffer = (u32 *)0x10100000; // safe to hardcode?  NO -- 
	printf("xchange_osvers_and_patch(%x)\n", version);

	u32 oldvers = read32(0x3140);
	if (version && version != oldvers) {
		printf("! Set OS version to %u !\n", version);
		write32(0x3140, version);
//		dc_flush();
	}
	
    replace_ios_loader(buffer);
	
	printf("! Returning old OS version %x !\n", oldvers);
//	jump_to_r0(buffer);    // I don't remember why I tried this, but this would immediately execute the new IOS
	return oldvers;
}
#endif

void replace_ios_loader(u32 *buffer) {
    printf("new hdr length = %x\n",buffer[0]);
    printf("new ELF offset = %x\n",buffer[1]);
    printf("new ELF length = %x\n",buffer[2]);
    printf("new param = %x\n",     buffer[3]);

    u32 *me = (u32 *)MEMHOLE_ADDR;

    printf("our hdr length = %x\n",me[0]);
	printf("our ELF offset = %x\n",me[1]);
	printf("our ELF length = %x\n",me[2]);
	printf("our param = %x\n",     me[3]);

    /* problem:  when IOS is loaded from NAND, it's 3 parts: IOS header, ELF loader, ELF
                 we want to overwrite the ELF loader with our own code, but it's probably bigger
                 than the existing IOS ELF loader
       solution: move the ELF file over in memory and modify the IOS header to give ourselves enough
                 space to copy in babelfish as the new ELF loader */	

    // buffer[1] = ELF loader size -- we use this to detect if we already patched ourselves into this or whatever
	if (buffer[1] > me[1]) { // should never happen, but can happen with e.g. MINI
		printf("wtf, their loader is bigger than ours :(\n");
		panic(0x40);
	}
	if (buffer[1] == me[1]) {
		printf("already using our loader, nothing to hax\n");
	} else { // buffer[1] < me[1]
		u32 *old_elf_start = buffer + (buffer[1] + buffer[0])/4;
		u32 *new_elf_start = buffer + (me[1] + me[0])/4;
		printf("moving %x bytes from %x to %x\n", buffer[2], (u32)old_elf_start, (u32)new_elf_start);

		u32 i;
		// copy in backwards order because these buffers overlap
		memcpyr(new_elf_start, old_elf_start, buffer[2]);

		printf("move complete\n");
		printf("copying in loader from %x to %x\n",(u32)&me[4],(u32)&buffer[4]);

		memcpyr(&buffer[4], &me[4], me[1]);
		printf("done\n");

		// copy over ELF offset and size
		buffer[1] = me[1];
		buffer[2] = me[2];
		printf("pwnt\n");
	} 
}

#ifdef PPCHAX
/* This code is a mess because I was flailing around trying to get PPC patching
  to work, sorry ... this code *doesn't* work. */
void stuff_EXI_stub_wrapper(u32 which_stub, u32 *insns, u32 len) {
	printf("stuff_EXI_stub(%x, %x, %x)\n", which_stub, (u32) insns, len);
	printf("Looking for fwrite\n");
	u32 addr;
	u32 *mem1 = (u32 *)0x1330000;
	printf("81330000: %08x %08x %08x %08x\n", mem1[0], mem1[1], mem1[2], mem1[3]);
	for (addr = 0; addr < 0x1800000; addr += 4) {
		if (!memcmp((void *)addr, sig_fwrite, sizeof sig_fwrite)) {
			printf("found fwrite at %x, patching\n", addr);
			u32 *ptr = (u32 *)addr;
			int i;
			for (i = 0; i < 16; i+=4)
				printf("%08x: %08x %08x %08x %08x\n",
					((u32)addr) + i*4, ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
//			memcpyr((void *)addr, fwrite_patch, sizeof fwrite_patch);
//			break;
		}
	}
	stuff_EXI_stub(which_stub, insns, len);
	printf("stuff_EXI_stub done\n");
}

void powerpc_reset_wrapper(void) {
//	printf("powerpc_reset()\n");
//		printf("Looking for fwrite\n");
		u32 addr;
		u32 *mem1 = (u32 *)0x1330000;
//		printf("81330000: %08x %08x %08x %08x\n", mem1[0], mem1[1], mem1[2], mem1[3]);
//	powerpc_reset();
			for (addr = 0; addr < 0x1800000; addr += 4) {
				if (!memcmp((void *)addr, sig_fwrite, sizeof sig_fwrite)) {
					fwrite_count++;
//					printf("found fwrite at %x, patching\n", addr);
					u32 *ptr = (u32 *)addr;
					int i;
	//				for (i = 0; i < 16; i+=4)
	//					printf("%08x: %08x %08x %08x %08x\n",
	//						((u32)addr) + i*4, ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
//					memcpyr((void *)addr, fwrite_patch, sizeof fwrite_patch);
					for (i=0; i < sizeof(fwrite_patch)/4; i++) ptr[i] = fwrite_patch[i];
					break;
				}
			}
//			dc_flushall();
		 u32* hw_ppcirqmask = (u32*)0x0D800034;
		  u32* hw_reset = (u32*)0x0D800194;
		  u32* hw_timer = (u32*)0x0D800010;
		  u32 time_next;

		  *hw_ppcirqmask = 0x40000000;
		  *hw_reset &= ~0x30;
		  // sleep. this isn't exactly right...
		  for (time_next = *hw_timer+0xF; time_next < *hw_timer;);
		  *hw_reset |= 0x20;
		  // sleep
		  for (time_next = *hw_timer+0x96; time_next < *hw_timer;);
		  *hw_reset |= 0x10;
//	printf("powerpc_reset done\n");
}
#endif

#ifdef USE_RELOAD_IOS
/* look for the reload_ios function in a newly-loaded IOS kernel */
u32 * find_reload_ios(void) {
//	these numbers are pretty much guaranteed to exist -- see above implementation of reload_ios
	u32 magic[] = {0x93400000, 0x93420000};
	u32 *kernel = (u32 *)0xFFFF0000;
	int i;
	printf("Looking for reload_ios\n");
	for(i = 0; i < 0x10000 / 4; i++) {
		if (kernel[i] == magic[0] && kernel[i+1] == magic[1]) {
			printf("Found reload_ios marker at %x\n", 0xFFFF0000 + i * 4);
			for (; i >=0; i--) {
				if ((kernel[i] >> 16) == 0xB570) {
					printf("Found function prolog at %x\n", 0xFFFF0000 + i * 4);
					return (u32 *)(&kernel[i]);
				}
			}
			printf("sry, i haz fail\n");  // could not find reload_ios, so we're screwed
			return NULL;
		}
	}
}
#else // !USE_RELOAD_IOS
/* look for the xchange_os_version function in a newly-loaded IOS kernel */
// kernel:FFFF5A24 B5 30                       PUSH    {R4,R5,LR}
// kernel:FFFF5A26 23 C4 01 9B                 MOVS    R3, 0x3100
// kernel:FFFF5A2A 6C 1D                       LDR     R5, [R3,#0x40]

u32 * find_xchange_osvers(void) {
	u32 magic[] = {0xB53023C4};
	u32 *kernel = (u32 *)0xFFFF0000;
	int i;
	printf("Looking for xchange_osvers\n");
	for(i = 0; i < 0x10000 / 4; i++) {
		if (kernel[i] == magic[0]) {
			printf("Found xchange_osvers start at %x\n",0xFFFF0000 + i * 4);
			return (u32 *)(0xFFFF0000 + i*4);
		}
	}
	printf("sry, I haz fail! :(\n");
	return NULL;
}
#endif

// this is where we patch teh kernel to add our hooks
void do_kernel_patches(u32 size) {
	printf("do_kernel_patches(%x)\n",size);

// this is just to make life easier
	u32 *kernel_mem32 = (u32 *)0xFFFF0000;
	u16 *kernel_mem16 = (u16 *)0xFFFF0000;

// sanity check of the syscall vector table
	if (kernel_mem32[1] != 0xe59ff018) {
		printf("ohnoes, unexpected offset to starlet_syscall_handler %x\n",kernel_mem32[1]);
		return;
	}
	if (kernel_mem32[2] != 0xe59ff018) {
		printf("ohnoes, unexpected offset to arm_syscall_handler %x\n",kernel_mem32[2]);
		return;
	}
	printf("starlet_syscall_handler vector: %x\n",kernel_mem32[9]);
	printf("svc_handler vector2: %x\n",kernel_mem32[10]);
	
/* SVC patch to get debug output over USBGecko -- we copy the code from vectors.s over some unused
   code present in all IOSes to make life easier -- SVC 4 is the only one used, but they include
   functions to call over SVC handlers which we can just blow away. thanks ninty */

/* scan from 0xFFFF0000 ... 0xFFFFFFFF looking for SVC 05 instruction */
	u32 i;
	for (i=0; i < 0x10000/2; i++) {
		if (kernel_mem16[i+0] == 0x4672 &&
		    kernel_mem16[i+1] == 0x1c01 &&
			kernel_mem16[i+2] == 0x2005) {
			dprintf("SVC 5 caller found at %0x\n",0xffff0000 + i*2);

			// copy in SVC vector code
			memcpyr(&kernel_mem16[i], vectors_bin, vectors_bin_size);
			// change SVC vector pointer to point to this new code
			kernel_mem32[10] = (u32) &kernel_mem16[i];
			dprintf("patch done\n");
		}
		// while we're here, look for the mem2 protection code and disable it
		if (kernel_mem16[i+0] == 0xB500 && 
		    kernel_mem16[i+1] == 0x4B09 &&
		    kernel_mem16[i+2] == 0x2201 &&
		    kernel_mem16[i+3] == 0x801A &&
		    kernel_mem16[i+4] == 0x22F0) {
			dprintf("Found MEM2 patch at %x\n",0xffff0000);
			kernel_mem16[i+2] = 0x2200;
		}
	}

#ifdef USE_RELOAD_IOS
/* patch reload_ios so that we can infect any IOS we reload */
	u32 *addr = find_reload_ios();
	if (addr) { // overwrite reload_ios with a jump to our wrapper
		addr[0] = 0x4B004718; // ldr r3, $+4 / bx r3
		addr[1] = (u32)reload_ios_wrapper;
	}
#else // !USE_RELOAD_IOS	
/* patch xchange_osvers so that we can infect any IOS we reload */
	u32 *addr = find_xchange_osvers();
	if (addr) { // overwrite xchange_osvers with a jump to our wrapper
		addr[0] = 0x4B004718; // ldr r3, $+4 / bx r3
		addr[1] = (u32)xchange_osvers_and_patch;
//		dprintf("wrote %08x %08x to %08x\n", addr[0], addr[1], (u32)addr);
	}
#endif

#ifdef PPCHAX
    find_powerpc_reset();
    addr = find_ppcreset();
    printf("ppcreset = %x\n", (u32)addr);
    if (addr) { // overwrite ppcreboot with a jump to our wrapper
        addr[0] = 0x4B004718;
        addr[1] = (u32)powerpc_reset_wrapper;
    }
#endif
	dprintf("\ndo_kernel_patches done\n");
}

/* perform patching of syscall table to install our hooks */
void handle_syscall_table(u32 *syscall_table, u32 size) {
	u32 i, num_syscalls;
	for (i = 0; i < size/4; i++) if ((syscall_table[i] >> 16) == 0) break;
	num_syscalls = i;
	printf("Syscall table @ 0x%x: %x entries\n",(u32) syscall_table, num_syscalls);

	/* We assume a specifc syscall ordering here, oops.   grab the existing function pointers */
	device_open = (void *)syscall_table[SYSCALL_OPEN];
	new_thread =  (void *)syscall_table[SYSCALL_NEW_THREAD];
	ppcboot =     (void *)syscall_table[SYSCALL_PPCBOOT];
	armboot =     (void *)syscall_table[SYSCALL_ARMBOOT];
	loadelf =     (void *)syscall_table[SYSCALL_LOADELF];
	
	/* modify the table to point to our wrapper functions */
	syscall_table[SYSCALL_OPEN] =       (u32)device_open_wrapper;
	syscall_table[SYSCALL_NEW_THREAD] = (u32)new_thread_wrapper;
	syscall_table[SYSCALL_PPCBOOT] =    (u32)ppcboot_wrapper;
	syscall_table[SYSCALL_ARMBOOT] =    (u32)armboot_wrapper;
	syscall_table[SYSCALL_LOADELF] =    (u32)loadelf_wrapper;

	printf("\nnew device_open: %x\n", syscall_table[SYSCALL_OPEN]); // this is just to give me the warm fuzzies

	printf("\nNew syscall table:\n");
	for(i=0; i < num_syscalls; i++) {
		printf("%x: %x\n", i, syscall_table[i]);
	}
}	

#ifdef PPCHAX
void * find_stuff_EXI_stub(void) {
	u32 magic[] = {0x7C631A78, 0x6463D7B0};
	u32 *kernel = (u32 *)0xFFFF0000;
	u32 ppc_stub1_addr = 0, stuff_EXI_stub_addr = 0;
	int i;
	printf("Looking for ppc_stub1\n\n");
	for(i = 0; i < 0x10000 / 4; i++) {
		if (kernel[i] == magic[0] && kernel[i+1] == magic[1]) {
			printf("Found ppc_stub1 at %x\n",0xFFFF0000 + i * 4);
			break;
		}
	}
	if (i == 0x10000/4) {
		printf("Couldn't find ppc_stub1\n");
		return NULL;
	}
	ppc_stub1_addr = 0xFFFF0000 + i*4;
	for(i = 0; i < 0x10000 / 4; i++) {
		if (kernel[i] == ppc_stub1_addr) {
			printf("Found ppc_stub1 reference at %x\n",0xFFFF0000 + i * 4);
			break;
		}
	}
	if (i == 0x10000/4) {
		printf("Couldn't find ppc_stub ref\n");
		return NULL;
	}
	for(; i > 0; i--) {
		if (kernel[i] == 0xB5001C03) {
			printf("Found stuff_EXI_stub start at %x\n", 0xFFFF0000 + i * 4);
			break;
		}
	}
	if (i == 0x10000) {
		printf("Couldn't find stuff_EXI_stub start\n");
		return NULL;
	}
	stuff_EXI_stub = (void *)(0xFFFF0000 + i * 4 + 1); // thumb offset
	for(i = 0; i < 0x10000 / 4; i++) {
		if (kernel[i] == (u32)stuff_EXI_stub) {
			printf("Found stuff_EXI_stub_addr reference at %x\n", 0xFFFF0000 + i * 4);
			break;
		}
	}
	if (i == 0x10000) {
		printf("Couldn't find stuff_EXI_stub reference\n");
		return NULL;
	}
	kernel[i] = (u32)&stuff_EXI_stub_wrapper;
	printf("Replaced stuff_EXI_stub reference with %x\n", kernel[i]);
	return NULL;
}

void find_powerpc_reset(void) {
	u32 magic[] = {0x0d800034, 0x0d800194};
	u32 *kernel = (u32 *)0xFFFF0000;
	u32 ppc_stub1_addr = 0, stuff_EXI_stub_addr = 0;
	int i;
	printf("Looking for powerpc_reset magic\n\n");
	for(i = 0; i < 0x10000 / 4; i++) {
		if (kernel[i] == magic[0] && kernel[i+1] == magic[1]) {
			printf("Found powerpc_reset magic at %x\n",0xFFFF0000 + i * 4);
			break;
		}
	}
	if (i == 0x10000/4) {
		printf("Couldn't find powerpc_reset magic\n");
		return;
	}
	for(; i > 0; i--) {
		if (kernel[i] == 0xB5704646) {
			printf("Found powerpc_reset start at %x\n", 0xFFFF0000 + i * 4);
			break;
		}
	}
	if (i == 0x10000) {
		printf("Couldn't find powerpc_reset start\n");
		return;
	}
	powerpc_reset = (void *)(0xFFFF0000 + i*4 + 1);
	for(i = 0; i < 0x10000 / 4; i++) {
		if (kernel[i] == (u32)powerpc_reset) {
			printf("Found powerpc_reset reference at %x\n", 0xFFFF0000 + i * 4);
			break;
		}
	}
	if (i == 0x10000) {
		printf("Couldn't find powerpc_reset reference\n");
		return;
	}
//		kernel[i] = (u32)&powerpc_reset_wrapper;
	printf("Replaced powerpc_reset reference with %x\n", kernel[i]);
}

u32 * find_ppcreset(void) {
  u32 magic[] = {0x0d800034, 0x0d800194};
  u32 *kernel = (u32*)0xFFFF0000;
  int i;
  for(i=0; i < 0x10000 / 4; i++) {
    if (kernel[i] == magic[0] && kernel[i+1] == magic[1]) {
      for (; i >=0; i--) {
        if ((kernel[i] >> 16) == 0xB570) // push {R4-R6,LR}, prolog
          return kernel+i;
      }
      return NULL;
    }
  }
  return NULL;
}
#endif
	
/* this gross patch is necessary to get WC24 debug spew */
void do_kd_patch(u8 *buffer, u32 size) {
	int i;
//  I don't remember what this first patch was, sorry
//	u8 match[] = {0x13, 0xdf, 0x5f, 0xcd, 0x13, 0xdf, 0x5f, 0xa9};
//	u8 replace[] = {0x13, 0xdf, 0x5f, 0x45, 0x13, 0xdf, 0x5f, 0x29};
	u8 match[] = {0x30, 0x01, 0x28, 0x7f, 0xd9, 0x01, 0x23, 0x00};
	u8 replace[] = {0x49, 0x16, 0x20, 0x04, 0xdf, 0xab, 0x23, 0x00};
	
	dprintf("Looking for can_haz_debug(%x, %x)\n",(u32)buffer, size);
	for(i=0; i < (size-16); i++) {
		if (!memcmp(match, buffer+i, 8)) {
			dprintf("Found match @ %x\n",(u32)&buffer[i]);
			memcpyr(buffer + i, replace, 8);
			return;
		}
	}
}

/* ye olde ELF loader, written in C for great justice
   also patches too */
void *_loadelf(const u8 *elf) {
	if(memcmp("\x7F" "ELF\x01\x02\x01",elf,7)) {
		panic(0xE3);
	}
	printf("ELF magic ok\n");
	
	Elf32_Ehdr *ehdr = (Elf32_Ehdr*)elf;
	if(ehdr->e_phoff == 0) {
		panic(0xE4);
	}
	dprintf("e_phoff=%x\n",ehdr->e_phoff);
	
	int count = ehdr->e_phnum;
	Elf32_Phdr *phdr = (Elf32_Phdr*)(elf + ehdr->e_phoff);
	while(count--)
	{
		dprintf("count=%x   phdr type=%x paddr=%x offset=%x filesz=%x\n",
			count, (u32)phdr->p_type, (u32)phdr->p_paddr, phdr->p_offset, (u32)phdr->p_filesz);
		
		if(phdr->p_type == PT_LOAD && phdr->p_filesz != 0) {
			const void *src = elf + phdr->p_offset;
			memcpyr(phdr->p_paddr, src, phdr->p_filesz); // could be memcpy, but trying to save code space
			if (phdr->p_paddr == (u32 *)0xFFFF0000) do_kernel_patches(phdr->p_filesz);
			if (count == 1) handle_syscall_table(phdr->p_paddr, phdr->p_filesz); // assumes syscall table will always be last phdr
//	this patch needs to be done when the kernel loads the module, not when loading the kernel
			do_kd_patch(phdr->p_paddr, phdr->p_filesz);
		}
		phdr++;
	}
	dprintf("done, entrypt = %x\n", (u32)ehdr->e_entry);

#ifdef PPCHAX
	find_stuff_EXI_stub();
#endif
	
	return ehdr->e_entry;
}

static inline void disable_boot0(void)
{
	set32(HW_BOOT0, 0x1000);
}

static inline void mem_setswap(void)
{
	set32(HW_MEMMIRR, 0x20);
}

void *_main(void *base)
{
	ioshdr *hdr = (ioshdr*)base;
	u8 *elf;
	void *entry;
	
	elf = (u8*) base;
	elf += hdr->hdrsize + hdr->loadersize;
	
	debug_output(0xF1);
	mem_setswap();
	disable_boot0();
	gecko_init();
	
	printf("elfloader elf=%x\n",(u32)elf);
	entry = _loadelf(elf);
	printf("loadelf done\n");
	debug_output(0xC1);
	return entry;

}
