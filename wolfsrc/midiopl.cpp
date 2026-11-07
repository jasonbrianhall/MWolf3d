// MIDIOPL.CPP -- a General MIDI synth on an OPL3 in software (Linux)
//
// The "classic MIDI" sound: what a Sound Blaster 16 or a Pro Audio
// Spectrum made of General MIDI with its FM chip, as zenamp does it
// (its instrument bank: midibank.h). 18 two-operator voices on a second
// DOSBox dbopl, apart from the AdLib's. sys_midi.c feeds it the MIDI
// messages the MPU-401 gets, and mixes what it makes in.

#include <stdint.h>
#include <string.h>
#include <math.h>
#include "dbopl.h"
#include "midibank.h"

#define VOICES		18
#define DRUMS		9			// MIDI channel 10

static DBOPL::Handler	opl;
static int				ready;

typedef struct
{
	int		on;					// key down
	int		sustained;			// key up, held by the pedal
	int		chan,note,patch;
	int		vel;
	double	pitch;				// the key it sounds, before the bend
	uint32_t age;				// when it started (or stopped)
} voice_t;

typedef struct
{
	int		program;
	int		volume,expression,pan;
	int		bend;				// -8192..8191
	int		bendrange;			// semitones
	int		sustain;
	int		rpn;				// the registered parameter selected, or -1
} channel_t;

static voice_t		voice[VOICES];
static channel_t	chan[16];
static uint32_t		voiceclock;

static const int	opoffset[9] = {0x00,0x01,0x02,0x08,0x09,0x0a,0x10,0x11,0x12};

static void Out (int reg, int val)
{
	opl.WriteReg ((Bit32u)reg,(Bit8u)val);
}

// the voice's register numbers: channel (A0, B0, C0) and operators
static int ChanReg (int v, int reg)
{
	return reg + (v % 9) + (v / 9) * 0x100;
}

static int OpReg (int v, int reg, int carrier)
{
	return reg + opoffset[v % 9] + (carrier ? 3 : 0) + (v / 9) * 0x100;
}

static void ResetChannel (int c)
{
	channel_t *ch = &chan[c];

	ch->program = 0;
	ch->volume = 100;
	ch->expression = 127;
	ch->pan = 64;
	ch->bend = 0;
	ch->bendrange = 2;
	ch->sustain = 0;
	ch->rpn = -1;
}

//
// Atten: how much quieter than the patch, in the OPL's 0.75 dB steps
// (velocity, volume and expression, each on General MIDI's 40 log curve)
//
static int Atten (voice_t *vo)
{
	channel_t	*ch = &chan[vo->chan];
	double		level,db;
	int			a;

	level = (vo->vel / 127.0) * (ch->volume / 127.0) * (ch->expression / 127.0);
	if (level <= 0.0001)
		return 63;
	db = -40.0 * log10 (level);
	a = (int)(db / 0.75 + 0.5);
	return a > 63 ? 63 : a;
}

static void SetLevel (int v)
{
	voice_t		*vo = &voice[v];
	const unsigned char *p = gmbank[vo->patch];
	int			att = Atten (vo),tl;

	// the carrier is what's heard; with additive synthesis the modulator too
	tl = (p[6] & 63) + att;
	Out (OpReg (v,0x40,1),(p[6] & 0xc0) | (tl > 63 ? 63 : tl));
	tl = p[1] & 63;
	if (p[10] & 1)
		tl += att;
	Out (OpReg (v,0x40,0),(p[1] & 0xc0) | (tl > 63 ? 63 : tl));
}

static void SetPan (int v)
{
	const unsigned char *p = gmbank[voice[v].patch];
	int		pan = chan[voice[v].chan].pan,bits;

	if (pan < 32)
		bits = 0x10;			// left
	else if (pan > 96)
		bits = 0x20;			// right
	else
		bits = 0x30;
	Out (ChanReg (v,0xc0),(p[10] & 0x0f) | bits);
}

static void SetFreq (int v, int keyon)
{
	voice_t		*vo = &voice[v];
	channel_t	*ch = &chan[vo->chan];
	double		key,f,fn;
	int			block,fnum;

	key = vo->pitch + (double)ch->bend * ch->bendrange / 8192.0;
	f = 440.0 * pow (2.0,(key - 69.0) / 12.0);
	fn = f * (1 << 20) / 49716.0;		// block 0
	for (block = 0;fn >= 1023.5 && block < 7;block++)
		fn /= 2;
	fnum = (int)(fn + 0.5);
	if (fnum > 1023)
		fnum = 1023;
	Out (ChanReg (v,0xa0),fnum & 0xff);
	Out (ChanReg (v,0xb0),(keyon ? 0x20 : 0) | (block << 2) | (fnum >> 8));
}

static void KeyOff (int v)
{
	voice_t	*vo = &voice[v];

	if (vo->on || vo->sustained)
	{
		SetFreq (v,0);
		vo->on = vo->sustained = 0;
		vo->age = voiceclock++;		// released: its tail is still sounding
	}
}

static int FindVoice (int c, int note)
{
	int		v,best = -1;

	// the same key again: that voice
	for (v = 0;v < VOICES;v++)
		if ((voice[v].on || voice[v].sustained) && voice[v].chan == c && voice[v].note == note)
			return v;
	// the one that's been quiet longest
	for (v = 0;v < VOICES;v++)
		if (!voice[v].on && !voice[v].sustained && (best < 0 || voice[v].age < voice[best].age))
			best = v;
	if (best >= 0)
		return best;
	// all busy: the oldest held by the pedal, else the oldest (drums last)
	for (v = 0;v < VOICES;v++)
		if (voice[v].sustained && (best < 0 || voice[v].age < voice[best].age))
			best = v;
	if (best >= 0)
		return best;
	for (v = 0;v < VOICES;v++)
		if (voice[v].chan != DRUMS && (best < 0 || voice[v].age < voice[best].age))
			best = v;
	return best >= 0 ? best : 0;
}

static void NoteOn (int c, int note, int vel)
{
	const unsigned char *p;
	voice_t	*vo;
	int		v,patch;
	double	pitch = note;

	if (c == DRUMS)
	{
		if (note < 35 || note > 87)
			return;
		patch = 128 + note - 35;
		if (gmbank[patch][11])
			pitch = gmbank[patch][11];
	}
	else
		patch = chan[c].program;

	v = FindVoice (c,note);
	vo = &voice[v];
	if (vo->on || vo->sustained)
		SetFreq (v,0);			// key up first, so it starts again

	vo->on = 1;
	vo->sustained = 0;
	vo->chan = c;
	vo->note = note;
	vo->patch = patch;
	vo->vel = vel;
	vo->pitch = pitch;
	vo->age = voiceclock++;

	p = gmbank[patch];
	Out (OpReg (v,0x20,0),p[0]);
	Out (OpReg (v,0x60,0),p[2]);
	Out (OpReg (v,0x80,0),p[3]);
	Out (OpReg (v,0xe0,0),p[4]);
	Out (OpReg (v,0x20,1),p[5]);
	Out (OpReg (v,0x60,1),p[7]);
	Out (OpReg (v,0x80,1),p[8]);
	Out (OpReg (v,0xe0,1),p[9]);
	SetLevel (v);
	SetPan (v);
	SetFreq (v,1);
}

static void NoteOff (int c, int note)
{
	int		v;

	for (v = 0;v < VOICES;v++)
		if (voice[v].on && voice[v].chan == c && voice[v].note == note)
		{
			if (chan[c].sustain)
			{
				voice[v].on = 0;
				voice[v].sustained = 1;
			}
			else
				KeyOff (v);
		}
}

static void AllOff (int c)
{
	int		v;

	for (v = 0;v < VOICES;v++)
		if (c < 0 || voice[v].chan == c)
			KeyOff (v);
}

static void Controller (int c, int num, int val)
{
	channel_t	*ch = &chan[c];
	int			v;

	switch (num)
	{
	case 6:						// data entry: the bend range, if that's selected
		if (ch->rpn == 0)
			ch->bendrange = val > 24 ? 24 : val;
		break;
	case 7:
	case 11:
		if (num == 7)
			ch->volume = val;
		else
			ch->expression = val;
		for (v = 0;v < VOICES;v++)
			if ((voice[v].on || voice[v].sustained) && voice[v].chan == c)
				SetLevel (v);
		break;
	case 10:
		ch->pan = val;
		for (v = 0;v < VOICES;v++)
			if ((voice[v].on || voice[v].sustained) && voice[v].chan == c)
				SetPan (v);
		break;
	case 64:
		ch->sustain = val >= 64;
		if (!ch->sustain)
			for (v = 0;v < VOICES;v++)
				if (voice[v].sustained && voice[v].chan == c)
					KeyOff (v);
		break;
	case 98:
	case 99:					// a non-registered parameter: nothing we do
		ch->rpn = -1;
		break;
	case 100:
		ch->rpn = (ch->rpn < 0 ? 0 : ch->rpn & ~0x7f) | val;
		break;
	case 101:
		ch->rpn = (ch->rpn < 0 ? 0 : ch->rpn & 0x7f) | (val << 7);
		break;
	case 120:					// all sound off
	case 123:					// all notes off
		AllOff (c);
		break;
	case 121:					// reset controllers
		ch->expression = 127;
		ch->bend = 0;
		ch->rpn = -1;
		Controller (c,64,0);
		break;
	}
}

static void Bend (int c, int value)
{
	int		v;

	chan[c].bend = value;
	for (v = 0;v < VOICES;v++)
		if ((voice[v].on || voice[v].sustained) && voice[v].chan == c)
			SetFreq (v,1);
}

extern "C" void MIDIOPL_Reset (void)
{
	int		c;

	if (!ready)
		return;
	AllOff (-1);
	for (c = 0;c < 16;c++)
		ResetChannel (c);
}

extern "C" void MIDIOPL_Init (int rate)
{
	int		v;

	opl.Init ((Bitu)rate);
	Out (0x105,0x01);			// OPL3: 18 voices, stereo
	Out (0x104,0x00);			// all two-operator
	Out (0x001,0x20);
	Out (0x008,0x40);
	Out (0x0bd,0x00);			// no rhythm mode
	memset (voice,0,sizeof(voice));
	for (v = 0;v < VOICES;v++)
		voice[v].chan = -1;
	ready = 1;
	MIDIOPL_Reset ();
}

extern "C" void MIDIOPL_Message (int status, int d1, int d2)
{
	int		c = status & 15;

	if (!ready)
		return;
	switch (status & 0xf0)
	{
	case 0x80:
		NoteOff (c,d1);
		break;
	case 0x90:
		if (d2)
			NoteOn (c,d1,d2);
		else
			NoteOff (c,d1);
		break;
	case 0xb0:
		Controller (c,d1,d2);
		break;
	case 0xc0:
		chan[c].program = d1 & 127;
		break;
	case 0xe0:
		Bend (c,((d2 << 7) | d1) - 8192);
		break;
	}
}

// GM on (F0 7E 7F 09 01 F7) and GS reset (F0 41 10 42 12 40 00 7F 00 41 F7):
// every channel as at power on
extern "C" void MIDIOPL_SysEx (const unsigned char *p, int len)
{
	static const unsigned char gmon[] = {0xf0,0x7e,0x7f,0x09,0x01};
	static const unsigned char gsreset[] = {0xf0,0x41,0x10,0x42,0x12,0x40,0x00,0x7f};

	if ((len >= 5 && !memcmp (p,gmon,4) && p[4] == 0x01)
	|| (len >= 8 && !memcmp (p,gsreset,2) && !memcmp (p+3,gsreset+3,5)))
		MIDIOPL_Reset ();
}

// interleaved stereo, about +-16000 for a loud chord
extern "C" void MIDIOPL_Generate (int32_t *buf, int frames)
{
	if (!ready)
	{
		memset (buf,0,frames*2*sizeof(int32_t));
		return;
	}
	opl.Generate ((Bit32s *)buf,(Bitu)frames);
}
