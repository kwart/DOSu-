# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

DOSu! is a rhythm game engine inspired by osu!, written in **C89 for 16-bit MS-DOS** and built with **Turbo C++ 3.0**. It targets real hardware (tested on Pentium II) and talks directly to a Sound Blaster card and Borland's BGI graphics library. This is not a portable Linux/modern C project — do not "modernize" the code (no C99 features, no POSIX headers, no stdint types). It must keep compiling under Turbo C 3.0.

## Repository layout

- `src/Dosu.c` — main engine (interactive gameplay). Entry point `main()` at line 469.
- `src/dosu!_demo.c` — autoplay/demo variant of the engine, used for demonstrations and low-end hardware testing. Has known sqrt issues on longer slider segments.
- `bin/DOSU!.EXE`, `bin/DOSU!_demo.EXE` — prebuilt DOS executables.
- `BGI/` — Borland Graphics Interface driver files (`EGAVGA.BGI`, etc.) and fonts. Engine `initgraph()` calls expect these at `C:\TC\BGI` by default on the DOS target; change the path in the source if needed (use double backslashes: `"C:\\TC\\BGI"`).

Note: `src/Dosu!.c` shows as deleted and `src/Dosu.c` as untracked in git — the file was renamed to drop the `!` (which is problematic on many shells/filesystems). Keep that rename.

## Build

There is no Makefile. The project must be built with Turbo C 2.01 / Turbo C++ 3.0 — `#include <dos.h>`, `<graphics.h>`, `<bios.h>`, the `interrupt` keyword, `inportb`/`outportb`, far pointers and 16-bit `int` are all Turbo C specific. Do not attempt `gcc`/`cc` from Linux.

Two build paths:

- **Inside a DOS/DOSBox Turbo C IDE**: open `src/Dosu.c` and press **F9** (Make). Graphics library must be linked.
- **From Linux via DOSBox automation**: `scripts/build.sh` mounts a local Turbo C install and invokes `TCC`/`BCC` non-interactively. Requires `dosbox` on PATH and `TC_DIR` pointing at a Turbo C install (default `./tools/TC`, containing `BIN/`, `INCLUDE/`, `LIB/`). Output lands in `build/` and is copied to `bin/DOSU.EXE`.
- `scripts/run.sh` stages a disposable `run/` directory (with EXE, `map.osu`, `audio.wav`, and a `TC/BGI` subdir matching the hard-coded `initgraph()` path) and launches it in DOSBox. Pass the map and audio with `MAP=... AUDIO=... scripts/run.sh`. It also writes a `dosbox.conf` that caps emulated CPU speed — DOSBox's `cycles=auto` default overclocks the game on modern notebooks. Speed is auto-configured from `/proc/cpuinfo` on first run and cached at `~/.cache/dosu/cycles`; override with `SPEED=slow|normal|fast|max`, `CYCLES=<int>`, or `RECALIBRATE=1`. At runtime in DOSBox, Ctrl+F11/Ctrl+F12 nudge cycles down/up live.
- `scripts/gen_sample.py` writes a demo `run/map.osu` + `run/audio.wav` (120 BPM, 34 circles, 8 kHz 8-bit mono PCM) for quick testing.

## Running

The EXE expects these files in its working directory:
- `map.osu` — an osu! v14 beatmap, renamed to exactly `map.osu`. Maps harder than ~2 stars are not recommended (BGI is slow; slider mechanics are imprecise).
- `audio.wav` — **8-bit mono PCM**, sample rate 8 kHz or 11 kHz (tested). Anything else will not play correctly.
- `BGI/` folder reachable at the path compiled into `initgraph()` (default `C:\TC\BGI`).

Sound Blaster defaults: base `0x220`, IRQ `5`, DMA `1`. These are `#define`d at the top of `Dosu.c` (`SB_BASE`, `IRQ`, `DMA_CHANNEL`) and must be edited in source to change.

## Architecture

The engine is a single-file C program with a tight game loop. Understanding the big picture:

1. **Beatmap parsing (`loadBeatmap`, src/Dosu.c:149)** — hand-written line-oriented parser for osu! v14 `.osu` format. Tracks section state (`[General]`, `[Difficulty]`, `[Events]`, `[TimingPoints]`, `[HitObjects]`) and fills the global arrays `objects[]`, `breaks[]`, `timingPoints[]`. Bounded by `MAX_OBJECTS`, `MAX_BREAKS`, `MAX_TIMING_POINTS`, `MAX_CURVE_POINTS` — do not raise `MAX_CURVE_POINTS` above ~20 or the Turbo C compiler will error with "array size too large".

2. **Audio path (Sound Blaster DSP + DMA, src/Dosu.c:342–408)** — `sb_reset`, `sb_write_dsp`, `setup_dma`, `sb_set_rate`, `sb_play`, `fill_buffer` drive the card directly through port I/O. `irq_handler` is installed as an `interrupt` vector on `IRQ 5` (`IRQ_VEC = 0x08 + IRQ`) to refill the DMA buffer. Audio is streamed in `BUFFER_SIZE`-chunks from `audio.wav`.

3. **Graphics (BGI)** — all drawing goes through Borland's `graphics.h` primitives (`circle`, `line`, `setcolor`, etc.). Slider paths are rasterized by `drawSliderPath` / `getFollowPosition` (src/Dosu.c:291, 302). Framerate/loop pacing is controlled by `FPS` and `loopDelay` `#define`s.

4. **Gameplay loop (`main`, src/Dosu.c:469)** — polls mouse (`getMousePos`), keyboard (`kbhit`/`bioskey`), and `getTimeMS()` each tick; walks the `objects[]` array comparing `now` against each object's `time` ± the hit windows (`HIT_WINDOW_300/100/50`, `MISS_WINDOW`); updates score, life (`drawLifeBar`), and combo. Sliders use the fallback "hold and move roughly in direction" mechanic described in the README — `checkDirection` (src/Dosu.c:416) gates the 300 on slider objects.

5. **Tunables** — essentially every gameplay/hardware knob is a `#define` at the top of `Dosu.c` (lines ~9–48). When the user asks to tweak difficulty, timing windows, approach-circle size, preview-ahead, HP drain, SB address, etc., edit those defines rather than introducing runtime configuration — there is no config file and no command-line parsing.

## Language and style constraints

- **C89 only.** No `//` comments in *new* code you add (the existing file already mixes both because Turbo C++ tolerates `//`, but stick to `/* */` to be safe). No declarations after statements — declare all locals at the top of a block (see `loadBeatmap` for the pattern).
- Source comments are a mix of English and Czech. Don't rewrite existing Czech comments unless asked; match the surrounding language when editing a block.
- Keep global arrays fixed-size. The engine runs in DOS real mode with tight memory — avoid `malloc` of anything large, avoid recursion on big inputs, and be mindful that `int` is 16-bit under Turbo C (use `long` for millisecond timestamps, as the code already does).

## Known limitations (do not "fix" without being asked)

- Spinners are unimplemented and intentionally ignored by the parser/loop.
- Sliders all display for the same fixed duration regardless of length — the "hold + direction mimic" fallback is deliberate, not a bug to patch.
- The demo variant has sqrt-related glitches on long slider segments; this is documented in the README.
