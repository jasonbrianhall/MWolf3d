// WL_NET.C -- IPX co-op for the DJGPP port
//
// Lockstep, like Doom: every machine runs the whole game and only the
// players' controls go over the wire.  The game advances in NETTICS-tic
// frames; the controls a machine samples at frame f are used at frame
// f+NETDELAY on every machine, so normally nobody waits.  Each packet
// carries the last NETBACKUP frames of controls, so a lost packet is
// covered by the next one, and a stalled machine resends its last one.
//
// Everything that differs between players lives in a netplayer_t and is
// swapped into the usual globals (gamestate.health, player, plux...) by
// NetSetContext, so the single player code runs unchanged for each of them.
//
// Deathmatch (-deathmatch) is the same lockstep game with no enemies, the
// exit shut, everybody armed and keyed, and the players coming in and
// going whenever they like: there are always MAXPLAYERS slots, the empty
// ones "removed". The first one in starts the game; a newcomer asks the
// arbiter (the lowest numbered player) to come in, the arbiter puts a
// join event for a free slot into its own controls for a frame a little
// ahead (so every machine adds the player at that same frame), and after
// that frame sends the newcomer the whole game as it then is (a
// snapshot). The newcomer carries on from there.
//
// On Linux the packets go over UDP instead of IPX (sys_linux.c).

#include "wl_def.h"
#ifndef __linux__
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#endif
#pragma hdrstop

/*
=============================================================================

						 GLOBALS

=============================================================================
*/

boolean	netgame;
boolean	deathmatch;				// -deathmatch
boolean	dmmonsters;				// -monsters: the map's enemies are there too
int		numplayers = 1;
int		localplayer;
int		netcontext;
objtype	*playerobjs[MAXPLAYERS];

extern	long	playerxmove,playerymove;
extern	int		gotgatgun;
extern	objtype	*LastAttacker;
extern	boolean	buttonstate[NUMBUTTONS];
extern	int		mouseadjustment;
extern	int		objcount;
extern	statetype	s_player;
int		kbhit (void);
int		getch (void);

void	PollKeyboardButtons (void);
void	PollMouseButtons (void);
void	PollJoystickButtons (void);
void	PollKeyboardMove (void);
void	PollMouseMove (void);
void	PollJoystickMove (void);
void	ClearSplitVWB (void);
int		US_RndIndex (void);
void	US_SetRndIndex (int index);

#define NETTICS		2			// tics per game frame (35 frames a second)
#define NETDELAY	2			// frames between sampling and using controls
#define NETBACKUP	8			// frames of controls in every packet
#define NETRING		64			// must be a power of two, > 2*NETBACKUP
#define RESPAWNTICS	(70*3)

#define NETSCALE	207			// projection of a full size (304 wide) view:
#define NETSHOOT	30			// shootdelta for it
#define NETHALFVIEW	152

#define NETFOCAL	(0x5700l)
#define NETACTORSIZE	0x4000

typedef struct
{
	byte	buttons;
	byte	event;				// EV_JOIN|slot: the arbiter lets a player in
	short	x,y;
} __attribute__((packed)) netcmd_t;

#define EV_JOIN		0x80

#define DMAMMO		50			// ammo after a deathmatch (re)spawn
#define DMITEMTICS	(70*30)		// a picked up item comes back after this
#define DMQUICKTICS	70			// dead this long: fire or use comes back
#ifndef DMENEMYTICS
#define DMENEMYTICS	(70*60)		// -monsters: a dead enemy is back after 1-2 min
#endif

typedef struct
{
	objtype	*ob;

	// the per player part of gamestate
	long	oldscore,score,nextextra;
	int		lives,health,ammo,keys;
	weapontype	bestweapon,weapon,chosenweapon;
	int		faceframe,attackframe,attackcount,weaponframe;

	// wl_agent.c / wl_play.c
	long	thrustspeed;
	unsigned plux,pluy;
	int		anglefrac,gotgatgun,facecount;
	long	playerxmove,playerymove;
	objtype	*lastattacker;
	int		controlx,controly;
	boolean	buttonstate[NUMBUTTONS],buttonheld[NUMBUTTONS];

	// not swapped
	boolean	dead,removed;
	int		respawn;
	long	spawnx,spawny;
	int		spawnangle;
	long	anim;
	int		moving;				// frames left of the walking animation
	int		pain;				// frames left of the pain frame

	boolean	gone;				// left the game: no controls after lastframe
	long	lastframe;
	long	firstcmd;			// controls before this frame are none (joined)

	char	name[NETNAMELEN+1];
	long	frags,deaths;

	long	cmdframe[NETRING];
	netcmd_t cmds[NETRING];
	long	ack;				// frames it has finished (this level)
	byte	addr[10];			// IPX network + node
} netplayer_t;

static	netplayer_t	np[MAXPLAYERS];
static	int		hello_ff;			// friendly fire (player 1's choice)
static	boolean	SpawnSpotClear (netplayer_t *p);
static	void	AddChatLine (char *text, int color);
static	void	DMSpawn (netplayer_t *p);
static	void	DMLoadout (void);
static	void	ShowFrags (int p);

static	char	myname[NETNAMELEN+1];	// -name
static	int		playercolors[MAXPLAYERS] = {10,12,14,15};	// as their uniforms

char *NetName (int p)
{
	static char	str[16];

	if (p < 0 || p >= MAXPLAYERS)
		return "?";
	if (np[p].name[0])
		return np[p].name;
	sprintf (str,"Player %d",p+1);
	return str;
}

/*
=============================================================================

						 CONTEXT

=============================================================================
*/

static void SaveContext (netplayer_t *p)
{
	p->oldscore = gamestate.oldscore;
	p->score = gamestate.score;
	p->nextextra = gamestate.nextextra;
	p->lives = gamestate.lives;
	p->health = gamestate.health;
	p->ammo = gamestate.ammo;
	p->keys = gamestate.keys;
	p->bestweapon = gamestate.bestweapon;
	p->weapon = gamestate.weapon;
	p->chosenweapon = gamestate.chosenweapon;
	p->faceframe = gamestate.faceframe;
	p->attackframe = gamestate.attackframe;
	p->attackcount = gamestate.attackcount;
	p->weaponframe = gamestate.weaponframe;

	p->thrustspeed = thrustspeed;
	p->plux = plux;
	p->pluy = pluy;
	p->anglefrac = anglefrac;
	p->gotgatgun = gotgatgun;
	p->facecount = facecount;
	p->playerxmove = playerxmove;
	p->playerymove = playerymove;
	p->lastattacker = LastAttacker;
	p->controlx = controlx;
	p->controly = controly;
	memcpy (p->buttonstate,buttonstate,sizeof(buttonstate));
	memcpy (p->buttonheld,buttonheld,sizeof(buttonheld));
}

static void LoadContext (netplayer_t *p)
{
	gamestate.oldscore = p->oldscore;
	gamestate.score = p->score;
	gamestate.nextextra = p->nextextra;
	gamestate.lives = p->lives;
	gamestate.health = p->health;
	gamestate.ammo = p->ammo;
	gamestate.keys = p->keys;
	gamestate.bestweapon = p->bestweapon;
	gamestate.weapon = p->weapon;
	gamestate.chosenweapon = p->chosenweapon;
	gamestate.faceframe = p->faceframe;
	gamestate.attackframe = p->attackframe;
	gamestate.attackcount = p->attackcount;
	gamestate.weaponframe = p->weaponframe;

	thrustspeed = p->thrustspeed;
	plux = p->plux;
	pluy = p->pluy;
	anglefrac = p->anglefrac;
	gotgatgun = p->gotgatgun;
	facecount = p->facecount;
	playerxmove = p->playerxmove;
	playerymove = p->playerymove;
	LastAttacker = p->lastattacker;
	controlx = p->controlx;
	controly = p->controly;
	memcpy (buttonstate,p->buttonstate,sizeof(buttonstate));
	memcpy (buttonheld,p->buttonheld,sizeof(buttonheld));

	player = p->ob;
}

void NetSetContext (int p)
{
	if (p == netcontext)
	{
		player = np[p].ob;
		return;
	}
	SaveContext (&np[netcontext]);
	LoadContext (&np[p]);
	netcontext = p;
}

boolean NetIsLocal (void)
{
	return !netgame || netcontext == localplayer;
}

boolean NetPlayerAlive (int p)
{
	return !np[p].dead;
}

boolean NetCurrentDead (void)
{
	return netgame && np[netcontext].dead;
}

boolean NetLocalDead (void)
{
	return netgame && np[localplayer].dead;
}

static int GetHealth (int p)
{
	return p == netcontext ? gamestate.health : np[p].health;
}


/*
=============================================================================

						 GAME LOGIC

=============================================================================
*/

/*
===================
=
= NetTarget
=
= The live player nearest an actor: the one its AI works against
=
===================
*/

int NetTarget (objtype *ob)
{
	int		i,best;
	long	dx,dy,d,bestd;

	best = -1;
	bestd = 0x7fffffffl;
	for (i=0;i<numplayers;i++)
	{
		if (np[i].dead)
			continue;
		dx = labs(ob->x - np[i].ob->x);
		dy = labs(ob->y - np[i].ob->y);
		d = dx>dy ? dx : dy;
		if (d < bestd)
		{
			bestd = d;
			best = i;
		}
	}
	if (best == -1)
	{
		for (i=0;i<numplayers;i++)
			if (!np[i].removed)
				return i;
		return 0;
	}
	return best;
}


/*
===================
=
= NetAngle
=
= The direction of dx,dy (dy up) in ANGLES, rounded down, as the bosses'
= atan2 has it; but from the sine table with integers, so a DOS and a
= Linux machine (different maths libraries, different FPUs) agree
=
===================
*/

int NetAngle (long dx, long dy)
{
	long	x,y,t;
	int		q,lo,hi,mid;

	if (!dx && !dy)
		return 0;
	if (dx > 0 && dy >= 0)
		q = 0;
	else if (dx <= 0 && dy > 0)
		q = 1;
	else if (dx < 0 && dy <= 0)
		q = 2;
	else
		q = 3;
	x = dx;
	y = dy;
	while (q-- > 0 && (t = x, x = y, y = -t, 1))
		;
	q = dx > 0 && dy >= 0 ? 0 : dx <= 0 && dy > 0 ? 1 : dx < 0 && dy <= 0 ? 2 : 3;

	// the biggest a in 0..89 with the vector at or past a
	lo = 0;
	hi = ANGLES/4-1;
	while (lo < hi)
	{
		mid = (lo+hi+1)/2;
		if ((int64_t)costable[mid]*y - (int64_t)sintable[mid]*x >= 0)
			lo = mid;
		else
			hi = mid-1;
	}
	return q*(ANGLES/4) + lo;
}


/*
===================
=
= NetProject
=
= Where ob is in viewer's view, worked out the same on every machine
= (the real view depends on the local screen size)
=
===================
*/

static boolean NetProject (objtype *viewer, objtype *ob, fixed *nxp, long *offp)
{
	fixed	vcos,vsin,vx,vy,gx,gy,nx,ny;

	vcos = costable[viewer->angle];
	vsin = sintable[viewer->angle];
	vx = viewer->x - FixedByFrac(NETFOCAL,vcos);
	vy = viewer->y + FixedByFrac(NETFOCAL,vsin);

	gx = ob->x - vx;
	gy = ob->y - vy;
	nx = FixedByFrac(gx,vcos) - FixedByFrac(gy,vsin) - NETACTORSIZE;
	ny = FixedByFrac(gy,vcos) + FixedByFrac(gx,vsin);

	*nxp = nx;
	if (nx < MINDIST)
		return false;
	*offp = ny*NETSCALE/nx;
	return true;
}


/*
===================
=
= NetCanSee
=
= Stands in for FL_VISABLE (set by the local renderer) in the game logic
=
===================
*/

boolean NetCanSee (objtype *viewer, objtype *ob)
{
	fixed	nx;
	long	off;

	if (!NetProject (viewer,ob,&nx,&off))
		return false;
	return off > -NETHALFVIEW && off < NETHALFVIEW;
}


/*
===================
=
= NetFindTarget
=
= What the current player's gun or knife would hit: the nearest shootable
= actor in the crosshair with a clear line to it
=
===================
*/

objtype *NetFindTarget (objtype *shooter, boolean knife, long *dist)
{
	objtype	*check,*closest;
	fixed	nx;
	long	off,best;

	closest = NULL;
	best = 0x7fffffffl;
	for (check = playerobjs[0] ; check ; check = check->next)
	{
		if (check->obclass == playerobj)
		{
			int i;
			if (!hello_ff || check == shooter)
				continue;
			for (i=0;i<numplayers;i++)
				if (np[i].ob == check)
					break;
			if (i == numplayers || np[i].dead)
				continue;
		}
		else if (!(check->flags & FL_SHOOTABLE))
			continue;
		if (!NetProject (shooter,check,&nx,&off))
			continue;
		if (off <= -NETSHOOT || off >= NETSHOOT)
			continue;
		if (nx >= best)
			continue;
		if (!CheckLine (check))
			continue;
		best = nx;
		closest = check;
	}
	*dist = best;
	return closest;
}


/*
===================
=
= NetKillPlayer
=
= The current player ran out of health: lie down until respawn
=
===================
*/

void T_NetDead (objtype *ob);
statetype s_netdead = {false,0,0,T_NetDead,NULL,NULL};

static int PlayerNum (objtype *ob)
{
	int i;

	for (i=0;ob && i<numplayers;i++)
		if (np[i].ob == ob)
			return i;
	return -1;
}

void NetKillPlayer (objtype *attacker)
{
	netplayer_t *p = &np[netcontext];
	char	str[80];
	int		k;

	p->dead = true;
	p->respawn = 0;
	player->state = &s_netdead;
	player->ticcount = 0;
	gamestate.attackframe = gamestate.weaponframe = 0;
	if (netcontext == localplayer)
		SD_PlaySound (PLAYERDEATHSND);

	k = PlayerNum (attacker);
	if (!deathmatch)
	{
		sprintf (str,"%s died",NetName (netcontext));
		AddChatLine (str,7);
		return;
	}

	//
	// the score: a frag for the one who did it, a death for us
	//
	p->deaths++;
	if (k >= 0 && k != netcontext)
	{
		np[k].frags++;
		ShowFrags (k);
		sprintf (str,"%s killed %s",NetName (k),NetName (netcontext));
		AddChatLine (str,k == localplayer ? 15 : playercolors[k]);
	}
	else if (attacker && attacker->obclass != playerobj)
	{
		sprintf (str,"%s was killed by the enemy",NetName (netcontext));
		AddChatLine (str,7);
	}
	else
	{
		p->frags--;					// no one else to blame
		ShowFrags (netcontext);
		sprintf (str,"%s died",NetName (netcontext));
		AddChatLine (str,7);
	}
}

//
// ShowFrags: player p's frags are its score (and on its status bar)
//
static void ShowFrags (int p)
{
	int cur = netcontext;

	if (p == netcontext)
		gamestate.score = np[p].frags;
	else
		np[p].score = np[p].frags;
	if (p == localplayer)
	{
		NetSetContext (localplayer);
		DrawScore ();
		NetSetContext (cur);
	}
}

/*
-----------------------------------------------------------------------------

 THE RESPAWN SOUND

 A teleport sound (DSTELEPT), read from RESPAWN.WAD (Freedoom's, which
 comes with the game: FREEDOOM.TXT), else from the player's own
 DOOM2.WAD / DOOM.WAD / ... in the current directory. Nothing of Doom's is in this program. Without any of them,
 an effect of our own is made: a rising shimmer over a burst of noise.
 Either is a digitized sound; with no digitized sound device the extra
 life sound is played instead.

-----------------------------------------------------------------------------
*/

#define RSRATE		7042		// the game's digitized sounds
#define RSPRIORITY	40

static	boolean	rstried;
static	byte	*rsdata;
static	long	rslen;

static long GetLong (byte *p)
{
	return p[0] | (p[1]<<8) | ((long)p[2]<<16) | ((long)p[3]<<24);
}

// DSTELEPT from a Doom WAD, resampled to RSRATE; false if there isn't one
static boolean LoadDoomTeleport (char *name)
{
	FILE	*f;
	byte	hdr[12],ent[16],*lump,*out;
	long	numlumps,dirofs,i,pos,size,rate,count,outlen,j,frac;

	if (!(f = fopen (name,"rb")))
		return false;
	if (fread (hdr,1,12,f) != 12 || (memcmp (hdr,"IWAD",4) && memcmp (hdr,"PWAD",4)))
	{
		fclose (f);
		return false;
	}
	numlumps = GetLong (hdr+4);
	dirofs = GetLong (hdr+8);
	if (numlumps <= 0 || numlumps > 65536 || fseek (f,dirofs,SEEK_SET))
	{
		fclose (f);
		return false;
	}
	pos = size = -1;
	for (i=0;i<numlumps;i++)
	{
		if (fread (ent,1,16,f) != 16)
			break;
		if (!strncmp ((char *)ent+8,"DSTELEPT",8))
		{
			pos = GetLong (ent);
			size = GetLong (ent+4);			// the last one wins, as in Doom
		}
	}
	if (pos < 0 || size < 8 || size > 0x40000 || !(lump = malloc (size)))
	{
		fclose (f);
		return false;
	}
	if (fseek (f,pos,SEEK_SET) || fread (lump,1,size,f) != size)
	{
		free (lump);
		fclose (f);
		return false;
	}
	fclose (f);

	// format 3: word 3, word rate, long count, count 8 bit unsigned samples,
	// the first and last 16 of them padding
	rate = lump[2] | (lump[3]<<8);
	count = GetLong (lump+4);
	if ((lump[0] | (lump[1]<<8)) != 3 || rate < 4000 || rate > 48000
	|| count <= 32 || count > size-8)		// (no overflow with 32 bit longs)
	{
		free (lump);
		return false;
	}
	count -= 32;
	outlen = count * RSRATE / rate;
	if (outlen < 1 || !(out = malloc (outlen)))
	{
		free (lump);
		return false;
	}
	for (i=0;i<outlen;i++)
	{
		pos = i * rate / RSRATE;			// linear interpolation
		frac = (i * rate % RSRATE) * 256 / RSRATE;
		j = pos+1 < count ? pos+1 : pos;
		out[i] = (lump[8+16+pos]*(256-frac) + lump[8+16+j]*frac) >> 8;
	}
	free (lump);
	rsdata = out;
	rslen = outlen;
	return true;
}

// our own: 0.6 s, a tone sweeping up with a fast warble, over noise that
// dies away, faded in and out
static void MakeTeleportSound (void)
{
	long	i,n;
	unsigned long seed = 0x1234567;		// not the game's random numbers
	double	t,phase,freq,env,noise,lp,v;

	n = RSRATE * 6 / 10;
	if (!(rsdata = malloc (n)))
		return;
	phase = lp = 0;
	for (i=0;i<n;i++)
	{
		t = (double)i / n;
		freq = 220 + 1500*t*t + 90*sin (2*3.14159265*38*t);
		phase += 2*3.14159265*freq / RSRATE;
		seed = seed*1103515245 + 12345;
		noise = ((seed >> 16) & 0xffff) / 32768.0 - 1;
		lp += (noise - lp) * (0.15 + 0.6*t);		// brighter as it rises
		env = (t < 0.08 ? t/0.08 : 1) * (1-t) * (1-t);
		v = (0.55*sin (phase) + 0.45*sin (phase*2.01)*t + 0.7*lp*(1-t)) * env;
		v = 128 + v*170;
		rsdata[i] = v < 0 ? 0 : v > 255 ? 255 : (byte)v;
	}
	rslen = n;
}

static void LoadRespawnSound (void)
{
	static char *wads[] = {"RESPAWN.WAD","DOOM2.WAD","DOOM.WAD","PLUTONIA.WAD","TNT.WAD",
		"DOOMU.WAD","FREEDOOM2.WAD","FREEDOOM1.WAD","DOOM1.WAD",NULL};
	int		i;

	rstried = true;
	for (i=0;wads[i];i++)
		if (LoadDoomTeleport (wads[i]))
			return;
	MakeTeleportSound ();
}

// from Respawn: everyone hears it, from where the player comes back
static void RespawnSound (objtype *ob)
{
	if (!rstried)
		LoadRespawnSound ();

	if (netcontext != localplayer)
	{
		SetSoundLoc (ob->x,ob->y);
		if (soundinaudible || (soundquiet && !SD_SoundHasVolume (LASTSOUND)))
			return;
		SD_PositionSound (leftchannel,rightchannel);
	}
	if (DigiMode == sds_Off)
	{
		// nothing digitized: the next best thing
		if (netcontext != localplayer)
			PlaySoundLocGlobal (BONUS1UPSND,ob->x,ob->y);
		else
			SD_PlaySound (BONUS1UPSND);
	}
	else if (SD_PlaySample (rsdata,rslen,RSPRIORITY) && netcontext != localplayer)
	{
		globalsoundx = ob->x;			// UpdateSoundLoc keeps it placed
		globalsoundy = ob->y;
	}
}

static void Respawn (netplayer_t *p)
{
	objtype	*ob = p->ob;

	p->dead = false;
	gamestate.health = 100;
	if (gamestate.ammo < STARTAMMO)
		gamestate.ammo = STARTAMMO;
	gamestate.weapon = gamestate.chosenweapon;
	gamestate.attackframe = gamestate.weaponframe = 0;
	if (deathmatch)
	{
		DMLoadout ();
		DMSpawn (p);			// somewhere new
	}

	ob->x = p->spawnx;
	ob->y = p->spawny;
	ob->angle = p->spawnangle;
	ob->state = &s_player;
	ob->ticcount = 0;
	Thrust (0,0);				// tile, area
	plux = ob->x >> UNSIGNEDSHIFT;
	pluy = ob->y >> UNSIGNEDSHIFT;
	ConnectAreas ();
	RespawnSound (ob);

	DrawHealth ();
	DrawFace ();
	DrawAmmo ();
	DrawWeapon ();
	DrawKeys ();
}

void T_NetDead (objtype *ob)
{
	netplayer_t *p = &np[netcontext];

	if (p->removed)
		return;
	p->respawn += tics;
	if (deathmatch && p->respawn >= DMQUICKTICS
	&& (buttonstate[bt_attack] || buttonstate[bt_use]))
		p->respawn = RESPAWNTICS;		// fire (or use) to come back sooner
	if (p->respawn >= RESPAWNTICS && (deathmatch || SpawnSpotClear (p)))
		Respawn (p);
}


/*
=============================================================================

						DEATHMATCH

=============================================================================
*/

//
// The places to (re)spawn: the floor that can be walked to from the
// map's start (through doors, not pushwalls), worked out the same on
// every machine at the level start
//
static	byte	dmspots[MAPSIZE*MAPSIZE][2];
static	int		numdmspots;

static	int		dmrespawn[MAXSTATS];	// tics until a picked up item is back
static	short	dmorig[MAXSTATS];		// the map's item there (-1: a dropped one)

// -monsters: where each of the map's enemies started, and how long until
// a dead one comes back there
typedef struct
{
	short	tile;
	byte	x,y;
	short	ob;					// its object in objlist (alive or a corpse)
	short	pad;
	long	timer;				// tics until it's back (0: alive)
} dmenemy_t;

static	dmenemy_t dmenemies[MAXACTORS];
static	int		numdmenemies;
static	FILE	*netlog;			// -netlog: NETLOG.TXT, for chasing desyncs

void NetEnemySpawned (int tile, int x, int y, objtype *ob)
{
	dmenemy_t *e;

	if (tile < 0)
	{
		numdmenemies = 0;		// a new level
		return;
	}
	if (numdmenemies == MAXACTORS)
		return;
	e = &dmenemies[numdmenemies++];
	e->tile = tile;
	e->x = x;
	e->y = y;
	e->ob = ob-objlist;
	e->timer = 0;
}

void NetEnemyKilled (objtype *ob)
{
	int i;

	if (!deathmatch || !dmmonsters)
		return;
	for (i=0;i<numdmenemies;i++)
		if (dmenemies[i].ob == ob-objlist && !dmenemies[i].timer)
		{
			dmenemies[i].timer = DMENEMYTICS + (long)US_RndT ()*DMENEMYTICS/255;
			if (netlog)
				fprintf (netlog,"enemy %d (%d at %d,%d) killed: back in %ld tics\n",
					i,dmenemies[i].tile,dmenemies[i].x,dmenemies[i].y,dmenemies[i].timer);
		}
}

boolean NetStatReserved (int i)
{
	return deathmatch && i >= 0 && i < MAXSTATS && dmrespawn[i] > 0;
}


static boolean DMFloor (int x, int y)
{
	unsigned area;

	if (x < 1 || y < 1 || x > MAPSIZE-2 || y > MAPSIZE-2)
		return false;
	if (tilemap[x][y] && !(tilemap[x][y] & 0x80))
		return false;					// a wall (doors are fine)
	area = *(mapsegs[0]+farmapylookup[y]+x);
	if (area < AREATILE || area >= AREATILE+NUMAREAS)
		return tilemap[x][y] & 0x80;	// a door's tile has no area
	return true;
}

static void DMFindSpots (int sx, int sy)
{
	static	byte	seen[MAPSIZE][MAPSIZE];
	static	byte	queue[MAPSIZE*MAPSIZE][2];
	static	int		dx[4] = {1,0,-1,0}, dy[4] = {0,-1,0,1};
	int		head,tail,x,y,d,nx,ny;

	memset (seen,0,sizeof(seen));
	numdmspots = 0;
	head = tail = 0;
	queue[tail][0] = sx;
	queue[tail][1] = sy;
	tail++;
	seen[sx][sy] = 1;
	while (head < tail)
	{
		x = queue[head][0];
		y = queue[head][1];
		head++;
		if (!tilemap[x][y] && !actorat[x][y])
		{
			dmspots[numdmspots][0] = x;
			dmspots[numdmspots][1] = y;
			numdmspots++;
		}
		for (d=0;d<4;d++)
		{
			nx = x+dx[d];
			ny = y+dy[d];
			if (nx < 0 || ny < 0 || nx >= MAPSIZE || ny >= MAPSIZE || seen[nx][ny])
				continue;
			seen[nx][ny] = 1;
			if (!DMFloor (nx,ny) || *(mapsegs[1]+farmapylookup[ny]+nx) == PUSHABLETILE)
				continue;
			queue[tail][0] = nx;
			queue[tail][1] = ny;
			tail++;
		}
	}
	memset (dmrespawn,0,sizeof(dmrespawn));
	for (x=0;x<MAXSTATS;x++)
		dmorig[x] = x < laststatobj-statobjlist ? statobjlist[x].shapenum : -1;
}

//
// DMSpawn: a random spot, the one of a few tries furthest from the others
//
static void DMSpawn (netplayer_t *p)
{
	int		try,i,s,best,x,y;
	long	d,nearest,bestd;

	if (!numdmspots)
		return;					// (the map's start, as set already)
	best = 0;
	bestd = -1;
	for (try=0;try<6;try++)
	{
		s = ((US_RndT() << 8) | US_RndT()) % numdmspots;
		x = dmspots[s][0];
		y = dmspots[s][1];
		if (actorat[x][y] || tilemap[x][y])
			continue;			// a door that closed on it, a pushwall...
		nearest = 0x7fffffffl;
		for (i=0;i<numplayers;i++)
		{
			if (&np[i] == p || np[i].dead || !np[i].ob)
				continue;
			d = labs (((long)x<<TILESHIFT)+TILEGLOBAL/2 - np[i].ob->x)
				+ labs (((long)y<<TILESHIFT)+TILEGLOBAL/2 - np[i].ob->y);
			if (d < nearest)
				nearest = d;
		}
		if (nearest > bestd)
		{
			bestd = nearest;
			best = s;
		}
	}
	p->spawnx = ((long)dmspots[best][0]<<TILESHIFT)+TILEGLOBAL/2;
	p->spawny = ((long)dmspots[best][1]<<TILESHIFT)+TILEGLOBAL/2;
	p->spawnangle = (US_RndT() & 3) * 90;
}

//
// DMLoadout: every weapon and key (for the current player)
//
static void DMLoadout (void)
{
	gamestate.health = 100;
	if (gamestate.ammo < DMAMMO)
		gamestate.ammo = DMAMMO;
	gamestate.bestweapon = wp_chaingun;
	gamestate.weapon = gamestate.chosenweapon = wp_chaingun;
	gamestate.keys = 3;
	gamestate.attackframe = gamestate.weaponframe = 0;
}

//
// DMPlace: put the current player at its spawn spot
//
static void DMPlace (netplayer_t *p)
{
	objtype *ob = p->ob;

	ob->x = p->spawnx;
	ob->y = p->spawny;
	ob->angle = p->spawnangle;
	ob->tilex = ob->x >> TILESHIFT;
	ob->tiley = ob->y >> TILESHIFT;
	ob->areanumber = *(mapsegs[0]+farmapylookup[ob->tiley]+ob->tilex)-AREATILE;
	ob->state = &s_player;
	ob->ticcount = 0;
	plux = ob->x >> UNSIGNEDSHIFT;
	pluy = ob->y >> UNSIGNEDSHIFT;
}

//
// DMItems: from RunFrame: the items picked up come back after a while, and
// (-monsters) the enemies killed, where they started (the corpse goes)
//
static void DMItems (void)
{
	int		i,n,p;
	dmenemy_t *e;
	objtype	*old,*before;

	n = laststatobj - statobjlist;
	for (i=0;i<n;i++)
		if (dmrespawn[i] && (dmrespawn[i] -= tics) <= 0)
		{
			dmrespawn[i] = 0;
			if (statobjlist[i].shapenum == -1 && dmorig[i] != -1)
			{
				statobjlist[i].shapenum = dmorig[i];
				statobjlist[i].flags |= FL_BONUS;
			}
		}

	for (i=0,e=dmenemies;i<numdmenemies;i++,e++)
	{
		if (!e->timer || (e->timer -= tics) > 0)
			continue;
		e->timer = 0;

		// not on top of anybody: later, then
		for (p=0;p<numplayers;p++)
			if (!np[p].dead && np[p].ob->tilex == e->x && np[p].ob->tiley == e->y)
				break;
		if (p < numplayers || actorat[e->x][e->y] || tilemap[e->x][e->y])
		{
			e->timer = 70*5;
			continue;
		}

		old = &objlist[e->ob];
		if (old->state && old->obclass != playerobj && !(old->flags & FL_SHOOTABLE))
			RemoveObj (old);
		before = lastobj;
		ScanInfoTile (e->tile,e->x,e->y);
		if (lastobj != before)
			e->ob = lastobj-objlist;
		if (netlog)
			fprintf (netlog,"enemy %d back (objects: %d)\n",i,objcount);
	}
}


/*
===================
=
= NetPickups
=
= What DrawScaleds does with bonus items, for any player
=
===================
*/

static void NetPickups (objtype *ob)
{
	statobj_t	*statptr;
	fixed	vcos,vsin,vx,vy,gx,gy,nx,ny;
	int		oldkeys,i;

	vcos = costable[ob->angle];
	vsin = sintable[ob->angle];
	vx = ob->x - FixedByFrac(NETFOCAL,vcos);
	vy = ob->y + FixedByFrac(NETFOCAL,vsin);

	for (statptr = &statobjlist[0] ; statptr != laststatobj ; statptr++)
	{
		if (statptr->shapenum == -1 || !(statptr->flags & FL_BONUS))
			continue;
		if (abs(statptr->tilex - ob->tilex) > 1 || abs(statptr->tiley - ob->tiley) > 1)
			continue;

		gx = ((long)statptr->tilex<<TILESHIFT)+0x8000-vx;
		gy = ((long)statptr->tiley<<TILESHIFT)+0x8000-vy;
		nx = FixedByFrac(gx,vcos) - FixedByFrac(gy,vsin) - 0x2000;
		ny = FixedByFrac(gy,vcos) + FixedByFrac(gx,vsin);
		if (nx < MINDIST || nx >= TILEGLOBAL || ny <= -TILEGLOBAL/2 || ny >= TILEGLOBAL/2)
			continue;

		oldkeys = gamestate.keys;
		GetBonus (statptr);
		if (deathmatch && statptr->shapenum == -1 && dmorig[statptr-statobjlist] != -1)
			dmrespawn[statptr-statobjlist] = DMITEMTICS;	// (not what guards drop)
		if (gamestate.keys != oldkeys)
		{
			// keys are for everybody
			for (i=0;i<numplayers;i++)
				if (i != netcontext)
					np[i].keys |= gamestate.keys;

			// and show up on our status bar, whoever picked them up
			// (DrawKeys draws only for the local player's context)
			if (netcontext != localplayer)
			{
				i = netcontext;
				NetSetContext (localplayer);
				DrawKeys ();
				NetSetContext (i);
			}
		}
	}
}


/*
===================
=
= NetPlaySound
=
= SD_PlayHook: the game plays some sounds unpositioned: a player's own
= (shots, pickups, the locked door, a pushwall...), which is right for
= the local player, and the bosses' and a few others' voices, meant to be
= heard anywhere. In a netgame that put everybody's at full volume in our
= ears, however far away and whatever walls were between. A sound made
= while an actor thinks (obj: in RunFrame) is played from where that
= actor is, so it fades with distance and closed doors like the rest.
= Our own player's, and anything outside the game logic (menus, chat),
= is left alone: -1.
=
===================
*/

static	boolean	inrunframe;		// RunFrame's actor loop: obj is the one thinking

static int NetPlaySound (soundnames s)
{
	if (!netgame || !inrunframe || !obj || obj == np[localplayer].ob)
		return -1;
	PlaySoundLocGlobal (s,obj->x,obj->y);
	return 0;
}


/*
===================
=
= NetDamagePlayer
=
= Friendly fire: the current player hit another one
=
===================
*/

void NetDamagePlayer (objtype *target, int damage, objtype *attacker)
{
	int	i,shooter;

	for (i=0;i<numplayers;i++)
		if (np[i].ob == target)
			break;
	if (i == numplayers || np[i].dead)
		return;
	shooter = netcontext;
	NetSetContext (i);
	TakeDamage (damage,attacker);
	NetSetContext (shooter);
}

void NetPlayerHurt (void)
{
	np[netcontext].pain = 6;
}


/*
===================
=
= NetPlayerBlocked
=
= Players are solid to each other, like actors: a move that ends up
= within MINACTORDIST of another live player is refused, unless they
= already overlapped (respawned on each other) and it takes them apart
=
===================
*/

long	netoldx,netoldy;		// set by ClipMove: where the mover started

boolean NetPlayerBlocked (objtype *ob)
{
	int		i;
	objtype	*other;
	long	dx,dy,olddx,olddy,newd,oldd;

	for (i=0;i<numplayers;i++)
	{
		other = np[i].ob;
		if (other == ob || np[i].dead)
			continue;
		dx = labs (ob->x - other->x);
		dy = labs (ob->y - other->y);
		if (dx >= MINACTORDIST || dy >= MINACTORDIST)
			continue;
		olddx = labs (netoldx - other->x);
		olddy = labs (netoldy - other->y);
		newd = dx > dy ? dx : dy;
		oldd = olddx > olddy ? olddx : olddy;
		if (oldd < MINACTORDIST && newd > oldd)
			continue;			// getting out of an overlap
		return true;
	}
	return false;
}

static boolean SpawnSpotClear (netplayer_t *p)
{
	int i;

	for (i=0;i<numplayers;i++)
		if (&np[i] != p && !np[i].dead
		&& labs (np[i].ob->x - p->spawnx) < MINACTORDIST
		&& labs (np[i].ob->y - p->spawny) < MINACTORDIST)
			return false;
	return true;
}


/*
=============================================================================

						 HOW THE OTHERS LOOK

The other players are drawn with BJ from PLAYERS.WL6 when it's there,
otherwise with the SS sprites (eight directions, a gun, walking,
shooting, pain and death frames). Either way they're recolored (BJ's
vest, the SS's uniform) so each player has a color and nobody takes
them for an enemy. The copies are made when first needed.

=============================================================================
*/

#define NUMSSFRAMES	(SPR_SS_SHOOT3-SPR_SS_S_1+1)

static	byte	*netshapes[MAXPLAYERS][NUMSSFRAMES];
static	byte	netcolors[MAXPLAYERS][256];
static	boolean	netcolorsmade;

/*
-----------------------------------------------------------------------------

 PLAYERS.WL6 (PLAYERS.<the data's extension>), made by msdos/mkplayers.py
 from a sprite sheet: BJ in 8 directions standing, walking, aiming and
 firing, plus pain and death frames. When it's there the other players
 are drawn with it instead of the SS; its team[] colors (the vest) are
 the ones recolored per player.

	char	magic[4]	"WLPS"
	word	version		1
	word	numframes
	byte	team[256]
	long	offset[numframes], word length[numframes]
	...		frames, t_compshape like VSWAP's sprites

 The scaler doesn't check what it reads, so every frame is checked here.

-----------------------------------------------------------------------------
*/

#define PF_STAND	0			// + direction
#define PF_WALK		8			// + walk frame*8 + direction
#define PF_AIM		40			// + direction
#define PF_FIRE		48			// + direction
#define PF_PAIN		56
#define PF_DIE1		57			// 57-60
#define PF_DEAD		61
#define PF_NUM		62

#define PFFRAME		0x3c0		// frame numbers above every sprite's: the file's

static	boolean	pftried,pfloaded;
static	byte	*pfdata;
static	byte	*pfframe[PF_NUM];
static	word	pflength[PF_NUM];
static	byte	pfteam[256];
static	byte	*pfshapes[MAXPLAYERS][PF_NUM];

static word GetWord (byte *p)
{
	return p[0] | (p[1]<<8);
}

static boolean CheckFrame (byte *f, unsigned len)
{
	unsigned	left,right,col,ofs,end,top,start;

	if (len < 6)
		return false;
	left = GetWord (f);
	right = GetWord (f+2);
	if (left > right || right > 63 || 4+2*(right-left+1) > len)
		return false;
	for (col=0 ; col <= right-left ; col++)
	{
		ofs = GetWord (f+4+col*2);
		for (;;)
		{
			if (ofs+2 > len)
				return false;
			end = GetWord (f+ofs);
			if (!end)
				break;
			if (ofs+6 > len)
				return false;
			top = GetWord (f+ofs+2);
			start = GetWord (f+ofs+4);
			if ((end|start)&1 || start >= end || end > 128
			|| (word)(top+start/2) >= len || (word)(top+end/2) > len
			|| (word)(top+start/2) > (word)(top+end/2))
				return false;
			ofs += 6;
		}
	}
	return true;
}

static void LoadPlayerSprites (void)
{
	char	fname[16];
	FILE	*f;
	long	size,ofs;
	unsigned len;
	int		i,n;

	pftried = true;
	strcpy (fname,"PLAYERS.");
	strcat (fname,extension);
	f = fopen (fname,"rb");
	if (!f)
		return;
	fseek (f,0,SEEK_END);
	size = ftell (f);
	fseek (f,0,SEEK_SET);
	if (size < 8+256 || size > 0x100000 || !(pfdata = malloc (size)))
	{
		fclose (f);
		return;
	}
	if (fread (pfdata,1,size,f) != size)
		size = 0;
	fclose (f);

	n = size ? GetWord (pfdata+6) : 0;
	if (memcmp (pfdata,"WLPS",4) || GetWord (pfdata+4) != 1 || n < PF_NUM
	|| 8+256+n*6 > size)
		goto bad;
	memcpy (pfteam,pfdata+8,256);
	for (i=0;i<PF_NUM;i++)
	{
		byte *t = pfdata+8+256+i*4;
		ofs = t[0] | (t[1]<<8) | ((long)t[2]<<16) | ((long)t[3]<<24);
		len = GetWord (pfdata+8+256+n*4+i*2);
		if (ofs < 0 || ofs+len > size || !CheckFrame (pfdata+ofs,len))
			goto bad;
		pfframe[i] = pfdata+ofs;
		pflength[i] = len;
	}
	pfloaded = true;
	return;

bad:
	free (pfdata);
	pfdata = NULL;
	{
		char str[40];
		sprintf (str,"%s is damaged: SS used",fname);
		AddChatLine (str,7);
	}
}

static void MakeColors (void)
{
	int		p,c,i,r,g,b,nr,ng,nb,best,bestd,d,dr,dg,db;
	byte	*pal = gamepal;

	for (p=0;p<MAXPLAYERS;p++)
		for (c=0;c<256;c++)
		{
			netcolors[p][c] = c;
			r = pal[c*3]; g = pal[c*3+1]; b = pal[c*3+2];
			if (pfloaded)
			{
				// BJ's gold vest
				if (!pfteam[c])
					continue;
				switch (p)
				{
				case 0: nr = b; ng = (r+g)/2; nb = b; break;		// green
				case 1: nr = (r+g)/2; ng = b; nb = b; break;		// red
				case 2: continue;									// gold
				default: nr = ng = nb = (r+g)/2; break;				// white
				}
			}
			else
			{
				if (b < r+4 || b < g+4)
					continue;			// only the blue of the uniform
				switch (p)
				{
				case 0: nr = r; ng = b; nb = g; break;			// green
				case 1: nr = b; ng = g; nb = r; break;			// red
				case 2: nr = b; ng = b*3/4; nb = r/2; break;	// gold
				default: nr = ng = nb = (r+g+b*2)/3; break;		// white
				}
			}
			best = c;
			bestd = 0x7fffffff;
			for (i=0;i<255;i++)
			{
				if (pfloaded && pfteam[i] && p != 2)
					continue;			// not back to gold
				dr = pal[i*3]-nr; dg = pal[i*3+1]-ng; db = pal[i*3+2]-nb;
				d = dr*dr*3 + dg*dg*4 + db*db*2;
				if (d < bestd)
				{
					bestd = d;
					best = i;
				}
			}
			netcolors[p][c] = best;
		}
	netcolorsmade = true;
}

// a copy of a shape with exactly the pixels the scaler reads recolored
static byte *Recolor (byte *orig, unsigned length, int p)
{
	int		col,src,end,start;
	byte	*copy;
	word	*cmd,top;
	t_compshape *shape;

	copy = malloc (length);
	if (!copy)
		return NULL;
	memcpy (copy,orig,length);

	shape = (t_compshape *)orig;
	for (col=0 ; col <= shape->rightpix-shape->leftpix ; col++)
	{
		cmd = (word *)(orig + shape->dataofs[col]);
		while ((end = cmd[0]/2) != 0)
		{
			top = cmd[1];
			start = cmd[2]/2;
			for (src=start;src<end;src++)
				copy[(word)(top+src)] = netcolors[p][orig[(word)(top+src)]];
			cmd += 3;
		}
	}
	return copy;
}

memptr NetShape (int shapenum)
{
	int		p,frame,page;
	byte	*copy;

	p = (shapenum >> 10) & 3;
	frame = shapenum & 0x3ff;

	if (!netcolorsmade)
		MakeColors ();

	if (frame >= PFFRAME)
	{
		frame -= PFFRAME;
		if (!pfshapes[p][frame])
			pfshapes[p][frame] = Recolor (pfframe[frame],pflength[frame],p);
		return pfshapes[p][frame] ? pfshapes[p][frame] : pfframe[frame];
	}

	if (frame < SPR_SS_S_1 || frame > SPR_SS_SHOOT3)
		return PM_GetSpritePage (frame);

	copy = netshapes[p][frame-SPR_SS_S_1];
	if (copy)
		return copy;

	page = PMSpriteStart + frame;
	copy = Recolor (PM_GetPage (page),PMPages[page].length,p);
	if (!copy)
		return PM_GetPage (page);
	netshapes[p][frame-SPR_SS_S_1] = copy;
	return copy;
}


/*
===================
=
= NetPlayerShape
=
= Which frame of another player the local view shows (0 = none); called
= after TransformActor, so ob->viewx is set
=
===================
*/

int NetPlayerShape (objtype *ob)
{
	int			i,angle,viewangle,frame,wf;
	netplayer_t	*p;

	for (i=0;i<numplayers;i++)
		if (np[i].ob == ob)
			break;
	if (i == numplayers || np[i].removed)
		return 0;
	p = &np[i];

	if (!pftried)
		LoadPlayerSprites ();

	if (p->dead)
	{
		if (pfloaded)
			frame = p->respawn < 24 ? PFFRAME+PF_DIE1+p->respawn/6 : PFFRAME+PF_DEAD;
		else if (p->respawn < 8)
			frame = SPR_SS_DIE_1;
		else if (p->respawn < 16)
			frame = SPR_SS_DIE_2;
		else if (p->respawn < 24)
			frame = SPR_SS_DIE_3;
		else
			frame = SPR_SS_DEAD;
		return NETSHAPEFLAG | (i<<10) | frame;
	}

	// which way it faces from here
	viewangle = player->angle + (centerx - ob->viewx)/8;
	angle = (viewangle-180) - ob->angle + ANGLES/16;
	while (angle >= ANGLES)
		angle -= ANGLES;
	while (angle < 0)
		angle += ANGLES;
	angle /= ANGLES/8;

	wf = i == netcontext ? gamestate.weaponframe : p->weaponframe;

	if (pfloaded)
	{
		// BJ aims and fires in every direction, not only at us
		if (p->pain)
			frame = PF_PAIN;
		else if (wf)
			frame = (wf == 2 ? PF_FIRE : PF_AIM) + angle;
		else if (p->moving)
			frame = PF_WALK + ((p->anim >> 14) & 3)*8 + angle;
		else
			frame = PF_STAND + angle;
		return NETSHAPEFLAG | (i<<10) | (PFFRAME+frame);
	}

	if (p->pain)
		frame = SPR_SS_PAIN_1;
	else if (wf && (angle == 0 || angle == 1 || angle == 7))
		frame = wf == 1 ? SPR_SS_SHOOT1 : wf == 2 ? SPR_SS_SHOOT2 : SPR_SS_SHOOT3;
	else if (p->moving)
		frame = SPR_SS_W1_1 + ((p->anim >> 14) & 3)*8 + angle;
	else
		frame = SPR_SS_S_1 + angle;

	return NETSHAPEFLAG | (i<<10) | frame;
}


/*
=============================================================================

						 IPX

=============================================================================
*/

#ifndef __linux__

#define IPX_SOCKET	0x7135
#define PKTMAX		200
#define NUMRECV		16
#define NUMSEND		MAXPLAYERS
#define ECBSIZE		48
#define HDRSIZE		30
#define SLOTSIZE	(ECBSIZE+HDRSIZE+PKTMAX)

static	boolean	ipxup;
static	word	ipxseg,ipxoff;
static	int		dossel;
static	word	dosseg;
static	unsigned long doslin;
static	word	sockw;				// socket, network byte order
static	byte	myaddr[10];

static unsigned long SlotLin (int slot)
{
	return doslin + (unsigned long)slot*SLOTSIZE;
}

static void IPXCall (__dpmi_regs *r)
{
	r->x.cs = ipxseg;
	r->x.ip = ipxoff;
	r->x.ss = r->x.sp = 0;
	r->x.flags = 0;
	__dpmi_simulate_real_mode_procedure_retf (r);
}

static void IPXListen (int slot)
{
	unsigned long l = SlotLin (slot);
	word	off = (word)(l - (unsigned long)dosseg*16);
	__dpmi_regs r;
	int		i;

	for (i=0;i<ECBSIZE;i++)
		_farpokeb (_dos_ds,l+i,0);
	_farpokew (_dos_ds,l+10,sockw);
	_farpokew (_dos_ds,l+34,1);						// one fragment
	_farpokew (_dos_ds,l+36,off+ECBSIZE);
	_farpokew (_dos_ds,l+38,dosseg);
	_farpokew (_dos_ds,l+40,HDRSIZE+PKTMAX);

	memset (&r,0,sizeof(r));
	r.x.bx = 4;
	r.x.es = dosseg;
	r.x.si = off;
	IPXCall (&r);
}

static void IPXSend (int slot, byte *addr, void *data, int len)
{
	unsigned long l = SlotLin (NUMRECV+slot);
	word	off = (word)(l - (unsigned long)dosseg*16);
	unsigned long h = l+ECBSIZE;
	__dpmi_regs r;
	long	start;
	int		i;

	start = _farpeekl (_dos_ds,0x46c);
	while (_farpeekb (_dos_ds,l+8))					// still going out
	{
		memset (&r,0,sizeof(r));
		r.x.bx = 0x0a;
		IPXCall (&r);
		if (_farpeekl (_dos_ds,0x46c) - start > 3)
			return;
	}

	for (i=0;i<ECBSIZE;i++)
		_farpokeb (_dos_ds,l+i,0);
	_farpokew (_dos_ds,l+10,sockw);
	for (i=0;i<6;i++)
		_farpokeb (_dos_ds,l+28+i,addr[4+i]);		// immediate address
	_farpokew (_dos_ds,l+34,1);
	_farpokew (_dos_ds,l+36,off+ECBSIZE);
	_farpokew (_dos_ds,l+38,dosseg);
	_farpokew (_dos_ds,l+40,HDRSIZE+len);

	_farpokew (_dos_ds,h+0,0xffff);					// checksum
	_farpokew (_dos_ds,h+2,0);
	_farpokeb (_dos_ds,h+4,0);
	_farpokeb (_dos_ds,h+5,4);						// IPX packet
	for (i=0;i<10;i++)
		_farpokeb (_dos_ds,h+6+i,addr[i]);
	_farpokew (_dos_ds,h+16,sockw);
	dosmemput (data,len,h+HDRSIZE);

	memset (&r,0,sizeof(r));
	r.x.bx = 3;
	r.x.es = dosseg;
	r.x.si = off;
	IPXCall (&r);
}

static boolean IPXStartup (void)
{
	__dpmi_regs r;
	int		seg,i;

	memset (&r,0,sizeof(r));
	r.x.ax = 0x7a00;
	__dpmi_int (0x2f,&r);
	if (r.h.al != 0xff)
		return false;
	ipxseg = r.x.es;
	ipxoff = r.x.di;

	seg = __dpmi_allocate_dos_memory ((SLOTSIZE*(NUMRECV+NUMSEND)+16+15)/16,&dossel);
	if (seg == -1)
		return false;
	dosseg = seg;
	doslin = (unsigned long)seg*16;

	sockw = (IPX_SOCKET>>8) | ((IPX_SOCKET&0xff)<<8);
	memset (&r,0,sizeof(r));
	r.x.bx = 0;
	r.h.al = 0;
	r.x.dx = sockw;
	IPXCall (&r);
	if (r.h.al == 0xff)			// left open by a crash: start over
	{
		memset (&r,0,sizeof(r));
		r.x.bx = 1;
		r.x.dx = sockw;
		IPXCall (&r);
		memset (&r,0,sizeof(r));
		r.x.bx = 0;
		r.x.dx = sockw;
		IPXCall (&r);
	}
	if (r.h.al)
	{
		__dpmi_free_dos_memory (dossel);
		return false;
	}

	// local address
	memset (&r,0,sizeof(r));
	r.x.bx = 9;
	r.x.es = dosseg;
	r.x.si = SLOTSIZE*(NUMRECV+NUMSEND);
	IPXCall (&r);
	dosmemget (doslin+SLOTSIZE*(NUMRECV+NUMSEND),10,myaddr);

	for (i=0;i<NUMSEND;i++)
		_farpokeb (_dos_ds,SlotLin(NUMRECV+i)+8,0);
	for (i=0;i<NUMRECV;i++)
		IPXListen (i);

	ipxup = true;
	return true;
}

void NetShutdown (void)
{
	__dpmi_regs r;

	if (!ipxup)
		return;
	NetLeaving ();				// tell the others, if we're playing
	ipxup = false;
	memset (&r,0,sizeof(r));
	r.x.bx = 1;
	r.x.dx = sockw;
	IPXCall (&r);				// cancels the listens
	__dpmi_free_dos_memory (dossel);
}

//
// IPXGet: next received packet, or 0
//
static int IPXGet (void *buf, byte *from)
{
	static int next;
	int		i,slot,len;
	unsigned long l;
	__dpmi_regs r;

	memset (&r,0,sizeof(r));
	r.x.bx = 0x0a;					// relinquish control: lets it work
	IPXCall (&r);

	for (i=0;i<NUMRECV;i++)
	{
		slot = (next+i)%NUMRECV;
		l = SlotLin (slot);
		if (_farpeekb (_dos_ds,l+8))
			continue;				// still listening
		next = slot+1;
		len = 0;
		if (!_farpeekb (_dos_ds,l+9))
		{
			len = ((_farpeekb (_dos_ds,l+ECBSIZE+2)<<8) | _farpeekb (_dos_ds,l+ECBSIZE+3)) - HDRSIZE;
			if (len < 0 || len > PKTMAX)
				len = 0;
			dosmemget (l+ECBSIZE+18,10,from);
			dosmemget (l+ECBSIZE+HDRSIZE,len,buf);
		}
		IPXListen (slot);
		if (len > 0)
			return len;
	}
	return 0;
}

#else	// __linux__

/*
-----------------------------------------------------------------------------

 UDP (sys_linux.c) in IPX's place: the 10 byte addresses hold an IPv4
 address and a port. -port N picks ours (27135 by default); hellos go to
 the local network's broadcast address on that port, and to -connect
 host[:port] for a game somewhere else.

-----------------------------------------------------------------------------
*/

#define PKTMAX		200
#define UDPPORT		27135

static	boolean	ipxup;
static	byte	myaddr[10];
static	boolean	ipxnet;				// -ipxnet / -ipxserver: DOSBox's IPX, not UDP

static void IPXSend (int slot, byte *addr, void *data, int len)
{
	if (ipxnet)
		SYS_IpxSend (addr,data,len);
	else
		SYS_NetSend (addr,data,len);
}

static boolean IPXStartup (void)
{
	int		i,port = UDPPORT;
	char	*server = NULL;
	int		serve = 0;

	for (i=1;i<_argc;i++)
	{
		if (!stricmp (_argv[i],"-port") && i+1 < _argc)
			port = atoi (_argv[i+1]);
		if (!stricmp (_argv[i],"-ipxnet") && i+1 < _argc)
			server = _argv[i+1];
		if (!stricmp (_argv[i],"-ipxserver"))
			serve = i+1 < _argc && isdigit (_argv[i+1][0]) ? atoi (_argv[i+1]) : 213;
	}
	if (server || serve)
	{
		// with DOS players (in DOSBox): IPX, as theirs
		if (!SYS_IpxStartup (server,serve))
			return false;
		SYS_IpxMyAddr (myaddr);
		ipxnet = true;
		ipxup = true;
		return true;
	}
	if (!SYS_NetStartup (port))
		return false;
	SYS_NetMyAddr (myaddr);
	ipxup = true;
	return true;
}

void NetShutdown (void)
{
	if (!ipxup)
		return;
	NetLeaving ();
	ipxup = false;
	SYS_NetShutdown ();
}

static int IPXGet (void *buf, byte *from)
{
	if (ipxnet)
		return SYS_IpxGet (buf,PKTMAX,from);
	return SYS_NetGet (buf,PKTMAX,from);
}

#endif	// __linux__


/*
=============================================================================

						 PACKETS

=============================================================================
*/

enum {pk_hello=1,pk_tics,pk_quit,pk_chat,pk_query,pk_info,pk_join,pk_snap,pk_snapack,pk_full,pk_drop};

#define NETVERSION	4

typedef struct
{
	byte	magic[2];
	byte	type,version;
	byte	numnodes,episode,level,skill;
	byte	gotcount,friendlyfire,numpeers,pad;
	unsigned long nonce;		// who sent it (its own broadcasts come back)
	char	name[NETNAMELEN+1];
	byte	peers[MAXPLAYERS][10];	// the others it has found (-connect)
} __attribute__((packed)) hellopacket_t;

// the arbiter's word on who comes in: with every tic packet whose
// controls include the frame of the join event
typedef struct
{
	byte	slot,pad[3];
	long	frame;
	byte	addr[10];
	char	name[NETNAMELEN+1];
} __attribute__((packed)) joinrec_t;

typedef struct
{
	byte	magic[2];
	byte	type,player;
	byte	seq,count,hasjoin,pad;
	long	first;				// frame of cmds[0]
	long	ack;				// frames the sender has finished
	long	hashframe;
	unsigned long hash;
	joinrec_t join;
	netcmd_t cmds[NETBACKUP];
} __attribute__((packed)) ticpacket_t;

// deathmatch: is there a game? (any player answers, with the arbiter)
typedef struct
{
	byte	magic[2];
	byte	type,version;
	unsigned long nonce;
} __attribute__((packed)) querypacket_t;

typedef struct
{
	byte	magic[2];
	byte	type,version;
	byte	episode,level,skill,players;
	unsigned long nonce;		// the asker's
	byte	arbiter[10];		// where to ask to come in (0: the sender)
	byte	monsters,pad[3];
	unsigned long build;		// BuildId: a snapshot only fits the same program
} __attribute__((packed)) infopacket_t;

typedef struct
{
	byte	magic[2];
	byte	type,version;
	unsigned long nonce;
	char	name[NETNAMELEN+1];
	unsigned long build;
} __attribute__((packed)) joinpacket_t;

#define SNAPCHUNK	176
#define SNAPWINDOW	12			// chunks out at once (IPX listens for 16)

typedef struct
{
	byte	magic[2];
	byte	type,len;
	word	chunk,total;
	unsigned long nonce;		// the newcomer's
	byte	data[SNAPCHUNK];
} __attribute__((packed)) snappacket_t;

typedef struct
{
	byte	magic[2];
	byte	type,pad;
	word	got;				// chunks in, in a row
	word	pad2;
	unsigned long nonce;
} __attribute__((packed)) snapackpacket_t;

// deathmatch: the arbiter gave up on a player (its machine went away
// without a word): its last frame, for everyone
typedef struct
{
	byte	magic[2];
	byte	type,slot;
	long	lastframe;
} __attribute__((packed)) droppacket_t;

static	droppacket_t dropped;
static	int		dropannounce;			// frames to keep telling them

static	byte	broadcast[10] = {0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff};

static	byte	levelseq;			// counts play loops, the same everywhere
static	long	netframe;			// frames finished this level
static	long	maketic;			// next frame to sample controls for
static	byte	oldseq;				// the previous level, for late barrier packets
static	long	oldack;

static	unsigned long hashring[NETRING];
static	long	hashframes[NETRING];
static	long	desyncframe = -1;

static	int		hello_episode,hello_level,hello_skill;
static	unsigned long mynonce;
static	boolean	inplay;				// in NetPlayLoop: tell the others when we go

// -connect: where else to say hello (Linux: a game not on this network)
#define MAXCONTACTS	8
static	byte	contacts[MAXCONTACTS][10];
static	int		numcontacts;

// deathmatch: a newcomer waiting to come in (on the arbiter)
static	struct
{
	boolean	active;
	int		slot;
	long	frame;				// of the join event, -1 until it's sent
	unsigned long nonce;
	byte	addr[10];
	char	name[NETNAMELEN+1];
} pending;
static	joinrec_t joinrecs[MAXPLAYERS];	// as the arbiter told us
static	joinrec_t lastjoin;			// the arbiter's last: sent while it's in reach
static	boolean	havelastjoin;
static	boolean	dmjoining;			// we're the newcomer
static	byte	arbaddr[10];		// whom to ask

static long BiosTicks (void)
{
	return _farpeekl (_dos_ds,0x46c);
}

static void AddContact (byte *addr)
{
	int i;

	for (i=0;i<10 && !addr[i];i++)
		;
	if (i == 10 || !memcmp (addr,broadcast,10))
		return;
	for (i=0;i<numcontacts;i++)
		if (!memcmp (contacts[i],addr,10))
			return;
	if (numcontacts < MAXCONTACTS)
		memcpy (contacts[numcontacts++],addr,10);
}

//
// SendAround: to the local network, and to whoever -connect named
//
static void SendAround (void *pk, int len)
{
	int i;

	IPXSend (0,broadcast,pk,len);
	for (i=0;i<numcontacts;i++)
		IPXSend (0,contacts[i],pk,len);
}

static int Arbiter (void)
{
	int i;

	for (i=0;i<numplayers;i++)
		if (!np[i].removed)
			return i;
	return localplayer;
}

static void SendTics (int type)
{
	ticpacket_t pk;
	netplayer_t *me = &np[localplayer];
	long	f;
	int		i,len;

	memset (&pk,0,sizeof(pk));
	pk.magic[0] = 'W'; pk.magic[1] = '3';
	pk.type = type;
	pk.player = localplayer;
	pk.seq = levelseq;
	f = maketic - NETBACKUP;
	if (f < NETDELAY)
		f = NETDELAY;
	if (f < me->firstcmd)
		f = me->firstcmd;
	pk.first = f;
	for (i=0 ; f < maketic ; f++,i++)
		pk.cmds[i] = me->cmds[f & (NETRING-1)];
	pk.count = i;
	pk.ack = netframe;
	pk.hashframe = netframe-1;
	pk.hash = netframe ? hashring[(netframe-1) & (NETRING-1)] : 0;

	// a join event of ours in there: say who it is
	if (havelastjoin && lastjoin.frame >= pk.first && lastjoin.frame < maketic)
	{
		pk.hasjoin = 1;
		pk.join = lastjoin;
	}

	len = sizeof(pk)-sizeof(pk.cmds)+pk.count*sizeof(netcmd_t);
	for (i=0;i<numplayers;i++)
		if (i != localplayer && !np[i].gone)
		{
			IPXSend (i,np[i].addr,&pk,len);
			if (dropannounce)
				IPXSend (i,np[i].addr,&dropped,sizeof(dropped));
		}
	if (dropannounce)
		dropannounce--;
}

static void MakeHello (hellopacket_t *h, int found, byte (*addrs)[10])
{
	int i;

	memset (h,0,sizeof(*h));
	h->magic[0] = 'W'; h->magic[1] = '3';
	h->type = pk_hello;
	h->version = NETVERSION;
	h->numnodes = numplayers;
	h->episode = hello_episode;
	h->level = hello_level;
	h->skill = hello_skill;
	h->friendlyfire = hello_ff;
	h->gotcount = found;
	h->nonce = mynonce;
	strcpy (h->name,myname);
	for (i=1;addrs && i<found && i<=MAXPLAYERS;i++)
		memcpy (h->peers[h->numpeers++],addrs[i],10);
}

static void SendHello (byte *to)
{
	hellopacket_t pk;

	MakeHello (&pk,numplayers,NULL);
	IPXSend (0,to,&pk,sizeof(pk));
}

/*
=============================================================================

						 CHAT

T starts a message, Enter sends it to everyone, Esc drops it; while
typing, Alt+1..Alt+0 send the macros from CHATMACS.CFG (SETUP edits
them). Messages show at the top of the view for a few seconds. Chat
doesn't touch the game, so it's sent on its own, twice, not in lockstep.

=============================================================================
*/

#define CHATLEN		60
#define NUMLINES	4
#define LINETICS	(70*6)

typedef struct
{
	byte	magic[2];
	byte	type,player;
	byte	msgid,len,pad[2];
	char	text[CHATLEN+1];
} __attribute__((packed)) chatpacket_t;

static	boolean	chatting;
static	char	chatbuf[CHATLEN+1];
static	int		chatlen;
static	byte	chatid,lastchatid[MAXPLAYERS];
static	boolean	gotchatid[MAXPLAYERS];

static	char	chatlines[NUMLINES][CHATLEN+24];
static	int		chatcolor[NUMLINES];
static	long	chattime[NUMLINES];
static	long	chatclock;

char	chatmacros[10][CHATLEN+1] =
{
	"Need ammo!",
	"Need health!",
	"Follow me!",
	"Wait for me!",
	"I've got the key.",
	"Found the exit!",
	"Watch out, guards!",
	"There's a secret here.",
	"Yes.",
	"No.",
};

static void LoadMacros (void)
{
	FILE	*f;
	char	line[128];
	int		i,l;

	f = fopen ("CHATMACS.CFG","r");
	if (!f)
		return;
	for (i=0;i<10 && fgets (line,sizeof(line),f);i++)
	{
		l = strlen (line);
		while (l && (line[l-1] == '\n' || line[l-1] == '\r'))
			line[--l] = 0;
		strncpy (chatmacros[i],line,CHATLEN);
		chatmacros[i][CHATLEN] = 0;
	}
	fclose (f);
}

static void AddChatLine (char *text, int color)
{
	int i;

	for (i=0;i<NUMLINES-1;i++)
	{
		strcpy (chatlines[i],chatlines[i+1]);
		chatcolor[i] = chatcolor[i+1];
		chattime[i] = chattime[i+1];
	}
	strncpy (chatlines[NUMLINES-1],text,sizeof(chatlines[0])-1);
	chatlines[NUMLINES-1][sizeof(chatlines[0])-1] = 0;
	chatcolor[NUMLINES-1] = color;
	chattime[NUMLINES-1] = chatclock + LINETICS;
}

static void ChatMessage (int player, char *text)
{
	char line[CHATLEN+24];

	sprintf (line,"%s: %s",NetName (player),text);
	AddChatLine (line,playercolors[player]);
}

static void SendChat (char *text)
{
	chatpacket_t pk;
	int		i,len;

	if (!*text)
		return;
	ChatMessage (localplayer,text);

	memset (&pk,0,sizeof(pk));
	pk.magic[0] = 'W'; pk.magic[1] = '3';
	pk.type = pk_chat;
	pk.player = localplayer;
	pk.msgid = ++chatid;
	strncpy (pk.text,text,CHATLEN);
	pk.len = len = strlen (pk.text);
	for (i=0;i<numplayers;i++)
		if (i != localplayer && !np[i].gone)
		{
			IPXSend (i,np[i].addr,&pk,sizeof(pk)-sizeof(pk.text)+len+1);
			IPXSend (i,np[i].addr,&pk,sizeof(pk)-sizeof(pk.text)+len+1);
		}
}

static void ReceiveChat (byte *buf, int len)
{
	chatpacket_t *pk = (chatpacket_t *)buf;

	if (len < sizeof(chatpacket_t)-sizeof(pk->text)+1 || pk->player >= numplayers
	|| pk->player == localplayer || pk->len > CHATLEN)
		return;
	if (gotchatid[pk->player] && lastchatid[pk->player] == pk->msgid)
		return;					// the second copy
	gotchatid[pk->player] = true;
	lastchatid[pk->player] = pk->msgid;
	pk->text[pk->len] = 0;
	ChatMessage (pk->player,pk->text);
	SD_PlaySound (MOVEGUN2SND);		// the menu's click
}

//
// ChatKeys: from NetCheckKeys; true while the keys are the chat's
//
static boolean ChatKeys (void)
{
	int		scan,ch,i,others;

	if (!chatting)
	{
		for (i=others=0;i<numplayers;i++)
			if (i != localplayer && !np[i].removed)
				others++;
		if (LastScan != sc_T || !others)
			return false;
		chatting = true;
		chatlen = 0;
		chatbuf[0] = 0;
		LastScan = sc_None;
		LastASCII = 0;
		return true;
	}

	scan = LastScan;
	ch = LastASCII;
	LastScan = sc_None;
	LastASCII = 0;

	if (scan == sc_Escape)
		chatting = false;
	else if (scan == sc_Return)
	{
		SendChat (chatbuf);
		chatting = false;
	}
	else if (scan == sc_BackSpace)
	{
		if (chatlen)
			chatbuf[--chatlen] = 0;
	}
	else if (Keyboard[sc_Alt] && scan >= sc_1 && scan <= sc_0)
	{
		SendChat (chatmacros[scan-sc_1]);
		chatting = false;
	}
	else if (ch >= ' ' && ch < 127 && chatlen < CHATLEN)
	{
		chatbuf[chatlen++] = ch;
		chatbuf[chatlen] = 0;
	}
	return true;
}

static void DrawText (int x, int y, char *text, int color)
{
	char	str[CHATLEN+30];
	word	w,h;
	int		l;

	strncpy (str,text,sizeof(str)-1);
	str[sizeof(str)-1] = 0;
	l = strlen (str);
	for (;;)					// it doesn't clip: shorten it to fit
	{
		VW_MeasurePropString (str,&w,&h);
		if (x+w+1 < viewwidth || !l)
			break;
		str[--l] = 0;
	}
	SETFONTCOLOR(0,0);
	px = x+1;
	py = y+1;
	VW_DrawPropString (str);
	SETFONTCOLOR(color,0);
	px = x;
	py = y;
	VW_DrawPropString (str);
}

/*
===================
=
= DrawScores
=
= The players and their frags and deaths (deathmatch) or scores (co-op),
= best first: with the automap, and while we're dead
=
===================
*/

static void DrawScores (void)
{
	int		order[MAXPLAYERS],n,i,j,t,y,x0,c;
	long	a,b;
	char	str[24];

	n = 0;
	for (i=0;i<numplayers;i++)
		if (!np[i].removed)
			order[n++] = i;
	for (i=0;i<n;i++)
		for (j=i+1;j<n;j++)
		{
			a = deathmatch ? np[order[i]].frags : (order[i] == netcontext ? gamestate.score : np[order[i]].score);
			b = deathmatch ? np[order[j]].frags : (order[j] == netcontext ? gamestate.score : np[order[j]].score);
			if (b > a || (b == a && order[j] < order[i]))
			{
				t = order[i]; order[i] = order[j]; order[j] = t;
			}
		}

	x0 = viewwidth/2 - 84;
	if (x0 < 4)
		x0 = 4;
	y = viewheight/2 - 6*n - 6;
	if (y < 44)
		y = 44;
	DrawText (x0,y,"Name",7);
	DrawText (x0+92,y,deathmatch ? "Frags" : "Score",7);
	if (deathmatch)
		DrawText (x0+132,y,"Deaths",7);
	y += 11;
	for (i=0;i<n;i++)
	{
		j = order[i];
		c = j == localplayer ? 15 : playercolors[j];
		DrawText (x0,y,NetName (j),c);
		if (deathmatch)
		{
			sprintf (str,"%ld",np[j].frags);
			DrawText (x0+92,y,str,c);
			sprintf (str,"%ld",np[j].deaths);
			DrawText (x0+132,y,str,c);
		}
		else
		{
			sprintf (str,"%ld",j == netcontext ? gamestate.score : np[j].score);
			DrawText (x0+92,y,str,c);
		}
		y += 10;
	}
}

/*
===================
=
= NetDrawChat
=
= From ThreeDRefresh, with bufferofs at the view's corner
=
===================
*/

void NetDrawChat (void)
{
	int		i,y,oldfont;
	char	line[CHATLEN+8];

	chatclock += tics;
	CA_CacheGrChunk (STARTFONT);
	oldfont = fontnumber;
	fontnumber = 0;

	if (automapmode || (np[localplayer].dead && !np[localplayer].removed))
		DrawScores ();

	y = 2;
	for (i=0;i<NUMLINES;i++)
		if (chatlines[i][0] && chattime[i] > chatclock)
		{
			DrawText (4,y,chatlines[i],chatcolor[i]);
			y += 10;
		}
	if (chatting)
	{
		sprintf (line,"Say: %s_",chatbuf);
		DrawText (4,y,line,15);
	}

	fontnumber = oldfont;
}

boolean NetChatting (void)
{
	return chatting;
}

static void PlayerLeft (int p, long lastframe)
{
	char str[60];

	if (np[p].gone)
		return;
	np[p].gone = true;
	sprintf (str,"%s left the game",NetName (p));
	AddChatLine (str,7);
	np[p].lastframe = lastframe;
}


/*
=============================================================================

						 SNAPSHOTS

 A deathmatch newcomer gets the game as it is after the frame it came in
 on: everything the game logic keeps (the map and what's changed in it,
 the doors, the pushwall, the items, the players and their state, the
 random numbers), written out without pointers, packed (lots of the map
 is the same byte over and over) and sent in chunks, SNAPWINDOW at a time
 until they're acknowledged.

=============================================================================
*/

#define SNAPMAX		0x20000

static	byte	*snapbuf;			// the arbiter's, packed, while it's going out
static	long	snaplen;
static	int		snaptotal,snapacked;
static	long	snaplast;
static	unsigned long snapnonce;
static	byte	snapaddr[10];
static	long	snapdue = -1;		// build one after this frame

static	byte	*sp;				// writing / reading position
static	byte	*spend;

static void Put (void *data, long len)
{
	if (sp+len > spend)
		Quit ("Deathmatch snapshot too big");
	memcpy (sp,data,len);
	sp += len;
}

static void Get (void *data, long len)
{
	if (sp+len > spend)
		Quit ("Deathmatch snapshot cut short");
	memcpy (data,sp,len);
	sp += len;
}

static void SnapPutLong (long v) { Put (&v,sizeof(v)); }
static long SnapGetLong (void) { long v; Get (&v,sizeof(v)); return v; }

static long ObjIndex (objtype *ob)
{
	if (!ob)
		return -1;
	return ob - objlist;
}

static objtype *IndexObj (long i)
{
	return i < 0 || i >= MAXACTORS ? NULL : &objlist[i];
}

//
// The states, by number (wl_states.h, made by msdos/mkstates.py): a
// snapshot names an actor's state by its place in the list, so a DOS and
// a Linux machine agree
//
#define STATE(n)	extern statetype n;
#include "wl_states.h"
#undef STATE

static statetype *statelist[] =
{
#define STATE(n)	&n,
#include "wl_states.h"
#undef STATE
	NULL
};
#define NUMSTATES	(sizeof(statelist)/sizeof(statelist[0])-1)

static long StateIndex (statetype *st)
{
	long i;

	if (!st)
		return -1;
	for (i=0;i<NUMSTATES;i++)
		if (statelist[i] == st)
			return i;
	return -2;						// (not in wl_states.h: run mkstates.py)
}

static statetype *IndexState (long i)
{
	return i >= 0 && i < NUMSTATES ? statelist[i] : NULL;
}

//
// BuildId: whether a snapshot from the other machine fits: the same game
// data version, the same states and limits (not the same program: DOS
// and Linux builds join each other)
//
static unsigned long BuildId (void)
{
	unsigned long h;
	int		i;

	h = NUMSTATES*31 + MAXACTORS*7 + MAXSTATS*131 + MAXDOORS + NETVERSION;
	h = h*2654435761u + sizeof(gametype);
	h = h*2654435761u + sizeof(doorobj_t);
	for (i=0;extension[i];i++)
		h = h*31 + toupper (extension[i]);
	return h;
}

// PackBits: runs of 3 to 130 of a byte, else literals of 1 to 128
static long Pack (byte *in, long len, byte *out)
{
	long	i,j,o,run;

	o = i = 0;
	while (i < len)
	{
		for (run=1 ; i+run < len && run < 130 && in[i+run] == in[i] ; run++)
			;
		if (run >= 3)
		{
			out[o++] = 0x80 | (run-3);
			out[o++] = in[i];
			i += run;
			continue;
		}
		for (j=i ; j < len && j-i < 128 ; j++)
			if (j+2 < len && in[j] == in[j+1] && in[j] == in[j+2])
				break;
		out[o++] = j-i-1;
		memcpy (out+o,in+i,j-i);
		o += j-i;
		i = j;
	}
	return o;
}

static long Unpack (byte *in, long len, byte *out, long max)
{
	long	i,o,n;

	o = i = 0;
	while (i < len)
	{
		if (in[i] & 0x80)
		{
			n = (in[i] & 0x7f)+3;
			if (o+n > max || i+1 >= len)
				return -1;
			memset (out+o,in[i+1],n);
			i += 2;
		}
		else
		{
			n = in[i]+1;
			if (o+n > max || i+1+n > len)
				return -1;
			memcpy (out+o,in+i+1,n);
			i += 1+n;
		}
		o += n;
	}
	return o;
}

//
// PutPlayer / GetPlayer: what a player is, but its controls
//
#define PLAYERFIELDS \
	F(oldscore) F(score) F(nextextra) F(lives) F(health) F(ammo) F(keys) \
	F(bestweapon) F(weapon) F(chosenweapon) F(faceframe) F(attackframe) \
	F(attackcount) F(weaponframe) F(thrustspeed) F(plux) F(pluy) F(anglefrac) \
	F(gotgatgun) F(facecount) F(playerxmove) F(playerymove) F(controlx) \
	F(controly) F(dead) F(removed) F(respawn) F(spawnx) F(spawny) \
	F(spawnangle) F(anim) F(moving) F(pain) F(gone) F(lastframe) F(firstcmd) \
	F(frags) F(deaths)

static void PutPlayer (netplayer_t *p)
{
	int i;

#define F(f)	SnapPutLong (p->f);
	PLAYERFIELDS
#undef F
	for (i=0;i<NUMBUTTONS;i++)
	{
		SnapPutLong (p->buttonstate[i]);
		SnapPutLong (p->buttonheld[i]);
	}
	SnapPutLong (ObjIndex (p->lastattacker));
	Put (p->name,sizeof(p->name));
	Put (p->addr,sizeof(p->addr));
}

static void GetPlayer (netplayer_t *p)
{
	int i;

	memset (p,0,sizeof(*p));
#define F(f)	p->f = SnapGetLong ();
	PLAYERFIELDS
#undef F
	for (i=0;i<NUMBUTTONS;i++)
	{
		p->buttonstate[i] = SnapGetLong ();
		p->buttonheld[i] = SnapGetLong ();
	}
	p->lastattacker = IndexObj (SnapGetLong ());
	Get (p->name,sizeof(p->name));
	p->name[NETNAMELEN] = 0;
	Get (p->addr,sizeof(p->addr));
}

//
// WriteGame: the game, after frame netframe, for player slot
//
static long WriteGame (byte *buf, int slot)
{
	int		i,x,y,n;
	long	v;
	objtype	ob;
	netplayer_t p;

	sp = buf;
	spend = buf+SNAPMAX;

	SaveContext (&np[netcontext]);		// np[] is all up to date

	SnapPutLong (0x4d443357);				// "W3DM"
	SnapPutLong (levelseq);
	SnapPutLong (netframe+1);				// the first frame the newcomer runs
	SnapPutLong (slot);
	SnapPutLong (gamestate.mapon);
	SnapPutLong (US_RndIndex ());
	SnapPutLong (objcount);
	Put (&gamestate,sizeof(gamestate));

	Put (tilemap,sizeof(tilemap));
	for (x=0;x<MAPSIZE;x++)
		for (y=0;y<MAPSIZE;y++)
		{
			word w;
			if (actorat[x][y] >= objlist && actorat[x][y] < objlist+MAXACTORS)
				w = 0x8000 + (actorat[x][y]-objlist);
			else
				w = (uintptr_t)actorat[x][y];	// a tile or door number (< 256)
			Put (&w,sizeof(w));
		}
	Put (mapsegs[0],MAPSIZE*MAPSIZE*sizeof(word));
	Put (mapsegs[1],MAPSIZE*MAPSIZE*sizeof(word));
	Put (areaconnect,sizeof(areaconnect));
	Put (areabyplayer,sizeof(areabyplayer));

	n = laststatobj - statobjlist;
	SnapPutLong (n);
	for (i=0;i<n;i++)
	{
		Put (&statobjlist[i].tilex,1);
		Put (&statobjlist[i].tiley,1);
		Put (&statobjlist[i].shapenum,sizeof(statobjlist[i].shapenum));
		Put (&statobjlist[i].flags,1);
		Put (&statobjlist[i].itemnumber,1);
	}
	Put (dmrespawn,n*sizeof(dmrespawn[0]));

	n = lastdoorobj - doorobjlist;
	SnapPutLong (n);
	Put (doorobjlist,n*sizeof(doorobjlist[0]));
	Put (doorposition,n*sizeof(doorposition[0]));

	SnapPutLong (pwallstate);
	SnapPutLong (pwallpos);
	SnapPutLong (pwallx);
	SnapPutLong (pwally);
	SnapPutLong (pwalldir);
	SnapPutLong (ObjIndex (pwallpusher));

	//
	// every object (the players, and -monsters' enemies), and the free
	// list: the links as indexes, the states as they are (BuildId)
	//
	SnapPutLong (objcount);
	SnapPutLong (ObjIndex (lastobj));
	SnapPutLong (ObjIndex (objfreelist));
	for (i=0;i<MAXACTORS;i++)
	{
		objtype *o = &objlist[i];

		v = StateIndex (o->state);
		if (v == -2)
			Quit ("Deathmatch snapshot: a state wl_states.h doesn't have");
		SnapPutLong (v);
		SnapPutLong (ObjIndex (o->next));
		SnapPutLong (ObjIndex (o->prev));
		SnapPutLong (o->active);
		SnapPutLong (o->ticcount);
		SnapPutLong (o->obclass);
		SnapPutLong (o->flags);
		SnapPutLong (o->distance);
		SnapPutLong (o->dir);
		SnapPutLong (o->x);
		SnapPutLong (o->y);
		SnapPutLong (o->tilex);
		SnapPutLong (o->tiley);
		SnapPutLong (o->areanumber);
		SnapPutLong (o->viewx);
		SnapPutLong (o->viewheight);
		SnapPutLong (o->transx);
		SnapPutLong (o->transy);
		SnapPutLong (o->angle);
		SnapPutLong (o->hitpoints);
		SnapPutLong (o->speed);
		SnapPutLong (o->temp1);
		SnapPutLong (o->temp2);
		SnapPutLong (o->temp3);
	}
	SnapPutLong (numdmenemies);
	Put (dmenemies,numdmenemies*sizeof(dmenemies[0]));

	for (i=0;i<MAXPLAYERS;i++)
		PutPlayer (&np[i]);

	return sp - buf;
}

//
// ReadGame: and back (the newcomer)
//
static boolean ReadGame (byte *buf, long len)
{
	int		i,x,y,n,slot;
	long	v;
	objtype	ob,*o;
	netplayer_t p;

	sp = buf;
	spend = buf+len;

	if (SnapGetLong () != 0x4d443357)
		return false;
	levelseq = SnapGetLong ();
	netframe = SnapGetLong ();
	slot = SnapGetLong ();
	if (slot < 0 || slot >= MAXPLAYERS || SnapGetLong () != gamestate.mapon)
		return false;
	US_SetRndIndex (SnapGetLong ());
	objcount = SnapGetLong ();
	Get (&gamestate,sizeof(gamestate));

	Get (tilemap,sizeof(tilemap));
	for (x=0;x<MAPSIZE;x++)
		for (y=0;y<MAPSIZE;y++)
		{
			word w;
			Get (&w,sizeof(w));
			if (w >= 0x8000)
				actorat[x][y] = IndexObj (w-0x8000);
			else
				actorat[x][y] = (objtype *)(uintptr_t)w;
		}
	Get (mapsegs[0],MAPSIZE*MAPSIZE*sizeof(word));
	Get (mapsegs[1],MAPSIZE*MAPSIZE*sizeof(word));
	Get (areaconnect,sizeof(areaconnect));
	Get (areabyplayer,sizeof(areabyplayer));

	n = SnapGetLong ();
	if (n < 0 || n > MAXSTATS)
		return false;
	laststatobj = statobjlist+n;
	for (i=0;i<n;i++)
	{
		Get (&statobjlist[i].tilex,1);
		Get (&statobjlist[i].tiley,1);
		Get (&statobjlist[i].shapenum,sizeof(statobjlist[i].shapenum));
		Get (&statobjlist[i].flags,1);
		Get (&statobjlist[i].itemnumber,1);
		statobjlist[i].visspot = &spotvis[statobjlist[i].tilex][statobjlist[i].tiley];
	}
	Get (dmrespawn,n*sizeof(dmrespawn[0]));

	n = SnapGetLong ();
	if (n < 0 || n > MAXDOORS)
		return false;
	lastdoorobj = doorobjlist+n;
	Get (doorobjlist,n*sizeof(doorobjlist[0]));
	Get (doorposition,n*sizeof(doorposition[0]));

	pwallstate = SnapGetLong ();
	pwallpos = SnapGetLong ();
	pwallx = SnapGetLong ();
	pwally = SnapGetLong ();
	pwalldir = SnapGetLong ();
	pwallpusher = IndexObj (SnapGetLong ());

	objcount = SnapGetLong ();
	lastobj = IndexObj (SnapGetLong ());
	objfreelist = IndexObj (SnapGetLong ());
	for (i=0;i<MAXACTORS;i++)
	{
		objtype *o = &objlist[i];

		v = SnapGetLong ();
		o->state = IndexState (v);
		if (v >= 0 && !o->state)
			return false;
		o->next = IndexObj (SnapGetLong ());
		o->prev = IndexObj (SnapGetLong ());
		o->active = SnapGetLong ();
		o->ticcount = SnapGetLong ();
		o->obclass = SnapGetLong ();
		o->flags = SnapGetLong ();
		o->distance = SnapGetLong ();
		o->dir = SnapGetLong ();
		o->x = SnapGetLong ();
		o->y = SnapGetLong ();
		o->tilex = SnapGetLong ();
		o->tiley = SnapGetLong ();
		o->areanumber = SnapGetLong ();
		o->viewx = SnapGetLong ();
		o->viewheight = SnapGetLong ();
		o->transx = SnapGetLong ();
		o->transy = SnapGetLong ();
		o->angle = SnapGetLong ();
		o->hitpoints = SnapGetLong ();
		o->speed = SnapGetLong ();
		o->temp1 = SnapGetLong ();
		o->temp2 = SnapGetLong ();
		o->temp3 = SnapGetLong ();
	}
	numdmenemies = SnapGetLong ();
	if (numdmenemies < 0 || numdmenemies > MAXACTORS)
		return false;
	Get (dmenemies,numdmenemies*sizeof(dmenemies[0]));

	for (i=0;i<MAXPLAYERS;i++)
	{
		GetPlayer (&np[i]);			// (addresses as the arbiter has them)
		np[i].ob = &objlist[i];
	}

	localplayer = slot;
	netcontext = slot;
	LoadContext (&np[slot]);
	return true;
}

//
// StartSnapshot: on the arbiter, after the frame the newcomer came in on
//
static void StartSnapshot (void)
{
	static	byte	*raw;
	long	len;

	if (!raw)
		raw = malloc (SNAPMAX);
	if (!snapbuf)
		snapbuf = malloc (SNAPMAX+SNAPMAX/128+16);
	if (!raw || !snapbuf)
		return;
	len = WriteGame (raw,pending.slot);
	snaplen = Pack (raw,len,snapbuf);
	snaptotal = (snaplen+SNAPCHUNK-1)/SNAPCHUNK;
	snapacked = 0;
	snaplast = TimeCount - 100;
	snapnonce = pending.nonce;
	memcpy (snapaddr,pending.addr,10);
	pending.active = false;
	if (netlog)
		fprintf (netlog,"snapshot for %s: %ld bytes, %ld packed, %d chunks\n",
			pending.name,len,snaplen,snaptotal);
}

static void SendChunks (void)
{
	snappacket_t pk;
	int		i,n;

	for (i=snapacked;i<snaptotal && i<snapacked+SNAPWINDOW;i++)
	{
		n = snaplen - (long)i*SNAPCHUNK;
		if (n > SNAPCHUNK)
			n = SNAPCHUNK;
		memset (&pk,0,sizeof(pk));
		pk.magic[0] = 'W'; pk.magic[1] = '3';
		pk.type = pk_snap;
		pk.len = n;
		pk.chunk = i;
		pk.total = snaptotal;
		pk.nonce = snapnonce;
		memcpy (pk.data,snapbuf+(long)i*SNAPCHUNK,n);
		IPXSend (i % MAXPLAYERS,snapaddr,&pk,sizeof(pk)-SNAPCHUNK+n);
	}
	snaplast = TimeCount;
}

static void SnapService (void)
{
	if (snaptotal && snapacked < snaptotal && TimeCount - snaplast >= 10)
		SendChunks ();			// lost some: again
}


/*
=============================================================================

						 RECEIVING

=============================================================================
*/

static void ReplyInfo (byte *from, unsigned long nonce)
{
	infopacket_t pk;
	int		i,arb;

	memset (&pk,0,sizeof(pk));
	pk.magic[0] = 'W'; pk.magic[1] = '3';
	pk.type = pk_info;
	pk.version = NETVERSION;
	pk.episode = gamestate.episode;
	pk.level = gamestate.mapon;
	pk.skill = gamestate.difficulty;
	for (i=0;i<numplayers;i++)
		if (!np[i].removed)
			pk.players++;
	pk.nonce = nonce;
	pk.monsters = dmmonsters;
	pk.build = BuildId ();
	arb = Arbiter ();
	if (arb != localplayer)
		memcpy (pk.arbiter,np[arb].addr,10);
	IPXSend (0,from,&pk,sizeof(pk));
}

//
// ReceiveJoin: a newcomer wants in (we're the arbiter)
//
static void ReceiveJoin (joinpacket_t *jp, byte *from)
{
	int		i,slot;

	if (!inplay || Arbiter () != localplayer)
		return;
	if (snaptotal && snapnonce == jp->nonce)
	{
		if (snapacked < snaptotal)
			SendChunks ();			// it's still waiting for them
		return;
	}
	if (pending.active)
		return;						// one at a time: it'll ask again

	if (jp->build != BuildId ())
	{
		byte pk[8];
		memset (pk,0,sizeof(pk));
		pk[0] = 'W'; pk[1] = '3'; pk[2] = pk_full; pk[3] = 1;	// 1: not the same program
		memcpy (pk+4,&jp->nonce,4);
		IPXSend (0,from,pk,sizeof(pk));
		return;
	}

	for (i=0;i<numplayers;i++)		// it's here already
		if (!np[i].removed && !memcmp (np[i].addr,from,10) && i != localplayer)
			return;
	slot = -1;
	for (i=0;i<numplayers;i++)
		if (np[i].removed && np[i].gone)
		{
			slot = i;
			break;
		}
	if (slot == -1)
	{
		byte pk[8];
		memset (pk,0,sizeof(pk));
		pk[0] = 'W'; pk[1] = '3'; pk[2] = pk_full;
		memcpy (pk+4,&jp->nonce,4);
		IPXSend (0,from,pk,sizeof(pk));
		return;
	}

	pending.active = true;
	pending.slot = slot;
	pending.frame = -1;			// NetPlayLoop puts the event in our controls
	pending.nonce = jp->nonce;
	memcpy (pending.addr,from,10);
	jp->name[NETNAMELEN] = 0;
	strcpy (pending.name,jp->name);
	snaptotal = 0;
}

//
// NetReceive: take in whatever has arrived
//
static void NetReceive (void)
{
	byte	buf[PKTMAX],from[10];
	ticpacket_t *pk = (ticpacket_t *)buf;
	netplayer_t *p;
	int		len,i;
	long	f;

	if (!ipxup)
		return;

	while ((len = IPXGet (buf,from)) != 0)
	{
		if (len < 4 || buf[0] != 'W' || buf[1] != '3')
			continue;

		switch (buf[2])
		{
		case pk_chat:
			ReceiveChat (buf,len);
			continue;

		case pk_hello:
			// a straggler still looking for us
			for (i=0;i<numplayers;i++)
				if (i != localplayer && !memcmp (np[i].addr,from,10))
					SendHello (from);
			continue;

		case pk_query:
			if (deathmatch && inplay && len >= sizeof(querypacket_t)
			&& ((querypacket_t *)buf)->version == NETVERSION)
				ReplyInfo (from,((querypacket_t *)buf)->nonce);
			continue;

		case pk_join:
			if (deathmatch && len >= sizeof(joinpacket_t)
			&& ((joinpacket_t *)buf)->version == NETVERSION)
				ReceiveJoin ((joinpacket_t *)buf,from);
			continue;

		case pk_snapack:
			if (len >= sizeof(snapackpacket_t) && snaptotal
			&& ((snapackpacket_t *)buf)->nonce == snapnonce)
			{
				i = ((snapackpacket_t *)buf)->got;
				if (i > snapacked && i <= snaptotal)
				{
					snapacked = i;
					SendChunks ();
				}
			}
			continue;

		case pk_drop:
			if (deathmatch && len >= sizeof(droppacket_t)
			&& ((droppacket_t *)buf)->slot < numplayers
			&& ((droppacket_t *)buf)->slot != localplayer)
				PlayerLeft (((droppacket_t *)buf)->slot,((droppacket_t *)buf)->lastframe);
			continue;

		case pk_tics:
		case pk_quit:
			break;

		default:
			continue;
		}

		if (len < sizeof(ticpacket_t)-sizeof(pk->cmds) || pk->player >= numplayers
		|| pk->player == localplayer || pk->count > NETBACKUP)
			continue;
		p = &np[pk->player];

		if (pk->seq != levelseq)
		{
			if (pk->seq == oldseq && pk->type == pk_tics)
				SendTics (pk_tics);		// it's waiting for us to finish
			if ((signed char)(pk->seq - levelseq) > 0)
				p->ack = 0x7fffffffl;	// it's ahead: done with this level
			continue;
		}

		if (pk->ack > p->ack)
			p->ack = pk->ack;

		if (pk->hasjoin && pk->join.slot < MAXPLAYERS)
			joinrecs[pk->join.slot] = pk->join;

		for (i=0;i<pk->count;i++)
		{
			f = pk->first+i;
			p->cmdframe[f & (NETRING-1)] = f;
			p->cmds[f & (NETRING-1)] = pk->cmds[i];
		}

		if (pk->type == pk_quit)
			PlayerLeft (pk->player,pk->first+pk->count-1);

		// the newcomer is playing: it has everything
		if (snaptotal && pk->player == pending.slot)
			snapacked = snaptotal;

		// consistency check
		f = pk->hashframe;
		if (f >= 0 && f < netframe && f > netframe-NETRING
		&& hashframes[f & (NETRING-1)] == f
		&& hashring[f & (NETRING-1)] != pk->hash
		&& desyncframe == -1)
			desyncframe = f;
	}

	SnapService ();
}


/*
=============================================================================

						 SETUP

=============================================================================
*/

static int ParmInt (char *name, int def)
{
	int i;

	for (i=1;i<_argc-1;i++)
		if (!stricmp (_argv[i],name))
			return atoi (_argv[i+1]);
	return def;
}

static boolean Parm (char *name)
{
	int i;

	for (i=1;i<_argc;i++)
		if (!stricmp (_argv[i],name))
			return true;
	return false;
}

static void SetName (char *dst, char *src)
{
	int i,j;

	for (i=j=0;src[i] && j<NETNAMELEN;i++)
		if (src[i] >= ' ' && src[i] < 127)
			dst[j++] = src[i];
	dst[j] = 0;
}

//
// NetOptions: what's common to co-op and deathmatch
//
static void NetOptions (void)
{
	int		i;
	FILE	*f;
	char	line[64];

	// -name, else NAME.CFG (SETUP writes it)
	myname[0] = 0;
	f = fopen ("NAME.CFG","r");
	if (f)
	{
		if (fgets (line,sizeof(line),f))
		{
			i = strlen (line);
			while (i && (line[i-1] == '\n' || line[i-1] == '\r'))
				line[--i] = 0;
			SetName (myname,line);
		}
		fclose (f);
	}
	for (i=1;i<_argc-1;i++)
		if (!stricmp (_argv[i],"-name"))
			SetName (myname,_argv[i+1]);

	hello_episode = ParmInt ("-episode",1)-1;
	hello_level = ParmInt ("-level",1)-1;
	hello_skill = ParmInt ("-skill",2);
#ifdef SPEAR
	hello_episode = 0;
#endif
	if (hello_episode < 0 || hello_episode > 5) hello_episode = 0;
	if (hello_level < 0 || hello_level > 20) hello_level = 0;
	if (hello_skill < 0 || hello_skill > 3) hello_skill = 2;
	hello_ff = 1;
	if (Parm ("-nofriendlyfire") || Parm ("-noff"))
		hello_ff = 0;

	netgame = true;
	SD_PlayHook = NetPlaySound;
	if (Parm ("-netlog"))
		netlog = fopen ("NETLOG.TXT","w");
	NoWait = true;
	LoadMacros ();
}

static void NetTransport (void)
{
	int		i;
	byte	a[10];

	if (!IPXStartup ())
	{
#ifdef __linux__
		printf ("Can't start the network (UDP port in use? -port N for another).\n");
#else
		printf ("No IPX driver found (load one, or use DOSBox's ipx=true and IPXNET).\n");
#endif
		exit (1);
	}
	atexit (NetShutdown);

#ifdef __linux__
	if (!ipxnet)
	{
		// the local network on the usual port (and on ours, if it's another)
		SYS_NetBroadcastAddr (broadcast,UDPPORT);
		SYS_NetBroadcastAddr (a,0);
		if (memcmp (a,broadcast,10))
			AddContact (a);
		for (i=1;i<_argc-1;i++)
			if (!stricmp (_argv[i],"-connect"))
			{
				if (SYS_NetResolve (_argv[i+1],a,UDPPORT))
					AddContact (a);
				else
					printf ("Can't find %s\n",_argv[i+1]);
			}
	}
	{
		struct timespec ts;
		clock_gettime (CLOCK_MONOTONIC,&ts);
		mynonce = (unsigned long)time (NULL) * 2654435761u ^ (unsigned long)getpid () * 40503u
			^ (unsigned long)ts.tv_nsec;
	}
#else
	mynonce = BiosTicks () * 2654435761ul;
	for (i=0;i<10;i++)
		mynonce = mynonce*31 + myaddr[i];
#endif
	if (!mynonce)
		mynonce = 1;
}

/*
===================
=
= NetStartupDM
=
= -deathmatch: find a game going on (or start one): no waiting for anyone
=
===================
*/

static void NetStartupDM (void)
{
	byte	buf[PKTMAX],from[10];
	infopacket_t *ip = (infopacket_t *)buf;
	querypacket_t q;
	long	start,lastsend;
	int		i;
	boolean	host,join;

	deathmatch = true;
	numplayers = MAXPLAYERS;
	hello_ff = 1;					// that's the point
	for (i=0;i<MAXPLAYERS;i++)
	{
		np[i].gone = np[i].removed = np[i].dead = true;
		np[i].lastframe = -1;
	}

	NetTransport ();

	host = Parm ("-host");
	join = Parm ("-join");
	dmmonsters = Parm ("-monsters");	// (the game's choice, if we join one)
	if (!host)
	{
		printf (join ? "Looking for a deathmatch game (Esc aborts)...\n"
			: "Looking for a deathmatch game to join...\n");
		memset (&q,0,sizeof(q));
		q.magic[0] = 'W'; q.magic[1] = '3';
		q.type = pk_query;
		q.version = NETVERSION;
		q.nonce = mynonce;
		start = BiosTicks ();
		lastsend = start - 100;
		while (1)
		{
			if (BiosTicks () - lastsend >= 5 || BiosTicks () < lastsend)
			{
				lastsend = BiosTicks ();
				SendAround (&q,sizeof(q));
			}
			while (IPXGet (buf,from))
			{
				if (buf[0] != 'W' || buf[1] != '3' || buf[2] != pk_info
				|| ip->version != NETVERSION || ip->nonce != mynonce)
					continue;
				if (ip->build != BuildId ())
				{
					printf ("There's a deathmatch game, but it's another build of the game\n"
						"(everyone needs the same %s).\n",_argv[0]);
					exit (1);
				}
				hello_episode = ip->episode;
				hello_level = ip->level;
				hello_skill = ip->skill;
				dmmonsters = ip->monsters;
				for (i=0;i<10 && !ip->arbiter[i];i++)
					;
				memcpy (arbaddr,i == 10 ? from : ip->arbiter,10);
				dmjoining = true;
				break;
			}
			if (dmjoining)
				break;
			if (!join && BiosTicks () - start > 36)
				break;					// two seconds: nobody's playing
			if (kbhit () && getch () == 27)
			{
				printf ("Aborted.\n");
				exit (1);
			}
#ifdef __linux__
			SYS_Sleep ();
#endif
		}
	}

	if (dmjoining)
	{
		printf ("Found a game: episode %d, level %d, skill %d%s, %d playing. Joining...\n",
			hello_episode+1,hello_level+1,hello_skill,dmmonsters ? ", enemies" : "",ip->players);
		localplayer = 0;				// till we're told
		return;
	}

	localplayer = 0;
	np[0].gone = np[0].removed = np[0].dead = false;
	np[0].lastframe = 0x7fffffffl;
	strcpy (np[0].name,myname);
	printf ("Started a deathmatch game: episode %d, level %d, skill %d%s.\n"
		"Others can join it any time (wolf3d -deathmatch).\n",
		hello_episode+1,hello_level+1,hello_skill,
		dmmonsters ? ", with the enemies (back 1-2 minutes after they die)" : "");
}

/*
===================
=
= NetStartup
=
= -nodes N [-episode E] [-level L] [-skill S]: waits in text mode until N
= copies of the game have found each other on the network
= -deathmatch: see NetStartupDM
=
===================
*/

void NetStartup (void)
{
	byte	addrs[MAXPLAYERS][10];
	unsigned long nonces[MAXPLAYERS];
	char	names[MAXPLAYERS][NETNAMELEN+1];
	int		settings[MAXPLAYERS][4];
	boolean	done[MAXPLAYERS];
	byte	buf[PKTMAX],from[10];
	hellopacket_t *pk = (hellopacket_t *)buf;
	int		nodes,found,i,j,len,shown,extra;
	long	lastsend;

	nodes = ParmInt ("-nodes",0);
	if (Parm ("-deathmatch") || Parm ("-dm"))
	{
		NetOptions ();
		NetStartupDM ();
		return;
	}
	if (!nodes)
		return;
	if (nodes < 1 || nodes > MAXPLAYERS)
	{
		printf ("-nodes must be 1 to %d\n",MAXPLAYERS);
		exit (1);
	}

	NetOptions ();
	numplayers = nodes;
	strcpy (np[0].name,myname);

	if (nodes == 1)
	{
		localplayer = 0;
		return;
	}

	NetTransport ();

	printf ("Looking for %d players on the network (Esc aborts)...\n",nodes);

	memcpy (addrs[0],myaddr,10);
	nonces[0] = mynonce;
	strcpy (names[0],myname);
	settings[0][0] = hello_episode;
	settings[0][1] = hello_level;
	settings[0][2] = hello_skill;
	settings[0][3] = hello_ff;
	done[0] = false;
	found = 1;
	shown = 0;
	lastsend = BiosTicks () - 100;
	extra = 0;

	while (1)
	{
		if (BiosTicks () - lastsend >= 9 || BiosTicks () < lastsend)
		{
			hellopacket_t h;

			lastsend = BiosTicks ();
			MakeHello (&h,found,addrs);
			h.numnodes = nodes;
			SendAround (&h,sizeof(h));
			for (i=1;i<found;i++)
				IPXSend (0,addrs[i],&h,sizeof(h));
			if (extra && !--extra)
				break;
		}

		while ((len = IPXGet (buf,from)) != 0)
		{
			if (len < 4 || buf[0] != 'W' || buf[1] != '3')
				continue;

			if (pk->type == pk_hello)
			{
				if (len < sizeof(hellopacket_t) || pk->version != NETVERSION)
					continue;
				if (pk->nonce == mynonce)
					continue;				// our own broadcast
				if (pk->numnodes != nodes)
				{
					printf ("Someone started with -nodes %d, ignoring them\n",pk->numnodes);
					continue;
				}
				for (i=1;i<found;i++)
					if (nonces[i] == pk->nonce)
						break;
				if (i == found)
				{
					if (found == nodes)
						continue;
					memcpy (addrs[found],from,10);
					nonces[found] = pk->nonce;
					done[found] = false;
					found++;
				}
				pk->name[NETNAMELEN] = 0;
				SetName (names[i],pk->name);
				settings[i][0] = pk->episode;
				settings[i][1] = pk->level;
				settings[i][2] = pk->skill;
				settings[i][3] = pk->friendlyfire;
				if (pk->gotcount == nodes)
					done[i] = true;
				for (j=0;j<pk->numpeers && j<MAXPLAYERS;j++)
					AddContact (pk->peers[j]);		// the ones it knows
			}
			else
			{
				for (i=1;i<found;i++)
					if (!memcmp (addrs[i],from,10))
						break;
				if (i < found)
					done[i] = true;			// already playing
			}
		}

		if (found != shown)
		{
			printf ("Found %d of %d\n",found,nodes);
			shown = found;
		}

		if (!extra && found == nodes)
		{
			for (i=1;i<found;i++)
				if (!done[i])
					break;
			if (i == found)
			{
				extra = 2;				// tell them a couple more times
				lastsend = BiosTicks () - 100;
			}
		}

		if (kbhit () && getch () == 27)
		{
			printf ("Aborted.\n");
			exit (1);
		}
#ifdef __linux__
		SYS_Sleep ();
#endif
	}

	//
	// player numbers go by the nonces, so everyone agrees
	//
	for (i=0;i<nodes;i++)
		for (j=i+1;j<nodes;j++)
			if (nonces[j] < nonces[i])
			{
				byte t[10];
				int  s[4];
				unsigned long n;
				char nm[NETNAMELEN+1];
				memcpy (t,addrs[i],10); memcpy (addrs[i],addrs[j],10); memcpy (addrs[j],t,10);
				memcpy (s,settings[i],sizeof(s)); memcpy (settings[i],settings[j],sizeof(s)); memcpy (settings[j],s,sizeof(s));
				n = nonces[i]; nonces[i] = nonces[j]; nonces[j] = n;
				strcpy (nm,names[i]); strcpy (names[i],names[j]); strcpy (names[j],nm);
			}

	for (i=0;i<nodes;i++)
	{
		memcpy (np[i].addr,addrs[i],10);
		strcpy (np[i].name,names[i]);
		if (nonces[i] == mynonce)
			localplayer = i;
	}

	hello_episode = settings[0][0];
	hello_level = settings[0][1];
	hello_skill = settings[0][2];
	hello_ff = settings[0][3];

	printf ("You are player %d of %d: episode %d, level %d, skill %d, friendly fire %s\n",
		localplayer+1,nodes,hello_episode+1,hello_level+1,hello_skill,hello_ff ? "on" : "off");
}

//
// NetLeaving: from NetShutdown (at exit): if we're in a game, say so
//
void NetLeaving (void)
{
	if (!inplay)
		return;
	inplay = false;
	SendTics (pk_quit);
	SendTics (pk_quit);
	SendTics (pk_quit);
}


/*
=============================================================================

						 GAME

=============================================================================
*/

/*
===================
=
= NetGameLoop
=
===================
*/

void NetGameLoop (void)
{
	int i;

	NewGame (hello_skill,hello_episode);
	gamestate.mapon = hello_level;
	startgame = false;

	netcontext = localplayer;
	for (i=0;i<numplayers;i++)
	{
		SaveContext (&np[i]);
		np[i].ob = &objlist[i];
	}

	GameLoop ();

	if (desyncframe != -1)
	{
		char str[80];
		sprintf (str,"The games went out of sync at frame %ld",desyncframe);
		Quit (str);
	}
	Quit (NULL);
}


/*
===================
=
= NetLevelStart
=
= SetupGameLevel spawned player 0 at the start spot; put the others on the
= nearest free tiles (deathmatch: everybody somewhere at random)
=
===================
*/

void NetLevelStart (void)
{
	static	byte	queue[MAPSIZE*MAPSIZE][2];
	static	byte	seen[MAPSIZE][MAPSIZE];
	int		head,tail,x,y,d,i,placed;
	unsigned area;
	objtype	*p0;
	static int dx[4] = {1,0,-1,0}, dy[4] = {0,-1,0,1};

	if (!rstried)
		LoadRespawnSound ();	// now, not in the middle of a fight

	player = np[netcontext].ob;
	p0 = playerobjs[0];

	if (deathmatch)
		DMFindSpots (p0->tilex,p0->tiley);

	memset (seen,0,sizeof(seen));
	head = tail = 0;
	queue[tail][0] = p0->tilex;
	queue[tail][1] = p0->tiley;
	tail++;
	seen[p0->tilex][p0->tiley] = 1;
	placed = 1;

	while (head < tail && placed < numplayers)
	{
		x = queue[head][0];
		y = queue[head][1];
		head++;

		if (head > 1)
		{
			NetSetContext (placed);
			SpawnPlayer (x,y,0);
			player->angle = p0->angle;
			placed++;
		}

		for (d=0;d<4;d++)
		{
			int nx = x+dx[d], ny = y+dy[d];
			if (nx < 1 || ny < 1 || nx > MAPSIZE-2 || ny > MAPSIZE-2 || seen[nx][ny])
				continue;
			seen[nx][ny] = 1;
			if (tilemap[nx][ny] || actorat[nx][ny])
				continue;
			area = *(mapsegs[0]+farmapylookup[ny]+nx);
			if (area < AREATILE || area >= AREATILE+NUMAREAS)
				continue;
			queue[tail][0] = nx;
			queue[tail][1] = ny;
			tail++;
		}
	}
	for ( ; placed < numplayers ; placed++)	// no room: stack them up
	{
		NetSetContext (placed);
		SpawnPlayer (p0->tilex,p0->tiley,0);
		player->angle = p0->angle;
	}

	for (i=0;i<numplayers;i++)
	{
		NetSetContext (i);
		if (np[i].dead || gamestate.health <= 0)
		{
			gamestate.health = 100;
			if (gamestate.ammo < STARTAMMO)
				gamestate.ammo = STARTAMMO;
			gamestate.weapon = gamestate.chosenweapon;
		}
		np[i].dead = np[i].removed;
		if (np[i].removed)
			player->state = &s_netdead;
		np[i].respawn = 0;
		np[i].spawnx = player->x;
		np[i].spawny = player->y;
		np[i].spawnangle = player->angle;

		gamestate.keys = 0;
#ifdef SPEAR
		if (gamestate.mapon == 20)
			gamestate.keys |= 1;
#endif
		gamestate.attackframe = gamestate.weaponframe = 0;
		thrustspeed = 0;
		anglefrac = 0;
		facecount = 0;
		gotgatgun = 0;
		controlx = controly = 0;
		memset (buttonstate,0,sizeof(buttonstate));
		memset (buttonheld,0,sizeof(buttonheld));
		plux = player->x >> UNSIGNEDSHIFT;
		pluy = player->y >> UNSIGNEDSHIFT;

		if (deathmatch && !np[i].removed)
		{
			DMLoadout ();
			gamestate.score = np[i].frags;
			DMSpawn (&np[i]);
			DMPlace (&np[i]);
		}
	}
	NetSetContext (localplayer);
	ConnectAreas ();

	if (deathmatch && !np[localplayer].removed)
	{
		// the status bar was drawn before the loadout
		DrawFace ();
		DrawHealth ();
		DrawAmmo ();
		DrawKeys ();
		DrawWeapon ();
		DrawScore ();
	}
}


/*
===================
=
= SampleControls
=
= The local controls for one frame, as PollControls would read them
=
===================
*/

static void SampleControls (netcmd_t *cmd)
{
	int		i,max,savex,savey;
	byte	bits;
	boolean	savestate[NUMBUTTONS];

	savex = controlx;
	savey = controly;
	memcpy (savestate,buttonstate,sizeof(savestate));
	controlx = controly = 0;
	memset (buttonstate,0,sizeof(buttonstate));

	PollKeyboardButtons ();
	if (mouseenabled)
		PollMouseButtons ();
	if (joystickenabled)
		PollJoystickButtons ();

	PollKeyboardMove ();
	if (mouseenabled)
		PollMouseMove ();
	if (joystickenabled)
		PollJoystickMove ();

	max = 100*tics;
	if (controlx > max) controlx = max;
	if (controlx < -max) controlx = -max;
	if (controly > max) controly = max;
	if (controly < -max) controly = -max;

	bits = 0;
	for (i=NUMBUTTONS-1;i>=0;i--)
	{
		bits <<= 1;
		if (buttonstate[i])
			bits |= 1;
	}
	cmd->buttons = bits;
	cmd->event = 0;
	cmd->x = controlx;
	cmd->y = controly;
	if (chatting)					// the keys are for the message
		memset (cmd,0,sizeof(*cmd));

	controlx = savex;
	controly = savey;
	memcpy (buttonstate,savestate,sizeof(savestate));
}


static boolean HaveCmd (int p, long f)
{
	if (f < NETDELAY || f < np[p].firstcmd)
		return true;
	if (np[p].gone && f > np[p].lastframe)
		return true;
	return np[p].cmdframe[f & (NETRING-1)] == f;
}

static netcmd_t *GetCmd (int p, long f)
{
	static netcmd_t zero;

	if (f < NETDELAY || f < np[p].firstcmd || (np[p].gone && f > np[p].lastframe))
		return &zero;
	return &np[p].cmds[f & (NETRING-1)];
}

static void ApplyCmd (int p, long f)
{
	netcmd_t	*cmd = GetCmd (p,f);
	byte	bits;
	int		i;

	memcpy (buttonheld,buttonstate,sizeof(buttonstate));
	bits = cmd->buttons;
	for (i=0;i<NUMBUTTONS;i++)
	{
		buttonstate[i] = bits&1;
		bits >>= 1;
	}
	controlx = cmd->x;
	controly = cmd->y;
}


static void ShowMessage (char *str)
{
	ClearSplitVWB ();
	VW_ScreenToScreen (displayofs,bufferofs,80,160);
	WindowH = 160;
	Message (str);
	SETFONTCOLOR(0,15);
}


/*
===================
=
= WaitForFrame
=
= Until everyone's controls for frame f are in
=
===================
*/

//
// DropArbiter: who decides to go on without player p: the lowest numbered
// player still in the game, not counting p
//
#define DROPSECONDS	8

static int DropArbiter (int p)
{
	int i;

	for (i=0;i<numplayers;i++)
		if (i != p && !np[i].removed && !np[i].gone)
			return i;
	return localplayer;
}

static void WaitForFrame (long f)
{
	long	start,lastsend;
	int		i;
	boolean	shown;
	char	str[80];

	start = lastsend = TimeCount;
	shown = false;

	while (1)
	{
		NetReceive ();
		for (i=0;i<numplayers;i++)
			if (!HaveCmd (i,f))
				break;
		if (i == numplayers)
			break;

		if (TimeCount - lastsend >= 7)
		{
			SendTics (pk_tics);
			lastsend = TimeCount;
		}
		if (!shown && TimeCount - start > 70)
		{
			sprintf (str,"Waiting for %s...\n(Esc leaves the game)",NetName (i));
			ShowMessage (str);
			shown = true;
		}
		if (shown && Keyboard[sc_Escape])
			Quit (NULL);				// NetShutdown tells them
		if (deathmatch && TimeCount - start > 70*DROPSECONDS && DropArbiter (i) == localplayer)
		{
			// it's gone quiet: play on without it
			memset (&dropped,0,sizeof(dropped));
			dropped.magic[0] = 'W'; dropped.magic[1] = '3';
			dropped.type = pk_drop;
			dropped.slot = i;
			dropped.lastframe = f-1;
			dropannounce = 70;
			PlayerLeft (i,f-1);
			start = TimeCount;
			continue;
		}
		if (TimeCount - start > 70*30)
		{
			sprintf (str,"Lost contact with %s",NetName (i));
			Quit (str);
		}
#ifdef __linux__
		SYS_Sleep ();
#endif
	}

	if (shown)
	{
		DrawAllPlayBorder ();
		lasttimecount = TimeCount;
	}
}


/*
===================
=
= ConsistencyHash
=
===================
*/

static unsigned long ConsistencyHash (void)
{
	unsigned long h;
	objtype	*ob;
	int		i;

	h = US_RndIndex () + objcount*31 + gamestate.killcount*977;
	for (i=0;i<numplayers;i++)
		h = h*31 + GetHealth (i);
	for (ob = playerobjs[0] ; ob ; ob = ob->next)
		h = h*31 + ob->x + ob->y*7 + ob->angle*13 + ob->obclass;
	return h;
}


/*
===================
=
= DMJoin
=
= At the start of frame f, on every machine: player k comes in
=
===================
*/

static void DMJoin (int k, long f)
{
	netplayer_t *p = &np[k];
	joinrec_t	*rec = &joinrecs[k];
	char	str[60];
	int		i,cur;

	if (!p->removed)
		return;					// (twice?)

	cur = netcontext;
	NetSetContext (k);

	p->removed = p->dead = p->gone = false;
	p->lastframe = 0x7fffffffl;
	p->firstcmd = f+1+NETDELAY;
	p->ack = 0;
	for (i=0;i<NETRING;i++)
		p->cmdframe[i] = -1;
	if (rec->frame == f)
	{
		rec->name[NETNAMELEN] = 0;
		strcpy (p->name,rec->name);
		if (k != localplayer)
			memcpy (p->addr,rec->addr,10);
	}
	else
		p->name[0] = 0;
	p->frags = p->deaths = 0;
	p->respawn = p->pain = p->moving = 0;
	gotchatid[k] = false;

	gamestate.score = gamestate.oldscore = 0;
	gamestate.lives = 3;
	thrustspeed = 0;
	anglefrac = 0;
	facecount = 0;
	gotgatgun = 0;
	controlx = controly = 0;
	memset (buttonstate,0,sizeof(buttonstate));
	memset (buttonheld,0,sizeof(buttonheld));
	LastAttacker = NULL;
	DMLoadout ();
	DMSpawn (p);
	DMPlace (p);
	ConnectAreas ();
	RespawnSound (p->ob);

	NetSetContext (cur);

	sprintf (str,"%s joined the game",NetName (k));
	AddChatLine (str,playercolors[k]);

	if (pending.active && pending.slot == k && pending.frame == f)
		snapdue = f;			// we let it in: send it the game after this frame
}


/*
===================
=
= RunFrame
=
===================
*/

static void RunFrame (long f)
{
	int		i,e;
	long	oldx,oldy;

	//
	// newcomers (the arbiter's controls say)
	//
	for (i=0;i<numplayers;i++)
	{
		e = GetCmd (i,f)->event;
		if (e & EV_JOIN)
			DMJoin (e & (MAXPLAYERS-1),f);
	}

	for (i=0;i<numplayers;i++)
	{
		NetSetContext (i);
		ApplyCmd (i,f);
		if (np[i].gone && f > np[i].lastframe && !np[i].removed)
		{
			np[i].removed = np[i].dead = true;
			player->state = &s_netdead;
		}
	}

	madenoise = false;

	NetSetContext (0);
	MoveDoors ();
	MovePWalls ();
	if (deathmatch)
		DMItems ();

	inrunframe = true;
	for (obj = playerobjs[0] ; obj ; obj = obj->next)
	{
		if (obj->obclass == playerobj)
		{
			for (i=0;i<numplayers;i++)
				if (np[i].ob == obj)
					break;
			if (i == numplayers)
				continue;
			NetSetContext (i);
			oldx = obj->x;
			oldy = obj->y;
			DoActor (obj);
			if (np[i].pain)
				np[i].pain--;
			if (!np[i].dead)
			{
				NetPickups (obj);
				if (obj->x != oldx || obj->y != oldy)
				{
					np[i].anim += labs(obj->x-oldx) + labs(obj->y-oldy);
					np[i].moving = 4;
				}
				else if (np[i].moving)
					np[i].moving--;
			}
		}
		else
		{
			NetSetContext (NetTarget (obj));
			DoActor (obj);
		}
	}

	inrunframe = false;

	gamestate.playtime += tics;
	NetSetContext (localplayer);

	hashring[f & (NETRING-1)] = ConsistencyHash ();
	hashframes[f & (NETRING-1)] = f;

	if (netlog)
	{
		fprintf (netlog,"%d %ld %08lx rnd %d",levelseq,f,hashring[f & (NETRING-1)],US_RndIndex ());
		for (i=0;i<numplayers;i++)
		{
			netcmd_t *c = GetCmd (i,f);
			fprintf (netlog," | %lx %lx %d %d c%02x,%d,%d",playerobjs[i]->x,playerobjs[i]->y,
				playerobjs[i]->angle,GetHealth (i),c->buttons,c->x,c->y);
		}
		fprintf (netlog,"\n");
	}
}


/*
===================
=
= NetCheckKeys
=
===================
*/

static void NetCheckKeys (void)
{
	if (Paused)
		Paused = false;

	if (screenfaded)
		return;

	if (ChatKeys ())
		return;

	if (AM_Keys (LastScan))
		LastScan = sc_None;

	if (LastScan == sc_Escape || LastScan == sc_F10)
	{
		long lastsend = TimeCount;

		LastScan = 0;
		ShowMessage ("Leave the network game?\nY to leave, N to play on");
		IN_ClearKeysDown ();
		while (1)
		{
			NetReceive ();
			if (TimeCount - lastsend >= 7)
			{
				SendTics (pk_tics);
				lastsend = TimeCount;
			}
			if (Keyboard[sc_Y])
				Quit (NULL);			// NetShutdown tells them
			if (Keyboard[sc_N] || Keyboard[sc_Escape])
				break;
#ifdef __linux__
			SYS_Sleep ();
#endif
		}
		IN_ClearKeysDown ();
		DrawAllPlayBorder ();
		lasttimecount = TimeCount;
		if (MousePresent)
			Mouse(MDelta);
	}
}


/*
===================
=
= JoinGame
=
= The newcomer, at the start of the play loop: ask the arbiter to come in
= and wait for the game it sends
=
===================
*/

static void JoinGame (void)
{
	byte	buf[PKTMAX],from[10];
	snappacket_t *sk = (snappacket_t *)buf;
	joinpacket_t jp;
	snapackpacket_t ack;
	static	byte	*packed,*raw;
	static	boolean	*got;
	int		total,have,i;
	long	start,lastsend,len;

	packed = malloc (SNAPMAX+SNAPMAX/128+16);
	raw = malloc (SNAPMAX);
	got = calloc (SNAPMAX/SNAPCHUNK+2,sizeof(boolean));
	if (!packed || !raw || !got)
		Quit ("No memory to join the game");

	ShowMessage ("Joining the game...");

	memset (&jp,0,sizeof(jp));
	jp.magic[0] = 'W'; jp.magic[1] = '3';
	jp.type = pk_join;
	jp.version = NETVERSION;
	jp.nonce = mynonce;
	strcpy (jp.name,myname);
	jp.build = BuildId ();

	total = -1;
	have = 0;
	start = TimeCount;
	lastsend = TimeCount - 100;
	while (1)
	{
		if (TimeCount - lastsend >= 35)
		{
			IPXSend (0,arbaddr,&jp,sizeof(jp));
			lastsend = TimeCount;
		}
		while (IPXGet (buf,from))
		{
			if (buf[0] != 'W' || buf[1] != '3')
				continue;
			if (buf[2] == pk_full && !memcmp (buf+4,&mynonce,4))
				Quit (buf[3] == 1 ? "That deathmatch game is another build of the game"
					: "That deathmatch game is full");
			if (buf[2] != pk_snap || sk->nonce != mynonce
			|| sk->total == 0 || sk->total > SNAPMAX/SNAPCHUNK || sk->chunk >= sk->total
			|| sk->len > SNAPCHUNK || (total != -1 && sk->total != total))
				continue;
			if (total == -1)
			{
				total = sk->total;
				memcpy (arbaddr,from,10);
			}
			if (!got[sk->chunk])
			{
				got[sk->chunk] = true;
				memcpy (packed+(long)sk->chunk*SNAPCHUNK,sk->data,sk->len);
				if (sk->chunk == total-1)
					len = (long)sk->chunk*SNAPCHUNK + sk->len;
			}
			while (have < total && got[have])
				have++;
			memset (&ack,0,sizeof(ack));
			ack.magic[0] = 'W'; ack.magic[1] = '3';
			ack.type = pk_snapack;
			ack.got = have;
			ack.nonce = mynonce;
			IPXSend (0,from,&ack,sizeof(ack));
			start = TimeCount;			// it's coming
		}
		if (total > 0 && have == total)
			break;
		if (Keyboard[sc_Escape])
			Quit (NULL);
		if (TimeCount - start > 70*20)
			Quit ("No answer from the deathmatch game");
#ifdef __linux__
		SYS_Sleep ();
#endif
	}

	len = Unpack (packed,len,raw,SNAPMAX);
	if (len < 0 || !ReadGame (raw,len))
		Quit ("The deathmatch game sent a bad snapshot");

	// the arbiter's own address is where it sent from
	memcpy (np[Arbiter ()].addr,arbaddr,10);
	for (i=0;i<numplayers;i++)
	{
		np[i].ack = 0;
		for (len=0;len<NETRING;len++)
			np[i].cmdframe[len] = -1;
	}

	free (packed);
	free (raw);
	free (got);

	DrawFace ();
	DrawHealth ();
	DrawLives ();
	DrawLevel ();
	DrawAmmo ();
	DrawKeys ();
	DrawWeapon ();
	DrawScore ();
	DrawAllPlayBorder ();
}


/*
===================
=
= NetPlayLoop
=
===================
*/

void NetPlayLoop (void)
{
	int		i,f;
	long	start,lastsend;

	levelseq++;
	netframe = 0;
	for (i=0;i<numplayers;i++)
	{
		for (f=0;f<NETRING;f++)
			np[i].cmdframe[f] = -1;
		np[i].ack = 0;
	}
	for (f=0;f<NETRING;f++)
		hashframes[f] = -1;

	playstate = TimeCount = lasttimecount = 0;
	frameon = 0;
	ClearPaletteShifts ();

	if (dmjoining)
	{
		JoinGame ();				// netframe, levelseq... from the game
		dmjoining = false;
		TimeCount = lasttimecount = 0;
	}
	maketic = netframe+NETDELAY;
	inplay = true;

	if (MousePresent)
		Mouse(MDelta);

	do
	{
		//
		// one frame every NETTICS tics
		//
		while (TimeCount < lasttimecount+NETTICS)
		{
			NetReceive ();
#ifdef __linux__
			SYS_Sleep ();
#endif
		}
		lasttimecount += NETTICS;
		if (TimeCount - lasttimecount > 10*NETTICS)
			lasttimecount = TimeCount;		// way behind: don't race to catch up
		tics = NETTICS;

		//
		// our controls for a later frame (and, the arbiter, a newcomer)
		//
		SampleControls (&np[localplayer].cmds[maketic & (NETRING-1)]);
		np[localplayer].cmdframe[maketic & (NETRING-1)] = maketic;
		if (pending.active && pending.frame < 0)
		{
			joinrec_t *r = &joinrecs[pending.slot];

			np[localplayer].cmds[maketic & (NETRING-1)].event = EV_JOIN | pending.slot;
			pending.frame = maketic;
			memset (r,0,sizeof(*r));
			r->slot = pending.slot;
			r->frame = maketic;
			memcpy (r->addr,pending.addr,10);
			strcpy (r->name,pending.name);
			lastjoin = *r;
			havelastjoin = true;
		}
		maketic++;
		SendTics (pk_tics);

		//
		// everyone's for this one
		//
		NetReceive ();
		WaitForFrame (netframe);
		RunFrame (netframe);
		if (snapdue == netframe)
		{
			snapdue = -1;
			StartSnapshot ();
			SendChunks ();
		}
		netframe++;

		//
		// show it
		//
		UpdatePaletteShifts ();
		ThreeDRefresh ();

		SD_Poll ();
		UpdateSoundLoc ();

		if (screenfaded)
			VW_FadeIn ();

		NetCheckKeys ();
	} while (!playstate);

	//
	// don't leave while somebody might still need our controls
	//
	start = lastsend = TimeCount;
	SendTics (pk_tics);
	while (TimeCount - start < 70*5)
	{
		NetReceive ();
		for (i=0;i<numplayers;i++)
			if (i != localplayer && !np[i].gone && np[i].ack < netframe)
				break;
		if (i == numplayers)
			break;
		if (TimeCount - lastsend >= 7)
		{
			SendTics (pk_tics);
			lastsend = TimeCount;
		}
#ifdef __linux__
		SYS_Sleep ();
#endif
	}
	oldseq = levelseq;
	oldack = netframe;
	inplay = false;

	FinishPaletteShifts ();
}
