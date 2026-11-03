// SYS_LINUX.H
//
// What the DJGPP source needs to build as a Linux (SDL2) program. The
// game keeps driving "the hardware": sys_linux.c stands in for it. The
// timer interrupt (IRQ 0) is called from a thread at the rate the game
// programs into the PIT, the keyboard interrupt (IRQ 9) is called with
// the scan codes of SDL's key events, INT 33h answers from SDL's mouse,
// the PC speaker is a square wave, and the "Gravis UltraSound" (an OPL2
// in software plus the digitized sounds, id_gus.c's interface) is SDL's
// audio. The game draws into main memory, as in the widescreen mode, and
// SDL shows the page.
//
// The game's code was written for 32 bit longs (and DJGPP's long is 32
// bits too). On 64 bit Linux it's 64 bits, so after all the system
// headers are in, long is made an int for the game's files (sys_linux.c
// itself, which talks to SDL, keeps the real one).

#ifndef __SYS_LINUX_H__
#define __SYS_LINUX_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#define far
#define near
#define huge
#define _seg

#define _fmemcpy	memcpy
#define _fmemset	memset
#define _fmemcmp	memcmp
#define _fstrcpy	strcpy
#define _fstrcat	strcat
#define _fstrlen	strlen
#define _fstrcmp	strcmp
#define _fstricmp	strcasecmp
#define _fstrnicmp	strncasecmp
#define stricmp		strcasecmp
#define strnicmp	strncasecmp

#define farmalloc	malloc
#define farfree		free

#ifndef O_BINARY
#define O_BINARY	0
#endif
#ifndef O_TEXT
#define O_TEXT		0
#endif
#define S_IREAD		S_IRUSR
#define S_IWRITE	S_IWUSR

// the command line (main() in sys_linux.c sets them)
extern int _argc;
extern char **_argv;

// The first megabyte, as DOS had it: the BIOS data area (the timer tick
// count at 46Ch is kept up by the timer), the VGA window and the text
// screen are there for the code that still looks at them
extern unsigned char SYS_lowmem[];
#define _dos_ds		0
#define VGAMEM		(SYS_lowmem + 0xa0000)
#define VGAPTR(ofs)	(VGAMEM + ((unsigned)(ofs) & 0xffff))

unsigned char	_farpeekb (int sel, uint32_t addr);
unsigned short	_farpeekw (int sel, uint32_t addr);
uint32_t		_farpeekl (int sel, uint32_t addr);
void	_farpokeb (int sel, uint32_t addr, unsigned char v);
void	_farpokew (int sel, uint32_t addr, unsigned short v);
void	_farpokel (int sel, uint32_t addr, uint32_t v);
void	dosmemput (const void *src, int len, uint32_t addr);
void	dosmemget (uint32_t addr, int len, void *dest);

// I/O ports: the PIT, the PC speaker, the keyboard controller and the
// VGA status register mean something; the rest read 0 (or all ones)
unsigned char	inportb (int port);
unsigned short	inportw (int port);
void	outportb (int port, unsigned char v);
void	outportw (int port, unsigned short v);
void	outportsb (int port, const unsigned char *buf, int n);

// Interrupts off = the timer thread kept out (it runs the "ISR")
int		disable (void);
int		enable (void);
#define DISABLE()	int __intson = disable()
#define RESTORE()	do { if (__intson) enable(); } while (0)

#define PIT_HZ		1192030

#define gotoxy(x,y)	((void)0)
#define clrscr()	((void)0)

// The real mode interrupts the game calls: INT 33h (mouse), INT 10h
// (only the VGA check is answered), INT 21h (the time)
typedef union
{
	struct { uint32_t edi,esi,ebp,res,ebx,edx,ecx,eax; } d;
	struct { uint16_t di,di_hi,si,si_hi,bp,bp_hi,res,res_hi,bx,bx_hi,dx,dx_hi,cx,cx_hi,ax,ax_hi,
		flags,es,ds,fs,gs,ip,cs,sp,ss; } x;
	struct { uint8_t edi[4],esi[4],ebp[4],res[4],bl,bh,ebx_b2,ebx_b3,dl,dh,edx_b2,edx_b3,
		cl,ch,ecx_b2,ecx_b3,al,ah,eax_b2,eax_b3; } h;
} __dpmi_regs;
int		__dpmi_int (int vector, __dpmi_regs *regs);
int		__dpmi_allocate_dos_memory (int paragraphs, int *selector);	// (none: -1)
int		__dpmi_free_dos_memory (int selector);

// Protected mode interrupt handlers: IRQ 0 (vector 8) and the keyboard's
// IRQ 1 (vector 9) are called by sys_linux.c
typedef struct
{
	uintptr_t	pm_offset;
	int			pm_selector;
} _go32_dpmi_seginfo;
int		_go32_dpmi_get_protected_mode_interrupt_vector (int vector, _go32_dpmi_seginfo *info);
int		_go32_dpmi_set_protected_mode_interrupt_vector (int vector, _go32_dpmi_seginfo *info);
int		_go32_dpmi_allocate_iret_wrapper (_go32_dpmi_seginfo *info);
int		_go32_dpmi_free_iret_wrapper (_go32_dpmi_seginfo *info);
int		_go32_my_cs (void);

// DOS
struct ffblk
{
	char	ff_name[260];
	int		ff_attrib;
	uint32_t ff_fsize;
	DIR		*ff_dir;
	char	ff_pattern[64];
};
#define FA_ARCH		0x20
int		findfirst (const char *pattern, struct ffblk *f, int attrib);
int		findnext (struct ffblk *f);

struct diskfree_t
{
	unsigned	total_clusters,avail_clusters,sectors_per_cluster,bytes_per_sector;
};
struct dostime_t
{
	unsigned char	hour,minute,second,hsecond;
};
struct dosdate_t
{
	unsigned char	day,month;
	unsigned short	year;
	unsigned char	dayofweek;
};
unsigned	_dos_getdiskfree (unsigned drive, struct diskfree_t *d);
void	_dos_gettime (struct dostime_t *t);
void	_dos_getdate (struct dosdate_t *d);
unsigned	_dos_write (int handle, const void *buf, unsigned len, unsigned *written);
int		filelength (int handle);
int		kbhit (void);
int		getch (void);
int		bioskey (int cmd);

// The data files can be in upper or lower case (or mixed): a file that
// isn't there as named is looked for whatever its case
int		SYS_Open (const char *name, int flags, ...);
FILE	*SYS_Fopen (const char *name, const char *mode);
#define open	SYS_Open
#define fopen	SYS_Fopen

char	*itoa (int value, char *string, int radix);
char	*strlwr (char *s);
char	*strupr (char *s);
char	*ltoa (int32_t value, char *string, int radix);
char	*ultoa (uint32_t value, char *string, int radix);

void	DOS32_Startup (void);
void	DOS32_Mark (char *what);
void	DOS32_WriteMarks (void);
int32_t	DOS32_MemoryFree (void);

// Busy loops on the time or the keyboard keep SDL's events coming: these
// are read through functions that poll SDL now and then (on the main
// thread). id_sd.c and id_in.c, where they live, use the variables.
volatile uint32_t	*SYS_TimeCount (void);
volatile int		*SYS_Keyboard (void);
volatile unsigned char *SYS_LastScan (void);
volatile char		*SYS_LastASCII (void);
void	SYS_Poll (void);
void	SYS_WaitVBL (int vbls);
void	SYS_Sleep (void);		// a moment, from loops that only wait
void	SYS_ShowText (const unsigned char *screen, int rows);	// the B800h text screens at Quit

// The network: UDP in place of IPX (wl_net.c)
int		SYS_NetStartup (int port);
void	SYS_NetShutdown (void);
void	SYS_NetSend (const unsigned char *addr, const void *data, int len);
int		SYS_NetGet (void *buf, int max, unsigned char *from);
void	SYS_NetMyAddr (unsigned char *addr);
void	SYS_NetBroadcastAddr (unsigned char *addr, int port);	// 0: ours
int		SYS_NetResolve (const char *name, unsigned char *addr, int defport);
void	SYS_NetAddrString (const unsigned char *addr, char *str);

#ifndef SYS_LINUX_IMPL
#define long int
#endif

#endif
