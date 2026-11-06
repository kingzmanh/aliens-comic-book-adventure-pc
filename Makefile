# Aliens: A Comic Book Adventure for Windows.
# Build from an MSYS2 MINGW64 shell:  make -j16
#
# src/gen is the game's code, recompiled from ALIENSP2.EXE by tools/recomp.py
# (make gen); src/rt is the PC runtime that replaces DOS, the DOS extender,
# the BIOS and the hardware.
CC    = gcc
B     = build
EXE   = aliens.exe
PY   ?= python
GAME_EXE ?= gamedata/patch103/ALIENSP2.EXE

$(shell mkdir -p $(B)/tmp)
export TMP  := $(shell cygpath -m "$(CURDIR)/$(B)/tmp")
export TEMP := $(TMP)

SDL_CFLAGS := $(shell sdl2-config --cflags | sed 's/-Dmain=SDL_main//')
SDL_LIBS   := $(shell sdl2-config --static-libs | sed 's/-lSDL2main//;s/-mwindows//')

CFLAGS_GEN := -std=gnu11 -O2 -g0 -fwrapv -fno-strict-aliasing -w -Isrc/rt -Isrc/gen
CFLAGS_RT  := -DSDL_MAIN_HANDLED -std=gnu11 -O2 -g -Wall -Wno-unused-function -fwrapv -fno-strict-aliasing -Isrc/rt -Isrc/gen $(SDL_CFLAGS)

GEN_SRCS := $(wildcard src/gen/*.c)
RT_SRCS  := $(wildcard src/rt/*.c)
# the dev build embeds the game's icon when the user's CD 1 is in gamedata/
ICON_SRC ?= gamedata/cd1/ALIENS/ALIENS.ICO
RES_OBJ := $(if $(wildcard $(ICON_SRC)),$(B)/aliens_res.o,)
OBJS := $(patsubst src/%.c,$(B)/%.o,$(GEN_SRCS) $(RT_SRCS)) $(RES_OBJ)

all: $(EXE)

$(EXE): $(OBJS)
	$(CC) -mwindows -o $@ $^ -static $(SDL_LIBS) -lwinmm -lm

$(B)/gen/%.o: src/gen/%.c src/rt/cpu.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_GEN) -c $< -o $@

$(B)/rt/%.o: src/rt/%.c src/rt/rt.h src/rt/cpu.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_RT) -c $< -o $@

# icon: made from the game's own ALIENS.ICO on the user's CD 1
$(B)/aliens.ico: $(ICON_SRC) tools/make_icon.py
	$(PY) tools/make_icon.py $< $@

$(B)/aliens_res.o: src/rt/aliens.rc $(B)/aliens.ico
	windres $< -o $@

# regenerate the recompiled code (after changing tools/ or tools/native.txt)
gen:
	$(PY) tools/recomp.py $(GAME_EXE) re/functions.tsv tools/native.txt src/gen

clean:
	rm -rf $(B) $(EXE)

.PHONY: all gen clean

# ---- release: game exe without the game's icon (setup puts it in from the
# user's CD), and the setup program with that exe inside ----
release: build/release/aliens.exe build/setup.exe
	cp build/setup.exe "Aliens PC Setup.exe"

build/release/aliens.exe: $(filter-out $(B)/aliens_res.o,$(OBJS))
	@mkdir -p build/release
	$(CC) -mwindows -o $@ $^ -static $(SDL_LIBS) -lwinmm -lm
	strip $@

SETUP_SRCS := installer/setup.c installer/install.c installer/lha.c installer/cdsrc.c installer/util.c
SETUP_LIBS := -lcomctl32 -lcomdlg32 -lole32 -luuid -lshell32 -ladvapi32

$(B)/setup_res.o: installer/setup.rc installer/setup.manifest build/release/aliens.exe
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
dist: build/setup.exe build/setup_nogame.exe
	rm -rf dist && mkdir -p dist/prebuilt dist/convert
	cp build/setup.exe "dist/prebuilt/Aliens PC Setup.exe"
	cp build/setup_nogame.exe "dist/convert/Aliens PC Setup.exe"
	$(PY) tools/make_kit.py dist/convert

.PHONY: dist
