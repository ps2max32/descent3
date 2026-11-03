# Descent 3 on maxsdk (PS2)

Descent 3 1.5.0 (the GPL source release, tag `v1.5.0`, branch `ps2`) built for the PS2 with
`libs/maxsdk` (clang, R5900) and drawn through `libs/libgl`. The port is in `ps2/` and the
`Makefile`; the engine changes are under `#ifdef __PS2__` (and `#ifdef DEMO` for the demo data).

```
make cxxlib                          picolibc (POSIX I/O) and libc++ for the port (once)
make [DEMO=1]                        $(OUT)/descent3.elf (build/apps/descent3[-demo])
make DEMO=1 FRAMES=300 PLAY=1 ARGS="-pilot ps2 -loadlevel polaris.d3l"
                                     a test run: a picture (host:descent3.tga) after 300
                                     frames of play, then the end marker
make run [D3DATA=<retail dir>]       copies the hogs and missions to build/pcsx2-test/d3, runs
                                     it (linked into virtual memory, the default: [-vm] build,
                                     started by mxload.elf on PCSX2's EE interpreter;
                                     MX_MEMORY=phys for the old way)
```

The game's directory is `host:d3` (`DIR`); the build variables are in the head of the
`Makefile` (`LOG`, `MEMLOG`, `OPT`, `FRAMES`, `PLAY`, `ARGS`, `SHOT`, `SWAP`).
The picture: the TV's SD mode with two buffers, or with these in `ARGS`: `-ps2beam` beam
racing, `-ps2rgb24` 24-bit colour (with beam racing), `-ps2hd` 1080i (libgl's
`mxgl_set_mode`). A `FRAMES` run ends with the frames a second of the frames counted.

## Data

- Retail: the hogs (`d3.hog`, `extra*.hog`, ...) and `missions/` of a retail copy
  (GOG, Steam, CD), `make` without `DEMO`.
- Demo: the 1998 Descent 3 demo (`D3DEMO.EXE` of archive.org's `D3DEMO_ISO`, unpacked with
  7z): `d3demo.hog` and `EXTRA.HOG` (as `extra.hog`) in `build/pcsx2-test/d3`, `make DEMO=1`.
  Its levels: Polaris and The Core run; PiccuStation does not fit in memory (see below).
  The demo's table file has no page lengths (an older format: detected, `manage.cpp`); the
  bitmaps the 1.5 engine has but the demo has not are stood in for (grey interface bitmaps,
  effects of the same kind, the guidebot for the red one).

## How

| Part | How |
|---|---|
| C++ | C++17 with `std::filesystem`: libc++ and libc++abi from the LLVM tree (no exceptions, no threads), built by `make cxxlib` on a picolibc built with POSIX I/O. `try`/`catch` are plain blocks (`ps2/ps2_port.h`); a cfile error ends the game |
| Files | `ps2/ps2_posix.c`: open, read, stat, opendir, mkdir, unlink ... on the IOP file server (`mx_fio_*`); `/` is `host:`; rename is a copy |
| SDL2 | not ported: `ps2/ps2_sdl.cpp` has the calls the game makes (window, GL procs, events, joystick) |
| Renderer | the game's OpenGL renderer on `libs/libgl` through `ps2/ps2_gl.cpp`: two texture units on the GS's one (the lightmap is a second pass, `DST_COLOR, ZERO`); the game's texture names mapped to libgl's (2048); lightmaps up to 64 x 64 in atlas pages of one size each; no framebuffer object; the gamma in the texels |
| Scripts | the level scripts (OSIRIS DLLs) linked in, each with its symbols local but its exports (`<script>_<export>`); `dlopen`/`dlsym` (`ps2/ps2_dl.cpp`) find them by name. The demo build links the demo's only |
| Input | pad 0 as the keyboard and mouse the game knows: right stick the mouse (look), left stick up/down forward/back (A/Z), d-pad the arrows, cross Enter, circle and start Escape, R1/R2 and square fire (mouse buttons), L1/L2 slide, triangle the next weapon, L3 afterburner, select the automap; also a 4-axis joystick |
| Virtual memory | maxsdk's (`<mx/vm.h>`, `mx_vm_config` in `ps2/ps2_sdl.cpp`), as Quake III's: `malloc` (libmxc's, not picolibc's) in up to 128 MiB, paged to `host:d3/descent3.swp` (`SWAP`); with `MX_MEMORY=vm` (started by `mxload.elf host:descent3.elf`) the game's code, data and bss too, only maxsdk, picolibc, libc++ and libgl (`MX_CORE_OBJS`) in core (1.5 MiB); libgl's arenas and textures in pinned memory, frames taken as needed (2.3 MiB in Polaris). In PCSX2 only on the EE interpreter: on the recompiler VM stays off and the game has the 32 MiB as below |
| Memory | 32 MiB (without VM): `-lowmem` (64 x 64 textures); a level's textures to libgl when paged in and the game's copy dropped (the renderer is not set up again for the game), other textures' copies freed once libgl has them (paged in again from their file); bitmaps paged out before a level loads; no level loading picture; the TelCom background loaded when it shows; terrain normals computed when asked, terrain and room points kept for what is drawn and made at the first frame; the rooms' and lightmaps' small blocks from chunks; lightmap coordinates in 16 bits; smaller fixed tables (`MAX_CELLS_TO_RENDER`, `MAX_LIGHTMAPS`, vis effects, dynamic lightmap memory, the network's); AABB lists grown as they fill; the renderer's texel conversion without tables; libgl's arenas 256 KB (`MXGL_ARENA_BYTES`) and its 1-bit-alpha textures in 16 bits; `-Os` but for the frame's hot paths |
| Alignment | the R5900 traps on unaligned loads and stores (PCSX2 does not): fixed in the TGA reader, the room memory pool and the network packers |

Not done yet: sound and music (no device: waits for maxsdk's sound), movies, fog (libgl
has none), network play. PiccuStation (the demo's largest level: 5 MiB of rooms, 32700
lightmaps) loads its rooms, terrain and objects but runs out of memory paging in its
models and textures after: measured with `BIGHEAP=1` (PCSX2 with 128 MB), it wants about
20 MiB of heap in play, the PS2 has 17.9. The larger retail levels will be the same; the
retail build also links all 55 level scripts (2.4 MiB), which loading each from a file when
its level starts would give back.

Memory tools: `MEMLOG=1` (allocations by caller, the peak, a report at the first failure
and when a level is loaded), built apart. Always: the VM's counters (with the pinned memory) when a
level is loaded and at the end of a `FRAMES` run.

## Tested

PCSX2: the demo's menus, Polaris and The Core. The real PS2 (`build/ps2-hw-test`, `d3`
linked to `build/pcsx2-test/d3`), the first 599 frames of Polaris: 21.6 fps in SD, 20.7
with beam racing in 24-bit colour, 21.0 in 1080i with beam racing (`libs/libgl/FPS.md`).
