// process.h: DOS only; sys_linux.h has what the game uses
#ifdef _WIN32
#include_next <process.h>	// (Windows has one too, which its own headers want)
#endif
