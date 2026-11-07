//
//	ID_MIDI.C -- General MIDI music through an MPU-401 (see id_midi.h)
//
//	The MPU-401 is put in UART mode and fed the MIDI bytes. A song is a
//	standard MIDI file (type 0 or 1, any division, tempo changes,
//	SMPTE time too, RIFF RMID wrapped or not), read into memory and played
//	from the timer interrupt (700 times a second, the AdLib music's rate),
//	looping like the AdLib music: at the end, or between "loopStart" and
//	"loopEnd" markers (or from a controller 111, as RPG Maker's files do).
//
//	The bytes go into a ring that the timer interrupt sends as fast as
//	the MPU-401 takes them, so nothing waits on its 31250 baud.
//
//	Which file: the AdLib song in AUDIOT carries its name after the IMF
//	data (CORNER, NAZI_NOR...): MUSIC\NAME (wolfmidi's layout, no
//	extension), MUSIC\NAME.MID or NAME.MID, whichever is there. Failing
//	that, MUSIC\_INFO (wolfmidi's: the song's IMF length and its name)
//	gives the name. Without a file the AdLib plays the song (id_sd.c).
//

#include <stdint.h>
#include "id_heads.h"
#include "id_midi.h"
void	GameLog (char *fmt, ...);	// wl_net.c: WOLFLOG.TXT
void	NetNotice (char *text);		// wl_net.c: on the screen for a moment, and logged

boolean	MIDIPresent;
word	MIDIPort = 0x330;

#define	mpuData		(MIDIPort)
#define	mpuStatus	(MIDIPort+1)
#define	mpuDRR		0x40		// clear: it takes a byte
#define	mpuDSR		0x80		// clear: it has a byte for us
#define	mpuACK		0xfe

#define	MAXTRACKS	64
#define	MAXSONG		(1024L*1024)

/*
=============================================================================

						THE MPU-401

=============================================================================
*/

#define	RINGSIZE	4096		// a power of two
static	byte			ring[RINGSIZE];
static	volatile word	ringr,ringw;

//
// Queue: a whole message into the ring, or none of it ("interrupts off")
//
static boolean Queue (byte *msg, int len)
{
	word	free,w;

	free = (RINGSIZE-1) - ((ringw - ringr) & (RINGSIZE-1));
	if (len > free)
		return false;
	w = ringw;
	while (len--)
	{
		ring[w] = *msg++;
		w = (w+1) & (RINGSIZE-1);
	}
	ringw = w;
	return true;
}

//
// MIDI_Flush: as much of the ring as the MPU-401 takes now. One without a
// buffer takes a byte every 320 microseconds (31250 baud): it's waited
// for a little (a few hundred status reads, about a microsecond each on
// the ISA bus), then the rest goes next tick
//
void MIDI_Flush (void)
{
	byte	s;
	int		polls;

	if (!MIDIPresent)
	{
		ringr = ringw;
		return;
	}
	for (polls = 0;ringr != ringw && polls < 600;polls++)
	{
		s = inportb (mpuStatus);
		if (!(s & mpuDSR))
		{
			inportb (mpuData);			// something it says (active sensing...)
			continue;
		}
		if (s & mpuDRR)
			continue;					// busy
		outportb (mpuData,ring[ringr]);
		ringr = (ringr+1) & (RINGSIZE-1);
	}
}

static boolean MPU_WaitWrite (void)
{
	long	i;
	byte	s;

	for (i = 0;i < 100000;i++)
	{
		s = inportb (mpuStatus);
		if (!(s & mpuDRR))
			return true;
		if (!(s & mpuDSR))
			inportb (mpuData);
	}
	return false;
}

static boolean MPU_WaitAck (void)
{
	long	i;

	for (i = 0;i < 100000;i++)
		if (!(inportb (mpuStatus) & mpuDSR) && inportb (mpuData) == mpuACK)
			return true;
	return false;
}

static boolean MPU_Command (byte cmd)
{
	if (!MPU_WaitWrite ())
		return false;
	outportb (mpuStatus,cmd);
	return MPU_WaitAck ();
}

//
// The port: -mpu 330 (hex), else BLASTER's P, else 330h; -nompu: none
//
static char *MPU_Arg (char *name)
{
	int		i,len = strlen (name);
	char	*a;

	for (i = 1;i < _argc;i++)
	{
		a = _argv[i];
		while (*a == '-' || *a == '/')
			a++;
		if (strnicmp (a,name,len))
			continue;
		a += len;
		if (!*a)
			return i+1 < _argc ? _argv[i+1] : "";
		if (*a == '=' || *a == ':')
			return a+1;
	}
	return NULL;
}

static boolean MPU_Off (void)
{
	int		i;
	char	*a;

	for (i = 1;i < _argc;i++)
	{
		a = _argv[i];
		while (*a == '-' || *a == '/')
			a++;
		if (!stricmp (a,"nompu") || !stricmp (a,"nomidi"))
			return true;
	}
	return false;
}

boolean MIDI_Startup (void)
{
	char	*v,*env;
	long	port = 0;
	boolean	forced = false;

	MIDIPresent = false;
	if (MPU_Off ())
	{
		GameLog ("MIDI: off (-nompu)");
		return false;
	}

	v = MPU_Arg ("mpu");
	if (v && *v)
	{
		port = strtol (v,NULL,16);
		forced = true;
	}
	else if ((env = getenv ("BLASTER")) != NULL)
	{
		while (*env)
		{
			while (*env == ' ' || *env == '\t')
				env++;
			if (*env == 'P' || *env == 'p')
				port = strtol (env+1,NULL,16);
			while (*env && *env != ' ' && *env != '\t')
				env++;
		}
	}
	if (port < 0x200 || port > 0x3ff)
		port = 0x330;
#ifdef SYS_SDL
	port = 0x330;				// where sys_midi.c answers
	forced = true;
#endif
	MIDIPort = port;

	if (!forced && inportb (mpuStatus) == 0xff)
	{
		GameLog ("MIDI: no MPU-401 at %X",MIDIPort);
		return false;			// nothing on the bus
	}
	if (!MPU_WaitWrite ())
	{
		GameLog ("MIDI: the MPU-401 at %X doesn't take commands",MIDIPort);
		return false;
	}
	if (!MPU_Command (0xff))	// reset; in UART mode already, it may not answer
		MPU_Command (0xff);
	MPU_Command (0x3f);			// UART mode (clones may not answer either)
	if (!MPU_WaitWrite ())
	{
		GameLog ("MIDI: the MPU-401 at %X doesn't take data",MIDIPort);
		return false;
	}

	ringr = ringw = 0;
	MIDIPresent = true;
	GameLog ("MIDI: MPU-401 at %X",MIDIPort);
	return true;
}


/*
=============================================================================

						THE SONG

=============================================================================
*/

typedef struct
{
	byte			*pos,*end;
	unsigned long	next;			// its next event's tick
	byte			running;
	byte			done;
} mtrack_t;

typedef struct
{
	mtrack_t		t[MAXTRACKS];
	unsigned long	tick;			// now, in the song's ticks
	unsigned long	tempo;			// microseconds a quarter note
} mstate_t;

static	byte			*song;
static	long			songlen;
static	int				ntracks;
static	byte			*trackstart[MAXTRACKS],*trackend[MAXTRACKS];
static	long			division;		// ticks a quarter note; < 0: SMPTE
static	mstate_t		cur,loopstate;
static	boolean			haveloop;
static	uint64_t acc;			// time not yet made into ticks
static	volatile boolean playing,paused;
static	byte			held[16][16];	// notes that are on, a bit each
static	word			pedals;			// channels with the sustain pedal down
static	char			songname[64];

static unsigned long VarLen (byte **pp, byte *end)
{
	unsigned long	v = 0;
	int				i;
	byte			*p = *pp;

	for (i = 0;i < 4 && p < end;i++)
	{
		v = (v << 7) | (*p & 0x7f);
		if (!(*p++ & 0x80))
			break;
	}
	*pp = p;
	return v;
}

static void Rewind (void)
{
	int			i;
	mtrack_t	*t;
	byte		*p;

	for (i = 0;i < ntracks;i++)
	{
		t = &cur.t[i];
		p = trackstart[i];
		t->end = trackend[i];
		t->running = 0;
		t->done = p >= t->end;
		t->next = t->done ? 0 : VarLen (&p,t->end);
		t->pos = p;
	}
	cur.tick = 0;
	cur.tempo = 500000;
	haveloop = false;
}

//
// Send: a channel message, keeping track of the notes that are on
//
static void Send (byte *msg, int len)
{
	int		ch = msg[0] & 15,note = msg[1] & 127;

	switch (msg[0] & 0xf0)
	{
	case 0x90:
		if (msg[2])
		{
			held[ch][note >> 3] |= 1 << (note & 7);
			break;
		}
		// note on, velocity 0: off
	case 0x80:
		held[ch][note >> 3] &= ~(1 << (note & 7));
		break;
	case 0xb0:
		if (note == 64)
		{
			if (msg[2] >= 64)
				pedals |= 1 << ch;
			else
				pedals &= ~(1 << ch);
		}
		break;
	}
	Queue (msg,len);
}

//
// NotesOff: every note that's on, off (and the sustain pedal up)
//
static void NotesOff (void)
{
	byte	msg[3];
	int		ch,n;

	for (ch = 0;ch < 16;ch++)
	{
		for (n = 0;n < 128;n++)
			if (held[ch][n >> 3] & (1 << (n & 7)))
			{
				msg[0] = 0x80 | ch;
				msg[1] = n;
				msg[2] = 64;
				Queue (msg,3);
			}
		memset (held[ch],0,sizeof(held[ch]));
		if (pedals & (1 << ch))
		{
			msg[0] = 0xb0 | ch;
			msg[1] = 64;		// sustain pedal up
			msg[2] = 0;
			Queue (msg,3);
		}
	}
	pedals = 0;
}

//
// ResetChannels: all notes off, controllers reset, volume as at power on
//
static void ResetChannels (void)
{
	byte	msg[9];
	int		ch;

	NotesOff ();
	for (ch = 0;ch < 16;ch++)
	{
		msg[0] = 0xb0 | ch;
		msg[1] = 123;			// all notes off
		msg[2] = 0;
		msg[3] = 121;			// reset controllers (running status)
		msg[4] = 0;
		msg[5] = 7;				// volume
		msg[6] = 100;
		msg[7] = 10;			// pan, the middle
		msg[8] = 64;
		Queue (msg,9);
	}
}

static boolean IsMarker (byte *text, unsigned long len, char *word)
{
	int		l = strlen (word);

	while (len && (*text == ' ' || *text == '['))
	{
		text++;
		len--;
	}
	return len >= l && !strnicmp ((char *)text,word,l);
}

//
// DoEvent: the track's next event; then when the one after it is due
//
static void DoEvent (mtrack_t *t)
{
	byte			*p = t->pos,*end = t->end;
	byte			status,type,msg[3];
	unsigned long	len;
	int				n,mark = 0;

	status = *p;
	if (status & 0x80)
		p++;
	else if (t->running)
		status = t->running;
	else
	{
		t->done = 1;			// data with no status: broken
		return;
	}

	if (status < 0xf0)
	{
		n = ((status & 0xe0) == 0xc0) ? 1 : 2;	// program, channel pressure
		if (p + n > end)
		{
			t->done = 1;
			return;
		}
		t->running = status;
		msg[0] = status;
		msg[1] = p[0];
		msg[2] = n > 1 ? p[1] : 0;
		p += n;
		if ((status & 0xf0) == 0xb0 && msg[1] == 111)
			mark = 1;			// loop start, RPG Maker style
		Send (msg,n+1);
	}
	else if (status == 0xf0 || status == 0xf7)
	{
		t->running = 0;
		len = VarLen (&p,end);
		if (len > (unsigned long)(end - p))
		{
			t->done = 1;
			return;
		}
		if (len < 512)
		{
			byte	buf[513];

			n = 0;
			if (status == 0xf0)
				buf[n++] = 0xf0;
			memcpy (buf+n,p,len);
			Queue (buf,n+len);
		}
		p += len;
	}
	else if (status == 0xff)
	{
		if (p >= end)
		{
			t->done = 1;
			return;
		}
		type = *p++;
		len = VarLen (&p,end);
		if (len > (unsigned long)(end - p))
		{
			t->done = 1;
			return;
		}
		switch (type)
		{
		case 0x2f:				// end of track
			t->pos = p + len;
			t->done = 1;
			return;
		case 0x51:				// tempo
			if (len >= 3 && (p[0] | p[1] | p[2]))
				cur.tempo = ((unsigned long)p[0] << 16) | (p[1] << 8) | p[2];
			break;
		case 0x01:				// text
		case 0x06:				// marker
			if (IsMarker (p,len,"loopstart") || IsMarker (p,len,"loop start"))
				mark = 1;
			else if (IsMarker (p,len,"loopend") || IsMarker (p,len,"loop end"))
				mark = 2;
			break;
		}
		p += len;
	}
	else
	{
		t->done = 1;			// F1-FE don't belong in a file
		return;
	}

	if (p >= end)
	{
		t->pos = p;
		t->done = 1;
		return;
	}
	t->next += VarLen (&p,end);
	t->pos = p;

	if (mark == 1)
	{
		loopstate = cur;
		haveloop = true;
	}
	else if (mark == 2 && haveloop)
	{
		NotesOff ();
		cur = loopstate;
	}
}

//
// MIDI_Service: the song's events up to now (the timer interrupt, rate
// times a second), then send what's waiting
//
void MIDI_Service (word rate)
{
	uint64_t	per;
	unsigned long		start,best;
	mtrack_t			*t,*next;
	int					i,events,loops;

	if (playing && !paused && rate)
	{
		if (division > 0)
		{
			acc += (uint64_t)division * 1000000;
			per = (uint64_t)rate * cur.tempo;
		}
		else
		{
			// SMPTE: frames a second (24, 25, 29 = 29.97, 30) times ticks a frame
			acc += (uint64_t)(-(division >> 8) & 0x7f) * (division & 0xff);
			per = rate;
		}
		if (acc >= per)
		{
			cur.tick += (unsigned long)(acc / per);
			acc %= per;
		}

		loops = 0;
		for (events = 0;events < 4000;events++)
		{
			next = NULL;
			best = 0;
			for (i = 0,t = cur.t;i < ntracks;i++,t++)
				if (!t->done && (!next || t->next < best))
				{
					next = t;
					best = t->next;
				}
			if (!next)
			{
				// the end: round again, from loopStart if there was one
				start = haveloop ? loopstate.tick : 0;
				if (cur.tick <= start || ++loops > 1)
				{
					NotesOff ();
					playing = false;	// nothing to it: don't spin
					break;
				}
				NotesOff ();
				if (haveloop)
					cur = loopstate;
				else
					Rewind ();
				acc = 0;
				continue;
			}
			if (best > cur.tick)
				break;
			DoEvent (next);
		}
	}

	MIDI_Flush ();
}


/*
=============================================================================

						LOADING

=============================================================================
*/

static boolean ReadSong (char *name)
{
	int		h;
	long	len;
	byte	*buf;

	h = open (name,O_RDONLY | O_BINARY);
	if (h == -1)
		return false;
	len = filelength (h);
	if (len < 22 || len > MAXSONG || !(buf = malloc (len)))
	{
		close (h);
		return false;
	}
	if (read (h,buf,len) != len)
	{
		free (buf);
		close (h);
		return false;
	}
	close (h);
	song = buf;
	songlen = len;
	return true;
}

static unsigned long BE32 (byte *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | (p[2] << 8) | p[3];
}

//
// ParseSong: the header and where the tracks are; false if it isn't MIDI
//
static boolean ParseSong (void)
{
	byte			*p = song,*end = song + songlen;
	unsigned long	len;
	int				format,count;

	// RIFF RMID: the MIDI file is the "data" chunk
	if (songlen > 20 && !memcmp (p,"RIFF",4) && !memcmp (p+8,"RMID",4))
	{
		p += 12;
		while (p + 8 <= end && memcmp (p,"data",4))
		{
			len = p[4] | (p[5] << 8) | ((unsigned long)p[6] << 16) | ((unsigned long)p[7] << 24);
			if (len > (unsigned long)(end - p - 8))
				return false;
			p += 8 + ((len + 1) & ~1);
		}
		if (p + 8 > end)
			return false;
		p += 8;
	}

	if (p + 14 > end || memcmp (p,"MThd",4) || (len = BE32 (p+4)) < 6 || len > (unsigned long)(end - p - 8))
		return false;
	format = (p[8] << 8) | p[9];
	count = (p[10] << 8) | p[11];
	division = (p[12] << 8) | p[13];
	if (division & 0x8000)
		division -= 0x10000;		// SMPTE: -frames a second, ticks a frame
	if (format > 2 || !count || !division || (division < 0 && !(division & 0xff)))
		return false;
	if (format == 2)
		count = 1;					// separate songs: the first
	p += 8 + len;

	ntracks = 0;
	while (p + 8 <= end && ntracks < count && ntracks < MAXTRACKS)
	{
		len = BE32 (p+4);
		p += 8;
		if (len > (unsigned long)(end - p))
			len = end - p;			// cut short: play what's there
		if (!memcmp (p-8,"MTrk",4))
		{
			trackstart[ntracks] = p;
			trackend[ntracks] = p + len;
			ntracks++;
		}
		p += len;
	}
	return ntracks > 0;
}

//
// SongName: the AdLib song's name, which Muse left after the IMF data
//
static boolean SongName (MusicGroup *music, char *name)
{
	int		i,chunk;
	long	size = 0;
	byte	*tag;

	for (chunk = STARTMUSIC;chunk < NUMSNDCHUNKS;chunk++)
		if (audiosegs[chunk] == (byte *)music)
		{
			size = audiostarts[chunk+1] - audiostarts[chunk];
			break;
		}
	if (size < (long)music->length + 4 + 2)
		return false;

	tag = (byte *)music + 2 + music->length + 2;
	for (i = 0;i < 9 && 2 + music->length + 2 + i < size;i++)
	{
		if (!tag[i])
			break;
		if (!isalnum (tag[i]) && tag[i] != '_' && tag[i] != '-')
			return false;
		name[i] = toupper (tag[i]);
	}
	if (!i || i > 8 || tag[i])
		return false;
	name[i] = 0;
	return true;
}

//
// InfoName: wolfmidi's MUSIC\_INFO: the IMF length (a word) and the name
// (8 characters) of each song
//
static boolean InfoName (char *dir, word length, char *name)
{
	char	path[32];
	byte	rec[10];
	int		h,i;

	sprintf (path,"%s/_INFO",dir);
	h = open (path,O_RDONLY | O_BINARY);
	if (h == -1)
		return false;
	while (read (h,rec,10) == 10 && (rec[0] | rec[1]))
		if ((rec[0] | (rec[1] << 8)) == length)
		{
			for (i = 0;i < 8 && rec[2+i];i++)
				name[i] = rec[2+i];
			name[i] = 0;
			close (h);
			return i > 0;
		}
	close (h);
	return false;
}

static boolean FindSong (char *name)
{
	static	char	*dirs[] = {"MUSIC","music","Music",NULL};
	char	**d;

	for (d = dirs;*d;d++)
	{
		sprintf (songname,"%s/%s",*d,name);
		if (ReadSong (songname))
			return true;
		sprintf (songname,"%s/%s.MID",*d,name);
		if (ReadSong (songname))
			return true;
#ifndef SYS_SDL
		break;						// DOS doesn't care about case
#endif
	}
	sprintf (songname,"%s.MID",name);
	return ReadSong (songname);
}

boolean MIDI_LoadSong (MusicGroup *music)
{
	static	boolean	told;
	char	name[16],line[128];
	boolean	found = false;

	strcpy (name,"(no name)");
	MIDI_Stop ();
	if (song)
	{
		free (song);
		song = NULL;
	}
	ntracks = 0;
	if (!MIDIPresent || !music)
		return false;

	if (SongName (music,name))
		found = FindSong (name);
	if (!found && (InfoName ("MUSIC",music->length,name)
#ifdef SYS_SDL
		|| InfoName ("music",music->length,name)
#endif
		))
		found = FindSong (name);
#ifdef SYS_SDL
	if (found)
		SYS_MidiOpen ();			// what plays it (a SoundFont takes a moment to load)
#endif
	if (!found)
	{
		GameLog ("MIDI: no MUSIC/%s, MUSIC/%s.MID or %s.MID",name,name,name);
		sprintf (line,"Music: %s, no MIDI file: AdLib",name);
		NetNotice (line);
		return false;
	}

	if (!ParseSong ())
	{
		GameLog ("MIDI: %s isn't a MIDI file",songname);
		free (song);
		song = NULL;
		ntracks = 0;
		return false;
	}

	// say so: on the screen the first time (and whenever there's no file),
	// in WOLFLOG.TXT every time
	sprintf (line,"Music: %s, General MIDI (%s)",name,songname);
	if (!told)
	{
		told = true;
		NetNotice (line);
	}
	else
		GameLog ("%s",line);
	return true;
}

void MIDI_Play (void)
{
	DISABLE();
	if (song && ntracks)
	{
		Rewind ();
		acc = 0;
		playing = true;
		paused = false;
	}
	RESTORE();
}

void MIDI_Stop (void)
{
	DISABLE();
	if (playing)
		ResetChannels ();
	playing = paused = false;
	RESTORE();
}

void MIDI_Pause (void)
{
	DISABLE();
	if (playing && !paused)
		NotesOff ();
	paused = true;
	RESTORE();
}

void MIDI_Resume (void)
{
	paused = false;
}

boolean MIDI_Playing (void)
{
	return playing && !paused;
}

void MIDI_Shutdown (void)
{
	long	i;

	if (!MIDIPresent)
		return;
	MIDI_Stop ();
	{
	DISABLE();
	for (i = 0;i < 2000 && ringr != ringw;i++)	// (a second or so, if it hangs)
		MIDI_Flush ();
	RESTORE();
	}
	if (song)
	{
		free (song);
		song = NULL;
	}
	MPU_Command (0xff);				// back out of UART mode
	MIDIPresent = false;
}

void MIDI_EmergencyStop (void)
{
	playing = false;
	if (MIDIPresent)
		outportb (mpuStatus,0xff);	// reset: out of UART mode
}
