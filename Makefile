# Aliens: A Comic Book Adventure for Windows.
# Build from an MSYS2 MINGW64 shell:  make -j16            (English 1.03US)
#                                     make -j16 GAME=fr    (French 1.02F)
#
# src/gen is the game's code, recompiled from ALIENSP2.EXE by tools/recomp.py
# (make gen); src/rt is the PC runtime that replaces DOS, the DOS extender,
# the BIOS and the hardware.  The French build has its own generated code
# (src/gen_fr), function list and native table, and -DGAME_FR=1 selects its
# addresses in src/rt/addrs.h.
CC    = gcc
PY   ?= python
GAME ?= en
ifeq ($(GAME),fr)
B        = build/fr
G        = src/gen_fr
EXE      = aliens_fr.exe
GAME_EXE ?= gamedata/fr_cd1/ALIENS/ALIENSP2.EXE
FUNCS    = re/functions_fr.tsv
NATIVE   = tools/native_fr.txt
DEFS     = -DGAME_FR=1
ICON_SRC ?= gamedata/fr_cd1/ALIENS/ALIENS.ICO
else
B        = build
G        = src/gen
EXE      = aliens.exe
GAME_EXE ?= gamedata/patch103/ALIENSP2.EXE
FUNCS    = re/functions.tsv
NATIVE   = tools/native.txt
DEFS     =
ICON_SRC ?= gamedata/cd1/ALIENS/ALIENS.ICO
endif

$(shell mkdir -p $(B)/tmp)
export TMP  := $(shell cygpath -m "$(CURDIR)/$(B)/tmp")
export TEMP := $(TMP)

SDL_CFLAGS := $(shell sdl2-config --cflags | sed 's/-Dmain=SDL_main//')
SDL_LIBS   := $(shell sdl2-config --static-libs | sed 's/-lSDL2main//;s/-mwindows//')

CFLAGS_GEN := -std=gnu11 -O2 -g0 -fwrapv -fno-strict-aliasing -w -Isrc/rt -I$(G) $(DEFS)
CFLAGS_RT  := -DSDL_MAIN_HANDLED -std=gnu11 -O2 -g -Wall -Wno-unused-function -fwrapv -fno-strict-aliasing -Isrc/rt -I$(G) $(DEFS) $(SDL_CFLAGS)

GEN_SRCS := $(wildcard $(G)/*.c)
RT_SRCS  := $(wildcard src/rt/*.c)
# the dev build embeds the game's icon when the user's CD 1 is in gamedata/
RES_OBJ := $(if $(wildcard $(ICON_SRC)),$(B)/aliens_res.o,)
OBJS := $(patsubst $(G)/%.c,$(B)/gen/%.o,$(GEN_SRCS)) $(patsubst src/%.c,$(B)/%.o,$(RT_SRCS)) $(RES_OBJ)

all: $(EXE)

$(EXE): $(OBJS)
	$(CC) -mwindows -o $@ $^ -static $(SDL_LIBS) -lwinmm -lm

$(B)/gen/%.o: $(G)/%.c src/rt/cpu.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_GEN) -c $< -o $@

$(B)/rt/%.o: src/rt/%.c src/rt/rt.h src/rt/cpu.h src/rt/addrs.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_RT) -c $< -o $@

# icon: made from the game's own ALIENS.ICO on the user's CD 1
$(B)/aliens.ico: $(ICON_SRC) tools/make_icon.py
	$(PY) tools/make_icon.py $< $@

$(B)/aliens_res.o: src/rt/aliens.rc $(B)/aliens.ico
	windres -I$(B) $< -o $@

# regenerate the recompiled code (after changing tools/ or tools/native.txt)
gen:
	$(PY) tools/recomp.py $(GAME_EXE) $(FUNCS) $(NATIVE) $(G)

clean:
	rm -rf $(B) $(EXE)

.PHONY: all gen clean

# ---- release: game exe without the game's icon (setup puts it in from the
# user's CD), and the setup program with that exe inside ----
# (both languages: build/release/aliens.exe and build/fr/release/aliens.exe)
release:
	$(MAKE) GAME=en build/release/aliens.exe
	$(MAKE) GAME=fr build/fr/release/aliens.exe
	$(MAKE) GAME=en build/setup.exe
	cp build/setup.exe "Aliens PC Setup.exe"

$(B)/release/aliens.exe: $(filter-out $(B)/aliens_res.o,$(OBJS))
	@mkdir -p $(B)/release
	$(CC) -mwindows -o $@ $^ -static $(SDL_LIBS) -lwinmm -lm
	strip $@

SETUP_SRCS := installer/setup.c installer/install.c installer/lha.c installer/cdsrc.c installer/util.c
SETUP_LIBS := -lcomctl32 -lcomdlg32 -lole32 -luuid -lshell32 -ladvapi32

$(B)/setup_res.o: installer/setup.rc installer/setup.manifest build/release/aliens.exe build/fr/release/aliens.exe
	windres -I installer $< -o $@

build/setup.exe: $(SETUP_SRCS) installer/setup.h installer/install.h $(B)/setup_res.o
	$(CC) -O2 -Wall -Wno-format-truncation -mwindows -static -o $@ $(SETUP_SRCS) $(B)/setup_res.o $(SETUP_LIBS)

.PHONY: release

# setup without the prebuilt game (for the "convert on this PC" package)
$(B)/setup_nogame_res.o: installer/setup_nogame.rc installer/setup.manifest
	windres -I installer $< -o $@

build/setup_nogame.exe: $(SETUP_SRCS) installer/setup.h installer/install.h $(B)/setup_nogame_res.o
	$(CC) -O2 -Wall -Wno-format-truncation -mwindows -static -o $@ $(SETUP_SRCS) $(B)/setup_nogame_res.o $(SETUP_LIBS)

# the two release packages:
#   dist/prebuilt/Aliens PC Setup.exe           (game exe inside)
#   dist/convert/Aliens PC Setup.exe + builder/ (converts the user's ALIENSP2.EXE)
dist: release build/setup_nogame.exe
	rm -rf dist && mkdir -p dist/prebuilt dist/convert
	cp build/setup.exe "dist/prebuilt/Aliens PC Setup.exe"
	cp build/setup_nogame.exe "dist/convert/Aliens PC Setup.exe"
	$(PY) tools/make_kit.py dist/convert

.PHONY: dist
