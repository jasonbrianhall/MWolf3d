Wolfenstein 3-D - Windows (SDL2)

The id Software source, built for Windows. No game data is included:
copy the data files of your copy of the game into the folder that
matches them (the Start menu's "Game folders" opens it):

  WL6\      Wolfenstein 3-D 1.4, GT / Activision / id
            (the version sold on Steam and GOG)
  WL6APO\   Wolfenstein 3-D 1.4, Apogee registered
  WL1\      Wolfenstein 3-D 1.4, Apogee shareware
  SOD\      Spear of Destiny
  SDM\      Spear of Destiny demo

The files: AUDIOHED.*  AUDIOT.*  GAMEMAPS.*  MAPHEAD.*  VGADICT.*
VGAGRAPH.*  VGAHEAD.*  VSWAP.*  (the extension is WL6, WL1, SOD or SDM).
Steam: Wolfenstein 3D\base. GOG: the game's install folder. The game's
CONFIG and saved games go in the same folder. Or run the game with
-data FOLDER for data somewhere else.

Options:   wolf3d -help lists them. -fullscreen (or Alt+Enter),
           -widescreen, -smooth, -nosound, -data FOLDER.
Setup:     SETUP.EXE (the Start menu's "... Setup") sets sound, keys,
           mouse and view size, as the DOS one does; IPXSETUP.EXE is the
           network game menu (co-op / deathmatch, your name, where to
           connect).
MIDI:      Music: General MIDI (SETUP, the game's Sound menu, or
           -music midi) plays MUSIC\ (the songs as MIDI files, see
           WOLFMIDI.TXT). SETUP's "General MIDI on" (or -mididev
           opl|fluidsynth|system) picks what plays it: an OPL3 in
           software (built in), FluidSynth (libfluidsynth-3.dll and its
           DLLs next to the game, and a SoundFont: -soundfont FILE.sf2),
           or Windows MIDI (the Microsoft GS Wavetable Synth, or another
           MIDI out: -mididev system:NAME). WOLFLOG.TXT says what each
           song played on.
Co-op:     wolf3d -nodes 2 [-episode 1] [-level 1] [-skill 2]
Deathmatch: wolf3d -deathmatch -name YOURNAME
           -bots N adds N bots; -botskill 0-3.
Network:   UDP port 27135 on the local network (Windows may ask to let
           it through the firewall); -connect host[:port] for a game
           elsewhere, or TCP through a server: -tcpserver [port] /
           -tcp host[:port]. DOS players in DOSBox: -ipxnet host:port.

Sources: https://github.com/jasonbrianhall/wolf3d
