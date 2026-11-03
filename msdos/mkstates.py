#!/usr/bin/env python3
"""
mkstates.py -- writes wolfsrc/wl_states.h: every statetype the game
defines, in order, inside the same #if's as its definition.

A deathmatch newcomer is sent the actors' states as numbers in this list
instead of pointers, so a DOS and a Linux machine (or two builds of the
same version) understand each other. Run it again after adding a state:

  python3 msdos/mkstates.py
"""

import os
import re
import sys

SRC = os.path.join (os.path.dirname (os.path.abspath (__file__)),"..","wolfsrc")
FILES = ["wl_agent.c","wl_act2.c","wl_net.c"]

DEF = re.compile (r"^\s*statetype\s+(s_\w+)\s*=")
PP = re.compile (r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b\s*(.*?)\s*(//.*)?$")


def condition (stack):
	parts = [c for c in stack if c != "1"]
	if not parts:
		return None
	return " && ".join ("(%s)" % c for c in parts)


def main ():
	out = ["// WL_STATES.H -- made by msdos/mkstates.py: don't edit",
		"//",
		"// Every statetype, in order, for wl_net.c's deathmatch snapshots",
		""]
	names = 0
	for name in FILES:
		stack = []			# the conditions in force
		alts = []			# for each level: what the earlier branches said
		with open (os.path.join (SRC,name),encoding="latin-1") as f:
			for line in f:
				m = PP.match (line)
				if m:
					kind, arg = m.group (1), m.group (2)
					if kind == "ifdef":
						cond = "defined(%s)" % arg.split ()[0]
					elif kind == "ifndef":
						cond = "!defined(%s)" % arg.split ()[0]
					elif kind == "if":
						cond = arg
					if kind in ("if","ifdef","ifndef"):
						stack.append (cond)
						alts.append ([cond])
					elif kind == "elif":
						prev = alts[-1]
						stack[-1] = " && ".join (["!(%s)" % c for c in prev] + ["(%s)" % arg])
						prev.append (arg)
					elif kind == "else":
						stack[-1] = " && ".join ("!(%s)" % c for c in alts[-1])
					elif kind == "endif":
						stack.pop ()
						alts.pop ()
					continue
				m = DEF.match (line)
				if m:
					cond = condition (stack)
					if cond:
						out.append ("#if %s" % cond)
					out.append ("STATE(%s)" % m.group (1))
					if cond:
						out.append ("#endif")
					names += 1
	with open (os.path.join (SRC,"wl_states.h"),"w") as f:
		f.write ("\n".join (out) + "\n")
	print ("wl_states.h: %d states" % names)


if __name__ == "__main__":
	main ()
