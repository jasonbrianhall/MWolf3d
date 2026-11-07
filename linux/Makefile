# Wolfenstein 3-D for Linux (SDL2)
#
#   make              wolf3d (VERSION=wl6), or spear for sod / sdm
#   make VERSION=wl1  for the shareware data (wl6, wl6apo, wl1, sod, sdm:
#                     as for msdos/Makefile)
#   make run DATA=dir run it with the game's files in dir
#   make setup        setup and ipxsetup (the same program; any data), the
#                     DOS version's SETUP / IPXSETUP in the terminal
#   make clean
#
# Needs a C and C++ compiler and SDL2 (Debian / Ubuntu: build-essential
# libsdl2-dev; Fedora: gcc-c++ SDL2-devel; Arch: base-devel sdl2).
#
# The same source as the DOS version: sys_linux.c stands in for the PC
# (timer and keyboard interrupts, mouse, PC speaker, an AdLib in software
# for the music, the digitized sounds, the screen, UDP for IPX), and
# sys_midi.c the MPU-401 (General MIDI: an OPL3 in software, FluidSynth if
# it's installed, opened at run time, or the ALSA sequencer).
VERSION ?= wl6

SRC     := ../wolfsrc
BUILD   := build/$(VERSION)

ifeq ($(VERSION),wl6)
VERDEF  = -DVER_WL6
EXENAME = wolf3d
endif
ifeq ($(VERSION),wl6apo)
VERDEF  = -DVER_WL6APO
EXENAME = wolf3d
endif
ifeq ($(VERSION),wl1)
VERDEF  = -DVER_WL1
EXENAME = wolf3d
endif
ifeq ($(VERSION),sod)
VERDEF  = -DVER_SOD
EXENAME = spear
endif
ifeq ($(VERSION),sdm)
VERDEF  = -DVER_SDM
EXENAME = spear
endif
ifndef VERDEF
$(error VERSION must be wl6, wl6apo, wl1, sod or sdm)
endif

CC       ?= gcc
CXX      ?= g++
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs)

# As in the DOS build: 1992's C (gnu89, common globals, floats stored at
# their size). linuxinc has empty stand-ins for the DOS headers. The
# program is linked at a fixed address (no PIE): saved games hold
# pointers, as they always did.
CFLAGS   ?= -O2 -g
ALLCFLAGS = $(CFLAGS) -std=gnu89 -fcommon -ffloat-store -fno-strict-aliasing -fwrapv \
            -fno-pie -w $(VERDEF) -I$(SRC) -I$(SRC)/linuxinc $(SDL_CFLAGS)
CXXFLAGS ?= -O2 -g
ALLCXXFLAGS = $(CXXFLAGS) -fno-exceptions -fno-rtti -fno-pie -w -I$(SRC)
LDFLAGS  ?=
ALLLDFLAGS = $(LDFLAGS) -no-pie

WOLF_SRC := id_ca id_in id_mm id_pm id_sd id_us_1 id_us_a id_vh id_vl \
            wl_act1 wl_act2 wl_agent wl_debug wl_draw wl_game wl_inter wl_main \
            wl_menu wl_net wl_play wl_scale wl_state \
            wl_map gamepal signon sys_linux id_midi sys_midi
ifeq ($(filter sod sdm,$(VERSION)),)
WOLF_SRC += wl_text
endif
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(WOLF_SRC))) $(BUILD)/dbopl.o $(BUILD)/oplemu.o $(BUILD)/midiopl.o
HDRS := $(wildcard $(SRC)/*.h)

all: $(EXENAME) setup

# SETUP / IPXSETUP (msdos/setup.c): one program for every VERSION
setup: ../msdos/setup.c Makefile
	$(CC) -O2 -std=gnu99 -w -o $@ ../msdos/setup.c
	cp $@ ipxsetup

$(EXENAME): $(OBJS)
	$(CXX) $(ALLLDFLAGS) $(OBJS) $(SDL_LIBS) -lm -ldl -o $@
	@echo "Built $@ ($(VERSION))"

$(BUILD)/%.o: $(SRC)/%.c $(HDRS) Makefile
	@mkdir -p $(BUILD)
	$(CC) $(ALLCFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.cpp $(SRC)/dbopl.h $(SRC)/midibank.h Makefile
	@mkdir -p $(BUILD)
	$(CXX) $(ALLCXXFLAGS) -c $< -o $@

DATA ?= .
run: $(EXENAME)
	./$(EXENAME) -data $(DATA) $(ARGS)

clean:
	rm -rf build wolf3d spear setup ipxsetup

# (always linked again: wolf3d is the same name for wl6, wl6apo and wl1)
.PHONY: all run clean $(EXENAME)
