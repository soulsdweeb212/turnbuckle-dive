# Turnbuckle Dive

[![CI](https://github.com/soulsdweeb212/turnbuckle-dive/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/soulsdweeb212/turnbuckle-dive/actions/workflows/ci.yml)

Turnbuckle Dive is a PC port of WCW/nWo Revenge (Nintendo 64), built on
[LibUltraShip](https://github.com/Kenix3/libultraship) and the matching decompilation of the game.

**Status:** planning and groundwork. Nothing is playable yet.

## No game assets included

This repository contains no ROM data, graphics, models, sounds or music from the game, and it never will. To use it you
must supply your own legally obtained copy of the game's ROM; the port reads it at run time (and, later, an extractor
builds an asset archive from it on your machine). Do not open issues or pull requests that contain game data.

Turnbuckle Dive is a fan project. It is not affiliated with or endorsed by THQ, AKI Corporation, Nintendo, WWE or
any rights holder of WCW or nWo. All trademarks belong to their owners.

## Roadmap

| Phase | Done when |
|---|---|
| P0 | groundwork in the decompilation's source is in and its ROM still builds byte-identical |
| P1 | the native build links and opens a window |
| P1.5 | a model from the game draws on screen |
| P2 | it boots to the first screen |
| P3 | menus and controllers work |
| P4 | a match runs |
| P5 | audio plays |
| P6 | saves work |
| P7 | Windows and macOS builds, and the Torch asset extractor |

Linux (x86-64) is planned first. Open design decisions are listed in [docs/DECISIONS.md](docs/DECISIONS.md).

## Building (Linux and WSL)

There are three programs so far:

- `turnbuckle-dive` loads your ROM, checks that it is the supported one, and prints what it found. When the build
  includes the game's code (below), it then loads all 7657 resources of the ROM with the game's own loader and prints
  how many it loaded. `turnbuckle-dive --res 0x3` loads only resource 3 and prints its size and first 16 bytes (the id
  can also be written in decimal).
- `hello` opens a LibUltraShip window and draws a test picture made of N64 display-list commands, at the game's
  480x240 resolution shown at 4:3.
- `viewer`, the asset viewer, built with the game's code: it draws a wrestler's head, an effect sprite and a 2D
  picture from your ROM with the game's own display-list functions. [docs/VIEWER.md](docs/VIEWER.md) explains its
  commands, its check (`viewer --check`), its command listing (`viewer --dump`), the debug menu that F1 shows with
  LibUltraShip's Gfx Debugger, and how to step through the game's functions.

Get the code together with its LibUltraShip submodule:

```
git clone --recursive https://github.com/soulsdweeb212/turnbuckle-dive.git
```

In a clone made without `--recursive`, run `git submodule update --init`.

**The game's code.** The build compiles the game's own C from the decompilation, in place. It needs the
decompilation's branch `port-groundwork`, the one with the game's PC code, in the folder `source` next to this
repository's folder (for example `projects/source` and `projects/turnbuckle-dive`). To get it there, run this in the
folder where you ran the clone above:

```
git clone --branch port-groundwork https://github.com/soulsdweeb212/wcw-nwo-revenge.git source
```

Put your ROM in it as `source/baserom.z64`: the decompilation's own build reads it there, and `turnbuckle-dive` and
the tests look for it there when they are given no ROM. For a decompilation in another folder, add
`-DTD_DECOMP_DIR=/path/to/it` to the first `cmake --preset` command. Configure prints whether the build includes the
game's code; without the decompilation, as on CI, it leaves it out. With it, a full build also compiles every game file
of the match (854 files, not linked yet). That part needs Python 3, and the decompilation's own build run once in
`source` (`python3 build.py`, as its README says), which extracts the data files the list is made from; without them
the build leaves only that part out and says why.
[docs/GAME_CODE.md](docs/GAME_CODE.md) explains how the game's code is compiled, tested and stepped through.

Configuring applies the fixes in `cmake/patches/libultraship` to the submodule's files, and `git status` does not list
them. When a pull moves the submodule to another commit, run `git -C lib/libultraship checkout -- .` before
`git submodule update --init`, then configure again.

Install the tools and libraries (Ubuntu 24.04):

```
sudo apt install build-essential gdb cmake ninja-build pkg-config libpng-dev libglew-dev libgl-dev libegl-dev \
    libgles-dev libzip-dev zipcmp zipmerge ziptool nlohmann-json3-dev libtinyxml2-dev libspdlog-dev libx11-dev libxext-dev \
    libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev \
    libdecor-0-dev libpulse-dev libasound2-dev libudev-dev libdbus-1-dev
```

The first configure of each build folder (every preset has its own) downloads SDL3 and the small libraries LibUltraShip
uses, at pinned versions, so it needs internet access and can take several minutes. Configuring again later works
offline.

From a terminal:

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/turnbuckle-dive /path/to/your/rom.z64
./build/debug/hello
```

The presets are `debug`, `relwithdebinfo` and `asan` (AddressSanitizer). The ROM can also be given with the `TD_ROM`
environment variable. With the game's code, `ctest` also runs `rom_loader`, which loads every resource of your ROM with
the game's loader and checks them against a known digest, and `viewer_check`, which checks what the asset viewer draws;
CTest lists them as skipped when it finds no ROM. [docs/TESTING.md](docs/TESTING.md) explains how to test the port at
this stage: where to run each command, what each test and check does, what the programs should show, and what happens
inside a program from its start to its end.

`hello --check` draws at twice the window's resolution, reads the picture back and checks it: the markers drawn at its
corners, a triangle placed only by a force matrix (`gSPForceMatrix`), a sprite built the way the game builds its
sprites (`gSPModifyVertex`) at a depth between two walls, and eight squares of texels. Four are palette indices (CI4
and CI8): each of the two textures is drawn once with its palette read as IA16 (grey levels with their own
transparency, over a red backdrop) and once as RGBA16 (colours), switched the way the game switches the palette type
(`gDPSetTextureLUT`, and once `gDPSetOtherMode`). The other four are tagged IA4 and IA8 (intensity and alpha), the way
the game loads its 2D pictures: each is drawn once with palettes on, when the N64 looks its texels up in the palette,
and once with palettes off. Each shape must fill exactly its box and each square must have the colours its texels and
palette type give; the triangle, the sprite and the squares rely on fixes 0003, 0004, 0005 and 0006 in
`cmake/patches/libultraship`. `hello --frames 600` draws 600 frames and prints how long they took. hello writes
`logs/`, `hello.cfg.json` and `imgui.ini` in the folder it is started from. Under WSL it draws with the GPU through
Direct3D 12 when it can; set `GALLIUM_DRIVER=llvmpipe` to draw on the CPU instead.

**VS Code** (connected to WSL): open the folder and install the suggested C/C++ and CMake Tools extensions. Choose
"Debug Turnbuckle Dive", "Debug hello window" or "Debug asset viewer" in the Run and Debug view; F5 builds that program
and starts it under the debugger (the first time, CMake Tools asks for a configure preset: pick Debug). F7 only builds.
To step through the game's own code, open one of its files with File, Open File, for example the decompilation's
`src/main/func_800037F4.c` (the game's decompressor), click left of a line number to set a breakpoint, and start "Debug
Turnbuckle Dive": it stops there with the game's variables in the Variables view. "Debug Turnbuckle Dive: one resource"
asks for a resource id and loads only that one.

**Visual Studio 2022**: install the "Linux and embedded development with C++" workload, run
`sudo apt install rsync zip` in WSL as well (Visual Studio copies files with them), open the folder, and pick the WSL
target in the toolbar. Visual Studio uses the same presets and debugs with gdb inside WSL.

## Continuous integration

After every push, GitHub builds and tests the project on Linux and on Windows. [docs/CI.md](docs/CI.md) explains how it
works, what each step does, how to run the same steps on your own computer (including a native Windows build), and how
to add to it.

## Licence

The code in this repository is MIT licensed (see [LICENSE](LICENSE)). It builds on LibUltraShip and its Fast3D renderer
(MIT), SDL3 (zlib licence), Dear ImGui (MIT), the other libraries LibUltraShip uses and, later, Torch (MIT), each
licensed by its authors; their notices will ship with the program.
