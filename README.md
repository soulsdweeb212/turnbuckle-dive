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

## Licence

The code in this repository is MIT licensed (see [LICENSE](LICENSE)). It builds on LibUltraShip, its Fast3D renderer and
Torch, which are MIT licensed by their authors; their notices will ship with the program.
