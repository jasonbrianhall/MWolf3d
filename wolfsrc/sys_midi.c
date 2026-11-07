// SYS_MIDI.C -- the MPU-401 of the PC the game thinks it runs on (Linux)
//
// id_midi.c drives an MPU-401 at 330h in UART mode, as on DOS. Here
// sys_linux.c's inportb / outportb hand its two ports to this file, which
// answers like one (as DOSBox's does) and gives the MIDI messages to
//
//	opl			a General MIDI synth on an OPL3 in software (midiopl.cpp):
//				the "classic MIDI" sound of a Sound Blaster 16. Always there,
//				and the default
//	fluidsynth	FluidSynth with a General MIDI SoundFont (-soundfont FILE,
//				else FluidSynth's default one, else the usual places).
//				libfluidsynth is opened when it's wanted: not needed to
//				build or to run
//	alsa		the system's MIDI: an ALSA sequencer port (the kernel's,
//				/dev/snd/seq: a hardware synth, FluidSynth or TiMidity
//				running as a server, Munt...). "alsa" takes the first port
//				that makes sound; alsa:CLIENT:PORT or alsa:NAME picks one
//	/dev/...	a raw MIDI device (/dev/midi1, /dev/snd/midiC1D0): the bytes
//				as they are
//
// -mididev picks one; else CONFIG's (SETUP, wl_main.c's mididev). If it
// can't be opened, it's the OPL3. The two synths are mixed with the rest
// of the sound (sys_linux.c's MakeSamples), in step with the timer.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sound/asequencer.h>

extern	int		_argc;
extern	char	**_argv;
extern	int		mididev;			// CONFIG: 0 opl, 1 fluidsynth, 2 alsa
void	GameLog (char *fmt, ...);
int		disable (void);
int		enable (void);

void	MIDIOPL_Init (int rate);
void	MIDIOPL_Reset (void);
void	MIDIOPL_Message (int status, int d1, int d2);
void	MIDIOPL_SysEx (const unsigned char *p, int len);
void	MIDIOPL_Generate (int32_t *buf, int frames);

enum {DEV_NONE,DEV_OPL,DEV_FLUID,DEV_SEQ,DEV_RAW};

static	int		dev = DEV_NONE;
static	int		rate = 44100;
static	char	devname[300];

/*
=============================================================================

						FLUIDSYNTH

=============================================================================
*/

static	void	*fluidlib,*fluidsettings,*fluidsynth;
static	void	*(*f_new_settings) (void);
static	int		(*f_settings_setnum) (void *,const char *,double);
static	int		(*f_settings_setint) (void *,const char *,int);
static	int		(*f_settings_getstr_default) (void *,const char *,char **);
static	void	*(*f_new_synth) (void *);
static	int		(*f_sfload) (void *,const char *,int);
static	int		(*f_noteon) (void *,int,int,int);
static	int		(*f_noteoff) (void *,int,int);
static	int		(*f_cc) (void *,int,int,int);
static	int		(*f_program_change) (void *,int,int);
static	int		(*f_pitch_bend) (void *,int,int);
static	int		(*f_channel_pressure) (void *,int,int);
static	int		(*f_key_pressure) (void *,int,int,int);
static	int		(*f_sysex) (void *,const char *,int,char *,int *,int *,int);
static	int		(*f_system_reset) (void *);
static	int		(*f_write_s16) (void *,int,void *,int,int,void *,int,int);
static	void	(*f_delete_synth) (void *);
static	void	(*f_delete_settings) (void *);

// -name VALUE, -name=VALUE (or /name, name:VALUE...), as wl_main.c takes them
static char *Arg (const char *name)
{
	int		i,len = strlen (name);
	char	*a;

	for (i = 1;i < _argc;i++)
	{
		a = _argv[i];
		while (*a == '-' || *a == '/')
			a++;
		if (strncasecmp (a,name,len))
			continue;
		if (!a[len] && i+1 < _argc)
			return _argv[i+1];
		if (a[len] == '=' || a[len] == ':')
			return a+len+1;
	}
	return NULL;
}

static int Exists (const char *path)
{
	return path && *path && access (path,R_OK) == 0;
}

static void CloseFluid (void)
{
	if (fluidsynth)
		f_delete_synth (fluidsynth);
	if (fluidsettings)
		f_delete_settings (fluidsettings);
	fluidsynth = fluidsettings = NULL;
}

static int OpenFluid (void)
{
	static const char *libs[] = {"libfluidsynth.so.3","libfluidsynth.so.2","libfluidsynth.so.1","libfluidsynth.so",NULL};
	static const char *fonts[] =
	{
		"/usr/share/sounds/sf2/default-GM.sf2",
		"/usr/share/sounds/sf2/FluidR3_GM.sf2",
		"/usr/share/soundfonts/default.sf2",
		"/usr/share/soundfonts/FluidR3_GM.sf2",
		"/usr/share/soundfonts/FluidR3_GM2-2.sf2",
		"/usr/share/sounds/sf2/TimGM6mb.sf2",
		"/usr/share/sounds/sf2/GeneralUser GS v1.471.sf2",
		"/usr/share/soundfonts/GeneralUser-GS.sf2",
		"/usr/local/share/soundfonts/default.sf2",
		NULL
	};
	const char	**l;
	char		*font = NULL,*def = NULL;
	int			i;

	for (l = libs;*l && !fluidlib;l++)
		fluidlib = dlopen (*l,RTLD_NOW | RTLD_LOCAL);
	if (!fluidlib)
	{
		GameLog ("MIDI: no FluidSynth (libfluidsynth): install it (Debian / Ubuntu: libfluidsynth3)");
		return 0;
	}
#define SYM(var,name)	if (!(*(void **)&var = dlsym (fluidlib,name))) { GameLog ("MIDI: FluidSynth has no %s",name); return 0; }
	SYM (f_new_settings,"new_fluid_settings");
	SYM (f_settings_setnum,"fluid_settings_setnum");
	SYM (f_settings_setint,"fluid_settings_setint");
	SYM (f_new_synth,"new_fluid_synth");
	SYM (f_sfload,"fluid_synth_sfload");
	SYM (f_noteon,"fluid_synth_noteon");
	SYM (f_noteoff,"fluid_synth_noteoff");
	SYM (f_cc,"fluid_synth_cc");
	SYM (f_program_change,"fluid_synth_program_change");
	SYM (f_pitch_bend,"fluid_synth_pitch_bend");
	SYM (f_channel_pressure,"fluid_synth_channel_pressure");
	SYM (f_sysex,"fluid_synth_sysex");
	SYM (f_system_reset,"fluid_synth_system_reset");
	SYM (f_write_s16,"fluid_synth_write_s16");
	SYM (f_delete_synth,"delete_fluid_synth");
	SYM (f_delete_settings,"delete_fluid_settings");
#undef SYM
	*(void **)&f_key_pressure = dlsym (fluidlib,"fluid_synth_key_pressure");	// 2.0 on
	*(void **)&f_settings_getstr_default = dlsym (fluidlib,"fluid_settings_getstr_default");

	fluidsettings = f_new_settings ();
	if (!fluidsettings)
		return 0;
	f_settings_setnum (fluidsettings,"synth.sample-rate",rate);
	f_settings_setnum (fluidsettings,"synth.gain",0.4);
	f_settings_setint (fluidsettings,"synth.polyphony",128);

	// the SoundFont: -soundfont, $SOUNDFONT, FluidSynth's default, the usual places
	font = Arg ("soundfont");
	if (!Exists (font))
	{
		if (font)
			GameLog ("MIDI: no SoundFont %s",font);
		font = getenv ("SOUNDFONT");
	}
	if (!Exists (font) && f_settings_getstr_default
	&& f_settings_getstr_default (fluidsettings,"synth.default-soundfont",&def) == 0 && Exists (def))
		font = def;
	for (i = 0;!Exists (font) && fonts[i];i++)
		font = (char *)fonts[i];
	if (!Exists (font))
	{
		GameLog ("MIDI: FluidSynth, but no General MIDI SoundFont: give one with -soundfont FILE.sf2 "
			"(Debian / Ubuntu: fluid-soundfont-gm)");
		CloseFluid ();
		return 0;
	}

	fluidsynth = f_new_synth (fluidsettings);
	if (!fluidsynth || f_sfload (fluidsynth,font,1) < 0)
	{
		GameLog ("MIDI: FluidSynth couldn't load %s",font);
		CloseFluid ();
		return 0;
	}
	snprintf (devname,sizeof(devname),"FluidSynth, %s",font);
	return 1;
}


/*
=============================================================================

						THE ALSA SEQUENCER (the kernel's)

=============================================================================
*/

static	int		seqfd = -1;
static	int		seqclient,seqport;

static int SeqWanted (struct snd_seq_port_info *pi, const char *client)
{
	unsigned	need = SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE;

	if ((pi->capability & need) != need || (pi->capability & SNDRV_SEQ_PORT_CAP_NO_EXPORT))
		return 0;
	if (pi->addr.client == SNDRV_SEQ_CLIENT_SYSTEM || pi->addr.client == seqclient)
		return 0;
	if (!strcasecmp (client,"Midi Through"))
		return 0;
	return 1;
}

static int OpenSeq (const char *want)
{
	struct snd_seq_client_info	ci,me;
	struct snd_seq_port_info	pi;
	struct snd_seq_port_subscribe sub;
	int		pversion,dclient = -1,dport = -1,score,best = -1;
	char	dname[128] = "";
	char	*end;

	seqfd = open ("/dev/snd/seq",O_RDWR | O_CLOEXEC);
	if (seqfd == -1)
	{
		GameLog ("MIDI: no ALSA sequencer (/dev/snd/seq: %s)",strerror (errno));
		return 0;
	}
	if (ioctl (seqfd,SNDRV_SEQ_IOCTL_PVERSION,&pversion) < 0
	|| ioctl (seqfd,SNDRV_SEQ_IOCTL_CLIENT_ID,&seqclient) < 0)
		goto fail;

	memset (&me,0,sizeof(me));
	me.client = seqclient;
	me.type = USER_CLIENT;
	strcpy (me.name,"Wolfenstein 3-D");
	ioctl (seqfd,SNDRV_SEQ_IOCTL_SET_CLIENT_INFO,&me);

	memset (&pi,0,sizeof(pi));
	pi.addr.client = seqclient;
	strcpy (pi.name,"Music");
	pi.capability = SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_SUBS_READ;
	pi.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | SNDRV_SEQ_PORT_TYPE_APPLICATION;
	pi.midi_channels = 16;
	if (ioctl (seqfd,SNDRV_SEQ_IOCTL_CREATE_PORT,&pi) < 0)
		goto fail;
	seqport = pi.addr.port;

	// alsa:CLIENT:PORT (numbers), or alsa:NAME (part of the client's or
	// port's name), or just alsa: the first port that makes sound, else
	// the first hardware port, else the first at all
	if (want && *want)
	{
		dclient = strtol (want,&end,10);
		if (end != want && (*end == ':' || *end == '.' || !*end))
			dport = *end ? atoi (end+1) : 0;
		else
			dclient = -1;
	}
	if (dclient < 0)
	{
		memset (&ci,0,sizeof(ci));
		ci.client = -1;
		while (ioctl (seqfd,SNDRV_SEQ_IOCTL_QUERY_NEXT_CLIENT,&ci) >= 0)
		{
			memset (&pi,0,sizeof(pi));
			pi.addr.client = ci.client;
			pi.addr.port = -1;
			while (ioctl (seqfd,SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT,&pi) >= 0)
			{
				if (!SeqWanted (&pi,ci.name))
					continue;
				if (want && *want)
					score = (strcasestr (ci.name,want) || strcasestr (pi.name,want)) ? 3 : -1;
				else if (pi.type & (SNDRV_SEQ_PORT_TYPE_SYNTHESIZER | SNDRV_SEQ_PORT_TYPE_SYNTH | SNDRV_SEQ_PORT_TYPE_SAMPLE))
					score = 2;
				else if (pi.type & SNDRV_SEQ_PORT_TYPE_HARDWARE)
					score = 1;
				else
					score = 0;
				if (score > best)
				{
					best = score;
					dclient = pi.addr.client;
					dport = pi.addr.port;
					snprintf (dname,sizeof(dname),"%s: %s",ci.name,pi.name);
				}
			}
		}
	}
	if (dclient < 0)
	{
		if (want && *want)
			GameLog ("MIDI: no ALSA sequencer port \"%s\" (aconnect -o lists them)",want);
		else
			GameLog ("MIDI: no ALSA sequencer port to play on (aconnect -o lists them; "
				"a synth running as a server, like fluidsynth -is -a pulseaudio, makes one)");
		goto fail;
	}

	memset (&sub,0,sizeof(sub));
	sub.sender.client = seqclient;
	sub.sender.port = seqport;
	sub.dest.client = dclient;
	sub.dest.port = dport;
	if (ioctl (seqfd,SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT,&sub) < 0)
	{
		GameLog ("MIDI: couldn't connect to ALSA sequencer port %d:%d (%s)",dclient,dport,strerror (errno));
		goto fail;
	}
	snprintf (devname,sizeof(devname),"ALSA sequencer port %d:%d%s%s",dclient,dport,
		dname[0] ? " " : "",dname);
	return 1;

fail:
	close (seqfd);
	seqfd = -1;
	return 0;
}

static void SeqEvent (struct snd_seq_event *ev, const unsigned char *ext, int extlen)
{
	unsigned char	buf[sizeof(*ev) + 600];
	int				len = sizeof(*ev);

	ev->queue = SNDRV_SEQ_QUEUE_DIRECT;
	ev->source.client = seqclient;
	ev->source.port = seqport;
	ev->dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
	ev->dest.port = SNDRV_SEQ_ADDRESS_UNKNOWN;
	memcpy (buf,ev,sizeof(*ev));
	if (ext && extlen > 0 && extlen <= 600)
	{
		memcpy (buf+len,ext,extlen);
		len += extlen;
	}
	if (write (seqfd,buf,len) != len)
		;						// (a full queue: the event is lost)
}

static void SeqMessage (int status, int d1, int d2)
{
	struct snd_seq_event	ev;
	int		c = status & 15;

	memset (&ev,0,sizeof(ev));
	ev.flags = SNDRV_SEQ_TIME_STAMP_TICK | SNDRV_SEQ_EVENT_LENGTH_FIXED;
	switch (status & 0xf0)
	{
	case 0x80:
	case 0x90:
	case 0xa0:
		ev.type = (status & 0xf0) == 0x80 ? SNDRV_SEQ_EVENT_NOTEOFF
			: (status & 0xf0) == 0x90 ? SNDRV_SEQ_EVENT_NOTEON : SNDRV_SEQ_EVENT_KEYPRESS;
		ev.data.note.channel = c;
		ev.data.note.note = d1;
		ev.data.note.velocity = d2;
		break;
	case 0xb0:
		ev.type = SNDRV_SEQ_EVENT_CONTROLLER;
		ev.data.control.channel = c;
		ev.data.control.param = d1;
		ev.data.control.value = d2;
		break;
	case 0xc0:
		ev.type = SNDRV_SEQ_EVENT_PGMCHANGE;
		ev.data.control.channel = c;
		ev.data.control.value = d1;
		break;
	case 0xd0:
		ev.type = SNDRV_SEQ_EVENT_CHANPRESS;
		ev.data.control.channel = c;
		ev.data.control.value = d1;
		break;
	case 0xe0:
		ev.type = SNDRV_SEQ_EVENT_PITCHBEND;
		ev.data.control.channel = c;
		ev.data.control.value = ((d2 << 7) | d1) - 8192;
		break;
	default:
		return;
	}
	SeqEvent (&ev,NULL,0);
}

static void SeqSysEx (const unsigned char *p, int len)
{
	struct snd_seq_event	ev;

	memset (&ev,0,sizeof(ev));
	ev.type = SNDRV_SEQ_EVENT_SYSEX;
	ev.flags = SNDRV_SEQ_TIME_STAMP_TICK | SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
	ev.data.ext.len = len;
	SeqEvent (&ev,p,len);
}


/*
=============================================================================

						A RAW MIDI DEVICE

=============================================================================
*/

static	int		rawfd = -1;

static int OpenRaw (const char *path)
{
	rawfd = open (path,O_WRONLY | O_CLOEXEC);
	if (rawfd == -1)
	{
		GameLog ("MIDI: can't open %s (%s)",path,strerror (errno));
		return 0;
	}
	snprintf (devname,sizeof(devname),"%s",path);
	return 1;
}

static void RawWrite (const unsigned char *p, int len)
{
	if (write (rawfd,p,len) != len)
		;
}


/*
=============================================================================

						WHICH ONE

=============================================================================
*/

//
// SYS_MidiOpen: what plays the music, opened (the main thread, before a
// song: loading a SoundFont takes a moment)
//
void SYS_MidiOpen (void)
{
	char	*want,*seqwant = NULL;
	int		kind,ok = 0;

	if (dev != DEV_NONE)
		return;

	want = Arg ("mididev");
	if (want)
	{
		if (!strcasecmp (want,"opl") || !strcasecmp (want,"fm") || !strcasecmp (want,"adlib"))
			kind = DEV_OPL;
		else if (!strncasecmp (want,"fluid",5))
			kind = DEV_FLUID;
		else if (want[0] == '/')
			kind = DEV_RAW;
		else if (!strncasecmp (want,"alsa",4) || !strncasecmp (want,"seq",3) || !strcasecmp (want,"system"))
		{
			kind = DEV_SEQ;
			seqwant = strchr (want,':');
			if (seqwant)
				seqwant++;
		}
		else
		{
			kind = DEV_SEQ;			// 128:0, or a name
			seqwant = want;
		}
	}
	else
		kind = mididev == 1 ? DEV_FLUID : mididev == 2 ? DEV_SEQ : DEV_OPL;

	switch (kind)
	{
	case DEV_FLUID:
		ok = OpenFluid ();
		break;
	case DEV_SEQ:
		ok = OpenSeq (seqwant);
		break;
	case DEV_RAW:
		ok = OpenRaw (want);
		break;
	}
	if (!ok)
	{
		if (kind != DEV_OPL)
			GameLog ("MIDI: so the OPL3 synth plays it");
		kind = DEV_OPL;
		MIDIOPL_Init (rate);
		strcpy (devname,"General MIDI on an OPL3 (in software)");
	}
	GameLog ("MIDI: %s",devname);

	disable ();
	dev = kind;
	enable ();
}

void SYS_MidiShutdown (void)
{
	disable ();
	switch (dev)
	{
	case DEV_FLUID:
		CloseFluid ();
		break;
	case DEV_SEQ:
		close (seqfd);
		seqfd = -1;
		break;
	case DEV_RAW:
		close (rawfd);
		rawfd = -1;
		break;
	}
	dev = DEV_NONE;
	enable ();
}

static void Message (int status, int d1, int d2)
{
	int		c = status & 15;

	switch (dev)
	{
	case DEV_OPL:
		MIDIOPL_Message (status,d1,d2);
		break;
	case DEV_SEQ:
		SeqMessage (status,d1,d2);
		break;
	case DEV_FLUID:
		switch (status & 0xf0)
		{
		case 0x80: f_noteoff (fluidsynth,c,d1); break;
		case 0x90: f_noteon (fluidsynth,c,d1,d2); break;
		case 0xa0: if (f_key_pressure) f_key_pressure (fluidsynth,c,d1,d2); break;
		case 0xb0: f_cc (fluidsynth,c,d1,d2); break;
		case 0xc0: f_program_change (fluidsynth,c,d1); break;
		case 0xd0: f_channel_pressure (fluidsynth,c,d1); break;
		case 0xe0: f_pitch_bend (fluidsynth,c,(d2 << 7) | d1); break;
		}
		break;
	}
}

static void SysEx (const unsigned char *p, int len)
{
	switch (dev)
	{
	case DEV_OPL:
		MIDIOPL_SysEx (p,len);
		break;
	case DEV_SEQ:
		SeqSysEx (p,len);
		break;
	case DEV_FLUID:
		if (len > 2)			// without the F0 and F7
			f_sysex (fluidsynth,(const char *)p+1,len-2,NULL,NULL,NULL,0);
		break;
	}
}

// everything quiet (the MPU-401 was reset)
static void Silence (void)
{
	int		c;

	switch (dev)
	{
	case DEV_OPL:
		MIDIOPL_Reset ();
		break;
	case DEV_FLUID:
		f_system_reset (fluidsynth);
		break;
	case DEV_SEQ:
		for (c = 0;c < 16;c++)
			SeqMessage (0xb0 | c,123,0);
		break;
	case DEV_RAW:
		for (c = 0;c < 16;c++)
		{
			unsigned char m[3] = {0xb0 | c,123,0};
			RawWrite (m,3);
		}
		break;
	}
}


/*
=============================================================================

						THE MPU-401 (UART mode)

=============================================================================
*/

static	int		ackwaiting;			// a byte (FEh) for the game to read
static	int		running,need,have;
static	unsigned char data[2];
static	unsigned char sysex[512];
static	int		sysexlen,insysex;

static void Byte (unsigned char b)
{
	if (b >= 0xf8)
		return;						// real time: clock, active sensing...
	if (insysex)
	{
		if (b < 0x80 || b == 0xf7)
		{
			if (sysexlen < (int)sizeof(sysex))
				sysex[sysexlen++] = b;
			if (b == 0xf7)
			{
				insysex = 0;
				if (sysexlen < (int)sizeof(sysex))
					SysEx (sysex,sysexlen);
			}
			return;
		}
		insysex = 0;				// cut short by a status byte
	}
	if (b == 0xf0)
	{
		insysex = 1;
		sysexlen = 0;
		sysex[sysexlen++] = b;
		running = 0;
		return;
	}
	if (b & 0x80)
	{
		running = b < 0xf0 ? b : 0;	// (system common: nothing for a synth)
		need = ((b & 0xe0) == 0xc0) ? 1 : 2;
		have = 0;
		return;
	}
	if (!running)
		return;
	data[have++] = b;
	if (have == need)
	{
		Message (running,data[0],need > 1 ? data[1] : 0);
		have = 0;
	}
}

unsigned char SYS_MpuIn (int port)
{
	if (port & 1)					// status: bit 6 clear, we take bytes; bit 7 clear, one to read
		return ackwaiting ? 0x00 : 0x80;
	if (ackwaiting)
	{
		ackwaiting = 0;
		return 0xfe;
	}
	return 0;
}

void SYS_MpuOut (int port, unsigned char v)
{
	disable ();						// (the synths are the timer thread's too)
	if (port & 1)					// a command: reset, UART mode... all acknowledged
	{
		if (v == 0xff)
		{
			running = have = insysex = 0;
			Silence ();
		}
		ackwaiting = 1;
	}
	else if (dev == DEV_RAW)
		RawWrite (&v,1);
	else if (dev != DEV_NONE)
		Byte (v);
	enable ();
}

//
// SYS_MidiRender: the synth's next frames, interleaved stereo (timer
// thread, "interrupts off"); 0 if this one doesn't make sound here
//
int SYS_MidiRender (int32_t *buf, int frames)
{
	int16_t	s16[2048];
	int		i,n;

	switch (dev)
	{
	case DEV_OPL:
		MIDIOPL_Generate (buf,frames);
		for (i = 0;i < frames*2;i++)
			buf[i] <<= 1;			// next to the AdLib music's level
		return 1;
	case DEV_FLUID:
		while (frames > 0)
		{
			n = frames > 1024 ? 1024 : frames;
			f_write_s16 (fluidsynth,n,s16,0,2,s16,1,2);
			for (i = 0;i < n*2;i++)
				buf[i] = s16[i];
			buf += n*2;
			frames -= n;
		}
		return 1;
	}
	return 0;
}

void SYS_MidiRate (int r)
{
	rate = r;
}
