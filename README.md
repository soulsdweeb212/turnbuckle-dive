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

There are two programs so far:

- `turnbuckle-dive` loads your ROM, checks that it is the supported one, and prints what it found.
- `hello` opens a LibUltraShip window and draws a test picture made of N64 display-list commands, at the game's
  480x240 resolution shown at 4:3.

Get the code together with its LibUltraShip submodule:

```
git clone --recursive https://github.com/soulsdweeb212/turnbuckle-dive.git
```

In a clone made without `--recursive`, run `git submodule update --init`.

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
environment variable.

`hello --check` draws at twice the window's resolution, reads the picture back and checks it: the markers drawn at its
corners, a triangle placed only by a force matrix (`gSPForceMatrix`), and a sprite built the way the game builds its
sprites (`gSPModifyVertex`) at a depth between two walls. Each shape must fill exactly its box; the triangle and the
sprite rely on fixes 0003 and 0004 in `cmake/patches/libultraship`. `hello --frames 600` draws 600 frames and prints
how long they took. hello writes `logs/`, `hello.cfg.json` and `imgui.ini` in the folder it is started from. Under WSL
it draws with the GPU through Direct3D 12 when it can; set `GALLIUM_DRIVER=llvmpipe` to draw on the CPU instead.

**VS Code** (connected to WSL): open the folder and install the suggested C/C++ and CMake Tools extensions. Choose
"Debug Turnbuckle Dive" or "Debug hello window" in the Run and Debug view; F5 builds that program and starts it under
the debugger (the first time, CMake Tools asks for a configure preset: pick Debug). F7 only builds.

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
