// SYS_LINUX.C -- the PC the game thinks it runs on, made of SDL2
//
// See sys_linux.h. In short: the game programs the PIT, hooks IRQ 0 and
// IRQ 1, calls INT 33h, writes AdLib registers and plays digitized sounds
// on "the Gravis UltraSound" (id_gus.c's interface), and draws into main
// memory as in the widescreen mode. This file answers all of that:
//
//	IRQ 0		a thread calls the game's timer handler at the rate in the
//				PIT (700 Hz in the game), then makes that tick's sound
//	IRQ 1		SDL's key events, as PC scan codes, through the game's
//				keyboard handler (on the main thread, when the game reads
//				the time or the keyboard: see SYS_Poll)
//	INT 33h		SDL's relative mouse motion and buttons
//	sound		an OPL2 in software (dbopl), the digitized sounds and the
//				PC speaker, mixed into a ring that SDL's audio plays
//	video		VESA_Start / VESA_Present / VESA_SetPalette (id_vesa.c's
//				interface): a window (or full screen) showing the page
//	network		UDP, for wl_net.c's IPX
//
// The same file is the Windows version (MinGW-w64): Winsock for the
// network, and the few other places where Windows differs are marked.

#define _GNU_SOURCE
#define SYS_LINUX_IMPL
#ifdef _WIN32
#define _WIN32_WINNT	0x0600		// (getaddrinfo, GetAdaptersAddresses)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#endif
#include <SDL.h>
#include "sys_linux.h"
#ifdef _WIN32
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET	_WSAIOW(IOC_VENDOR,12)
#endif
#define MSG_NOSIGNAL	0
#define SOCKERR			WSAGetLastError ()
#define SOCK_AGAIN(e)	((e) == WSAEWOULDBLOCK || (e) == WSAEINTR)
#define SOCK_INPROGRESS(e)	((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#define SOCK_ACCESS(e)	((e) == WSAEACCES)
#define CloseSocket(s)	closesocket (s)
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <termios.h>
#include <link.h>
#include <sys/mman.h>
#define SOCKERR			errno
#define SOCK_AGAIN(e)	((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINTR)
#define SOCK_INPROGRESS(e)	((e) == EINPROGRESS)
#define SOCK_ACCESS(e)	((e) == EACCES)
#define CloseSocket(s)	close (s)
#endif

// Windows: a UDP socket that sent to a port nobody listens on gets the
// "port unreachable" back as an error from the next recvfrom: never mind
static void UdpQuiet (int fd)
{
#ifdef _WIN32
	BOOL	no = FALSE;
	DWORD	got;
	WSAIoctl (fd,SIO_UDP_CONNRESET,&no,sizeof(no),NULL,0,&got,NULL,NULL);
#endif
}

// a socket that doesn't wait
static void NonBlocking (int fd)
{
#ifdef _WIN32
	u_long	one = 1;
	ioctlsocket (fd,FIONBIO,&one);
#else
	fcntl (fd,F_SETFL,fcntl (fd,F_GETFL) | O_NONBLOCK);
#endif
}

// what went wrong with a socket, in words
static const char *SockError (int err)
{
#ifdef _WIN32
	static char	msg[200];
	int			l;

	if (!FormatMessageA (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,NULL,err,0,msg,sizeof(msg),NULL))
		sprintf (msg,"error %d",err);
	for (l = strlen (msg);l && (msg[l-1] == '\n' || msg[l-1] == '\r' || msg[l-1] == '.');)
		msg[--l] = 0;
	return msg;
#else
	return strerror (err);
#endif
}

int		_argc;
char	**_argv;
unsigned char SYS_lowmem[0x110000];

int		wolf_main (void);
void	Quit (char *error);
void	OPLEMU_Init (int rate);
void	OPLEMU_Write (int reg, int val);
void	OPLEMU_Generate (int32_t *buf, int samples);
void	SDL_DigitizedDone (void);

extern	volatile uint32_t	TimeCount;
extern	volatile int		Keyboard[];
extern	volatile unsigned char LastScan;
extern	volatile char		LastASCII;

static	SDL_mutex	*intlock;			// "interrupts off"
static	__thread int intdepth;
static	SDL_Thread	*timerthread;
static	volatile int timerquit;
static	SDL_threadID mainthread;
static	volatile int quitting;
static	volatile sig_atomic_t interrupted;	// Ctrl+C, kill

static	int		HasArg (const char *name)
{
	int i;
	for (i=1;i<_argc;i++)
		if (!strcasecmp (_argv[i],name))
			return i;
	return 0;
}


/*
=============================================================================

						INTERRUPTS

=============================================================================
*/

int disable (void)
{
	if (intlock)
		SDL_LockMutex (intlock);
	intdepth++;
	return 1;
}

int enable (void)
{
	if (intdepth <= 0)
		return 1;			// enable() without a disable(): fine
	intdepth--;
	if (intlock)
		SDL_UnlockMutex (intlock);
	return 1;
}

typedef void (*isr_t) (void);
static	volatile isr_t vectors[256];

int _go32_dpmi_get_protected_mode_interrupt_vector (int vector, _go32_dpmi_seginfo *info)
{
	info->pm_offset = (uintptr_t)vectors[vector & 255];
	info->pm_selector = 0;
	return 0;
}

int _go32_dpmi_set_protected_mode_interrupt_vector (int vector, _go32_dpmi_seginfo *info)
{
	disable ();
	vectors[vector & 255] = (isr_t)info->pm_offset;
	enable ();
	return 0;
}

int _go32_dpmi_allocate_iret_wrapper (_go32_dpmi_seginfo *info)
{
	return 0;
}

int _go32_dpmi_free_iret_wrapper (_go32_dpmi_seginfo *info)
{
	return 0;
}

int _go32_my_cs (void)
{
	return 0;
}


/*
=============================================================================

						MEMORY BELOW 1 MB

=============================================================================
*/

unsigned char _farpeekb (int sel, uint32_t addr)
{
	return addr < sizeof(SYS_lowmem) ? SYS_lowmem[addr] : 0;
}

unsigned short _farpeekw (int sel, uint32_t addr)
{
	return _farpeekb (sel,addr) | (_farpeekb (sel,addr+1) << 8);
}

uint32_t _farpeekl (int sel, uint32_t addr)
{
	return _farpeekw (sel,addr) | ((uint32_t)_farpeekw (sel,addr+2) << 16);
}

void _farpokeb (int sel, uint32_t addr, unsigned char v)
{
	if (addr < sizeof(SYS_lowmem))
		SYS_lowmem[addr] = v;
}

void _farpokew (int sel, uint32_t addr, unsigned short v)
{
	_farpokeb (sel,addr,v);
	_farpokeb (sel,addr+1,v >> 8);
}

void _farpokel (int sel, uint32_t addr, uint32_t v)
{
	_farpokew (sel,addr,v);
	_farpokew (sel,addr+2,v >> 16);
}

void dosmemget (uint32_t addr, int len, void *dest)
{
	if (addr+len <= sizeof(SYS_lowmem))
		memcpy (dest,SYS_lowmem+addr,len);
}

void dosmemput (const void *src, int len, uint32_t addr)
{
	if (addr+len > sizeof(SYS_lowmem))
		return;
	memcpy (SYS_lowmem+addr,src,len);
	if (addr >= 0xb8000 && addr < 0xb8000+4000)
		SYS_ShowText (SYS_lowmem+0xb8000,25);		// a text screen at Quit
}


//
// SYS_ShowText: the 80x25 text screen, to the terminal (in its colors if
// it's a terminal), for the ending and error screens
//
static const char *cp437[128] =
{
	"Ç","ü","é","â","ä","à","å","ç","ê","ë","è","ï","î","ì","Ä","Å",
	"É","æ","Æ","ô","ö","ò","û","ù","ÿ","Ö","Ü","¢","£","¥","₧","ƒ",
	"á","í","ó","ú","ñ","Ñ","ª","º","¿","⌐","¬","½","¼","¡","«","»",
	"░","▒","▓","│","┤","╡","╢","╖","╕","╣","║","╗","╝","╜","╛","┐",
	"└","┴","┬","├","─","┼","╞","╟","╚","╔","╩","╦","╠","═","╬","╧",
	"╨","╤","╥","╙","╘","╒","╓","╫","╪","┘","┌","█","▄","▌","▐","▀",
	"α","ß","Γ","π","Σ","σ","µ","τ","Φ","Θ","Ω","δ","∞","φ","ε","∩",
	"≡","±","≥","≤","⌠","⌡","÷","≈","°","∙","·","√","ⁿ","²","■"," ",
};

void SYS_ShowText (const unsigned char *screen, int rows)
{
	static const int ansi[8] = {0,4,2,6,1,5,3,7};
	int		x,y,c,a,lasta,tty,end;

	tty = isatty (1);
	for (y=0;y<rows;y++)
	{
		lasta = -1;
		for (end=80;end>0;end--)			// no trailing blanks
		{
			c = screen[(y*80+end-1)*2];
			a = screen[(y*80+end-1)*2+1];
			if ((c != ' ' && c != 0 && c != 255) || (tty && (a & 0x70)))
				break;
		}
		for (x=0;x<end;x++)
		{
			c = screen[(y*80+x)*2];
			a = screen[(y*80+x)*2+1];
			if (tty && a != lasta)
			{
				printf ("\033[0;%s3%d;4%dm",(a & 8) ? "1;" : "",ansi[a & 7],ansi[(a >> 4) & 7]);
				lasta = a;
			}
			if (c < 32)
				putchar (c ? '.' : ' ');
			else if (c < 128)
				putchar (c);
			else
				fputs (cp437[c-128],stdout);
		}
		if (tty)
			printf ("\033[0m");
		putchar ('\n');
	}
	fflush (stdout);
}


/*
=============================================================================

						I/O PORTS

=============================================================================
*/

static	volatile unsigned	t0divisor = 65536;	// PIT channel 0
static	int		pitlatch0,pitbyte0;
static	volatile unsigned	spkdivisor = 1000;	// PIT channel 2
static	int		pitlatch2,pitbyte2;
static	volatile unsigned char port61;
static	unsigned char port60;
static	unsigned	vgastatus;

unsigned char inportb (int port)
{
	switch (port)
	{
	case 0x60:
		return port60;
	case 0x61:
		return port61;
	case 0x3da:							// the retrace bit comes and goes
		vgastatus++;
		return (vgastatus & 0x40) ? 8 : 0;
	case 0x201:
		return 0xf0;					// no joystick buttons
	case 0x330:
	case 0x331:
		return SYS_MpuIn (port);		// the MPU-401 (sys_midi.c)
	}
	return 0;
}

unsigned short inportw (int port)
{
	return inportb (port) | (inportb (port+1) << 8);
}

void outportb (int port, unsigned char v)
{
	switch (port)
	{
	case 0x43:
		if ((v & 0xc0) == 0x00)
			pitbyte0 = 0;
		else if ((v & 0xc0) == 0x80)
			pitbyte2 = 0;
		break;
	case 0x40:
		if (!pitbyte0)
		{
			pitlatch0 = v;
			pitbyte0 = 1;
		}
		else
		{
			pitlatch0 |= v << 8;
			pitbyte0 = 0;
			t0divisor = pitlatch0 ? pitlatch0 : 65536;
		}
		break;
	case 0x42:
		if (!pitbyte2)
		{
			pitlatch2 = v;
			pitbyte2 = 1;
		}
		else
		{
			pitlatch2 |= v << 8;
			pitbyte2 = 0;
			spkdivisor = pitlatch2 ? pitlatch2 : 65536;
		}
		break;
	case 0x61:
		port61 = v;
		break;
	case 0x330:
	case 0x331:
		SYS_MpuOut (port,v);
		break;
	}
}

void outportw (int port, unsigned short v)
{
	outportb (port,v);
	outportb (port+1,v >> 8);
}

void outportsb (int port, const unsigned char *buf, int n)
{
	while (n--)
		outportb (port,*buf++);
}


/*
=============================================================================

						SOUND

 Each timer tick makes its share of the samples (an OPL2's, the digitized
 sound's and the PC speaker's) into a ring, which SDL's audio callback
 plays a few milliseconds later. The ring runs on the wall clock and the
 audio on the card's: when they drift apart, samples are dropped or
 silence is put in.

=============================================================================
*/

#define MIXRATE		44100
#define DIGIRATE	7042
#define RINGLEN		16384			// frames, a power of two
#define STARTFILL	1024			// frames buffered before playing
#define MAXFILL		4096			// more than this: drop down to STARTFILL
#define OPLCHUNK	256

static	SDL_AudioDeviceID audiodev;
static	int16_t	ring[RINGLEN*2];
static	volatile uint32_t ringw,ringr;
static	int		ringplaying;
static	uint32_t samplefrac;		// PIT counts not yet made into samples

static	int		oplon;				// GUS_Startup: the OPL2 and the digitized sounds
static	int32_t	oplbuf[OPLCHUNK];
static	int32_t	midibuf[OPLCHUNK*2];	// the General MIDI synth's, stereo
static	unsigned char * volatile digiptr;
static	volatile uint32_t digilen;
static	uint32_t digipos,digistep;	// 16.16
static	volatile int digivoll = 16,digivolr = 16;

static	uint32_t spkphase;			// 16.16 of a period
static	int		spklevel;
static	int32_t	spkfilter;

static void AudioCallback (void *userdata, Uint8 *stream, int len)
{
	int16_t	*out = (int16_t *)stream;
	int		frames = len/4,n,i;
	uint32_t fill;

	SDL_LockMutex (intlock);
	fill = ringw - ringr;
	if (!ringplaying && fill >= STARTFILL)
		ringplaying = 1;
	if (fill > MAXFILL)
	{
		ringr = ringw - STARTFILL;
		fill = STARTFILL;
	}
	n = 0;
	if (ringplaying)
	{
		n = fill < (uint32_t)frames ? fill : frames;
		for (i=0;i<n;i++)
		{
			out[i*2] = ring[((ringr+i) & (RINGLEN-1))*2];
			out[i*2+1] = ring[((ringr+i) & (RINGLEN-1))*2+1];
		}
		ringr += n;
		if (n < frames)
			ringplaying = 0;		// ran dry: build it up again
	}
	SDL_UnlockMutex (intlock);
	if (n < frames)
		memset (out+n*2,0,(frames-n)*4);
}

static void StartAudio (void)
{
	SDL_AudioSpec want,have;

	if (HasArg ("-nosound") || SDL_InitSubSystem (SDL_INIT_AUDIO))
		return;
	memset (&want,0,sizeof(want));
	want.freq = MIXRATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;
	want.callback = AudioCallback;
	audiodev = SDL_OpenAudioDevice (NULL,0,&want,&have,0);
	if (!audiodev)
	{
		fprintf (stderr,"No sound: %s\n",SDL_GetError ());
		return;
	}
	SDL_PauseAudioDevice (audiodev,0);
}

//
// MakeSamples: n frames of everything that's playing, into the ring
// (timer thread, "interrupts off")
//
static void MakeSamples (int n)
{
	int		i,count,midi;
	int32_t	l,r,s,d,sp;
	uint32_t f;

	if (!audiodev)
	{
		if (oplon)
		{
			// nobody hears it, but the digitized sounds still have to end
			if (digiptr)
			{
				digipos += digistep*n;
				if ((digipos >> 16) >= digilen)
				{
					digiptr = NULL;
					SDL_DigitizedDone ();
				}
			}
		}
		return;
	}

	while (n > 0)
	{
		count = n > OPLCHUNK ? OPLCHUNK : n;
		if (oplon)
			OPLEMU_Generate (oplbuf,count);
		midi = SYS_MidiRender (midibuf,count);
		for (i=0;i<count;i++)
		{
			s = oplon ? oplbuf[i] << 2 : 0;	// dbopl is quiet next to a real OPL
			l = r = s;
			if (midi)
			{
				l += midibuf[i*2];
				r += midibuf[i*2+1];
			}

			if (digiptr)
			{
				d = ((int)digiptr[digipos >> 16] - 128) << 7;
				l += d * digivoll / 16;
				r += d * digivolr / 16;
				digipos += digistep;
				if ((digipos >> 16) >= digilen)
				{
					digiptr = NULL;
					SDL_DigitizedDone ();	// may start the next part
				}
			}

			// the PC speaker: a square wave while the gate and the data
			// bits are on, else the data bit as it is; high-passed
			if ((port61 & 3) == 3)
			{
				spkphase += (uint32_t)(((uint64_t)PIT_HZ << 16) / spkdivisor / MIXRATE);
				sp = (spkphase & 0x8000) ? 5000 : -5000;
			}
			else
				sp = (port61 & 2) ? 5000 : 0;
			spkfilter += ((sp << 8) - spkfilter) >> 9;
			sp -= spkfilter >> 8;
			l += sp;
			r += sp;

			if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
			if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
			f = (ringw & (RINGLEN-1))*2;
			ring[f] = l;
			ring[f+1] = r;
			ringw++;
		}
		n -= count;
	}
	if (ringw - ringr > RINGLEN - OPLCHUNK)
		ringr = ringw - MAXFILL;		// nobody's taking them (paused?)
}


//
// The "Gravis UltraSound" of id_sd.c: here, the OPL2 and digitized sounds
// of the mixer above
//
int GUS_Startup (void)
{
	disable ();
	OPLEMU_Init (MIXRATE);
	SYS_MidiRate (MIXRATE);
	digistep = ((uint32_t)DIGIRATE << 16) / MIXRATE;
	oplon = 1;
	enable ();
	return 1;
}

void GUS_Shutdown (void)
{
	disable ();
	digiptr = NULL;
	enable ();
}

void GUS_Service (void)
{
	// the timer thread makes the samples after each tick
}

void GUS_AdLibWrite (int reg, int val)
{
	OPLEMU_Write (reg,val);
}

void GUS_PlaySample (unsigned char *data, uint32_t len)
{
	disable ();
	digipos = 0;
	digilen = len;
	digiptr = data;
	enable ();
}

void GUS_SetVolume (int leftpos, int rightpos)
{
	digivoll = ((15 - leftpos) * 16 + 7) / 15;
	digivolr = ((15 - rightpos) * 16 + 7) / 15;
}

void GUS_StopSample (void)
{
	disable ();
	digiptr = NULL;
	enable ();
}


/*
=============================================================================

						THE TIMER

=============================================================================
*/

static void BiosTick (void)
{
	uint32_t ticks = _farpeekl (0,0x46c) + 1;
	if (ticks >= 0x1800b0)
		ticks = 0;
	_farpokel (0,0x46c,ticks);
}

static int TimerThread (void *unused)
{
	Uint64	freq,last,now,counts;
	uint64_t biosfrac = 0;
	unsigned div;
	int		n,ticks;

	freq = SDL_GetPerformanceFrequency ();
	last = SDL_GetPerformanceCounter ();
	counts = 0;
	while (!timerquit)
	{
		SDL_Delay (1);
		now = SDL_GetPerformanceCounter ();
		counts += (now - last) * PIT_HZ / freq;
		last = now;
		if (counts > PIT_HZ/4)
			counts = PIT_HZ/4;			// way behind (stopped in a debugger?)

		ticks = 0;
		while (counts >= (div = t0divisor) && ticks < 200)
		{
			counts -= div;
			ticks++;
			disable ();
			if (vectors[8])
				vectors[8] ();
			else
			{
				biosfrac += div;
				while (biosfrac >= 65536)
				{
					biosfrac -= 65536;
					BiosTick ();
				}
			}
			samplefrac += div * MIXRATE;
			n = samplefrac / PIT_HZ;
			samplefrac %= PIT_HZ;
			MakeSamples (n);
			enable ();
		}
	}
	return 0;
}


/*
=============================================================================

						KEYBOARD, MOUSE, EVENTS

=============================================================================
*/

static	SDL_Window	*window;
static	SDL_Renderer *renderer;
static	SDL_Texture	*texture;
static	int		mousegrab;
static	int		mousex = 320,mousey = 100,mousedx,mousedy,mousebuttons;
static	int		keydown[SDL_NUM_SCANCODES];
static	Uint32	lastpoll;
static	int		spins;

// SDL scan code -> PC (set 1) scan code; 0x100 means an E0 prefix first
static int PCScan (SDL_Scancode sc)
{
	static const struct { SDL_Scancode sdl; int pc; } map[] =
	{
		{SDL_SCANCODE_ESCAPE,0x01},{SDL_SCANCODE_1,0x02},{SDL_SCANCODE_2,0x03},
		{SDL_SCANCODE_3,0x04},{SDL_SCANCODE_4,0x05},{SDL_SCANCODE_5,0x06},
		{SDL_SCANCODE_6,0x07},{SDL_SCANCODE_7,0x08},{SDL_SCANCODE_8,0x09},
		{SDL_SCANCODE_9,0x0a},{SDL_SCANCODE_0,0x0b},{SDL_SCANCODE_MINUS,0x0c},
		{SDL_SCANCODE_EQUALS,0x0d},{SDL_SCANCODE_BACKSPACE,0x0e},{SDL_SCANCODE_TAB,0x0f},
		{SDL_SCANCODE_Q,0x10},{SDL_SCANCODE_W,0x11},{SDL_SCANCODE_E,0x12},
		{SDL_SCANCODE_R,0x13},{SDL_SCANCODE_T,0x14},{SDL_SCANCODE_Y,0x15},
		{SDL_SCANCODE_U,0x16},{SDL_SCANCODE_I,0x17},{SDL_SCANCODE_O,0x18},
		{SDL_SCANCODE_P,0x19},{SDL_SCANCODE_LEFTBRACKET,0x1a},{SDL_SCANCODE_RIGHTBRACKET,0x1b},
		{SDL_SCANCODE_RETURN,0x1c},{SDL_SCANCODE_LCTRL,0x1d},{SDL_SCANCODE_A,0x1e},
		{SDL_SCANCODE_S,0x1f},{SDL_SCANCODE_D,0x20},{SDL_SCANCODE_F,0x21},
		{SDL_SCANCODE_G,0x22},{SDL_SCANCODE_H,0x23},{SDL_SCANCODE_J,0x24},
		{SDL_SCANCODE_K,0x25},{SDL_SCANCODE_L,0x26},{SDL_SCANCODE_SEMICOLON,0x27},
		{SDL_SCANCODE_APOSTROPHE,0x28},{SDL_SCANCODE_GRAVE,0x29},{SDL_SCANCODE_LSHIFT,0x2a},
		{SDL_SCANCODE_BACKSLASH,0x2b},{SDL_SCANCODE_Z,0x2c},{SDL_SCANCODE_X,0x2d},
		{SDL_SCANCODE_C,0x2e},{SDL_SCANCODE_V,0x2f},{SDL_SCANCODE_B,0x30},
		{SDL_SCANCODE_N,0x31},{SDL_SCANCODE_M,0x32},{SDL_SCANCODE_COMMA,0x33},
		{SDL_SCANCODE_PERIOD,0x34},{SDL_SCANCODE_SLASH,0x35},{SDL_SCANCODE_RSHIFT,0x36},
		{SDL_SCANCODE_KP_MULTIPLY,0x37},{SDL_SCANCODE_LALT,0x38},{SDL_SCANCODE_SPACE,0x39},
		{SDL_SCANCODE_CAPSLOCK,0x3a},{SDL_SCANCODE_F1,0x3b},{SDL_SCANCODE_F2,0x3c},
		{SDL_SCANCODE_F3,0x3d},{SDL_SCANCODE_F4,0x3e},{SDL_SCANCODE_F5,0x3f},
		{SDL_SCANCODE_F6,0x40},{SDL_SCANCODE_F7,0x41},{SDL_SCANCODE_F8,0x42},
		{SDL_SCANCODE_F9,0x43},{SDL_SCANCODE_F10,0x44},{SDL_SCANCODE_NUMLOCKCLEAR,0x45},
		{SDL_SCANCODE_SCROLLLOCK,0x46},{SDL_SCANCODE_KP_7,0x47},{SDL_SCANCODE_KP_8,0x48},
		{SDL_SCANCODE_KP_9,0x49},{SDL_SCANCODE_KP_MINUS,0x4a},{SDL_SCANCODE_KP_4,0x4b},
		{SDL_SCANCODE_KP_5,0x4c},{SDL_SCANCODE_KP_6,0x4d},{SDL_SCANCODE_KP_PLUS,0x4e},
		{SDL_SCANCODE_KP_1,0x4f},{SDL_SCANCODE_KP_2,0x50},{SDL_SCANCODE_KP_3,0x51},
		{SDL_SCANCODE_KP_0,0x52},{SDL_SCANCODE_KP_PERIOD,0x53},{SDL_SCANCODE_F11,0x57},
		{SDL_SCANCODE_F12,0x58},
		// the gray keys: E0 and the keypad key's code, as the game reads them
		{SDL_SCANCODE_KP_ENTER,0x11c},{SDL_SCANCODE_RCTRL,0x11d},{SDL_SCANCODE_KP_DIVIDE,0x135},
		{SDL_SCANCODE_RALT,0x138},{SDL_SCANCODE_HOME,0x147},{SDL_SCANCODE_UP,0x148},
		{SDL_SCANCODE_PAGEUP,0x149},{SDL_SCANCODE_LEFT,0x14b},{SDL_SCANCODE_RIGHT,0x14d},
		{SDL_SCANCODE_END,0x14f},{SDL_SCANCODE_DOWN,0x150},{SDL_SCANCODE_PAGEDOWN,0x151},
		{SDL_SCANCODE_INSERT,0x152},{SDL_SCANCODE_DELETE,0x153},
		{SDL_SCANCODE_LGUI,0x15b},{SDL_SCANCODE_RGUI,0x15c},
	};
	int i;

	for (i=0;i<(int)(sizeof(map)/sizeof(map[0]));i++)
		if (map[i].sdl == sc)
			return map[i].pc;
	return 0;
}

static void KeyByte (int b)
{
	disable ();
	port60 = b;
	if (vectors[9])
		vectors[9] ();
	enable ();
}

static void Key (SDL_Scancode sc, int down)
{
	int pc = PCScan (sc);

	if (!pc)
		return;
	if (sc < SDL_NUM_SCANCODES)
		keydown[sc] = down;
	if (pc & 0x100)
		KeyByte (0xe0);
	KeyByte ((pc & 0x7f) | (down ? 0 : 0x80));
}

static void ReleaseKeys (void)
{
	int i;

	for (i=0;i<SDL_NUM_SCANCODES;i++)
		if (keydown[i])
			Key ((SDL_Scancode)i,0);
	mousebuttons = 0;
}

static	char	wintitle[64];

static void GrabMouse (int on)
{
	char	title[100];

	if (!window || HasArg ("nomouse"))
		on = 0;
	if (on == mousegrab)
		return;
	mousegrab = on;
	SDL_SetRelativeMouseMode (on ? SDL_TRUE : SDL_FALSE);
	SDL_GetRelativeMouseState (NULL,NULL);
	if (window && !HasArg ("nomouse"))
	{
		sprintf (title,"%s - %s",wintitle,on ? "F12 lets the mouse go" : "click to use the mouse");
		SDL_SetWindowTitle (window,title);
	}
}

static void ToggleFullscreen (void)
{
	if (!window)
		return;
	if (SDL_GetWindowFlags (window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
		SDL_SetWindowFullscreen (window,0);
	else
		SDL_SetWindowFullscreen (window,SDL_WINDOW_FULLSCREEN_DESKTOP);
}

static void PresentAgain (void);

void SYS_Poll (void)
{
	static	int	inpoll;
	SDL_Event ev;
	Uint32	now;
	int		b;

	if (inpoll || SDL_ThreadID () != mainthread)
		return;
	now = SDL_GetTicks ();
	if (now == lastpoll)
	{
		// called over and over within a millisecond: something's waiting
		if (++spins > 400)
		{
			SDL_Delay (1);
			spins = 0;
		}
		return;
	}
	lastpoll = now;
	spins = 0;
	if (interrupted && !quitting)
	{
		quitting = 1;
		Quit (NULL);
	}
	inpoll = 1;

	while (SDL_PollEvent (&ev))
	{
		switch (ev.type)
		{
		case SDL_QUIT:
			if (!quitting)
			{
				quitting = 1;
				Quit (NULL);
			}
			break;
		case SDL_KEYDOWN:
			if (ev.key.keysym.scancode == SDL_SCANCODE_F11
			|| (ev.key.keysym.scancode == SDL_SCANCODE_RETURN && (ev.key.keysym.mod & KMOD_ALT)))
			{
				if (!ev.key.repeat)
					ToggleFullscreen ();
				break;
			}
			if (ev.key.keysym.scancode == SDL_SCANCODE_F12
			|| (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE && (ev.key.keysym.mod & KMOD_SHIFT)))
			{
				if (!ev.key.repeat)
					GrabMouse (!mousegrab);	// F12 (or Shift+Esc): let the mouse go / take it
				break;
			}
			if (ev.key.keysym.scancode == SDL_SCANCODE_PAUSE)
			{
				KeyByte (0xe1);			// the game takes E1 as Pause
				break;
			}
			Key (ev.key.keysym.scancode,1);
			break;
		case SDL_KEYUP:
			Key (ev.key.keysym.scancode,0);
			break;
		case SDL_MOUSEMOTION:
			if (mousegrab)
			{
				mousedx += ev.motion.xrel;
				mousedy += ev.motion.yrel;
				mousex += ev.motion.xrel;
				mousey += ev.motion.yrel;
				if (mousex < 0) mousex = 0;
				if (mousex > 639) mousex = 639;
				if (mousey < 0) mousey = 0;
				if (mousey > 199) mousey = 199;
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if (!mousegrab)
			{
				if (ev.type == SDL_MOUSEBUTTONDOWN)
					GrabMouse (1);			// a click in the window takes the mouse
				break;
			}
			b = ev.button.button == SDL_BUTTON_LEFT ? 1 : ev.button.button == SDL_BUTTON_RIGHT ? 2
				: ev.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
			if (ev.type == SDL_MOUSEBUTTONDOWN)
				mousebuttons |= b;
			else
				mousebuttons &= ~b;
			break;
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			{
				ReleaseKeys ();
				GrabMouse (0);
			}
			else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
				GrabMouse (1);
			else if (ev.window.event == SDL_WINDOWEVENT_EXPOSED
			|| ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				PresentAgain ();
			break;
		}
	}
	inpoll = 0;
}

uint32_t SYS_Millis (void)
{
	return SDL_GetTicks ();
}

volatile uint32_t *SYS_TimeCount (void)
{
	SYS_Poll ();
	return &TimeCount;
}

volatile int *SYS_Keyboard (void)
{
	SYS_Poll ();
	return Keyboard;
}

volatile unsigned char *SYS_LastScan (void)
{
	SYS_Poll ();
	return &LastScan;
}

volatile char *SYS_LastASCII (void)
{
	SYS_Poll ();
	return &LastASCII;
}

void SYS_Sleep (void)
{
	SDL_Delay (1);
	SYS_Poll ();
}

void SYS_WaitVBL (int vbls)
{
	Uint64	freq = SDL_GetPerformanceFrequency ();
	Uint64	end = SDL_GetPerformanceCounter () + freq*vbls/70;

	while (SDL_GetPerformanceCounter () < end)
	{
		SYS_Poll ();
		SDL_Delay (1);
	}
}

//
// INT 33h, INT 10h, INT 21h: what the game asks of them
//
int __dpmi_int (int vector, __dpmi_regs *r)
{
	time_t		t;
	struct tm	*tm;

	switch (vector)
	{
	case 0x33:
		SYS_Poll ();
		switch (r->x.ax)
		{
		case 0:							// reset
			if (HasArg ("nomouse"))
				break;
			r->x.ax = 0xffff;
			r->x.bx = 3;
			break;
		case 3:							// buttons and position
			r->x.bx = mousebuttons;
			r->x.cx = mousex;
			r->x.dx = mousey;
			break;
		case 4:							// set the position
			mousex = (short)r->x.cx;
			mousey = (short)r->x.dx;
			break;
		case 11:						// motion since last time
			r->x.cx = mousedx;
			r->x.dx = mousedy;
			mousedx = mousedy = 0;
			break;
		}
		break;
	case 0x10:
		if (r->x.ax == 0x1a00)			// a color VGA
		{
			r->h.al = 0x1a;
			r->h.bl = 8;
		}
		break;
	case 0x21:
		if (r->h.ah == 0x2c)			// the time
		{
			t = time (NULL);
			tm = localtime (&t);
			r->h.ch = tm->tm_hour;
			r->h.cl = tm->tm_min;
			r->h.dh = tm->tm_sec;
			r->h.dl = (SDL_GetTicks () / 10) % 100;
		}
		break;
	}
	return 0;
}


// no DOS memory to give (only the Sound Blaster wanted it)
int __dpmi_allocate_dos_memory (int paragraphs, int *selector)
{
	return -1;
}

int __dpmi_free_dos_memory (int selector)
{
	return 0;
}


/*
=============================================================================

						VIDEO

 The page is scrwidth x 200; its pixels are 1.2 times taller than wide,
 as on a 4:3 monitor, so it's shown scrwidth x 240 (scaled to the window,
 black around it).

=============================================================================
*/

static	int		pagew;
static	Uint32	lut[256];
static	unsigned char vpal[768];
static	unsigned char *lastpage;
static	Uint32	*pixels;

int VESA_Start (int wantw, int wanth)
{
	int		width,scale,flags;
	SDL_DisplayMode dm;

	if (window)
		return pagew;

	//
	// the width: 320 (4:3), or the shape of -vesa WxH, or 16:9
	//
	if (wantw < 0)
		width = 320;
	else if (wantw > 0 && wanth > 0)
		width = (int)(240.0*wantw/wanth/16 + 0.5)*16;
	else
		width = 432;
	if (width < 320)
		width = 320;
	if (width > 512)
		width = 512;
	pagew = width;

	if (SDL_InitSubSystem (SDL_INIT_VIDEO))
	{
		fprintf (stderr,"SDL: %s\n",SDL_GetError ());
		exit (1);
	}

	scale = 3;
	if (!SDL_GetDesktopDisplayMode (0,&dm))
		while (scale > 1 && (width*scale > dm.w*9/10 || 240*scale > dm.h*9/10))
			scale--;
	flags = SDL_WINDOW_RESIZABLE;
	if (HasArg ("-fullscreen"))
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#ifdef SPEAR
	strcpy (wintitle,"Spear of Destiny");
#else
	strcpy (wintitle,"Wolfenstein 3-D");
#endif
#ifdef SPEAR
	window = SDL_CreateWindow ("Spear of Destiny",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
		width*scale,240*scale,flags);
#else
	window = SDL_CreateWindow ("Wolfenstein 3-D",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
		width*scale,240*scale,flags);
#endif
	if (!window)
	{
		fprintf (stderr,"SDL: %s\n",SDL_GetError ());
		exit (1);
	}
	renderer = SDL_CreateRenderer (window,-1,SDL_RENDERER_ACCELERATED);
	if (!renderer)
		renderer = SDL_CreateRenderer (window,-1,0);
	if (!renderer)
	{
		fprintf (stderr,"SDL: %s\n",SDL_GetError ());
		exit (1);
	}
	SDL_SetHint (SDL_HINT_RENDER_SCALE_QUALITY,HasArg ("-smooth") ? "1" : "0");
	SDL_RenderSetLogicalSize (renderer,width,240);
	texture = SDL_CreateTexture (renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,200);
	pixels = calloc (width*200,4);
	if (!texture || !pixels)
	{
		fprintf (stderr,"SDL: %s\n",SDL_GetError ());
		exit (1);
	}
	SDL_ShowCursor (SDL_DISABLE);
	SDL_RenderClear (renderer);
	SDL_RenderPresent (renderer);
	GrabMouse (1);
	return width;
}

void VESA_Stop (void)
{
	if (!window)
		return;
	GrabMouse (0);
	SDL_HideWindow (window);
}

static void Show (void)
{
	SDL_Rect dst = {0,0,pagew,240};

	SDL_UpdateTexture (texture,NULL,pixels,pagew*4);
	SDL_RenderClear (renderer);
	SDL_RenderCopy (renderer,texture,NULL,&dst);
	SDL_RenderPresent (renderer);
}

void VESA_Present (unsigned char *page)
{
	int		i;

	if (!window)
		return;
	lastpage = page;
	for (i=0;i<pagew*200;i++)
		pixels[i] = lut[page[i]];
	Show ();
	SYS_Poll ();
}

static void PresentAgain (void)
{
	if (window && renderer && pixels)
		Show ();
}

int VESA_SetPalette (unsigned char *palette, int first, int count)
{
	int		i,r,g,b;

	memcpy (vpal+first*3,palette,count*3);
	for (i=first;i<first+count;i++)
	{
		r = (vpal[i*3] & 63)*255/63;
		g = (vpal[i*3+1] & 63)*255/63;
		b = (vpal[i*3+2] & 63)*255/63;
		lut[i] = 0xff000000u | (r << 16) | (g << 8) | b;
	}
	return 1;		// show the page again in the new colors
}

void VESA_GetPalette (unsigned char *palette)
{
	memcpy (palette,vpal,768);
}

int VESA_Indexed (void)
{
	return 0;
}

char vesamessage[160];


/*
=============================================================================

						FILES AND DOS

=============================================================================
*/

//
// FindCase: name as it is on the disk, whatever the case; 0 if not there
//
static int FindCase (const char *name, char *out, int outlen)
{
	char	dir[1024];
	const char *base,*slash;
	DIR		*d;
	struct dirent *e;

	slash = strrchr (name,'/');
	if (slash)
	{
		if (slash-name >= (int)sizeof(dir))
			return 0;
		memcpy (dir,name,slash-name);
		dir[slash-name] = 0;
		base = slash+1;
		if (!dir[0])
			strcpy (dir,"/");
	}
	else
	{
		strcpy (dir,".");
		base = name;
	}
	d = opendir (dir);
	if (!d)
		return 0;
	while ((e = readdir (d)) != NULL)
		if (!strcasecmp (e->d_name,base))
		{
			if (slash)
				snprintf (out,outlen,"%s/%s",dir,e->d_name);
			else
				snprintf (out,outlen,"%s",e->d_name);
			closedir (d);
			return 1;
		}
	closedir (d);
	return 0;
}

#undef open
#undef fopen

int SYS_Open (const char *name, int flags, ...)
{
	char	real[1100];
	int		mode = 0666,h;
	va_list	ap;

	if (flags & O_CREAT)
	{
		va_start (ap,flags);
		mode = va_arg (ap,int);
		va_end (ap);
		mode |= 0644;
	}
	h = open (name,flags,mode);
#ifdef _WIN32
	return h;						// (Windows doesn't care about case)
#endif
	if (h == -1 && errno == ENOENT && FindCase (name,real,sizeof(real)))
		h = open (real,flags,mode);
	else if (h != -1 && (flags & O_CREAT) && FindCase (name,real,sizeof(real)) && strcmp (real,name))
	{
		// made a second file next to one in another case: use that one
		close (h);
		unlink (name);
		h = open (real,flags,mode);
	}
	return h;
}

FILE *SYS_Fopen (const char *name, const char *mode)
{
	char	real[1100];
	FILE	*f;

	if (mode[0] != 'r' && FindCase (name,real,sizeof(real)))
		return fopen (real,mode);
	f = fopen (name,mode);
	if (!f && errno == ENOENT && FindCase (name,real,sizeof(real)))
		f = fopen (real,mode);
	return f;
}

// DOS's * and ?, whatever the case
static int Wildcard (const char *pat, const char *name)
{
	for ( ; *pat ; pat++,name++)
	{
		if (*pat == '*')
		{
			do
				if (Wildcard (pat+1,name))
					return 1;
			while (*name++);
			return 0;
		}
		if (!*name || (*pat != '?' && tolower ((unsigned char)*pat) != tolower ((unsigned char)*name)))
			return 0;
	}
	return !*name;
}

int findfirst (const char *pattern, struct ffblk *f, int attrib)
{
	memset (f,0,sizeof(*f));
	strncpy (f->ff_pattern,pattern,sizeof(f->ff_pattern)-1);
	f->ff_dir = opendir (".");
	if (!f->ff_dir)
		return -1;
	return findnext (f);
}

int findnext (struct ffblk *f)
{
	struct dirent *e;
	struct stat st;

	if (!f->ff_dir)
		return -1;
	while ((e = readdir (f->ff_dir)) != NULL)
	{
		if (!Wildcard (f->ff_pattern,e->d_name))
			continue;
		if (stat (e->d_name,&st) || !S_ISREG (st.st_mode))
			continue;
		strncpy (f->ff_name,e->d_name,sizeof(f->ff_name)-1);
		f->ff_fsize = st.st_size;
		return 0;
	}
	closedir (f->ff_dir);
	f->ff_dir = NULL;
	return -1;
}

unsigned _dos_getdiskfree (unsigned drive, struct diskfree_t *d)
{
	d->total_clusters = d->avail_clusters = 65535;
	d->sectors_per_cluster = 64;
	d->bytes_per_sector = 512;
	return 0;
}

void _dos_gettime (struct dostime_t *t)
{
	time_t		now = time (NULL);
	struct tm	*tm = localtime (&now);

	t->hour = tm->tm_hour;
	t->minute = tm->tm_min;
	t->second = tm->tm_sec;
	t->hsecond = (SDL_GetTicks () / 10) % 100;
}

void _dos_getdate (struct dosdate_t *d)
{
	time_t		now = time (NULL);
	struct tm	*tm = localtime (&now);

	d->day = tm->tm_mday;
	d->month = tm->tm_mon+1;
	d->year = tm->tm_year+1900;
	d->dayofweek = tm->tm_wday;
}

unsigned _dos_write (int handle, const void *buf, unsigned len, unsigned *written)
{
	int n = write (handle,buf,len);
	*written = n < 0 ? 0 : n;
	return n < 0;
}

int filelength (int handle)
{
	struct stat st;

	if (fstat (handle,&st))
		return -1;
	return st.st_size;
}

int kbhit (void)
{
	fd_set	fds;
	struct timeval tv = {0,0};

	SYS_Poll ();
	if (window && Keyboard[0x01])
		return 1;
#ifdef _WIN32
	return 0;						// (a window, no terminal to type in)
#endif
	if (!isatty (0))
		return 0;					// (no terminal to type in)
	FD_ZERO (&fds);
	FD_SET (0,&fds);
	return select (1,&fds,NULL,NULL,&tv) > 0;
}

int getch (void)
{
	unsigned char c;

	if (window && Keyboard[0x01])
		return 27;
	if (read (0,&c,1) != 1)
		return 0;
	return c;
}

int bioskey (int cmd)
{
	while (!LastScan)
		SYS_Sleep ();
	return LastScan << 8;
}

char *ultoa (uint32_t value, char *string, int radix)
{
	char	tmp[40];
	int		i = 0,j = 0,d;

	do
	{
		d = value % radix;
		tmp[i++] = d < 10 ? '0'+d : 'a'+d-10;
		value /= radix;
	} while (value);
	while (i)
		string[j++] = tmp[--i];
	string[j] = 0;
	return string;
}

char *ltoa (int32_t value, char *string, int radix)
{
	if (value < 0 && radix == 10)
	{
		string[0] = '-';
		ultoa (-(uint32_t)value,string+1,radix);
		return string;
	}
	return ultoa ((uint32_t)value,string,radix);
}

char *itoa (int value, char *string, int radix)
{
	return ltoa (value,string,radix);
}

char *strlwr (char *s)
{
	char *p;
	for (p=s;*p;p++)
		*p = tolower ((unsigned char)*p);
	return s;
}

char *strupr (char *s)
{
	char *p;
	for (p=s;*p;p++)
		*p = toupper ((unsigned char)*p);
	return s;
}


/*
=============================================================================

						STARTUP

=============================================================================
*/

static	int		timing;
static	int		nummarks;
static	char	*marknames[32];
static	Uint32	marktimes[32];

void DOS32_Mark (char *what)
{
	if (!timing || nummarks == 32)
		return;
	marknames[nummarks] = what;
	marktimes[nummarks] = SDL_GetTicks ();
	nummarks++;
}

void DOS32_WriteMarks (void)
{
	FILE	*f;
	int		i;

	if (!timing || !nummarks)
		return;
	f = fopen ("STARTUP.TXT","w");
	if (!f)
		return;
	fprintf (f,"Seconds since main() started:\n");
	for (i=0;i<nummarks;i++)
		fprintf (f,"%7.2f  %s\n",(marktimes[i]-marktimes[0])/1000.0,marknames[i]);
	fclose (f);
	nummarks = 0;
}

int32_t DOS32_MemoryFree (void)
{
	return 16*1024*1024;
}

void DOS32_Startup (void)
{
	if (HasArg ("-timing"))
		timing = 1;
	DOS32_Mark ("main");
}

static void Shutdown (void)
{
	while (intdepth > 0)		// (Quit from inside a DISABLE ... RESTORE)
		enable ();
	timerquit = 1;
	if (timerthread)
		SDL_WaitThread (timerthread,NULL);
	timerthread = NULL;
	SYS_MidiShutdown ();
	if (audiodev)
		SDL_CloseAudioDevice (audiodev);
	audiodev = 0;
	SDL_Quit ();
}

static void Interrupt (int sig)
{
	interrupted = 1;			// Quit, cleanly, from the main thread
	signal (sig,SIG_DFL);		// (a second Ctrl+C: right away)
}

static void Fault (int sig)
{
	signal (sig,SIG_DFL);
	SDL_SetRelativeMouseMode (SDL_FALSE);
	raise (sig);
}

//
// The game writes into its string constants now and then (US_Print puts
// a 0 at each newline and puts it back), which DOS never minded: make
// the program's read-only data writable, as it was there
//
#ifdef _WIN32
static void WritableConstants (void)
{
	unsigned char		*base = (unsigned char *)GetModuleHandle (NULL);
	IMAGE_NT_HEADERS	*nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION (nt);
	DWORD				old;
	int					i;

	for (i=0;i<nt->FileHeader.NumberOfSections;i++,sec++)
		if (!(sec->Characteristics & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE))
		&& (sec->Characteristics & IMAGE_SCN_MEM_READ) && sec->Misc.VirtualSize)
			VirtualProtect (base + sec->VirtualAddress,sec->Misc.VirtualSize,PAGE_READWRITE,&old);
}

// Quit with an error: a window says it (there's no terminal to print it on)
void SYS_ErrorBox (const char *error)
{
	char	text[1400],dir[MAX_PATH];

	if (!getcwd (dir,sizeof(dir)))
		strcpy (dir,"?");
	if (strstr (error,"DATA FILES") || strstr (error,"data files"))
		snprintf (text,sizeof(text),"%s\n\nCopy the data files of your copy of the game "
			"(AUDIOHED, AUDIOT, GAMEMAPS, MAPHEAD, VGADICT, VGAGRAPH, VGAHEAD, VSWAP) "
			"into this folder:\n\n%s\n\n(Steam: Wolfenstein 3D\\base; GOG: the game's folder.) "
			"Or run the game with -data FOLDER.",error,dir);
	else
		snprintf (text,sizeof(text),"%s",error);
	MessageBoxA (NULL,text,"Wolfenstein 3-D",MB_OK | MB_ICONERROR);
}
#else
void SYS_ErrorBox (const char *error)
{
	// (the terminal has it)
}

static int WritableConstants (struct dl_phdr_info *info, size_t size, void *data)
{
	int			i;
	uintptr_t	start,end;
	long		page = sysconf (_SC_PAGESIZE);

	if (info->dlpi_name && info->dlpi_name[0])
		return 1;					// the program is the first, nameless one
	for (i=0;i<info->dlpi_phnum;i++)
	{
		const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
		if (ph->p_type != PT_LOAD || (ph->p_flags & (PF_W|PF_X)))
			continue;
		start = (info->dlpi_addr + ph->p_vaddr) & ~(uintptr_t)(page-1);
		end = (info->dlpi_addr + ph->p_vaddr + ph->p_memsz + page-1) & ~(uintptr_t)(page-1);
		mprotect ((void *)start,end-start,PROT_READ|PROT_WRITE);
	}
	return 1;
}
#endif

int main (int argc, char **argv)
{
	int		i;

	_argc = argc;
	_argv = argv;
#ifdef _WIN32
	{
		WSADATA	wsa;
		WSAStartup (MAKEWORD (2,2),&wsa);
	}
	setvbuf (stdout,NULL,_IONBF,0);
	// files are binary unless they say otherwise, as Borland's (and DOS
	// DJGPP's: dos32.c) were: the saved games are written with creat(),
	// which Windows would otherwise open in text mode (every 0Ah byte
	// written as 0Dh 0Ah)
	_fmode = _O_BINARY;
	WritableConstants ();
#else
	setvbuf (stdout,NULL,_IOLBF,0);
	dl_iterate_phdr (WritableConstants,NULL);
#endif

	// -data dir: the game's files (and its CONFIG and saved games) are there
	for (i=1;i<argc-1;i++)
		if (!strcasecmp (argv[i],"-data"))
		{
			if (chdir (argv[i+1]))
			{
				fprintf (stderr,"Can't go to %s: %s\n",argv[i+1],strerror (errno));
				return 1;
			}
		}

	if (SDL_Init (SDL_INIT_TIMER | SDL_INIT_EVENTS))
	{
		fprintf (stderr,"SDL: %s\n",SDL_GetError ());
		return 1;
	}
	intlock = SDL_CreateMutex ();
	mainthread = SDL_ThreadID ();
	atexit (Shutdown);
	signal (SIGSEGV,Fault);
	signal (SIGFPE,Fault);
	signal (SIGABRT,Fault);
	signal (SIGINT,Interrupt);
	signal (SIGTERM,Interrupt);
#ifdef SIGHUP
	signal (SIGHUP,Interrupt);
#endif

	// a mouse driver's vector (the game looks before calling INT 33h)
	_farpokew (0,0x33*4,0x0010);
	_farpokew (0,0x33*4+2,0xf000);
	SYS_lowmem[0xf0010] = 0xcf+1;		// not an IRET

	for (i=1;i<argc;i++)			// (just the help: no need for sound)
		if (!strcmp (argv[i],"/?") || !strcmp (argv[i],"-?") || !strcasecmp (argv[i],"-h")
		|| !strcasecmp (argv[i],"-help") || !strcasecmp (argv[i],"--help"))
			break;
	if (i == argc)
		StartAudio ();
	timerthread = SDL_CreateThread (TimerThread,"timer",NULL);

	return wolf_main ();
}


/*
=============================================================================

						UDP

 wl_net.c's addresses are IPX's 10 bytes (network, node); here they hold
 an IPv4 address and a port: 4 bytes of address, 2 of port (network
 order), the rest 0.

=============================================================================
*/

static	int		netsock = -1;
static	int		netport;

int SYS_NetStartup (int port)
{
	struct sockaddr_in sa;
	int		one = 1;

	netsock = socket (AF_INET,SOCK_DGRAM,0);
	if (netsock < 0)
		return 0;
	UdpQuiet (netsock);
	setsockopt (netsock,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one));
	memset (&sa,0,sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl (INADDR_ANY);
	sa.sin_port = htons (port);
	if (bind (netsock,(struct sockaddr *)&sa,sizeof(sa)))
	{
		fprintf (stderr,"UDP port %d: %s\n",port,SockError (SOCKERR));
		CloseSocket (netsock);
		netsock = -1;
		return 0;
	}
	NonBlocking (netsock);
	netport = port;
	return 1;
}

void SYS_NetShutdown (void)
{
	if (netsock >= 0)
		CloseSocket (netsock);
	netsock = -1;
}

static void ToSockaddr (const unsigned char *addr, struct sockaddr_in *sa)
{
	memset (sa,0,sizeof(*sa));
	sa->sin_family = AF_INET;
	memcpy (&sa->sin_addr.s_addr,addr,4);
	memcpy (&sa->sin_port,addr+4,2);
}

static void FromSockaddr (const struct sockaddr_in *sa, unsigned char *addr)
{
	memset (addr,0,10);
	memcpy (addr,&sa->sin_addr.s_addr,4);
	memcpy (addr+4,&sa->sin_port,2);
}

void SYS_NetSend (const unsigned char *addr, const void *data, int len)
{
	struct sockaddr_in sa;

	if (netsock < 0)
		return;
	ToSockaddr (addr,&sa);
	if (!sa.sin_port)
		return;
	sendto (netsock,data,len,0,(struct sockaddr *)&sa,sizeof(sa));
}

int SYS_NetGet (void *buf, int max, unsigned char *from)
{
	struct sockaddr_in sa;
	socklen_t salen;
	int		n;

	if (netsock < 0)
		return 0;
	for (;;)
	{
		salen = sizeof(sa);
		n = recvfrom (netsock,buf,max,0,(struct sockaddr *)&sa,&salen);
		if (n <= 0)
			return 0;
		if (sa.sin_family != AF_INET)
			continue;
		FromSockaddr (&sa,from);
		return n;
	}
}

void SYS_NetMyAddr (unsigned char *addr)
{
	struct sockaddr_in sa;

	memset (&sa,0,sizeof(sa));
	sa.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	sa.sin_port = htons (netport);
	FromSockaddr (&sa,addr);
}

void SYS_NetBroadcastAddr (unsigned char *addr, int port)
{
	struct sockaddr_in sa;

	memset (&sa,0,sizeof(sa));
	sa.sin_addr.s_addr = htonl (INADDR_BROADCAST);
	sa.sin_port = htons (port ? port : netport);
	FromSockaddr (&sa,addr);
}

//
// SYS_NetInterfaces: each network interface's broadcast address (with
// port), for machines where 255.255.255.255 only goes out of one of them;
// names: a line about them, for the log
//
#ifdef _WIN32
int SYS_NetInterfaces (unsigned char (*addrs)[10], int max, int port, char *names, int nameslen)
{
	IP_ADAPTER_ADDRESSES	*list,*a;
	IP_ADAPTER_UNICAST_ADDRESS *u;
	ULONG		size = 16384;
	struct sockaddr_in sa;
	uint32_t	ip,mask;
	int			n = 0;
	char		one[200];

	names[0] = 0;
	list = malloc (size);
	if (!list)
		return 0;
	if (GetAdaptersAddresses (AF_INET,GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
		| GAA_FLAG_SKIP_DNS_SERVER,NULL,list,&size) != NO_ERROR)
	{
		free (list);
		return 0;
	}
	for (a=list;a;a=a->Next)
	{
		if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;
		for (u=a->FirstUnicastAddress;u;u=u->Next)
		{
			if (u->Address.lpSockaddr->sa_family != AF_INET)
				continue;
			memcpy (&sa,u->Address.lpSockaddr,sizeof(sa));
			ip = ntohl (sa.sin_addr.s_addr);
			mask = u->OnLinkPrefixLength ? 0xffffffffu << (32 - u->OnLinkPrefixLength) : 0;
			snprintf (one,sizeof(one),"%s%s %s",names[0] ? ", " : "",a->AdapterName,inet_ntoa (sa.sin_addr));
			sa.sin_addr.s_addr = htonl (ip | ~mask);
			snprintf (one+strlen (one),sizeof(one)-strlen (one)," (broadcast %s)",inet_ntoa (sa.sin_addr));
			if (n < max)
			{
				sa.sin_port = htons (port ? port : netport);
				FromSockaddr (&sa,addrs[n++]);
			}
			if (strlen (names)+strlen (one) < (size_t)nameslen)
				strcat (names,one);
		}
	}
	free (list);
	return n;
}
#else
int SYS_NetInterfaces (unsigned char (*addrs)[10], int max, int port, char *names, int nameslen)
{
	struct ifaddrs *list,*i;
	struct sockaddr_in sa;
	int		n = 0;
	char	one[100];

	names[0] = 0;
	if (getifaddrs (&list))
		return 0;
	for (i=list;i;i=i->ifa_next)
	{
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP))
			continue;
		snprintf (one,sizeof(one),"%s%s %s",names[0] ? ", " : "",i->ifa_name,
			inet_ntoa (((struct sockaddr_in *)i->ifa_addr)->sin_addr));
		if ((i->ifa_flags & IFF_BROADCAST) && i->ifa_broadaddr && !(i->ifa_flags & IFF_LOOPBACK))
		{
			memcpy (&sa,i->ifa_broadaddr,sizeof(sa));
			snprintf (one+strlen (one),sizeof(one)-strlen (one)," (broadcast %s)",inet_ntoa (sa.sin_addr));
			if (n < max)
			{
				sa.sin_port = htons (port ? port : netport);
				FromSockaddr (&sa,addrs[n++]);
			}
		}
		if (strlen (names)+strlen (one) < (size_t)nameslen)
			strcat (names,one);
	}
	freeifaddrs (list);
	return n;
}
#endif

//
// SYS_NetResolve: "host", "host:port" (the port defaults to defport)
//
int SYS_NetResolve (const char *name, unsigned char *addr, int defport)
{
	char	host[256],*colon;
	int		port = defport;
	struct addrinfo hints,*res;
	struct sockaddr_in sa;

	strncpy (host,name,sizeof(host)-1);
	host[sizeof(host)-1] = 0;
	colon = strrchr (host,':');
	if (colon)
	{
		*colon = 0;
		port = atoi (colon+1);
	}
	memset (&hints,0,sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo (host,NULL,&hints,&res) || !res)
		return 0;
	memcpy (&sa,res->ai_addr,sizeof(sa));
	freeaddrinfo (res);
	sa.sin_port = htons (port);
	FromSockaddr (&sa,addr);
	return 1;
}

void SYS_NetAddrString (const unsigned char *addr, char *str)
{
	struct sockaddr_in sa;

	ToSockaddr (addr,&sa);
	sprintf (str,"%s:%d",inet_ntoa (sa.sin_addr),ntohs (sa.sin_port));
}


/*
=============================================================================

						IPXNET (DOSBox's IPX over UDP)

 Every packet is a whole IPX packet: the 30 byte header (big endian), then
 the data. A node registers with the server by sending a header with
 destination socket 2 and node 0; the server answers with one whose
 destination is the node's address: network 0, node = its IP address and
 UDP port (as the server sees them). The server passes packets on by the
 destination node (all ones: to everybody but the sender).

 -ipxnet host[:port] registers with a server (DOSBox's IPXNET STARTSERVER,
 or another copy of this game); -ipxserver [port] is the server, as well
 as a node (so DOSBox's IPXNET CONNECT can find us).

=============================================================================
*/

#define IPXNETPORT	213
#define IPXSOCKET	0x7135
#define IPXHDR		30
#define MAXIPXNODES	32

static	int		ipxsock = -1;
static	int		ipxserving;
static	struct sockaddr_in ipxserver;
static	unsigned char ipxmyaddr[10];
static	struct sockaddr_in ipxnodes[MAXIPXNODES];	// the server's: who's registered
static	int		numipxnodes;

// a received packet the server is keeping for the local game
static	unsigned char ipxlocal[64][700];
static	int		ipxlocallen[64],ipxlocalhead,ipxlocaltail;

static void IpxHeader (unsigned char *h, int len, const unsigned char *dest, int dsock,
	const unsigned char *src, int ssock)
{
	h[0] = h[1] = 0xff;					// no checksum
	h[2] = len >> 8;
	h[3] = len;
	h[4] = 0;							// transport control
	h[5] = 4;							// packet type: IPX
	memcpy (h+6,dest,10);
	h[16] = dsock >> 8;
	h[17] = dsock;
	memcpy (h+18,src,10);
	h[28] = ssock >> 8;
	h[29] = ssock;
}

static void NodeOf (const struct sockaddr_in *sa, unsigned char *addr)
{
	memset (addr,0,4);					// network 0
	memcpy (addr+4,&sa->sin_addr.s_addr,4);
	memcpy (addr+8,&sa->sin_port,2);
}

static int IsBroadcast (const unsigned char *addr)
{
	int i;

	for (i=4;i<10;i++)
		if (addr[i] != 0xff)
			return 0;
	return 1;
}

static void Keep (const unsigned char *pkt, int len)		// for the local game
{
	int n = (ipxlocalhead+1) & 63;

	if (n == ipxlocaltail || len > (int)sizeof(ipxlocal[0]))
		return;							// (full: lost, as on a network)
	memcpy (ipxlocal[ipxlocalhead],pkt,len);
	ipxlocallen[ipxlocalhead] = len;
	ipxlocalhead = n;
}

//
// Relay: the server passes a packet on (from: the node it came from, or
// NULL for our own)
//
static void Relay (const unsigned char *pkt, int len, const struct sockaddr_in *from)
{
	const unsigned char *dest = pkt+6;
	unsigned char	node[10];
	int		i;

	for (i=0;i<numipxnodes;i++)
	{
		if (from && ipxnodes[i].sin_addr.s_addr == from->sin_addr.s_addr
		&& ipxnodes[i].sin_port == from->sin_port)
			continue;					// not back to the sender
		NodeOf (&ipxnodes[i],node);
		if (IsBroadcast (dest) || !memcmp (dest+4,node+4,6))
			sendto (ipxsock,pkt,len,0,(struct sockaddr *)&ipxnodes[i],sizeof(ipxnodes[i]));
	}
	if (from && (IsBroadcast (dest) || !memcmp (dest+4,ipxmyaddr+4,6)))
		Keep (pkt,len);
}

static void Register (const struct sockaddr_in *from)
{
	unsigned char	h[IPXHDR],node[10],me[10];
	int		i;

	for (i=0;i<numipxnodes;i++)
		if (ipxnodes[i].sin_addr.s_addr == from->sin_addr.s_addr
		&& ipxnodes[i].sin_port == from->sin_port)
			break;
	if (i == numipxnodes)
	{
		if (numipxnodes == MAXIPXNODES)
			return;
		ipxnodes[numipxnodes++] = *from;
		fprintf (stderr,"IPX server: %s:%d connected\n",inet_ntoa (from->sin_addr),ntohs (from->sin_port));
	}
	NodeOf (from,node);
	memcpy (me,ipxmyaddr,10);
	me[3] = 1;							// (DOSBox's server says network 1)
	IpxHeader (h,IPXHDR,node,2,me,2);
	sendto (ipxsock,h,IPXHDR,0,(struct sockaddr *)from,sizeof(*from));
}

int SYS_IpxStartup (const char *server, int serve)
{
	struct sockaddr_in sa;
	unsigned char	h[IPXHDR],zero[10],buf[700];
	socklen_t		salen;
	int		try,n,port;
	char	host[256],*colon;
	struct addrinfo hints,*res;

	ipxsock = socket (AF_INET,SOCK_DGRAM,0);
	if (ipxsock >= 0)
		UdpQuiet (ipxsock);
	if (ipxsock < 0)
		return 0;
	memset (&sa,0,sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl (INADDR_ANY);
	sa.sin_port = htons (serve ? serve : 0);
	if (bind (ipxsock,(struct sockaddr *)&sa,sizeof(sa)))
	{
		if (serve == IPXNETPORT && SOCK_ACCESS (SOCKERR))
		{
			// 213 is for root: the next best thing
			serve = 21300;
			sa.sin_port = htons (serve);
			if (!bind (ipxsock,(struct sockaddr *)&sa,sizeof(sa)))
				goto bound;
		}
		fprintf (stderr,"IPX: UDP port %d: %s\n",serve,SockError (SOCKERR));
		return 0;
	}
bound:

	if (serve)
	{
		// our own node: a made up address no real one has (127.0.0.2)
		ipxserving = 1;
		memset (ipxmyaddr,0,10);
		ipxmyaddr[4] = 127; ipxmyaddr[7] = 2;
		ipxmyaddr[8] = serve >> 8;
		ipxmyaddr[9] = serve;
		if (serve == IPXNETPORT)
			printf ("IPX server on UDP port %d (in DOSBox: IPXNET CONNECT <this machine>)\n",serve);
		else
			printf ("IPX server on UDP port %d (in DOSBox: IPXNET CONNECT <this machine> %d)\n",serve,serve);
		NonBlocking (ipxsock);
		return 1;
	}

	// a client: register with the server
	strncpy (host,server,sizeof(host)-1);
	host[sizeof(host)-1] = 0;
	port = IPXNETPORT;
	colon = strrchr (host,':');
	if (colon)
	{
		*colon = 0;
		port = atoi (colon+1);
	}
	memset (&hints,0,sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo (host,NULL,&hints,&res) || !res)
	{
		fprintf (stderr,"IPX: can't find %s\n",host);
		return 0;
	}
	memcpy (&ipxserver,res->ai_addr,sizeof(ipxserver));
	freeaddrinfo (res);
	ipxserver.sin_port = htons (port);

	memset (zero,0,10);
	IpxHeader (h,IPXHDR,zero,2,zero,2);
	for (try=0;try<6;try++)
	{
		sendto (ipxsock,h,IPXHDR,0,(struct sockaddr *)&ipxserver,sizeof(ipxserver));
		{
			fd_set	fds;
			struct timeval tv = {1,0};
			FD_ZERO (&fds);
			FD_SET (ipxsock,&fds);
			if (select (ipxsock+1,&fds,NULL,NULL,&tv) <= 0)
				continue;
		}
		salen = sizeof(sa);
		n = recvfrom (ipxsock,buf,sizeof(buf),0,(struct sockaddr *)&sa,&salen);
		if (n >= IPXHDR && buf[16] == 0 && buf[17] == 2)
		{
			memcpy (ipxmyaddr,buf+6,10);	// what the server calls us
			NonBlocking (ipxsock);
			return 1;
		}
	}
	fprintf (stderr,"IPX: no answer from the IPX server at %s:%d\n",host,port);
	return 0;
}

void SYS_IpxSend (const unsigned char *addr, const void *data, int len)
{
	unsigned char pkt[IPXHDR+600];

	if (ipxsock < 0 || len > 600)
		return;
	IpxHeader (pkt,IPXHDR+len,addr,IPXSOCKET,ipxmyaddr,IPXSOCKET);
	memcpy (pkt+IPXHDR,data,len);
	if (ipxserving)
		Relay (pkt,IPXHDR+len,NULL);
	else
		sendto (ipxsock,pkt,IPXHDR+len,0,(struct sockaddr *)&ipxserver,sizeof(ipxserver));
}

int SYS_IpxGet (void *buf, int max, unsigned char *from)
{
	unsigned char	pkt[700];
	struct sockaddr_in sa;
	socklen_t		salen;
	int		n;

	if (ipxsock < 0)
		return 0;
	for (;;)
	{
		if (ipxserving)
		{
			// take in what's come, passing it on; then what's for us
			salen = sizeof(sa);
			n = recvfrom (ipxsock,pkt,sizeof(pkt),0,(struct sockaddr *)&sa,&salen);
			if (n >= IPXHDR)
			{
				if (pkt[16] == 0 && pkt[17] == 2 && !pkt[10] && !pkt[11] && !pkt[12] && !pkt[13])
					Register (&sa);
				else if (pkt[16] == 0 && pkt[17] == 2)
					;						// (an IPXNET ping: never mind)
				else
					Relay (pkt,n,&sa);
				continue;
			}
			if (ipxlocaltail == ipxlocalhead)
				return 0;
			n = ipxlocallen[ipxlocaltail];
			memcpy (pkt,ipxlocal[ipxlocaltail],n);
			ipxlocaltail = (ipxlocaltail+1) & 63;
		}
		else
		{
			salen = sizeof(sa);
			n = recvfrom (ipxsock,pkt,sizeof(pkt),0,(struct sockaddr *)&sa,&salen);
			if (n <= 0)
				return 0;
		}
		if (n < IPXHDR || pkt[16] != (IPXSOCKET >> 8) || pkt[17] != (IPXSOCKET & 0xff))
			continue;						// not the game's
		memcpy (from,pkt+18,10);
		n -= IPXHDR;
		if (n > max)
			n = max;
		memcpy (buf,pkt+IPXHDR,n);
		return n;
	}
}

void SYS_IpxMyAddr (unsigned char *addr)
{
	memcpy (addr,ipxmyaddr,10);
}


/*
=============================================================================

						TCP (IPX packets over TCP, through a server)

 For a game over the internet where UDP doesn't get through (a firewall,
 or a router that forwards one port to one machine): IPXNET's packets
 (header, then the data), each with a 2 byte length (big endian) in front,
 over a TCP connection to a server, which passes them on by destination
 as the IPXNET server does. -tcpserver [port] is the server, as well as a
 node; -tcp host[:port] connects to one. The server tells each connection
 its address (the IP address and port it came from, as the node) in the
 first packet, as IPXNET's server answers a registration.

 A packet that won't fit in a connection's send buffer (the other end
 isn't reading) is dropped, as a network would: the game sends its
 controls again anyway.

=============================================================================
*/

#define TCPPORT		27135
#define TCPMAXPKT	(IPXHDR+600)
#define TCPOUTBUF	32768

typedef struct
{
	int		fd;
	struct sockaddr_in sa;			// where it connected from (its node)
	unsigned char in[2*(2+TCPMAXPKT)];
	int		inlen;
	unsigned char out[TCPOUTBUF];
	int		outlen;
} tcpconn_t;

static	int		tcplisten = -1;
static	int		tcpserving;
static	tcpconn_t tcpconns[MAXIPXNODES];	// the server's: who's connected
static	int		numtcpconns;
static	tcpconn_t tcpserverconn = {-1};	// a client's: to the server
static	unsigned char tcpmyaddr[10];

static void TcpSetup (int fd)
{
	int one = 1;

	NonBlocking (fd);
	setsockopt (fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));	// no batching up
}

//
// TcpFlush: send what's waiting; 0: the connection's gone
//
static int TcpFlush (tcpconn_t *c)
{
	int n;

	if (c->fd < 0)
		return 0;
	while (c->outlen)
	{
		n = send (c->fd,c->out,c->outlen,MSG_NOSIGNAL);
		if (n > 0)
		{
			memmove (c->out,c->out+n,c->outlen-n);
			c->outlen -= n;
			continue;
		}
		if (n < 0 && SOCK_AGAIN (SOCKERR))
			return 1;
		return 0;
	}
	return 1;
}

static void TcpQueue (tcpconn_t *c, const unsigned char *pkt, int len)
{
	if (c->fd < 0 || c->outlen+2+len > TCPOUTBUF)
		return;							// backed up: lost
	c->out[c->outlen++] = len >> 8;
	c->out[c->outlen++] = len;
	memcpy (c->out+c->outlen,pkt,len);
	c->outlen += len;
	TcpFlush (c);						// (a dead one is found reading)
}

//
// TcpFrame: the next whole packet from c: its length, 0 none yet, -1 the
// connection's closed (or sent nonsense)
//
static int TcpFrame (tcpconn_t *c, unsigned char *pkt)
{
	int n,len;

	if (c->fd < 0)
		return -1;
	for (;;)
	{
		if (c->inlen >= 2)
		{
			len = (c->in[0] << 8) | c->in[1];
			if (len < IPXHDR || len > TCPMAXPKT)
				return -1;
			if (c->inlen >= 2+len)
			{
				memcpy (pkt,c->in+2,len);
				memmove (c->in,c->in+2+len,c->inlen-2-len);
				c->inlen -= 2+len;
				return len;
			}
		}
		n = recv (c->fd,c->in+c->inlen,sizeof(c->in)-c->inlen,0);
		if (n > 0)
		{
			c->inlen += n;
			continue;
		}
		if (n < 0 && SOCK_AGAIN (SOCKERR))
			return 0;
		return -1;
	}
}

static void TcpAccept (void)
{
	struct sockaddr_in sa;
	socklen_t	salen;
	unsigned char	h[IPXHDR],node[10],me[10];
	tcpconn_t	*c;
	int		fd;

	for (;;)
	{
		salen = sizeof(sa);
		fd = accept (tcplisten,(struct sockaddr *)&sa,&salen);
		if (fd < 0)
			return;
		if (numtcpconns == MAXIPXNODES || sa.sin_family != AF_INET)
		{
			CloseSocket (fd);
			continue;
		}
		TcpSetup (fd);
		c = &tcpconns[numtcpconns++];
		memset (c,0,sizeof(*c));
		c->fd = fd;
		c->sa = sa;
		fprintf (stderr,"TCP server: %s:%d connected\n",inet_ntoa (sa.sin_addr),ntohs (sa.sin_port));

		// its address, as IPXNET's server answers a registration
		NodeOf (&sa,node);
		memcpy (me,tcpmyaddr,10);
		me[3] = 1;
		IpxHeader (h,IPXHDR,node,2,me,2);
		TcpQueue (c,h,IPXHDR);
	}
}

static void TcpDrop (int i)
{
	tcpconn_t *c = &tcpconns[i];

	fprintf (stderr,"TCP server: %s:%d disconnected\n",inet_ntoa (c->sa.sin_addr),ntohs (c->sa.sin_port));
	if (c->fd >= 0)
		CloseSocket (c->fd);
	numtcpconns--;
	if (i != numtcpconns)
		memcpy (c,&tcpconns[numtcpconns],sizeof(*c));
}

//
// TcpRelay: the server passes a packet on (from: the connection it came
// from, or NULL for our own)
//
static void TcpRelay (const unsigned char *pkt, int len, const tcpconn_t *from)
{
	const unsigned char *dest = pkt+6;
	unsigned char	node[10];
	int		i;

	for (i=0;i<numtcpconns;i++)
	{
		if (&tcpconns[i] == from)
			continue;					// not back to the sender
		NodeOf (&tcpconns[i].sa,node);
		if (IsBroadcast (dest) || !memcmp (dest+4,node+4,6))
			TcpQueue (&tcpconns[i],pkt,len);
	}
	if (from && (IsBroadcast (dest) || !memcmp (dest+4,tcpmyaddr+4,6)))
		Keep (pkt,len);
}

int SYS_TcpStartup (const char *server, int serve)
{
	struct sockaddr_in sa;
	unsigned char	buf[TCPMAXPKT];
	char	host[256],*colon;
	struct addrinfo hints,*res;
	int		port,n,fd,err,one = 1;
	socklen_t	errlen;
	fd_set	fds;
	struct timeval tv;
	Uint32	start;

	if (serve)
	{
		tcplisten = socket (AF_INET,SOCK_STREAM,0);
		if (tcplisten < 0)
			return 0;
		setsockopt (tcplisten,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
		memset (&sa,0,sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = htonl (INADDR_ANY);
		sa.sin_port = htons (serve);
		if (bind (tcplisten,(struct sockaddr *)&sa,sizeof(sa)) || listen (tcplisten,8))
		{
			fprintf (stderr,"TCP port %d: %s\n",serve,SockError (SOCKERR));
			CloseSocket (tcplisten);
			tcplisten = -1;
			return 0;
		}
		NonBlocking (tcplisten);

		// our own node: a made up address no real one has (127.0.0.2)
		tcpserving = 1;
		memset (tcpmyaddr,0,10);
		tcpmyaddr[4] = 127; tcpmyaddr[7] = 2;
		tcpmyaddr[8] = serve >> 8;
		tcpmyaddr[9] = serve;
		printf ("TCP server on port %d (the others: -tcp <this machine>%s)\n",serve,
			serve == TCPPORT ? "" : ":<port>");
		return 1;
	}

	// a client: connect, and wait for our address
	strncpy (host,server,sizeof(host)-1);
	host[sizeof(host)-1] = 0;
	port = TCPPORT;
	colon = strrchr (host,':');
	if (colon)
	{
		*colon = 0;
		port = atoi (colon+1);
	}
	memset (&hints,0,sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo (host,NULL,&hints,&res) || !res)
	{
		fprintf (stderr,"TCP: can't find %s\n",host);
		return 0;
	}
	memcpy (&sa,res->ai_addr,sizeof(sa));
	freeaddrinfo (res);
	sa.sin_port = htons (port);

	fd = socket (AF_INET,SOCK_STREAM,0);
	if (fd < 0)
		return 0;
	TcpSetup (fd);
	if (connect (fd,(struct sockaddr *)&sa,sizeof(sa)) && !SOCK_INPROGRESS (SOCKERR))
	{
		fprintf (stderr,"TCP: %s:%d: %s\n",host,port,SockError (SOCKERR));
		CloseSocket (fd);
		return 0;
	}
	FD_ZERO (&fds);
	FD_SET (fd,&fds);
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	err = ETIMEDOUT;
	errlen = sizeof(err);
	if (select (fd+1,NULL,&fds,NULL,&tv) > 0)
		getsockopt (fd,SOL_SOCKET,SO_ERROR,&err,&errlen);
	if (err)
	{
		fprintf (stderr,"TCP: %s:%d: %s\n",host,port,SockError (err));
		CloseSocket (fd);
		return 0;
	}

	memset (&tcpserverconn,0,sizeof(tcpserverconn));
	tcpserverconn.fd = fd;
	tcpserverconn.sa = sa;
	start = SDL_GetTicks ();
	while (SDL_GetTicks () - start < 10000)
	{
		n = TcpFrame (&tcpserverconn,buf);
		if (n < 0)
			break;
		if (n >= IPXHDR && buf[16] == 0 && buf[17] == 2)
		{
			memcpy (tcpmyaddr,buf+6,10);	// what the server calls us
			return 1;
		}
		if (!n)
		{
			FD_ZERO (&fds);
			FD_SET (fd,&fds);
			tv.tv_sec = 0;
			tv.tv_usec = 100000;
			select (fd+1,&fds,NULL,NULL,&tv);
		}
	}
	fprintf (stderr,"TCP: no answer from the game's server at %s:%d (is it -tcpserver?)\n",host,port);
	CloseSocket (fd);
	tcpserverconn.fd = -1;
	return 0;
}

void SYS_TcpShutdown (void)
{
	Uint32	start = SDL_GetTicks ();
	int		i,waiting;

	// what's still to go (the goodbyes) gets a moment
	do
	{
		waiting = 0;
		if (tcpserverconn.fd >= 0 && tcpserverconn.outlen && TcpFlush (&tcpserverconn))
			waiting = 1;
		for (i=0;i<numtcpconns;i++)
			if (tcpconns[i].outlen && TcpFlush (&tcpconns[i]))
				waiting = 1;
		if (waiting)
			SDL_Delay (5);
	} while (waiting && SDL_GetTicks () - start < 500);

	if (tcpserverconn.fd >= 0)
		CloseSocket (tcpserverconn.fd);
	tcpserverconn.fd = -1;
	for (i=0;i<numtcpconns;i++)
		CloseSocket (tcpconns[i].fd);
	numtcpconns = 0;
	if (tcplisten >= 0)
		CloseSocket (tcplisten);
	tcplisten = -1;
}

void SYS_TcpSend (const unsigned char *addr, const void *data, int len)
{
	unsigned char pkt[TCPMAXPKT];

	if (len > TCPMAXPKT-IPXHDR)
		return;
	IpxHeader (pkt,IPXHDR+len,addr,IPXSOCKET,tcpmyaddr,IPXSOCKET);
	memcpy (pkt+IPXHDR,data,len);
	if (tcpserving)
		TcpRelay (pkt,IPXHDR+len,NULL);
	else
		TcpQueue (&tcpserverconn,pkt,IPXHDR+len);
}

int SYS_TcpGet (void *buf, int max, unsigned char *from)
{
	static	int	toldgone;
	unsigned char	pkt[TCPMAXPKT];
	int		i,n;

	for (;;)
	{
		if (tcpserving)
		{
			// take in what's come, passing it on; then what's for us
			TcpAccept ();
			for (i=0;i<numtcpconns;)
			{
				tcpconn_t *c = &tcpconns[i];

				if (!TcpFlush (c))
				{
					TcpDrop (i);
					continue;
				}
				while ((n = TcpFrame (c,pkt)) > 0)
					if (pkt[16] != 0 || pkt[17] != 2)
						TcpRelay (pkt,n,c);
				if (n < 0)
				{
					TcpDrop (i);
					continue;
				}
				i++;
			}
			if (ipxlocaltail == ipxlocalhead)
				return 0;
			n = ipxlocallen[ipxlocaltail];
			memcpy (pkt,ipxlocal[ipxlocaltail],n);
			ipxlocaltail = (ipxlocaltail+1) & 63;
		}
		else
		{
			TcpFlush (&tcpserverconn);
			n = TcpFrame (&tcpserverconn,pkt);
			if (n < 0 && tcpserverconn.fd >= 0)
			{
				if (!toldgone)
					fprintf (stderr,"TCP: the server closed the connection\n");
				toldgone = 1;
				CloseSocket (tcpserverconn.fd);
				tcpserverconn.fd = -1;
			}
			if (n <= 0)
				return 0;
		}
		if (n < IPXHDR || pkt[16] != (IPXSOCKET >> 8) || pkt[17] != (IPXSOCKET & 0xff))
			continue;						// not the game's
		memcpy (from,pkt+18,10);
		n -= IPXHDR;
		if (n > max)
			n = max;
		memcpy (buf,pkt+IPXHDR,n);
		return n;
	}
}

void SYS_TcpMyAddr (unsigned char *addr)
{
	memcpy (addr,tcpmyaddr,10);
}
