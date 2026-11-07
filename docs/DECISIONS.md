# Design decisions

Decisions for the port, with the option recommended in the port plan. Each one is settled before the work that needs
it. Dates are when the decision was made. Several recommendations draw on the experience of
[BattleShip](https://github.com/JRickey/BattleShip), an MIT-licensed LibUltraShip port of Super Smash Bros. 64.

| # | Decision | Status | Recommended / chosen |
|---|---|---|---|
| D1 | Pointer size | open | native 64-bit from the start (no 32-bit bring-up build) |
| D2 | Assets for the first playable build | open | the user's ROM read at run time, each resource type converted exactly once at load time; a Torch archive later (P7) through the same loader |
| D3 | How game data becomes C without committing ROM bytes | open | set by D14: (a) a committed typed schema plus a generator that writes the C from the user's ROM at build time, or (c) typed zero-initialised definitions filled from the checked ROM at start-up. Never commit generated game data |
| D4 | Repo layout | **decided 2026-11-06** | a separate repo (this one); the decompilation stays its own repo, and all game C stays there |
| D5 | LibUltraShip version | open | current upstream head (SDL3), pinned as a submodule and upgraded only on purpose, with a short listed set of local patches |
| D6 | Licence for the port code | **decided 2026-11-06** | MIT |
| D7 | First platforms | open | Linux x86-64, then Windows x64, then macOS |
| D8 | Save format | open | emulator-style raw images (SRAM, controller pak `.mpk`), kept in N64 byte order |
| D9 | Audio microcode replacement | open | keep the game's own audio command lists in a 32-bit arena and interpret them with our own CPU mixer: common commands from Starship's CC0 mixer, the rest written from measurements; never GPL code |
| D10 | Extras (widescreen, 60 fps, HD textures) | open | after P7; keep the original 30 fps logic first |
| D11 | Windows compiler | open | clang-cl |
| D12 | Storage of the two overlays' data on PC | open | a generated per-overlay reset that re-initialises the overlay's globals (portable) |
| D13 | Reference emulator for comparisons and traces | open | two tiers: (A) a scriptable emulator for traces, chosen first; (B) an accurate LLE reference, built outside this repo |
| D14 | Distribution model | open | (a) source only: users build with their own ROM; or (b) prebuilt binaries that contain no ROM bytes, which needs D3(c). Decided together with D3 |
