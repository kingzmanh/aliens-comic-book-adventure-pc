# Aliens: A Comic Book Adventure — PC Native

A native Windows version of *Aliens: A Comic Book Adventure* (Cryo Interactive / Mindscape, 1995),
built from the official **1.03US update**. No DOSBox: the game's own code is converted from x86
machine code to C by a static recompiler, and runs on a small PC runtime that replaces DOS, the
DOS extender, the BIOS and the sound/video hardware with Windows services.

**This repository contains no game files and no game code.** You need your own copy of the game:

- **Aliens CD 1 and CD 2** (discs, ISO / BIN+CUE images, or copied folders)
- **The official 1.03 update**, `ALIEN103.EXE`

Built on the official Aliens 1.03 update by Cryo Interactive / Mindscape (programming: Nicolas Choukroun).
*Aliens* © 20th Century Fox. This is a fan preservation project and is not affiliated with any of them.

## Installing

Run **`Aliens PC Setup.exe`**, point it at your two CDs and `ALIEN103.EXE`, choose a folder, press Install.
It copies both discs (they are then always "in the drive", so there is no disc swapping), unpacks the
update, lays the game out exactly like the original DOS installer did, and creates the game shortcut and
an uninstaller. About 1.5 GB of disk space is needed.

Two kinds of setup package:

| Package | Game executable |
|---|---|
| **prebuilt** — `Aliens PC Setup.exe` | included, installed directly |
| **convert** — `Aliens PC Setup.exe` + `builder\` | converted on your PC from your own `ALIENSP2.EXE` (about a minute) |

Scripted install: `"Aliens PC Setup.exe" /silent /cd1 <cd1> /cd2 <cd2> /patch <ALIEN103.EXE> /dir <folder> [/build] [/nodesktop] [/nostartmenu]`

## Playing

- Mouse and keyboard work as in the original. **Alt+Enter**: fullscreen.
- **F1**: switch between *continuous click* (the original: holding the button keeps clicking) and
  *single click* (one click per press). Saved in `aliens_pc.ini`.
- Music plays through the Windows General MIDI synthesizer (the game's GM arrangements);
  sound effects and speech through a software mixer.

## How it works

| Part | Where |
|---|---|
| LE loader (the PMODE/W executable), objects moved above 1 MB | `tools/le.py`, `src/rt/le.c` |
| Function discovery (recursive descent, code pointers from fixups, Watcom switch tables) | `tools/x86an.py` |
| x86 → C recompiler (registers in C locals, flags only where used, x87) | `tools/recomp.py` |
| Function start list (from Ghidra analysis of the 1.03 executable) | `re/functions.tsv` |
| DOS (`int 21h`) on Windows files, DPMI (`int 31h`), VESA/VGA BIOS, mouse, MSCDEX | `src/rt/dos.c`, `dpmi.c`, `bios.c` |
| Timer and keyboard interrupts driven from the Windows clock and window events | `src/rt/irq.c` |
| Window, palette, VESA frame buffer (SDL2) | `src/rt/video.c` |
| Native replacements for code that drove hardware: HMI SOS digital sound → software mixer, HMI MIDI → HMP player on Windows MIDI, Watcom C startup, `_chain_intr`, the graphics library's self-generating line routine, keyboard controller setup | `tools/native.txt`, `src/rt/native*.c` |
| Installer: ISO 9660 / BIN+CUE reader, LHA (`-lh0-`, `-lh5-`) unpacker, setup UI, uninstaller | `installer/` |
| Build kit for the convert package (Python + capstone + minimal GCC) | `tools/make_kit.py` |

## Building

Needs MSYS2 (MINGW64 shell, with `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-SDL2`, `make`) and
Python 3 with `capstone` (`pip install capstone pillow`).

```sh
# 1. generate the C code from your own ALIENSP2.EXE (from the 1.03 update)
make gen GAME_EXE=/path/to/ALIENSP2.EXE PY=/path/to/python.exe
# 2. build the game (game files go in game/hd, game/cd1, game/cd2 next to aliens.exe)
make -j16
# 3. setup packages: dist/prebuilt and dist/convert
make release dist PY=/path/to/python.exe
```

Development helpers: `run_test.sh <seconds> [args]` runs the game hidden and muted
(`ALIENS_SHOT=<ms>` screenshots, `ALIENS_CLICK=x,y,ms` scripted clicks, `ALIENS_DUMP=<ms>` call stacks);
functions listed in `tools/trace.txt` log their entry and exit registers.
