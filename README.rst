Wolfenstein 3D
==============

MS-DOS (DJGPP) and Linux (SDL2) ports, with network co-op and
deathmatch.

MS-DOS port (DJGPP)
-------------------

This fork builds the id source as a 32-bit DJGPP (DPMI) program for
MS-DOS / FreeDOS, the same way as
`baremetaldoom's msdos build <https://github.com/jasonbrianhall/baremetaldoom/tree/master/msdos>`_.
File names are lowercase; the source is in ``wolfsrc``.

::

  cd msdos
  make                    # wolf3d.exe with CWSDPMI built in
  make VERSION=wl1        # for the shareware data (default: wl6)
  make run DATA=~/wolf3d  # try it in DOSBox

``VERSION`` picks the data the game is built for: ``wl6`` (1.4 GT /
Activision, as sold on Steam and GOG), ``wl6apo`` (1.4 Apogee),
``wl1`` (1.4 shareware), ``sod`` and ``sdm`` (Spear of Destiny and its
demo, make ``spear.exe``). It uses ``i586-pc-msdosdjgpp-gcc`` if it's on
the PATH, the ``djfdyuruiry/djgpp`` Docker image otherwise.

There's no memory check at startup at all: memory is plain ``malloc``
(a machine without enough gets "Out of memory"), the page file is read
into memory once, and the signon screen's gauges just show full, so the
conventional / EMS / XMS probing and paging are gone and nothing asks
the DPMI host how much memory there is. The assembly (raycaster, compiled scalers,
sound interrupt handlers, decompressors) is C. The hardware is driven as
before: mode X VGA, keyboard and timer interrupts, PC speaker, AdLib,
Sound Blaster (``BLASTER=``) and Sound Source. ``CONFIG`` files are
shared with the original; saved games are not.

``make setup`` builds ``setup.exe`` and ``ipxsetup.exe`` (one program
for every version: it finds the data in the current directory). SETUP
edits the ``CONFIG`` file in blue DOS screens: sound, keys, mouse,
joystick, view size. IPXSETUP opens on the co-op menu (players,
episode, level, skill, friendly fire) and runs the game; with options
(``ipxsetup -nodes 3 -level 2``) it runs it straight away.

Co-op over IPX for 2 to 4 players: every player starts the game with
an IPX driver loaded (DOSBox: ``ipx=true``, then ``IPXNET STARTSERVER``
on one and ``IPXNET CONNECT <address>`` on the others)::

  wolf3d -nodes 2 -episode 1 -level 1 -skill 2

It's lockstep, like Doom: only the controls go over the network, so all
machines run the same game. Player 1's episode, level and skill are
used. A dead player comes back at the level start after three seconds;
keys are shared (and show on everyone's status bar). Players block each other like actors do, and can shoot
each other (``-nofriendlyfire`` turns that off; player 1's choice is
used). The other players are drawn as BJ from ``PLAYERS.WL6`` (see
below) when it's next to the game, otherwise with the SS sprites; either
way recolored green, red, gold and white. Another player's shots,
pickups and doors are heard from where that player is. Esc leaves the game and the others play on.
T starts a chat message (Enter sends, Esc drops it); while typing,
Alt+1 to Alt+0 send the chat macros, which SETUP edits
(``CHATMACS.CFG``).

Deathmatch, over IPX like co-op (2 to 4 players)::

  wolf3d -deathmatch -name Hans [-episode 1] [-level 1] [-skill 2]

No waiting for anyone: the first one to run it starts the game (with
its episode, level and skill), and everyone after that joins it whenever
they like, and leaves (Esc) whenever they like; a newcomer takes a free
slot and gets the game as it is right then. ``-host`` always starts a new
game, ``-join`` waits for one instead of starting it. There are no
enemies, no treasure, keys or extra lives on the map; everybody starts
(and comes back) with every weapon, both keys and 50 rounds, somewhere
at random, and the ammo, food and first aid kits that are picked up come
back after 30 seconds. The exit is shut. With ``-monsters`` (Enemies in IPXSETUP) the map's
guards, dogs and bosses stay in, and one that's killed comes back where
it started 1 to 2 minutes later; being killed by one costs no frag.
The game's starter decides. The SCORE on the status bar is
your frags (one for a kill, one off if you die without anyone to thank);
Tab's automap also shows everybody's frags and deaths, and so does the
screen while you're dead. Fire or use brings you back after a second,
or wait three. A player whose machine goes quiet (crashed, unplugged) is
dropped after 8 seconds and the rest play on. Names (``-name``, or NAME.CFG, which IPXSETUP writes) are
in the chat, the kills and the score table, in co-op too. IPXSETUP's
network screen has a Co-op / Deathmatch choice and the name.

``-widescreen`` (or Widescreen in SETUP's Display screen) is for 16:9
monitors: the game draws its pages in main memory instead of planar VGA
memory and shows them in a 16:9 (or 16:10) VESA mode, 8 bit or 15/16/24/32
bit, linear framebuffer or banked. The 3D view gets wider (432 pixels
instead of 320 at view size 20, the same height, more angle); the status
bar, menus and full screen pictures stay 320 wide in the middle, with
black or matching sides. ``-vesa 848x480`` picks the mode, ``-vesabits``
its depth. Without a wide VESA mode it stays in the normal VGA mode.

``msdos/PLAYERS.WL6`` (copy it next to the exe as ``PLAYERS.WL1``,
``.SOD``... for the other data; the release zip has it named for each)
holds BJ in eight directions standing, walking,
aiming and firing, with pain and death frames. ``msdos/mkplayers.py``
makes it from a sprite sheet laid out like AndersonMiguel1105's "New BJ
Blazkowicz Sprites" (Python 3 with Pillow, NumPy and SciPy)::

  python3 msdos/mkplayers.py bj_sheet.png PLAYERS.WL6 --preview check.png

A player coming back makes a teleport sound: Freedoom's, from
``msdos/RESPAWN.WAD`` next to the exe (its license: ``FREEDOOM.TXT``;
both are in the release zip). Without ``RESPAWN.WAD`` a Doom WAD in
the game's directory is used, and without any it's an effect of our
own. Both need a digitized sound
device; with none, it's the extra life sound.

Sounds fade with distance: past 8 tiles (where the original tables level
off at about half volume) the volume halves every 8 tiles, and a sound
over about 40 tiles away isn't played. That's on the Sound Blaster Pro
(or later) and the Gravis UltraSound; AdLib and PC speaker sounds have no
volume, so those just drop out past that distance.

Tab shows an automap like Doom's (Tab again: the map over the 3D view,
again: off; ``+`` / ``-`` zoom). It draws the walls around floor you've
seen, doors, the elevator and the players.

``wolf3d /?`` lists the options. Sound and control settings can be set
from the command line (``-sfx``, ``-music``, ``-digi``, ``-mouse``,
``-joystick``, ``-sensitivity``, ``-viewsize``) and are saved when you
quit; ``-defaults`` ignores the ``CONFIG`` file.

The Gravis UltraSound works too (``ULTRASND=`` as set by its install;
``nogus`` to ignore it): it has no AdLib chip, so the music and AdLib
sound effects go through an OPL2 in software (DOSBox's dbopl) and are
mixed with the digitized sounds into a stream the card plays from its
own memory. That takes some CPU: a fast 486 or a Pentium.

The built-in CWSDPMI has its swap file turned off (``cwsnoswap.py``
makes CWSPARAM's change when the build fetches it): the game locks its
memory anyway, and on machines with a lot of RAM CWSDPMI would otherwise
page its own page tables to ``C:\CWSDPMI.SWP``, which makes startup
crawl. ``wolf3d -timing`` writes how long each startup step took to
``STARTUP.TXT``.

Needs a 386 with a 387 (or a 486DX or later; else put ``EMU387.DXE``
next to it) and 4 MB of RAM. Runs in plain DOS, FreeDOS, Windows 9x and
DOSBox, not in Windows NT/XP's DOS box.

Linux (SDL2)
------------

The same source builds for Linux: ``wolfsrc/sys_linux.c`` stands in for
the PC. The game still programs the timer chip and hooks the timer and
keyboard interrupts, calls the mouse driver, writes AdLib registers and
draws into the pages in memory (as in the widescreen mode); that file
calls its timer handler from a thread at the rate it set, feeds its
keyboard handler PC scan codes from SDL's key events, answers INT 33h
from SDL's mouse, plays the music and AdLib sounds on an OPL2 in software
(DOSBox's dbopl, as for the Gravis UltraSound), mixes in the digitized
sounds (in stereo) and the PC speaker, shows the page in a window, and
sends the network packets over UDP instead of IPX. Needs a C/C++ compiler
and SDL2 (Debian/Ubuntu: ``sudo apt install build-essential libsdl2-dev``)::

  cd linux
  make                    # ./wolf3d for the .WL6 data
  make VERSION=wl1        # (the same VERSIONs as for DOS: wl6 wl6apo wl1 sod sdm)
  ./wolf3d -data ~/wolf3d # or run it in the directory with the data

``make`` also makes ``setup`` and ``ipxsetup``: the DOS SETUP and
IPXSETUP, in the terminal (same blue screens, same CONFIG, NAME.CFG and
WOLFNET.CFG files; ``-data DIR`` as for the game). They run ``wolf3d``
(or ``spear``) from next to themselves, or from the current directory.
A terminal can't tell Ctrl, Alt or Shift pressed by themselves, so in
the keyboard screen F1 to F4 stand for Ctrl, Alt, left and right Shift.
IPXSETUP's network screen also has a "Connect to" (``-connect``).

The data files can be in upper or lower case. ``-fullscreen`` (or
Alt+Enter), ``-widescreen`` (a 16:9 view), ``-smooth`` (no square
pixels), ``-nosound``; ``./wolf3d --help`` lists the rest. A click in the
window takes the mouse for turning (it's let go when the window loses
the focus). CONFIG and the saved games are the DOS version's formats for
the settings, but saved games only load in the build that made them.

Network games go over UDP port 27135 to the local network (``-port N``
for another). For a game somewhere else, ``-connect host[:port]``; for
co-op with ``-connect``, the others it finds are passed along::

  ./wolf3d -deathmatch -name Hans                     # start or join one
  ./wolf3d -deathmatch -connect 192.168.1.20 -name Fritz
  ./wolf3d -nodes 2 -connect example.org:27135        # co-op

Playing with DOS players: the DOS game in DOSBox talks IPX through
DOSBox's IPXNET (IPX packets in UDP, through a server everyone connects
to), and the Linux game can join in. Either DOSBox is the server::

  DOSBox:  IPXNET STARTSERVER 21300            (then WOLF3D -deathmatch ...)
  Linux:   ./wolf3d -deathmatch -ipxnet dosboxhost:21300

or the Linux game is (port 213, DOSBox's default, if it may; else
21300, and it says so)::

  Linux:   ./wolf3d -deathmatch -ipxserver [port]
  DOSBox:  IPXNET CONNECT linuxhost [port]       (then WOLF3D -deathmatch ...)

Co-op (``-nodes``) and deathmatch both work across, either way round, a
deathmatch newcomer included. Everyone needs the same game data version.
IPXSETUP's "Network" line picks UDP, joining an IPXNET server or being
one. (Ports below 1024 need root on Linux, so pick something like 21300
for DOSBox's server there too.) No joysticks yet.

The original source release was made using the ``DEICE.EXE``
installer (DeIce) in the root directory.  (Basically a way to
split self-extracting zip files over multiple floppy disks.)
After execution, that installer will create the contents of
the ``WOLFSRC`` directory.  In the "installed" directory, the
the ``README.TXT``, ``RELEASE.TXT``, and ``LICENCE.DOC`` reside
in the ``README`` directory.  They are duplicated below.

Travis "Ash" Bradshaw <travis.bradshaw@idsoftware.com>

February 6, 2012

RELEASE.TXT
-----------

::

  We are releasing this code for the entertainment of the 
  user community.  We don't guarentee that anything even 
  builds in here.  Projects just seem to rot when you leave 
  them alone for long periods of time.

  This is all the source we have relating to the original 
  PC wolfenstein 3D project.  We haven't looked at this 
  stuff in years, and I would probably be horribly embarassed 
  to dig through my old code, so please don't ask any questions 
  about it. The original project was built in borland c++ 3.0.  
  I think some minor changes were required for later versions.

  You will need the data from a released version of wolf or spear 
  to use the exe built from this code.  You can just use a 
  shareware version if you are really cheap.


  Some coding comments in retrospect:

  The ray casting refresh architecture is still reasonably 
  appropriate for the game.  A BSP based texture mapper could 
  go faster, but ray casting was a lot simpler to do at the time.

  The dynamically compiled scaling routines are now a Bad Thing.  
  On uncached machines (the original target) they are the fastest 
  possible way to scale walls, but on modern processors you just 
  wind up thrashing the code cash and wrecking performance.  
  A simple looping texture mapper would be faster on 486+ machines.

  The whole page manager caching scheme is unecessarily complex.

  Way too many #ifdefs in the code!


  Some project ideas with this code:

  Add new monsters or weapons.

  Add taller walls and vertical motion.   This should only be 
  done if the texture mapper is rewritten.

  Convert to a 32 bit compiler.  This would be a fair amount 
  of work, but I would hate to even mess with crusty old 16 
  bit code.  The code would get a LOT smaller.

  Make a multi-player game that runs on DOOM sersetup / ipxsetup drivers.


  Have fun...

  John Carmack
  Technical Director
  Id Software

README.TXT
----------

NOTES:

This version will compile under BORLAND C++ 3.0/3.1 and compiled perfectly
before it was uploaded. 

Please do not send your questions to id Software.

LICENCE.DOC
-----------

 :: 
  	LIMITED USE SOFTWARE LICENSE AGREEMENT

  	This Limited Use Software License Agreement (the "Agreement") 
  is a legal agreement between you, the end-user, and Id Software, Inc. 
  ("ID").  By continuing the downloading of this Wolfenstein 3D 
  (the "Trademark") software material, which includes source code 
  (the "Source Code"), artwork data, music and software tools 
  (collectively, the "Software"), you are agreeing to be bound by the 
  terms of this Agreement.  If you do not agree to the terms of this 
  Agreement, promptly destroy the Software you may have downloaded.  

  ID SOFTWARE LICENSE

  	Grant of License.  ID grants to you the right to use one (1) 
  copy of the Software on a single computer.  You have no ownership or 
  proprietary rights in or to the Software, or the Trademark.  For purposes 
  of this section, "use" means loading the Software into RAM, as well as 
  installation on a hard disk or other storage device.  The Software, 
  together with any archive copy thereof, shall be destroyed when no longer 
  used in accordance with this Agreement, or when the right to use the 
  Software is terminated.  You agree that the Software will not be shipped, 
  transferred or exported into any country in violation of the U.S. 
  Export Administration Act (or any other law governing such matters) and 
  that you will not utilize, in any other manner, the Software in violation 
  of any applicable law.

  	Permitted Uses.  For educational purposes only, you, the end-user, 
  may use portions of the Source Code, such as particular routines, to 
  develop your own software, but may not duplicate the Source Code, except 
  as noted in paragraph 4.  The limited right referenced in the preceding 
  sentence is hereinafter referred to as "Educational Use."  By so exercising 
  the Educational Use right you shall not obtain any ownership, copyright,
  proprietary or other interest in or to the Source Code, or any portion of
  the Source Code.  You may dispose of your own software in your sole
  discretion.  With the exception of the Educational Use right, you may not
  otherwise use the Software, or an portion of the Software, which includes
  the Source Code, for commercial gain.

  	Prohibited Uses:  Under no circumstances shall you, the end-user, 
  be permitted, allowed or authorized to commercially exploit the Software.
  Neither you nor anyone at your direction shall do any of the following acts
  with regard to the Software, or any portion thereof:

  	Rent;

  	Sell;

  	Lease;

  	Offer on a pay-per-play basis;

  	Distribute for money or any other consideration; or

  	In any other manner and through any medium whatsoever commercially 
  exploit or use for any commercial purpose.

  Notwithstanding the foregoing prohibitions, you may commercially exploit the
  software you develop by exercising the Educational Use right, referenced in
  paragraph 2. hereinabove. 

  	Copyright.  The Software and all copyrights related thereto 
  (including all characters and other images generated by the Software 
  or depicted in the Software) are owned by ID and is protected by 
  United States copyright laws and international treaty provisions.  
  Id shall retain exclusive ownership and copyright in and to the Software 
  and all portions of the Software and you shall have no ownership or other 
  proprietary interest in such materials. You must treat the Software like 
  any other copyrighted material, except that you may either (a) make one 
  copy of the Software solely for back-up or archival purposes, or (b) 
  transfer the Software to a single hard disk provided you keep the original 
  solely for back-up or archival purposes.  You may not otherwise reproduce, 
  copy or disclose to others, in whole or in any part, the Software.  You 
  may not copy the written materials accompanying the Software.  You agree 
  to use your best efforts to see that any user of the Software licensed 
  hereunder complies with this Agreement.

  	NO WARRANTIES.  ID DISCLAIMS ALL WARRANTIES, BOTH EXPRESS IMPLIED,
  INCLUDING BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND
  FITNESS FOR A PARTICULAR PURPOSE WITH RESPECT TO THE SOFTWARE.  THIS LIMITED
  WARRANTY GIVES YOU SPECIFIC LEGAL RIGHTS.  YOU MAY HAVE OTHER RIGHTS WHICH
  VARY FROM JURISDICTION TO JURISDICTION.  ID DOES NOT WARRANT THAT THE
  OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED, ERROR FREE OR MEET YOUR
  SPECIFIC REQUIREMENTS.  THE WARRANTY SET FORTH ABOVE IS IN LIEU OF ALL OTHER
  EXPRESS WARRANTIES WHETHER ORAL OR WRITTEN.  THE AGENTS, EMPLOYEES,
  DISTRIBUTORS, AND DEALERS OF ID ARE NOT AUTHORIZED TO MAKE MODIFICATIONS TO
  THIS WARRANTY, OR ADDITIONAL WARRANTIES ON BEHALF OF ID. 

  	Exclusive Remedies.  The Software is being offered to you free of any
  charge.  You agree that you have no remedy against ID, its affiliates,
  contractors, suppliers, and agents for loss or damage caused by any defect
  or failure in the Software regardless of the form of action, whether in
  contract, tort, includinegligence, strict liability or otherwise, with
  regard to the Software.  This Agreement shall be construed in accordance
  with and governed by the laws of the State of Texas.  Copyright and other
  proprietary matters will be governed by United States laws and international
  treaties.  IN ANY CASE, ID SHALL NOT BE LIABLE FOR LOSS OF DATA, LOSS OF
  PROFITS, LOST SAVINGS, SPECIAL, INCIDENTAL, CONSEQUENTIAL, INDIRECT OR OTHER
  SIMILAR DAMAGES ARISING FROM BREACH OF WARRANTY, BREACH OF CONTRACT,
  NEGLIGENCE, OR OTHER LEGAL THEORY EVEN IF ID OR ITS AGENT HAS BEEN ADVISED
  OF THE POSSIBILITY OF SUCH DAMAGES, OR FOR ANY CLAIM BY ANY OTHER PARTY.
  Some jurisdictions do not allow the exclusion or limitation of incidental or
  consequential damages, so the above limitation or exclusion may not apply to
  you.

  	General Provisions.  Neither this Agreement nor any part or portion 
  hereof shall be assigned, sublicensed or otherwise transferred by you.  
  Should any provision of this Agreement be held to be void, invalid, 
  unenforceable or illegal by a court, the validity and enforceability of the 
  other provisions shall not be affected thereby.  If any provision is 
  determined to be unenforceable, you agree to a modification of such 
  provision to provide for enforcement of the provision's intent, to the 
  extent permitted by applicable law.  Failure of a party to enforce any 
  provision of this Agreement shall not constitute or be construed as a 
  waiver of such provision or of the right to enforce such provision.  If 
  you fail to comply with any terms of this Agreement, YOUR LICENSE IS 
  AUTOMATICALLY TERMINATED and you agree to the issuance of an injunction 
  against you in favor of Id.  You agree that Id shall not have to post 
  bond or other security to obtain an injunction against you to prohibit 
  you from violating Id's rights.

  	YOU ACKNOWLEDGE THAT YOU HAVE READ THIS AGREEMENT, THAT YOU 
  UNDERSTAND THIS AGREEMENT, AND UNDERSTAND THAT BY CONTINUING THE 
  DOWNLOADING OF THE SOFTWARE, YOU AGREE TO BE BOUND BY THIS AGREEMENT'S 
  TERMS AND CONDITIONS. YOU FURTHER AGREE THAT, EXCEPT FOR WRITTEN SEPARATE 
  AGREEMENTS BETWEEN ID AND YOU, THIS AGREEMENT IS A COMPLETE AND EXCLUSIVE 
  STATEMENT OF THE RIGHTS AND LIABILITIES OF THE PARTIES.  THIS AGREEMENT 
  SUPERSEDES ALL PRIOR ORAL AGREEMENTS, PROPOSALS OR UNDERSTANDINGS, AND 
  ANY OTHER COMMUNICATIONS BETWEEN ID AND YOU RELATING TO THE SUBJECT MATTER 
  OF THIS AGREEMENT 
  
