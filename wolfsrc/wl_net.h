// WL_NET.H -- network co-op and deathmatch (lockstep): IPX on MS-DOS,
// UDP on Linux

#ifndef __WL_NET_H__
#define __WL_NET_H__

#define MAXPLAYERS	4
#define NETNAMELEN	15

extern	boolean	netgame;			// true for a -nodes co-op or a deathmatch game
extern	boolean	deathmatch;			// -deathmatch
extern	boolean	dmmonsters;			// -monsters: the map's enemies too, coming back
extern	int		numplayers;			// 1 unless netgame
extern	int		localplayer;		// the one this machine controls and views
extern	int		netcontext;			// whose globals are loaded right now
extern	objtype	*playerobjs[MAXPLAYERS];

void	NetStartup (void);			// parses -nodes, finds the other players
void	NetShutdown (void);
void	NetLeaving (void);			// tells the others we've gone (at exit)
char	*NetName (int p);			// player p's name (-name)
void	NetEnemySpawned (int tile, int x, int y, objtype *ob);	// deathmatch -monsters
void	NetEnemyKilled (objtype *ob);
boolean	NetStatReserved (int i);	// statobjlist[i] is an item that comes back
void	ScanInfoTile (int tile, int x, int y);
int		NetAngle (long dx, long dy);	// atan2's, in ANGLES, from the tables
void	NetGameLoop (void);			// runs the whole co-op game, never returns
void	NetLevelStart (void);		// after SetupGameLevel: spawn the others
void	NetPlayLoop (void);			// replaces PlayLoop in a netgame

void	NetSetContext (int p);		// load player p's state into the globals
boolean	NetIsLocal (void);			// drawing allowed (current context is local)
boolean	NetPlayerAlive (int p);
int		NetTarget (objtype *ob);	// player an enemy is after
void	NetKillPlayer (objtype *attacker);
boolean	NetCurrentDead (void);
boolean	NetLocalDead (void);
objtype	*NetFindTarget (objtype *shooter, boolean knife, long *dist);
boolean	NetCanSee (objtype *viewer, objtype *ob);
int		NetPlayerShape (objtype *ob);
memptr	NetShape (int shapenum);
void	NetDamagePlayer (objtype *target, int damage, objtype *attacker);
void	NetPlayerHurt (void);
void	NetDrawChat (void);
boolean	NetChatting (void);
boolean	NetPlayerBlocked (objtype *ob);
extern	long	netoldx,netoldy;

#define NETSHAPEFLAG	0x4000		// shapenum of a recolored player sprite

#endif
