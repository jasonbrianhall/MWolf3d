//
//	ID_MIDI.H -- General MIDI music through an MPU-401
//
//	The music as Bobby Prince wrote it was MIDI; the game plays the AdLib
//	version (IMF, register writes). This plays standard MIDI files in
//	its place, sent to an MPU-401 in UART mode (port 330h, or BLASTER's
//	P, or -mpu PORT). wolfmidi's MUSIC directory (the songs converted from
//	the game's own, as wolfdosmpu uses) works as it is.
//
//	On Linux sys_linux.c answers the MPU-401's ports: the bytes go to a
//	General MIDI synth on an OPL3 in software, FluidSynth, or the
//	system's MIDI (the ALSA sequencer, or a raw MIDI device).
//

#ifndef	__ID_MIDI__
#define	__ID_MIDI__

extern	boolean	MIDIPresent;		// an MPU-401 answered (always, on Linux)
extern	word	MIDIPort;

boolean	MIDI_Startup (void);		// find the MPU-401, put it in UART mode
void	MIDI_Shutdown (void);		// notes off, everything sent

// a standard MIDI file for this song (MUSIC\NAME, MUSIC\NAME.MID, NAME.MID,
// or MUSIC\_INFO's name for it), loaded and ready; false if there is none
boolean	MIDI_LoadSong (MusicGroup *music);
void	MIDI_Play (void);			// from the start of the loaded song
void	MIDI_Stop (void);			// stop, notes off, controllers reset
void	MIDI_Pause (void);			// notes off, and wait where it is
void	MIDI_Resume (void);
boolean	MIDI_Playing (void);

void	MIDI_Service (word rate);	// timer interrupt: rate calls a second
void	MIDI_Flush (void);			// timer interrupt: send what's waiting

void	MIDI_EmergencyStop (void);	// after a crash: reset the MPU-401

#endif
