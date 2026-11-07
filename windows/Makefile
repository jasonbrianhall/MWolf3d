# Wolfenstein 3-D for Windows (SDL2, built with MinGW-w64)
#
#   make              wolf3d.exe (VERSION=wl6), or spear.exe for sod / sdm,
#                     and setup.exe / ipxsetup.exe
#   make VERSION=wl1  for the shareware data (wl6, wl6apo, wl1, sod, sdm:
#                     as for msdos/Makefile)
#   make msi          the installer, every VERSION (wixl, from msitools;
#                     the GitHub workflow uses WiX 3's candle and light)
#   make clean
#
# The Linux source as it is (sys_linux.c and sys_midi.c have the Windows
# parts): cross-compiled from Linux with MinGW-w64 (Debian / Ubuntu:
# mingw-w64), or built in MSYS2 (CROSS= ). SDL2's MinGW development files:
# SDL2-devel-2.x-mingw.tar.gz from https://github.com/libsdl-org/SDL/releases,
# SDL2=its x86_64-w64-mingw32 directory. Everything is linked in: the
# .exe needs no DLLs.
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

CROSS   ?= x86_64-w64-mingw32-
CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
WINDRES := $(CROSS)windres
SDL2    ?= /opt/SDL2/x86_64-w64-mingw32

SDL_CFLAGS := -I$(SDL2)/include/SDL2
SDL_LIBS   := -L$(SDL2)/lib -lmingw32 -lSDL2main -Wl,-Bstatic -lSDL2 \
              -lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 \
              -loleaut32 -lshell32 -lsetupapi -lversion -luuid -lws2_32 -liphlpapi -lpthread

# As on Linux: 1992's C. The program is at a fixed address (no ASLR):
# saved games hold pointers, as they always did.
CFLAGS   ?= -O2
ALLCFLAGS = $(CFLAGS) -std=gnu89 -fcommon -ffloat-store -fno-strict-aliasing -fwrapv \
            -w $(VERDEF) -I$(SRC) -I$(SRC)/linuxinc $(SDL_CFLAGS)
CXXFLAGS ?= -O2
ALLCXXFLAGS = $(CXXFLAGS) -fno-exceptions -fno-rtti -w -I$(SRC)
ALLLDFLAGS = $(LDFLAGS) -static -mwindows -Wl,--disable-dynamicbase,--disable-high-entropy-va

WOLF_SRC := id_ca id_in id_mm id_pm id_sd id_us_1 id_us_a id_vh id_vl \
            wl_act1 wl_act2 wl_agent wl_debug wl_draw wl_game wl_inter wl_main \
            wl_menu wl_net wl_play wl_scale wl_state \
            wl_map gamepal signon sys_linux id_midi sys_midi
ifeq ($(filter sod sdm,$(VERSION)),)
WOLF_SRC += wl_text
endif
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(WOLF_SRC))) $(BUILD)/dbopl.o $(BUILD)/oplemu.o \
        $(BUILD)/midiopl.o build/wolf3d.res.o
HDRS := $(wildcard $(SRC)/*.h)

all: $(EXENAME).exe setup.exe

build/wolf3d.res.o: wolf3d.rc wolf3d.ico
	@mkdir -p build
	$(WINDRES) wolf3d.rc -O coff -o $@

# SETUP / IPXSETUP (msdos/setup.c): one program for every VERSION, in a
# console window
setup.exe: ../msdos/setup.c build/wolf3d.res.o Makefile
	$(CC) -O2 -std=gnu99 -w -static -o $@ ../msdos/setup.c build/wolf3d.res.o
	cp $@ ipxsetup.exe

$(EXENAME).exe: $(OBJS)
	$(CXX) $(ALLLDFLAGS) $(OBJS) $(SDL_LIBS) -o $@
	@echo "Built $@ ($(VERSION))"

$(BUILD)/%.o: $(SRC)/%.c $(HDRS) Makefile
	@mkdir -p $(BUILD)
	$(CC) $(ALLCFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.cpp $(SRC)/dbopl.h $(SRC)/midibank.h Makefile
	@mkdir -p $(BUILD)
	$(CXX) $(ALLCXXFLAGS) -c $< -o $@

# Every VERSION into release/ (as the zips have them: WL6\WOLF3D.EXE...),
# then the installer from it
release:
	@for v in wl6 wl6apo wl1 sod sdm; do \
		case $$v in sod|sdm) exe=spear ;; *) exe=wolf3d ;; esac; \
		$(MAKE) --no-print-directory VERSION=$$v $$exe.exe setup.exe || exit 1; \
		d=release/$$(echo $$v | tr a-z A-Z); mkdir -p $$d; \
		cp $$exe.exe setup.exe ipxsetup.exe $$d/; rm -f $$exe.exe; \
		case $$v in wl6apo) ext=WL6 ;; *) ext=$$(echo $$v | tr a-z A-Z) ;; esac; \
		cp ../msdos/PLAYERS.WL6 $$d/PLAYERS.$$ext; \
		cp ../msdos/RESPAWN.WAD ../msdos/FREEDOOM.TXT ../msdos/WOLFMIDI.TXT $$d/; \
		rm -rf $$d/MUSIC; cp -r ../msdos/MUSIC $$d/MUSIC; \
	done
	cp README.TXT wolf3d.ico License.rtf release/

# (the workflow makes the real one with WiX 3, dialogs and all; this is
# the same installer without them, to try it out)
msi: release
	python3 mkwxs.py release build/wolf3d.wxs $(or $(PRODUCT_VERSION),1.0.0.0) --no-ui
	cd release && wixl -a x64 -o ../Wolfenstein3D.msi ../build/wolf3d.wxs

clean:
	rm -rf build release wolf3d.exe spear.exe setup.exe ipxsetup.exe Wolfenstein3D.msi

.PHONY: all release msi clean
