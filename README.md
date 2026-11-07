# Turnbuckle Dive

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

At this stage the program only loads your ROM, checks that it is the supported one, and prints what it found.

Install the tools (Ubuntu 24.04):

```
sudo apt install build-essential gdb cmake ninja-build
```

From a terminal:

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/turnbuckle-dive /path/to/your/rom.z64
```

The presets are `debug`, `relwithdebinfo` and `asan` (AddressSanitizer). The ROM can also be given with the `TD_ROM`
environment variable.

**VS Code** (connected to WSL): open the folder and install the suggested C/C++ and CMake Tools extensions. F5 builds
the program and starts it under the debugger (the first time, CMake Tools asks for a configure preset: pick Debug); F7
only builds.

**Visual Studio 2022**: install the "Linux and embedded development with C++" workload, open the folder, and pick the
WSL target in the toolbar. Visual Studio uses the same presets and debugs with gdb inside WSL.

## Licence

The code in this repository is MIT licensed (see [LICENSE](LICENSE)). It builds on LibUltraShip, its Fast3D renderer and
Torch, which are MIT licensed by their authors; their notices will ship with the program.
