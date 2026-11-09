# Game code

Turnbuckle Dive runs the game's own code: the C of the WCW/nWo Revenge decompilation, compiled for the PC. This page
explains where that C comes from, how it is compiled, what the port provides to it, how it is tested, and how to step
through it in a debugger.

So far the game code in the port is the game's ROM loader, the functions that read resources (textures, models,
animations and the rest of the game's data) out of the ROM and unpack the compressed ones, and the functions that turn
a model, an effect sprite or a picture into display lists, which the asset viewer draws ([VIEWER.md](VIEWER.md)).

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

These write 16-byte commands that hold whole pointers, and the record they fill has a 64-bit layout: the
decompilation's `docs/port.md` explains how.

Fast3D, LibUltraShip's renderer, keeps every texture it has decoded, filed under the addresses of its texels and
palette, not under what they hold. The game loads new textures and palettes into buffers it already has
(`func_80004E08` and `func_8000670C` keep a buffer when the new resource fits in it) and changes palettes where they
are (`convertRgba16ToGrayscale`), so once the game's own scene code runs, the port must drop Fast3D's textures each
time the game writes into a texture or palette that was drawn (`gfx_texture_cache_clear`). The viewer loads each
subject once, and drops them after freeing a subject.

## What the port provides to the game

The game's C calls a few functions that the port provides. The decompilation's `include/port.h` declares them for the
game's side; this repository's `src/game.h` declares the same functions for the port's side.

- `port_rom_read` (`src/rom.c`) copies bytes of the user's ROM, which the program has checked and keeps in memory. A
  read that does not lie wholly inside the ROM stops the program with a message.
- `port_fatal` (`src/port.c`) prints a message and stops the program, where the N64 would hang or crash.
- `port_res_loaded` (`src/port.c`) is called by the game's loader each time a whole resource is in memory.

For parts of the game that are not compiled yet, the port has stand-ins with the game's names:

- `src/game_heap.c`: the game's heap functions, `allocateHeap` and `releaseHeapBlock`, built on the C library's malloc.
  The file explains why the game's own heap cannot run on a 64-bit PC yet.
- `src/game_data.c`: the game variables the compiled files use. The loader's two tables, `D_800482E0` and
  `D_800482E4`, start empty and the loader fills them. The others are values the game keeps in its ROM (the grayscale
  switch, the grayscale factors, a picture's height scale): the repository holds none of them, and `game_data_init`
  copies them from your ROM when `res_init` (`src/resources.c`) starts the game's code. A data generator will later
  write all of the game's data as C, and then this file goes.

`src/game_check.c` keeps the two headers in step: it is compiled with the game's settings and includes both
`include/port.h` and `src/game.h`, so the build stops with an error if they ever declare a function or the resource
descriptor differently. `td_game` depends on it, so this also holds when only one program is built.

`src/resources.c` is the port's code that calls the game's loader, for the program and the tests.

## Adding a game file

1. In `CMakeLists.txt`, add the file's path to `add_library(td_game ...)`, written like the others:
   `${TD_DECOMP}/src/main/...`.
2. Build. The link names every function or variable that the new file uses and the build does not have yet, even
   while nothing calls the new file. Add the game file that defines each one the same way. When that file cannot run
   on the PC yet, write a stand-in in `src/` instead, like `src/game_heap.c`, with a comment that says why it is
   needed and what replaces it.
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
