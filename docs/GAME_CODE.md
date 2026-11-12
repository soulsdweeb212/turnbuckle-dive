# Game code

Turnbuckle Dive runs the game's own code: the C of the WCW/nWo Revenge decompilation, compiled for the PC. This page
explains where that C comes from, how it is compiled, what the port provides to it, how it is tested, and how to step
through it in a debugger.

So far the game code in the port is the game's ROM loader, the functions that read resources (textures, models,
animations and the rest of the game's data) out of the ROM and unpack the compressed ones, and the functions that turn
a model, an effect sprite or a picture into display lists, which the asset viewer draws ([VIEWER.md](VIEWER.md)).
Besides those, the build compiles every game file of the match, without linking them yet ("Every file of the developer
program", below).

## Where the game's C comes from

The game's C is in the decompilation, a separate repository whose files also build the original N64 ROM, byte for
byte. This repository keeps no copy of them: CMake compiles them in place, from the decompilation's folder, so a change
made there is in the next build here.

The PC code that the game's C needs is on the decompilation's branch `port-groundwork`, which starts it with the file
`include/port.h`; the decompilation's other branches do not have it. The decompilation's folder is the CMake setting
`TD_DECOMP_DIR`. By default it is the folder `source` next to this repository's folder:

```
projects/
  source/            the decompilation (the build uses its include/ and src/ folders, and its baserom.z64)
  turnbuckle-dive/   this repository
```

To get it there, from the folder that holds `turnbuckle-dive` (`projects` above):

```
git clone --branch port-groundwork https://github.com/soulsdweeb212/wcw-nwo-revenge.git source
```

A decompilation that is already there, on another branch, switches with `git checkout port-groundwork`, run in its
folder. Your ROM goes in that folder as `baserom.z64`: the decompilation's own build reads it there, and
`turnbuckle-dive` and the tests look for it there when they are given no ROM.

For a decompilation somewhere else, give its folder the first time a build folder is configured, from this
repository's folder:

```
cmake --preset debug -DTD_DECOMP_DIR=/path/to/the/decompilation
```

The build folder remembers it, so later configures and builds use it too, and the program and the tests then look for
the ROM as `baserom.z64` in that folder.

Configure prints one line that says whether the build includes game code, for example:

```
-- Game code: compiled from /home/you/projects/source
-- Game code: not built (no decompilation in /home/you/projects/source; set TD_DECOMP_DIR to its folder)
-- Game code: not built (/home/you/projects/source has no include/port.h, the start of the game's PC code: check out the decompilation's port-groundwork branch)
-- Game code: not built (the game's C needs GCC or Clang, and this compiler takes MSVC's options)
-- Game code: not built (/home/you/projects/source has no port/game_vars.tsv, the map of the game's variables: update the decompilation's port-groundwork branch)
-- Game code: not built (no Python 3, which writes the C of the game's variables)
```

A build without game code still works: `turnbuckle-dive` only checks the ROM, and the tests that need game code are
left out. CI builds this way, because it has no copy of the decompilation. So does Visual Studio's compiler, because
the game's C uses options and extensions of GCC and Clang.

## How it is compiled

The game's C was written for the N64's compiler, KMC GCC 2.7.2. The function `td_game_code` in `CMakeLists.txt`
gives every game file the same settings, each with a comment that says why. In short:

- `TARGET_PC` selects the PC version of the code. Where the PC needs different code, a decompilation file has it in an
  `#ifdef TARGET_PC` block, and the N64 code stays as it was in the `#else` branch. The decompilation's
  `include/port.h` and `docs/port.md` explain how the two fit together.
- `-std=gnu17`, `-fno-strict-aliasing`, `-fwrapv`, `-fsigned-char` and `-ffp-contract=off` make GCC and Clang treat the
  code the way the N64's compiler did, so that it computes the same results.
- The `-Werror=...` options stop the build on code that would cut a 64-bit pointer down to the N64's 32 bits.
- `-ftrivial-auto-var-init=zero` starts every local variable as zero, so a function that reads one before it sets it
  gives the same result on every run ("The same match every time", below).
- In a Debug build, `-g3` puts the game's macros into the debug information, so a debugger knows names such as
  `WR_FLAG_LEGAL`. In the asan preset, two of UndefinedBehaviorSanitizer's checks are off for game files
  ("The developer match", below, and `td_game_code` say why).

Game files do not get the warnings of `td_strict`, which the port's own code is compiled with: they are the
decompilation's code, written to match the N64 ROM, and the `-Werror=...` options pick out the problems that matter on
a 64-bit PC. A Debug build compiles game files with debug information and no optimisation, like the port's own code,
so a debugger can step through them line by line.

The game files are in the library `td_game`. Every file in it goes into each program that uses game code, even a file
that nothing calls yet (CMake links `td_game` with `WHOLE_ARCHIVE`), so the link names everything that a game file
needs and the build does not have. Today the files are the ROM loader, from the decompilation's `src/main`:

| File | What it does |
|---|---|
| `initializeAssetSizeTable.c` | At boot, reads the resource table from ROM and works out each resource's size. |
| `lookupResourceDescriptor.c` | Gives a resource's place in ROM, its size once loaded, and whether it is compressed. |
| `func_80003684.c` | Loads one resource into memory: unpacks it if it is compressed, else copies it. |
| `func_800037F4.c` | The game's LZSS decompressor, which unpacks a compressed resource. |
| `readRomSynchronousChunks.c` | Reads bytes from ROM: a DMA transfer on the N64, a copy from the ROM in memory here. |

and the functions that build display lists for a model, an effect sprite or a picture, from `src/main` and, the last
two, from overlay 1 (`src/code_80090000`):

| File | What it does |
|---|---|
| `func_8000486C.c` | Turns a mesh's 8-byte vertices into the 16-byte vertices (`Vtx`) the N64 draws. |
| `buildVertexDisplayList.c` | Writes a mesh's triangle list. |
| `buildObjectDisplayListTail.c` | Writes the material list of a mesh without a texture. |
| `buildCiTextureDisplayList.c` | Writes the material list of a mesh with a texture, which loads its texels. |
| `func_80004E08.c` | Loads a texture and its palette into a display record. |
| `convertRgba16ToGrayscale.c` | Turns a palette into grey levels, when the game's grayscale switch is on. |
| `func_8000670C.c` | Loads an effect sprite's texture and palette. |
| `func_80007920.c` | Fills in one vertex (`Vtx`), such as the corners of the effect sprites' square. |
| `buildCi4TextureDisplayList.c` | Writes an effect sprite's render state and the commands that load its texels. |
| `func_80096A6C.c` | Writes the commands that load one tile of a picture. |
| `func_80096D8C.c` | Draws a picture as tiles, each a texture rectangle. |

and the game's two heap functions, `allocateHeap.c` and `releaseHeapBlock.c` from `src/main`, which on PC hand the work
to the port (`port_heap_alloc`, below).

These write 16-byte commands that hold whole pointers, and the record they fill has a 64-bit layout: the
decompilation's `docs/port.md` explains how.

Fast3D, LibUltraShip's renderer, keeps every texture it has decoded, filed under the addresses of its texels and
palette. The game loads new textures and palettes into buffers it already has (`func_80004E08` and `func_8000670C`
keep a buffer when the new resource fits in it) and changes palettes where they are (`convertRgba16ToGrayscale`).
So each cached texture also keeps a fingerprint of the bytes it was decoded from, and Fast3D decodes it again when
they have changed (`cmake/patches/libultraship/0009-…`); the port does not have to tell it. The viewer still drops
the textures after freeing a subject.

## What the port provides to the game

The game's C calls a few functions that the port provides. The decompilation's `include/port.h` declares them for the
game's side; this repository's `src/game.h` declares the same functions for the port's side.

- `port_rom_read` (`src/rom.c`) copies bytes of the user's ROM, which the program has checked and keeps in memory. A
  read that does not lie wholly inside the ROM stops the program with a message.
- `port_fatal` (`src/port.c`) prints a message and stops the program, where the N64 would hang or crash.
- `port_res_loaded` (`src/port.c`) is called by the game's loader each time a whole resource is in memory.
- `port_ptr_to_slot` and `port_slot_to_ptr` (`src/slots.c`), which port.h's `PTR_TO_SLOT` and `SLOT_TO_PTR` call, are
  the handle table. Some of the game's records keep their N64 layout on the PC, because many game files reach their
  fields at N64 offsets, so each address they hold has only 4 bytes, too few for a 64-bit pointer. Such a field holds
  a 32-bit handle instead, a number that the table turns back into the address (the decompilation's `docs/port.md`
  says which fields). The table's rules:
  - The same address always gets the same handle, for the whole run, and a handle is never given back. When the game
    frees a heap block and later gets the same address again, the old handle names the new block, as the N64's old
    address would.
  - `NULL` is handle 0, and handle 0 is `NULL`.
  - `port_slot_to_ptr` of a number the table never gave out stops the program, so a handle field that holds a raw
    value or a cut pointer stops there instead of being followed.
  - The table holds 2^20 handles (`PORT_SLOT_CAP` in `src/game.h`), 0 included; one more stops the program. A match
    is expected to use about 1400. `port_slot_count()` says how many it has given out, for the tests and for checks.
  - The game's main thread and its render thread both use it. Giving out a handle takes a lock; reading one does not,
    because the array of addresses never moves and each entry is written before its handle is given out. A handle that
    one thread stores in a plain field of a game record and another reads there can arrive before its entry can be
    seen (on ARM64 processors), so a reader that finds an empty entry takes the lock and looks again before it stops
    the program.

- `port_heap_alloc` and `port_heap_free` (`src/game_heap.c`) are the game's heap on PC: `allocateHeap` and
  `releaseHeapBlock` call them. The game's own heap cannot run on a 64-bit PC, because its block header grows from 16
  to 24 bytes while its code counts 16-byte units; the file explains it. A block is allocated with `calloc` at exactly
  the size asked for, so it starts as zeros, as the N64's heap does, and AddressSanitizer catches a write one byte
  past it.

The N64 SDK functions that the developer program's files call are in `src/game_sdk.c` (its comment explains each
group). The game runs on one thread, its own: the N64's render thread, audio thread and scheduler do not run. The
game's three frame hand-offs (`func_80002BC0`, `func_80002C0C`, `func_80002C74`), which on the N64 send a message to
the render thread and wait for its answer, call `port_present` instead (the decompilation's `port.h`). The "hardware"
finishes at once: a cartridge read (`osEPiStartDma`) copies the bytes from the ROM and posts its message before it
returns, and a controller read does the same, so a game function that waits for such a message always finds it.
Message queues keep the N64's 0x18 bytes and 4-byte messages; a wait on an empty queue that nothing could fill stops
the program with a message instead of hanging. Controllers read what the program running the game gives through
`game_sdk_set` (`src/game_sdk.h`). Sound is not ported yet: the music library's functions do nothing and report that
nothing plays.

`src/game_check.c` keeps the two headers in step: it is compiled with the game's settings and includes both
`include/port.h` and `src/game.h`, so the build stops with an error if they ever declare a function or the resource
descriptor differently. `td_game` depends on it, so this also holds when only one program is built.

`src/resources.c` is the port's code that calls the game's loader, for the program and the tests. `res_init` first
fills the game's variables of main and of overlay 1's data from the ROM (next section).

## The game's variables

The game's files read and write about 1,250 variables of the N64's data and BSS sections (`D_8007E228`, the wrestlers
`D_800F9D10`, the tables of overlay 2, ...). The port defines all of them, and fills those the N64 starts from the ROM
from your ROM when the game starts, so the repository holds none of the game's bytes.

**The map.** The decompilation's `port/game_vars.tsv` lists every variable with its N64 address, its N64 section, its
storage on PC, its size and its byte order. Its tool `tools/port_data.py` writes it from the game files' own types (the
decompilation's `docs/port.md`, "The game's variables", says how), and the file is committed there.

**The C.** When CMake configures, `tools/game_vars.py` turns the map into `game_vars.c` in the build folder (CMake
runs it again when the map or the tool changes). `td_game_vars` compiles it with `src/game_vars.c`, which fills the
variables. Storage on PC is of two kinds:

- *Block.* Each N64 section is one array of PC memory (`td_main_data`, `td_main_bss`, `td_ovl1_data`, `td_ovl2_data`,
  `td_ovl2_bss`), and each variable that holds no address is a name for its N64 offset in that array. Such a variable's
  type is laid out the same on both machines, so its neighbours are where they were on the N64: a record read through a
  label inside it (`D_8007E280` is `D_8007E228` + 0x58), arrays read as one, and the buffer that `func_80001260` finds
  by rounding `D_8007FC4F`'s address all work as on the N64. C cannot name the middle of an array, so `game_vars.c`
  sets these names in assembly (`.set D_8007E280, td_main_bss + 0x...`).
- *Own.* A variable whose type holds an address is bigger on PC, so it gets its own array, as many bytes as its PC type.
  Code reaches it only by its own name (the decompilation's `docs/port.md`, "Shared records on PC").

One variable with storage of its own gets zeroed bytes before it (`tools/game_vars.py`, `GUARD_BEFORE`): the
wrestlers, `D_800F9D10`. The game uses -1 for "no wrestler" and reads fields of `D_800F9D10[-1]` where it then does
not use them; on PC those reads find one record of zeros.

A few names the port provides itself (`tools/game_vars.py`, `PORT`): `osTvType` (1, NTSC), the three framebuffers, and
the address of the RSP's boot code. Some game files still call functions by an old name, which the N64 link keeps as
a second name of the function (`func_800E2400` is `tryBeginBlockReaction`). Only the linker can give a function a second
name, so `game_vars.py` also writes `game_vars_link.txt`, the linker options (`--defsym`) that do it, which CMake adds
to the programs that link every game file.

**Filling.** `game_vars_load(overlay)` (`src/game_vars.h`) starts the variables of main (0) or of overlay 1 or 2 as the
N64 does when it loads that code: it copies each of its data sections from the ROM, then turns each 2-, 4- or 8-byte
value into the PC's byte order by reversing its bytes, as the map's swap patterns say; its BSS and own storage go back
to zeros. Loading an overlay again starts its variables afresh, as on the N64.

**Address tables.** 24 variables that hold addresses are started by the N64 from the ROM: the tables of handlers
`D_800E2FF0` and `D_800E3660`, the wrestler name tables, the tables of animation data and others. Many of them hold
addresses of further tables, down to three levels. The map describes each of them, and each array or record their
addresses reach (357 in all), as a list of fields: where each field is in the N64 element and in the PC element, and
whether it is a value, an address or a function. After the sections, `game_vars_load` builds each table in its PC
layout from its N64 bytes in the ROM: a value is turned into the PC's byte order, and an address into the PC address
of what it names (`game_vars_address`): another table, a variable the port provides (the framebuffers), or the place
in a section's block. The data that the addresses point at is turned into the PC's byte order like the variables
(the map's "data" lines). A function address becomes the PC function (`game_vars_function`). The functions the tables
hold are listed in `game_functions.c`, which `tools/game_vars.py` also writes; it names game functions, so it is the
library `td_game_functions`, which only programs that link every game file use, and such a program hands the list
over with `game_vars_use_functions` before it loads overlay 2. The viewer and `rom_loader` never load overlay 2.

**The check.** The test `game_vars` (`tests/game_vars/main.c`) is linked from every game file of the developer program,
the variables and the SDK stand-ins, so it builds only when no game file uses a name the port lacks. It then fills the
variables and compares each section with the ROM: each value read as a PC integer must equal the ROM's bytes read
big-endian, and every other byte must equal the ROM's. Then every address and function field of every address table:
each must hold what its N64 word names, found by a plain search of its own, so each entry of the handler tables is the
function the N64 calls there. It also checks that loading overlay 2 again starts it afresh.
`game_vars_detects_change` runs it with one value left in the N64's byte order, and passes only if the check fails,
which shows that the comparison really looks at the values. Both need the ROM and are skipped without it.

## Resources in the PC's byte order

Resources come from the ROM in the N64's byte order, most significant byte first; a PC puts the least significant
byte first. Where the game's code reads a resource's 16-, 32- or 64-bit values as plain integers, they must be in the
PC's order, so `port_res_loaded` (`src/port.c`), which the game's loader calls once for each resource it loads, turns
them around (`res_convert`, `src/res_convert.c`): each value of the kinds of resource that need it, once, as the
resource arrives. Meshes, textures and palettes stay in the N64's order, because the game's display-list code and
LibUltraShip's renderer read them so.

So far that is the three resources of each character, which sit in the resource table as triples from id 0x13F to
0x20E: its profile (64 bytes: bytes, then five 16-bit animation ids, five 16-bit character ids and a 64-bit set of
ability bits), its move table (32-bit words) and its animation list (16-bit ids); and resource 0x126, the move table
every wrestler shares (32-bit words). `src/res_convert.c` says where each layout was read from. `tools/res_digest.py` makes the same conversion, so the `rom_loader` test checks it.

## The developer match

`turnbuckle-dive --dev-match` starts the game straight into a match, without the menus (`src/dev/dev_match.c`). It does
what the N64 does before a match, in the same order: the game's variables of main start from the ROM and overlay 2's
are loaded; the four calls that open the boot loop (graphics, audio, controllers, the table of resource sizes);
`func_8002B750`, which builds the wrestlers' name table; the settings the menus write for an exhibition match (Sting
against Hollywood Hogan, both played by the CPU, arena 0, a 5-minute limit, run-ins off); then overlay 2's entry,
`func_80090000_ovl2`, which runs the match and returns when it ends. When the results screen waits for a button, it
presses A on controller 1. It needs the build with every game file of the developer program (`td_game_all`, below).

```
./build/debug/turnbuckle-dive --dev-match
```

### The picture

The window shows the match as the game draws it. On the N64 a render thread builds each frame's display lists and
gives them to the graphics chip; on PC nothing runs beside the game, so at each frame's hand-off (`port_present`, in
`src/game_sdk.c`) the port calls `port_render_frame`, the PC version of that thread's loop (in the decompilation's
`func_80002CC4.c`). It builds the frame's lists with the game's own functions, and the game sends each finished list
to `port_gfx_submit`, which copies its commands into one list for the whole frame. The window then has Fast3D,
LibUltraShip's renderer, draw that list (`src/dev/inspector.cpp`). `--dump-frame` writes one frame's list to a file
([TESTING.md](TESTING.md), "Seeing the drawing commands").

Fast3D decodes each texture once and keeps it; a texture whose bytes the game changes is decoded again (see "How it
is compiled" above, and LibUltraShip patch 0009). While the match is paused, the window draws the same
frame's list again at each of its own frames.

### The Match Inspector

The Match Inspector (`src/dev/inspector.cpp`) is a window over the picture, hidden at first; F5 shows and hides it. It
has a table of the four wrestlers (character, state and its name, step, animation and frame, position, heading,
health, spirit, who holds whom, opponent) and a map of the ring from above, with each wrestler as a dot and a line
where it faces. Above them are the frame, the match frame (`D_800E2FAC`), the phase (setup, intro, fight, ending,
results) and the random seed. The match runs at the N64's speed, 30 frames a second.

Beside the map are the draw groups, one switch each. The game files each object of the scene under a group, and
hiding a group shows what it holds: 0 the arena (crowd, stage, signs), 3 the ring posts and turnbuckles, 4 the
wrestlers and ropes, 5 the HUD, 6 the ring mat. A switch takes effect from the next frame (F7 shows it while paused),
and changes only the picture: while the port builds a frame, the objects of a hidden group have their hide bit set,
and right after it their group is put back, so the game's own code never sees it (the wrestler log stays the same).
Under "Draw order (depth sort)" is the list of the scene's work blocks in the order the game sorts them, farthest
first, with each one's group, kind (a mesh or a display list, 2D or not), sort key and distance. The match draws
without a depth buffer, so that order alone decides which object covers which.

- F6 pauses and resumes the match, F7 runs one frame and pauses, F8 slows it down (each frame shown 1, 2, 4 or 8 times).
  The same buttons are in the window, and the console (F1, Debug, Console) takes `pause`, `step 10` and `slowmo 4`.
- F1 shows the menu bar, whose Debug menu opens the Match Inspector and LibUltraShip's Console, Stats and Gfx Debugger.
  The Gfx Debugger's Debug button holds the match on its current frame and lists the frame's commands as a tree;
  clicking one draws the frame only up to it. Resume Game lets the match go on.
- Closing the window ends the program.

The inspector only reads the game's records, through `src/dev/dev_records.c`, which is compiled with the game's
settings. It runs inside the game's frame hand-off, on the game's thread, while the game waits, so the two never clash.

### Playing the match

`--players 1` puts Sting on controller 1, against the CPU's Hogan:

```
./build/debug/turnbuckle-dive --dev-match --players 1
```

The controllers come from LibUltraShip's control deck, read once a frame just before the game reads its pads.
Controller 1 starts with Turnbuckle Dive's own defaults (`src/context.cpp`), which take the place of LibUltraShip's:

| N64 | Keyboard | Gamepad (Xbox names) |
|---|---|---|
| stick | arrow keys | right stick |
| d-pad | W A S D (up, left, down, right) | left stick, and the d-pad |
| A, B | H, Y | A, X |
| C up, down, left, right | I, K, J, L | Y, B, LB, RB |
| L, R, Z | U, O, Z | LT, RT, View |
| Start | space bar | Menu |

LibUltraShip uses the defaults while the settings file (`turnbuckle-dive.cfg.json` in the build folder) holds no
mapping of its own, and when its input editor resets a controller; F1, Debug, Input Editor changes the mapping, and
the settings file then keeps the change.

The match can also be played without anyone at the keyboard, and the same way every time:

- `--record-pads FILE` saves every pad read, in the order the game makes them, with the frame of each.
  `--replay-pads FILE` gives the game those reads again, so the match plays the same, frame for frame.
- `--pad-script FILE` presses buttons at given frames: lines `FRAME BUTTONS [FRAMES]`, the buttons joined with `+`
  (A, B, Z, START, L, R, DUP, DDOWN, DLEFT, DRIGHT, CUP, CDOWN, CLEFT, CRIGHT), held for FRAMES frames (1 if left out),
  the frames counted from the match's first frame hand-off. `tests/pads/restart.txt` pauses and chooses the pause
  menu's restart.
- `--pad-bot SEED` moves the stick and presses buttons from a seeded random sequence (never Start). A script or the
  bot also gets the A that the results screen waits for, as a CPU-against-CPU match does.
- `--time-limit MINUTES` sets the limit: 5 by default, 10, 15, 30 or 60, or 0 for none, when the match ends only by a
  fall, a submission, a count-out and the like. When it is over, the developer match prints how the wrestlers stand
  (for a pinfall, one of them "pinned").
- `--matches N` plays N matches one after the other. Before each match after the first, overlay 2 is loaded afresh,
  as the N64's boot loop does, the settings are written again (a match writes the end health into them), and the
  random seed and the pads start again from the first match's start. Match n's wrestler log is the `--wr-log` file
  with `.n` after it. Each later match plays as the first, but for the joints in its first entry: the game copies
  them from the scene's objects, which keep the last match's poses, on the N64 too (`tools/wr_log.py diff A B
  --from 1` leaves that entry out).

The pause menu (Start) can restart the match: overlay 2 then runs the match again without being loaded afresh, as on
the N64, and the developer match says so ("the match started again"). The match frame `D_800E2FAC` counts on through
a restart.

### Options

| Option | What it does |
|---|---|
| `--headless` | no window: the match runs as fast as it can and prints a line every 300 frames (the tests use it) |
| `--wr-log FILE` | writes the wrestler log: the four wrestler records at every frame |
| `--res-log FILE` | writes each resource the game loads, with the frame |
| `--seed N` | starts the random seed (`xseed`) at N instead of the ROM's value: a different match |
| `--screenshot FILE` | saves the window at frame 300 (`--screenshot-frame N` for another) as a PNG; OpenGL only |
| `--screenshot-every N` | with `--screenshot PREFIX`: saves the window every N frames, as PREFIX-NNNNN.png |
| `--hide-group N` | does not draw draw group N (as the inspector's switches); may be given more than once |
| `--dump-frame N FILE` | writes every drawing command of frame N to FILE |
| `--players 1` | Sting is played on controller 1 ("Playing the match"); 0, the default, is CPU against CPU |
| `--record-pads FILE`, `--replay-pads FILE` | save every pad read, or give the game the reads of such a file |
| `--pad-script FILE`, `--pad-bot SEED` | controller 1 from a script of presses, or from the pad bot |
| `--time-limit MINUTES` | 5 by default; 10, 15, 30, 60, or 0 for none |
| `--matches N` | plays N matches one after the other, each as the first |
| `--no-idle-randoms` | draws no random numbers in place of the N64's idle thread ("Comparing with the original game") |
| `--capture-match-frames N,N,... DIR` | at each of those match frames, saves the window as DIR/port_NNNNN.png and the display list as DIR/port_NNNNN.txt |
| `--no-watchdog` | no watchdog (the VS Code entries use it, so that stepping in the debugger is not taken for a hang) |

The logs and screenshots are made from your ROM: keep them in the build folder, never in the repository.

### The checks

The developer match checks the G6 gate as it runs, and ends with `dev-match: passed after N frames` or with a `FAIL:`
line that says which check failed (exit code 1):

- the match ends: the phase (`D_800EB465`) reaches 4, and never goes past 4 (two end checks in one frame would skip
  phase 3, three push it past 4, where the match never ends);
- it ends within 20000 frames (a 5-minute match takes about 9100);
- its first loads are resources 0x126 to 0x12A, in that order (`resetMatchState`);
- a watchdog thread stops the program when no frame arrives for 3 seconds, which is a hang inside one frame.

The tests `dev_match` to `dev_match_detects_change` run it ([TESTING.md](TESTING.md)).

### The same match every time

A run with the same seed gives the same match, frame for frame, in a Debug build and in the asan preset, and wherever
the system puts the program in memory. Two things make it so:

- On the N64 an idle thread calls `guRandom` whenever the CPU has nothing to do, so the random seed moves on by however
  much idle time each frame leaves. On PC no thread idles; `port_present` (`src/game_sdk.c`) draws one random number at
  each frame and each wait for the retrace instead (`g_idle_randoms`).
- A few game functions read a local variable before they set it, where the N64 reads whatever its stack held. Game
  files are compiled with `-ftrivial-auto-var-init=zero` (`td_game_code`), so such a variable starts as zero on every
  run.

`tools/wr_log.py` reads the wrestler log: `show FILE` prints each wrestler's state, health and position every 300 frames,
and `diff A B` names the first frame, wrestler and field where two logs differ, or says they are the same:

```
./build/debug/turnbuckle-dive --dev-match --headless --wr-log build/debug/a.wrlog
./build/debug/turnbuckle-dive --dev-match --headless --wr-log build/debug/b.wrlog
python3 tools/wr_log.py diff build/debug/a.wrlog build/debug/b.wrlog
```

Other seeds play other matches, which reach other code: running many of them in the asan preset is a quick way to
find bugs (`--seed 1`, `--seed 2` and so on).

### Debugging the match

- In VS Code, Run and Debug: "Debug Turnbuckle Dive: dev match" starts the match with the window under the debugger.
  "Debug Turnbuckle Dive: dev match (asan preset)" does the same in the asan preset's build (choose that preset in
  CMake Tools first): AddressSanitizer and UndefinedBehaviorSanitizer then stop the program at the bad line; it is
  about 11 frames down the call stack, under the C library's and the sanitizers' own.
- In Watch, `D_800F9D10[0]` shows a wrestler as one line (state name, health, position, who holds it) with its fields
  below: every entry loads `tools/gdb/wrestler.py`. `(enum TdWrState)D_800F9D10[1].state` and
  `(enum TdMatchPhase)D_800EB465` show names (`src/dev/wr_states.h`). In a Debug build the game's macros work too, in a
  game file: `D_800F9D10[0].flags & WR_FLAG_LEGAL`.
- Inside the match, `D_800E2FAC` counts the match's frames and `(int)D_800EB465` is the phase; a breakpoint with a
  condition such as `D_800F9D10[1].health < 100` stops where it happens.
- In gdb in a terminal, `source tools/gdb/wrestler.py` gives the same one-line records.

### Comparing with the original game

The developer match can be checked against the original game, run in the Project64 emulator on Windows. The emulator
is only a measuring tool: it never becomes part of Turnbuckle Dive, and its script stays out of both repositories, in
the decompilation's folder at `notes/gameplay_route/work/reference/out/pj64/Scripts/td_lockstep.js`.

The two can play the very same match because of one change on each side. On the N64, an idle thread calls the random
number generator (`guRandom`) whenever nothing else runs, so the random seed moves on by however much idle time each
frame leaves, which no two machines share. The script turns that one call (at `0x80000560`) into a no-op, and the port
then runs with `--no-idle-randoms`, which drops its stand-in for it (`g_idle_randoms`, above). Both then draw a random
number only where the game asks for one, the same numbers in the same order, unless the port's code does something
the original does not, which is what the comparison looks for.

The script also starts the match the way the developer match does: when the boot loop is about to run overlay 1 (the
menus), it makes overlay 1's entry jump to `func_8002B750`, which builds the name table, and before overlay 2 runs it
writes the same settings as `write_settings`. Then, at every frame hand-off, it saves the match frame, the seed and
the four wrestler records, and for a list of match frames the display lists and the picture.

1. In Project64 (a 4.0 development build): the debugger on, the interpreter core, "Randomize SI/PI interrupts" off,
   and GLideN64's `frameBufferEmulation\copyToRDRAM=1` in its `GLideN64.ini`, so that each frame's picture is copied
   back to the N64's memory whole. Copy the script into Project64's `Scripts` folder and make it run at start
   (`Autorun Scripts=td_lockstep.js` under `[Debugger]` in `Config/Project64.cfg`), then open the ROM. The script
   writes into `C:/n64/out/lockstep/` and, a little after the match ends, writes `done.txt` and pauses the emulator.
   Take the autorun line out again afterwards: with it, the game always skips its menus. A `config.json` in that
   folder sets the match, `{"players": 1, "timeLimit": 0, "pads": "FILE", "capture": [30, 300]}`: Sting on
   controller 1, the time limit (an index of the game's table: 0 none, 1 five minutes), a pad file of the port's
   `--record-pads` in the same folder, whose controller 1 the script writes into the game's pad buffer at each read
   (just after `osContGetReadData`, at `0x80003F08`), and the match frames to capture. Without it, the match is CPU
   against CPU, with the default capture.
2. The port, the same match, its records and the same frames:

   ```
   ./build/debug/turnbuckle-dive --dev-match --headless --no-idle-randoms --wr-log build/debug/port.wrlog
   ./build/debug/turnbuckle-dive --dev-match --headless --no-idle-randoms --players 1 --pad-bot 4 --time-limit 0 --record-pads build/debug/bot4.pads --wr-log build/debug/port.wrlog
   ./build/debug/turnbuckle-dive --dev-match --no-idle-randoms --capture-match-frames 29,30,31,299,300,301 build/debug/lists
   ./build/debug/turnbuckle-dive --dev-match --no-idle-randoms --hide-group 5 --capture-match-frames 29,30,31,299,300,301 build/debug/pictures
   ```

   Each match frame is taken with its neighbours, because Project64 names a frame by the hand-off before its drawing.
   GLideN64 leaves the HUD out of the picture it copies back, so the pictures come from a run without it.
3. `tools/n64_compare.py` compares them:

   ```
   python3 tools/n64_compare.py records /mnt/c/n64/out/lockstep/lockstep.bin build/debug/port.wrlog
   python3 tools/n64_compare.py frames /mnt/c/n64/out/lockstep build/debug/lists --pictures build/debug/pictures
   ```

   `records` compares every field of every wrestler, and the seed, at every frame, and prints `the same: N frames` or
   the first frame that differs with each field and its two values. `frames` compares each frame's display lists
   command by command (leaving out the words that hold addresses) and its picture (the port's window cut to the 4:3
   picture and averaged down to the N64's 480x240): it passes when the lists agree, but for the clear of the next
   framebuffer that the port leaves out on purpose, and 95% of the pixels are within 32 of 255 in every channel.
   `--out DIR` also writes the two pictures and their difference.

## Every file of the developer program: td_game_all

The developer program is the PC program that will start straight into a match, without the menus. Its game files are
the ones the match reaches, which the decompilation's tool `tools/port_filelist.py` works out by following the game's
calls from its starting functions (the decompilation's `docs/port.md` says how). Today they are 857 files.

The target `td_game_all` compiles every one of them, with the same settings as `td_game`. `turnbuckle-dive` links them
all, with the game's variables and the SDK functions, for the developer match (above), and so does the test program
`game_vars_check`; so a build stops on a game file that no longer compiles or uses a name the port lacks. Without
`td_game_all` (no Python 3, or the decompilation's build not run), `turnbuckle-dive` links only the ROM loader's files
(`td_game`) and has no `--dev-match`.

Configure runs `port_filelist.py` and prints how many files it found:

```
-- Game code: td_game_all compiles the developer program's 857 files
```

The tool writes two text files into the build folder: the list (`game_match_files.txt`), and the paths of every file it
read to make it (`game_match_depends.txt`). When one of those files changes, CMake configures again, so the list
follows the decompilation.

The tool needs the data files that the decompilation's own build extracts from your ROM on its first run
(its `asm/data` and `assets` folders). So, once, in the decompilation's folder, run its build as its README says
(`python3 build.py`, which needs decompdecomp). The tool also stops when the decompilation's list is not complete: a
game file calls a name that no file defines, or the decompilation's `build/game.ld` no longer matches its `build.json`
(after a pull, until its build runs again). In each of these cases configure prints a warning that quotes the tool's
message (a "README" in it means the decompilation's), and the build goes on without `td_game_all`. Once the cause is
fixed, the next build configures again by itself and `td_game_all` is back.

Every record of these files that holds addresses has its PC layout, which the files follow (the decompilation's
`docs/port.md`, "Shared records on PC", lists them). A few functions stop the program with a message where the N64
would go wrong on bad input (`port_fatal`).

## Adding a game file

1. In `CMakeLists.txt`, add the file's path to `add_library(td_game ...)`, written like the others:
   `${TD_DECOMP}/src/main/...`.
2. Build. The link names every function or variable that the new file uses and the build does not have yet, even
   while nothing calls the new file. Add the game file that defines each one the same way. When that file cannot run
   on the PC yet, write a stand-in in `src/` instead, like those in `src/game_sdk.c`, with a comment that says why it
   is needed and what replaces it. A game variable the link lacks is missing from the decompilation's
   `port/game_vars.tsv`: run its `tools/port_data.py` (its `docs/port.md` says how).
3. If port code calls a function of the new file, declare the function in `src/game.h`.
4. A game file that needs PC code gets it in the decompilation, in an `#ifdef TARGET_PC` block. After such a change,
   the decompilation must still build the original ROM: in its folder, `python3 build.py` must end with "identical"
   and `python3 tools/check_rodata.py` must report 0 not found (its `docs/port.md` explains both).

## The ROM loader test

The test `rom_loader` (`tests/rom_loader/main.c`) runs the game's loader on your ROM. It is built only with game code.

1. It loads all 7657 resources with the game's own functions and compares them with one known digest: a SHA-1 over
   every resource's place in the ROM, its size and its bytes. The digest is a fingerprint, like the ROM's SHA-1 in
   `src/rom.h`: the repository holds no game data.
2. It makes copies of the ROM in the `.v64` and `.n64` byte orders and checks that they give the same digest, then a
   copy with one byte of a resource changed, which the ROM check must refuse; it prints that copy's SHA-1 and the
   supported one, as the program does when it refuses a ROM. The copies stay in memory: the test writes no ROM data to
   disk.

A second test, `rom_loader_detects_change`, runs the same program with one byte of one resource changed after it is
loaded. It passes only if the program reports that the digest does not match, which shows that the comparison really
looks at the resources.

The test finds the ROM the way the program does: the file named by the environment variable `TD_ROM`, else
`baserom.z64` in the current folder, else `baserom.z64` in the decompilation's folder. When there is none, CTest lists
both tests as skipped. To run only these two, from this repository's folder:

```
ctest --preset debug -R rom_loader
```

When the digest does not match, the test prints the one it got.

### A second reader of the resources

`tools/res_digest.py` reads every resource of your ROM without any of the game's code: it is written in Python from
the format of the game's resource table and of its compression. The digest in the test is the one that both it and
the game's loader compute. From this repository's folder:

```
python3 tools/res_digest.py ../source/baserom.z64
```

It prints the number of resources, the digest, and whether the digest is the one in the test. With `--manifest FILE`
it also writes a manifest, a text file with one line per resource: its id, ROM start and ROM end, 1 if it is
compressed, its size, and the SHA-1 of its bytes. When the test's digest does not match, the environment variable
`TD_MANIFEST` makes the test print the first resources that differ from the manifest:

```
python3 tools/res_digest.py ../source/baserom.z64 --manifest build/debug/manifest.txt
TD_MANIFEST=build/debug/manifest.txt ./build/debug/rom_loader
```

A manifest lists hashes of game data, so it stays in the build folder, which git ignores, and is never committed.

When a change is meant to change what the game's loader loads, for example once resources are converted to the PC's
byte order, `tools/res_digest.py` gets the same change. The digest that both then compute goes into `RES_DIGEST` in
`tests/rom_loader/main.c`.

## Stepping through game code

In VS Code (see the README for the setup), with a Debug build that includes game code:

1. Open a file of the game's C with File, Open File. For example, the decompilation's `src/main/func_800037F4.c` is
   the game's decompressor.
2. Click left of a line number inside the function, for example on the line `size = READ_BE32(arg0);`. A red dot marks
   the breakpoint.
3. In the Run and Debug view, choose "Debug Turnbuckle Dive" and press F5. The program checks the ROM and starts
   loading resources, and it stops at the breakpoint when it unpacks the first compressed one. F10 runs one line:
   after it, the Variables view shows `size`, the number of bytes the function is about to unpack. The Call Stack view
   shows which resource is being loaded (`id` in `func_80003684`). F11 steps into a call, and F5 runs on to the next
   stop.
4. "Debug Turnbuckle Dive: one resource" asks for a resource id (for example `0x3`) and loads only that resource.

The same from a terminal, with gdb, from this repository's folder. This command starts the program under gdb and
stops where the function starts; `next 2` runs on by two lines, past the one that sets `size`, then gdb prints `size`
and, with `backtrace`, the calls that led there:

```
gdb -ex 'break func_800037F4' -ex run -ex 'next 2' -ex 'print size' -ex backtrace \
    --args ./build/debug/turnbuckle-dive --res 0x3
```

gdb then waits at its `(gdb)` prompt: `next` runs one more line, `step` goes into a call, `continue` runs on to the
next stop, and `quit` ends.
