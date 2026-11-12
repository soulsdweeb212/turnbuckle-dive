# Testing in early development

Turnbuckle Dive does not run the game yet. Until it does, testing it means building it, running its tests, and running
its three programs to see what they print and draw. This page says where to run each command, what each test and check
does, what the programs should show, and what happens inside a program from its start to its end.

It is written for a Linux or WSL build that includes the game's code: the decompilation, on its branch
`port-groundwork`, in the folder `source` next to this repository's folder, with your ROM in it as `baserom.z64`. The
README's "Building (Linux and WSL)" section explains that setup and the first build, which makes the build folder
`build/debug`.

## Where each command runs

The commands on this page run in one of two folders:

- **This repository's folder** (`turnbuckle-dive`): building, and running the tests with `ctest`.
  `cmake --build --preset debug` and `ctest --preset debug` read their settings, the presets, from `CMakePresets.json`
  in the folder they are typed in, and that file is in this one.
- **The build folder** of the `debug` preset, `build/debug` in this repository's folder (each preset has its own, such
  as `build/asan`): running the programs. The build puts `turnbuckle-dive`, `hello` and `viewer` there. In the
  repository's folder, `hello` and `viewer` are the folders of those two programs' source code.

From this repository's folder, `cd build/debug` goes into the build folder, and from there `cd ../..` comes back. The
terminal stays in a folder until the next `cd`. Its prompt, the text before where you type, ends with that folder, for
example `turnbuckle-dive/build/debug$`.

hello and the viewer keep their files in the folder they are started from: their log (`logs/`), their settings
(`hello.cfg.json`, `viewer.cfg.json`) and, once a window has been open for a few seconds, `imgui.ini`. The viewer's
check also reads its reference, `viewer_ref.json`, from there. So they are started in `build/debug`, which keeps all of
these with the build.

## Running all the tests

From this repository's folder:

```
cmake --build --preset debug
ctest --preset debug
```

The first command builds what has changed since the last build. The second runs the tests with CTest, CMake's tool for
running tests. Two words in the table: a SHA-1 is a fingerprint worked out from bytes, the same for the same bytes and
different after any change; a digest is such a fingerprint taken over many pieces of data at once.

| Test | What it checks |
|---|---|
| `sha1` | The port's SHA-1 code, with which it recognises the supported ROM, gives the published SHA-1s of sample inputs. |
| `slots` | The handle table (`src/slots.c`, [GAME_CODE.md](GAME_CODE.md) explains it), which stands in for an address where a game record has only 4 bytes for one: `NULL` is handle 0, an address gets the same handle each time and two addresses get two handles, also after the table has grown, and four threads that add handles while a fifth reads others all get the same, right answers. |
| `slots_never_issued` | The same program reads a handle the table never gave out. This test passes when the program stops and says so. |
| `slots_out_of_range` | The same program reads handle 0x80123456, an N64 address where a handle should be, far beyond the end of the table. This test passes when the program stops and says the handle was never given out, instead of reading outside the table. |
| `slots_full` | The same program fills the table, all 2^20 handles, and asks for one more. This test passes when the program stops and says the table is full. |
| `rom_missing` | `turnbuckle-dive`, given a ROM path that does not exist, starts and says "file not found". |
| `rom_loader` | The game's own loader loads all 7657 resources of your ROM (its textures, models, animations and the rest of the game's data), and their digest is the one the test knows ([GAME_CODE.md](GAME_CODE.md) explains it). |
| `rom_loader_detects_change` | The same with one byte of one resource changed after loading. This test passes when `rom_loader` reports that the digest does not match. |
| `dev_match` | The developer match ([GAME_CODE.md](GAME_CODE.md), "The developer match") plays a whole CPU-against-CPU match without a window, writing the wrestler log, and checks the G6 gate: the match ends (phase 4) and never goes past its end, within 20000 frames, its first loads are resources 0x126 to 0x12A in order, and no frame takes 3 seconds. |
| `dev_match_again` | The same match again, into a second log. |
| `dev_match_other_seed` | The same with the random seed started elsewhere, a different match, into a third log. |
| `dev_match_repeatable` | `tools/wr_log.py` compares the first two logs, every wrestler at every frame: they must be the same. |
| `dev_match_detects_change` | The same comparison of the first log with the other seed's. This test passes when the tool reports where they differ, which shows that the comparison sees a different match. |
| `game_vars` | Every game file of the developer program links with the game's variables, and the variables start as on the N64: each value in the PC's byte order equals the ROM's, each address table holds what its N64 addresses name, and each handler table entry is the function the N64 calls. |
| `game_vars_detects_change` | The same with one value left in the N64's byte order. This test passes when the check reports it. |
| `gdb_knows_game_macros` | gdb, reading `turnbuckle-dive`, prints the game's macro `WR_FLAG_LEGAL`: the debug information holds the game's macros. Only in a Debug build, with gdb installed. |
| `gdb_prints_wrestlers` | gdb, with `tools/gdb/wrestler.py`, prints a wrestler record as one readable line, and a state by its name. Same conditions. |
| `sanitizer_catches_overflow` | Only in the asan preset: a program reads one byte past the wrestler records, and passes when AddressSanitizer reports it with the program's file and line. |
| `byte_order_match` | Only in the order preset: plays the developer match under the byte-order check (below, "The byte-order check") and passes when no read of the game's code gives another value than the N64's. It takes about a minute. |
| `viewer_reference` | `tools/viewer_ref.py` works out from your ROM, without the game's code, what the asset viewer must build and draw, and writes it to `build/debug/viewer_ref.json`: the reference. |
| `viewer_check` | The asset viewer builds its three subjects (a wrestler's head from its three parts, an effect sprite and a 2D picture) with the game's code and compares them with the reference: first the vertices and triangles of each of the head's parts, and the camera placed over them, then every pixel of each picture. Between the head's two sides Fast3D draws at another resolution, as when someone stretches the window. Last, the picture's palette is changed in memory, and the picture must be drawn in the new colours. Its window opens, draws and closes by itself. |

Near the end, CTest prints how many passed, `100% tests passed, 0 tests failed out of 19` (20 in the asan and order presets), and
then the time the tests took. The tests from `rom_loader` on, but for the two gdb tests, need your ROM. When they find none, CTest marks them `***Skipped` instead of `Passed`, still
counts them in that line, and names them at the very end, under `The following tests did not run:`.

hello's check is not one of these tests: it is run by hand, below.

## How a check decides

hello and the viewer each have a check, `--check`, in which the program judges its own picture, so nobody has to look
at the window. The program draws its picture, reads the finished picture back from the framebuffer, the image the
picture is drawn into, and compares it with what it must be. hello compares it with the shapes and colours of its own
test picture. The viewer compares it, pixel by pixel, with the reference; before it draws the head, it also compares
the vertices and the triangles that the game's functions built for each of its parts, and the camera it placed over
them, with the reference's.

A program prints its text in the terminal it was started from; its window shows only the picture. LibUltraShip prints
its log lines there too, each starting with the date and time in brackets, and keeps them in `logs/`. The check's own
lines start with `check:`. Some say what it read or counted, and each comparison ends with a line that starts
`check: PASS`, or `check: FAIL` and what is wrong.

When a program ends, it hands back a number, its exit code. CTest, and CI, the build and test that GitHub runs after
every push ([CI.md](CI.md)), go by it:

| Exit code | Meaning |
|---|---|
| 0 | every comparison passed |
| 2 | a comparison failed, or the check could not be finished: for example, the viewer found no reference, or the window was closed before the check was done |
| 1 | the program could not start: an option it does not know, a log or `f3d.o2r` it could not open, no screen to open its window on or no OpenGL 3.0 or newer to draw in it with, or, for the viewer, a ROM it cannot use, such as one that is not the supported one |
| 77 | only the viewer's check: it found no ROM; CTest lists the test as skipped |

## The checks by hand

Run `ctest` first, as above: its test `viewer_reference` writes the reference, `viewer_ref.json`, into `build/debug`,
where the viewer's check reads it. Then, from this repository's folder:

```
cd build/debug
./hello --check --frames 30
echo $?
./viewer --check --reference viewer_ref.json
echo $?
```

`echo $?` prints the exit code of the command before it. A frame is one whole picture, and the programs draw 60 a
second. hello makes its check after its third frame and goes on drawing until its window is closed; `--frames 30` ends
it after 30 frames, as CI runs it. The viewer's check closes its window by itself once it has compared all its
subjects. Meanwhile the window shows each picture it compares for a moment: the head from in front of one side, then
from behind the other side, then the sprite and the picture.

## Looking at it yourself

From this repository's folder (leave out the `cd` if you are still in `build/debug` from the checks above):

```
cd build/debug
./turnbuckle-dive
./hello
./viewer
./viewer --sprite
./viewer --picture
```

`turnbuckle-dive` prints what it found in your ROM, whether it is the supported one, and how many resources the game's
loader loaded. `./turnbuckle-dive --dev-match` plays a match in the Match Inspector's window: a table of the wrestlers
and a map of the ring, moving live ([GAME_CODE.md](GAME_CODE.md), "The developer match"). Each of the other commands opens a window that stays open until you close it.
Closing the window ends the program, and the terminal then takes the next command. You should see:

- `./hello`: its test picture. On a dark background are a shaded triangle, three ring ropes and a square that moves
  across, and above them the shapes and the eight palette squares that its check measures (the comment at the top of
  `hello/main.cpp` describes them all).
- `./viewer`: a wrestler's head, turning half a degree each frame: the face, with white and black face paint, at the
  front, brown hair over the top, the sides and the back, and below them the jaw and the neck. These are the three
  parts the game builds the head from ([VIEWER.md](VIEWER.md) lists them).
- `./viewer --sprite`: an effect sprite, a white puff with soft see-through edges.
- `./viewer --picture`: the game's legal screen, with sharp text.

[VIEWER.md](VIEWER.md) lists the viewer's other commands: other models, sprites and pictures, a fixed angle, and
screenshots.

## Seeing the drawing commands

The game draws by writing lists of N64 drawing commands, called display lists. Fast3D, LibUltraShip's renderer, turns
those commands into OpenGL calls, the standard way for a program to have the GPU (the graphics card) draw. There are
two ways to see them, both in `build/debug` (`cd build/debug` from this repository's folder):

- `./viewer --dump` draws one frame, prints every command of its lists in the order Fast3D runs them, and ends.
  `./turnbuckle-dive --dev-match --dump-frame 600 frame600.txt` does the same for the match: it writes the commands of
  the match's frame 600 to `frame600.txt`, and the match goes on.
- In a viewer window, press F1: a menu bar appears at the top. Open its Debug menu and choose Gfx Debugger, then click
  the Debug button in the Gfx Debugger's window. The frame freezes, and its commands show as a tree. Clicking a
  command draws the frame only up to that command. The Resume Game button, which takes the Debug button's place, lets
  the viewer go on.

## The byte-order check

The N64 keeps the bytes of a number biggest first; a PC keeps them smallest first. Game code that writes a value at
one size and reads the same bytes at another size (one byte of a 4-byte number, or two 2-byte fields read as one
4-byte number) then reads a different value on a PC than on the N64, and the match plays differently. That is how
the Irish whip stopped short of the ropes. The compiler cannot see it when the two sizes are in different files, so
a check watches the game run instead. It is a build of its own, the `order` preset (Linux and GCC only), run from this
repository's folder:

```
cmake --preset order
cmake --build --preset order
cd build/order
./turnbuckle-dive --dev-match --headless --no-watchdog
cd ../..
python3 tools/order_check.py build/order/turnbuckle-dive build/order/order_check.txt
```

`ctest --preset order` runs the tests in that build, among them `byte_order_match`, which does the same for the
default match and fails when the list names a line.

The match takes about a minute instead of a few seconds, because every read and write of the game's code goes
through the check. At the end the program writes `order_check.txt`, and `tools/order_check.py` turns it into names:
each line of game code whose read gave another value than the N64 would have read, how many times, from which frame,
and the lines that wrote those bytes. `no read differs from the N64` means that no read did, in that match. `--seed N`
plays another match, which runs other code; `TD_ORDER_CHECK_OUT=name.txt` before the command names the list's file,
and the tool adds up the lists it is given.

How it works: for every byte the game code writes, the check records the size and place of the value it belongs to,
and the line that wrote it. When game code reads bytes that were written at another size, the check works out the
value the N64 would read there (each value's bytes in the other order) and compares it with the value the PC read.
A copy that keeps the bytes in order (a value read and written again at the same size) is not a finding. Bytes the
port converted from the ROM count as written at the size the port converted them, and bytes nothing has written yet
match any read. The comment at the top of `src/dev/order_check.c` says more. The check sees only the code that the
match runs.

A finding is fixed in the game file, in a `#ifdef TARGET_PC` version of the line that reads the value the N64 reads
(the decompilation's docs/port.md, "Byte order on PC").

## What happens inside a program, from start to end

This is `viewer --check`, in order. Steps 1 and 2 come before the window opens, in step 3, and step 9 ends the
program. The file named in each step holds its code.

1. **The ROM.** `viewer_open_rom` (`viewer/viewer_rom.c`) finds your ROM, reads it into memory and checks its SHA-1.
   Then `res_init` (`src/resources.c`) starts the game's code on it: the game's loader reads the ROM's table of
   resources.
2. **The reference.** `LoadReference` (`viewer/main.cpp`) reads `viewer_ref.json`: the subjects to draw, the vertices
   and triangles of each of the head's parts as the tool decoded them, the camera the tool placed over them, and the
   colours each pixel may have.
3. **LibUltraShip's parts.** Under WSL, unless you have set `GALLIUM_DRIVER` yourself, the program first sets it to
   `d3d12` when WSL can reach the GPU, so that the GPU draws through Direct3D 12, and prints a line that starts `WSL:`.
   Then `StartLibUltraShip` (`src/context.cpp`) creates a `Ship::Context` and adds LibUltraShip's parts to it in a
   fixed order. Among them:
   - the log and the settings;
   - the resource manager, which opens `f3d.o2r`, LibUltraShip's shaders (the small programs the GPU runs to draw);
   - the window (`Fast::Fast3dWindow`), the controllers and the sound;
   - the `UltraBridge`, which lets plain C calls such as `WindowIsRunning()` find the window.

   The window appears at `window->Init()`: SDL3, the library LibUltraShip uses for windows, input and sound, creates
   it with an OpenGL context, through which the program hands the GPU its drawing. Before it adds the window,
   `StartLibUltraShip` has SDL start its video and, on Linux, load OpenGL; after `window->Init()` it asks SDL and
   OpenGL whether the window, its OpenGL context and an OpenGL of version 3.0 or newer are there. When one of them is
   missing, the program says which and ends with exit code 1.
4. **The picture's shape.** `ShowGamePicture` (`src/context.cpp`) sets up the game's 480x240 picture, shown at 4:3, at
   60 frames a second. For a check, Fast3D draws it at twice the window's resolution into a framebuffer of its own, an
   image apart from the window, which can be read back.
5. **The game's data and drawing commands.** For each of the head's parts, `viewer_head_load` (`viewer/viewer_game.c`)
   takes memory from `allocateHeap`, the port's stand-in for the game's heap (`src/game_heap.c`), has the game's loader
   put the part's mesh in it (the part's shape: its corner points, called vertices, and the triangles between them),
   and fills in the game's display record for it (`Nw2TextureDisplayObj`). Then the game's own functions do the work:
   - `func_8000486C` writes the vertices;
   - `buildVertexDisplayList` writes the triangle list, and `buildObjectDisplayListTail` a first material list, for a
     mesh without a texture;
   - `func_80004E08` loads the texture, the image laid over the triangles, and its palette, the table of colours from
     which each of the texture's pixels picks one by its number;
   - `buildCiTextureDisplayList` writes the material list again: it loads the palette and the texture, then goes on to
     the triangle list.

   The lists are arrays of `Gfx`, the C type of one drawing command: 8 bytes on the N64, and 16 in this build, so that
   a command can hold a whole 64-bit address. Then `CheckHeadData` (`viewer/main.cpp`) compares each part's vertices
   and triangles with the reference's, before anything is drawn, and `CheckCamera` compares the camera that `FitHead`
   places over the vertices of all the parts with the reference's. When they do not match, nothing is drawn, and the
   check ends with exit code 2.
6. **The frame.** `BuildHeadFrame` (`viewer/main.cpp`) writes the viewer's own list, `sFrame`. It clears the picture
   to dark blue, sets the render state, with a depth buffer so that the nearest part shows at each pixel, and sets the
   camera's two matrices. Then, for each part, it loads the part's vertices and calls the game's material list, which
   loads the texture and goes on into the triangle list, which draws the part and returns. Last, it turns the depth
   buffer off again, draws two white one-pixel markers in opposite corners, at (0,0) and (479,239), and ends the list.
7. **Each frame.** `DrawFrames` (`viewer/main.cpp`) checks with `WindowIsRunning()` that the window is still open, and
   calls `window->HandleEvents()`, which takes key presses, the mouse and the close button from SDL. Then it calls
   `window->DrawAndRunGraphicsCommands(sFrame, {})`: Fast3D's interpreter (`Interpreter::Run`) steps through the
   commands and turns them into OpenGL calls, and `SDL_GL_SwapWindow` puts the picture on the screen.
8. **The read-back.** After the third frame, `ReadPicture` (`src/frame.cpp`) copies the framebuffer into the program's
   memory. `CheckCorners` (`src/frame.cpp`) finds the two markers, which show whether the rows were read top-down or
   bottom-up, and `CheckPixels` (`viewer/main.cpp`) compares every pixel with the reference. The head is loaded once and
   drawn from two sides, steps 6 to 8 for each side, with Fast3D drawing at another resolution for the second, as when
   someone stretches the window, and at its own again after them. Then the sprite and the picture are each loaded, drawn
   and read back the same way, with their own functions: `viewer_sprite_load` and `BuildSpriteFrame` for the sprite, and
   `viewer_picture_load`, `BuildPictureCommands` and `BuildPictureFrame` for the picture ([VIEWER.md](VIEWER.md) names
   the game's functions they call).
9. **Closing.** `StopLibUltraShip` (`src/context.cpp`) calls `window->Close()`, which turns off the window's "running"
   flag, and then lets go of the context. The context saves the settings, takes its parts out and stops the log. A part
   is destroyed only when nothing else holds on to it, as the sound and the events parts are: the "Destructing
   component" lines at the end of the output are theirs. The window's part is still held by other parts, the F1 menu
   bar for one, so it is not destroyed, and the window stays on the screen, no longer drawn. Last,
   `viewer_close_rom` frees the ROM, and the program ends with its exit code. The system then closes the window, as it
   closes everything a program leaves open.

Clicking a window's close button ends the program the same way. SDL sends a "close requested" event, `HandleEvents`
calls `Close()`, and `WindowIsRunning()` becomes false. The program's frame loop then ends, and it shuts down as in
step 9. In a check, a window closed before the check is done ends it with exit code 2.

hello goes through the same steps without the ROM, the reference and the game's code: it writes the commands of its own
test picture itself, and its check knows the shapes and colours that picture must have.
