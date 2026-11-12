# The asset viewer

`viewer` is the first program that draws with the game's own code. It loads a model, an effect sprite or a picture
from your ROM with the game's loader, turns it into display lists with the game's own functions, compiled from the
decompilation, and draws those lists with LibUltraShip, in the game's 480x240 picture shown at 4:3, like hello. It
shows that the game's display-list functions work on a 64-bit PC, and it is where you can watch them work, command by
command.

It has three subjects:

- **A wrestler's head, from its three parts**: the lower part of the head, the face and the hair, each a mesh with its
  own texture and palette, turning slowly. They are the parts the game builds this head from ("Where a model's texture
  comes from", below). The viewer calls this subject the head, after its option `--head`, which can also show one part
  alone, or any other model.
- **An effect sprite**, one frame of the short sprite animations that the game plays on the wrestlers in a match: the
  texture 0x3EC (4-bit palette indices) with the palette 0x3DF (16 entries), which the game reads as IA16, a grey level
  and a transparency for each entry.
- **A TEX picture**, one of the game's 2D pictures: by default resource 0x3, drawn in the middle of the frame.

The viewer is built when the build includes the game's code ([GAME_CODE.md](GAME_CODE.md) says when it does) and
LibUltraShip, so CI, which has no game code, does not build it.

## Running it

From this repository's folder, after `cmake --build --preset debug`:

```
cd build/debug
./viewer
```

It looks for the ROM as `turnbuckle-dive` does: the path given as the last argument, else the file named by `TD_ROM`,
else `baserom.z64` in the current folder, else in the decompilation's folder. It writes `logs/viewer.log`,
`viewer.cfg.json` and `imgui.ini` in the folder it is started from.

| Command | What it does |
|---|---|
| `./viewer` | the head, turning half a degree per frame |
| `./viewer --angle 120` | the head at a fixed angle, in degrees, about the vertical axis |
| `./viewer --head 0xA69 0xA6A 0xA68` | a model: a mesh, its texture and the texture's palette (here the face alone) |
| `./viewer --head 0xA69 0xA6A 0xA68 0xA63 0xA64 0xA62` | a model of several parts, three ids for each (here the face and the hair) |
| `./viewer --sprite` | the effect sprite 0x3EC with its palette 0x3DF |
| `./viewer --sprite 0x3E8 0x3DB` | another effect sprite: a texture and its palette |
| `./viewer --picture` | the TEX picture 0x3 |
| `./viewer --picture 0x53` | another TEX picture |
| `./viewer --frames 600` | draws 600 frames, then exits |
| `./viewer --screenshot head.png` | draws three frames and saves the third as a PNG file, then exits |
| `./viewer --dump` | draws one frame and prints its display lists (below), then exits |
| `./viewer --check` | the check (below) |

Ids are written in hex with `0x` or in decimal. `--head` takes up to 23 parts, as the game keeps 23 display records
for each wrestler. The viewer checks each id before the game's code sees it: an id that is not one of the game's (1 to
0x1DE9), or a resource of another kind than the option needs (a picture given as a mesh, a palette too short for its
texture, and so on), ends it with a message that says what is wrong. A screenshot shows the game's data from your ROM:
save it in the build folder, as above, or anywhere outside the repository (git ignores PNG files).

## How it draws the head

The game keeps a mesh in a display record, which `func_8000498C` fills from the mesh resource, and then gives the record
a texture: `func_80004F80` does that for a wrestler's body part, and `func_80007020` for the meshes of a scene. The
viewer cannot call `func_8000498C` and `func_80007020` yet, because on the PC they would keep the record's pointers in
32-bit numbers. So `viewer/viewer_game.c` does their steps itself, in the same order, for each part of the head in a
record of its own, and calls the game's functions that do the work:

1. It loads the mesh with the game's loader and copies its header into the record: the number of vertices and of
   triangles.
2. The game's `func_8000486C` turns the mesh's 8-byte vertices into the 16-byte vertices (`Vtx`) the N64 draws.
3. The game's `buildVertexDisplayList` writes the triangle list: it turns back-face culling on, then has one
   `G_TRI2` command per two triangles, and ends with `G_ENDDL`. `buildObjectDisplayListTail` writes the material list,
   which branches to the triangle list.
4. The game's `func_80004E08` loads the texture and its palette into the record, as `func_80004F80` has it do for a
   wrestler's body part (`func_80007020` keeps each texture once, in a list its models share, and has
   `func_80004E08` load only the palette). Then `buildCiTextureDisplayList` writes the material list again, in the
   mode `func_80007020` uses, 1, which leaves the render state to the viewer's frame: it loads the palette and the
   texels, then branches to the triangle list. For a wrestler's body part the game uses mode 0, which also writes the
   wrestlers' render state at the start of the list: a render mode that neither tests nor keeps depths, so it would
   turn off the viewer's depth buffer (below), and a colour combiner that multiplies each texel by the vertex's alpha,
   where the game keeps its lighting, and that LibUltraShip cannot draw yet.

The viewer's frame then sets the render state that the game's frame would have set (one cycle, textures on, palettes
read as RGBA16, and the state `func_80007020` sets before its models), places the camera with two matrices, and for
each part loads its vertices with `G_VTX` and calls its material list with `G_DL`. The game's lists do the rest.

Two settings are the viewer's own. The game filters its textures, and the viewer does not, so every texel shows as a
block of one colour, which the check can compare pixel by pixel. And the viewer draws the head with a depth buffer:
for each pixel the GPU keeps the depth of what it has drawn there, and draws a later triangle there only where that
triangle is nearer, so each pixel shows the nearest part, whatever order the parts are drawn in (Fast3D clears the
depth buffer at the start of every frame). The game draws a wrestler without one: it paints whole body parts from the
farthest to the nearest (`func_80001EA8` sorts them by how far away they are, `func_80002110` lists them in that
order, and `func_80002454` writes their commands from that list), each part's triangles in the order of its list, with
those that face away left out. Painted that way, each of the three parts would cover nearer triangles of its own at
some angles (the face only when it is seen from behind); with the depth buffer the viewer shows the head as it is
modelled.

The camera puts the middle of the model at the middle of the picture, turns the model about the vertical axis, and
scales it so that it fits the picture at every angle. Its projection has no perspective. The parts of a head share one
space, so the camera is fitted over the vertices of all the parts together, and the parts meet where the head's model
has them meet.

When the window's size changed, LibUltraShip went on drawing the model without its texture, in black, until the fix
`cmake/patches/libultraship/0007-keep-the-bound-texture-cache-right.patch`.

## Where a model's texture comes from

The viewer draws the mesh, the texture and the palette it is given: nothing in a mesh says which texture belongs to it.
In the game, tables say. For the objects of a scene, `func_80007020` is given a list of meshes and a list of palettes,
and takes each mesh's texture from the resource after its palette. For a wrestler, `func_8000BDD0` loads the mesh of
each body part into a display record of its own (through `func_80004BA8`, which calls `func_8000498C`), and
`func_8000C1B8` loads each part's texture and palette (through `func_80004F80`, which calls `func_80004E08`, the
function the viewer calls too).

The head's parts come from a head record: it starts with the meshes of body parts 4, 5 and 20 (the game's numbers for
the parts of a head; a head without part 20 has 0 there), followed by the palette and the texture of each. Each
wrestler has a list of head records, and each of its four costumes names one of them, so two costumes can wear the same
head (`func_8000B630` reads which, from the table `D_80036710`). The list of the wrestler with id 1 starts with the
record at 0x800319C0, which that wrestler's second and fourth costumes wear. It gives the head these three parts:

| Body part | Mesh | Texture | Palette | What it shows |
|---|---|---|---|---|
| 4 | 0xA66 | 0xA67 | 0xA65 | the lower part of the head: skin at the front, hair at the back |
| 5 | 0xA69 | 0xA6A | 0xA68 | the face, with its paint |
| 20 | 0xA63 | 0xA64 | 0xA62 | the hair over the top, the sides and the back of the head |

`./viewer` draws all three together. `./viewer --head` followed by the three ids of one or more rows draws those parts.

## How it draws an effect sprite

The game's `initializeMatchResources` loads each effect sprite's texture and palette with `func_8000670C` and makes
the four vertices all its sprites share with `func_80007920`, a square around the origin. `func_80011764` draws a
sprite: it sets the primitive colour, calls `buildCi4TextureDisplayList`, which writes the sprites' render state (the
texel blended over what is behind it by its alpha), their colour combiner (the texel times the primitive colour) and
the commands that load the palette and the texels, and adds its own commands: the palette type IA16, the four
vertices, two triangles, and the palette type RGBA16 again. Neither `initializeMatchResources` nor `func_80011764`
can be compiled yet (they keep the sprites in arrays sized for the N64's 4-byte pointers), so
`viewer/viewer_game.c` and the viewer's frame do their steps, in their order and with their commands, and call the
game's `func_8000670C`, `func_80007920` and `buildCi4TextureDisplayList` for the work.

Two settings are the viewer's own: the primitive colour is white, so every texel shows as it is (the game takes the
colour from a table for each kind of effect), and `buildCi4TextureDisplayList` is asked for the one kind of sprite
the game draws without the N64's dithered alpha test, which LibUltraShip turns into random noise. The camera faces the
sprite.

LibUltraShip read every palette as RGBA16, whatever the palette type, until the fix
`cmake/patches/libultraship/0005-read-palettes-in-the-type-gdpsettexturelut-sets.patch`.

## How it draws a picture

A TEX picture is a 16-byte header (its size, its bits per texel and its number of colours), its palette, and its
texels. The game loads one with `func_80004E08`, loads its palette, and draws it with `func_80096D8C`, which cuts it
into tiles of 64 texels by 48 rows (24 for 8-bit pictures) and draws each with a texture rectangle; `func_80096A6C`
writes the commands that load each tile. The viewer calls the same functions. The palette load and the render state
come from overlay-1 code that the port cannot compile yet, so the viewer writes them itself: the palette load with the
game's own commands, and the render state with the game's blending, without its filtering, as for the head. A picture
taller or wider than the frame starts at the frame's edge on that side and shows as much of it as fits.

The game loads a picture's texels as IA4 or IA8 tiles (intensity and alpha), not as palette indices (CI). On the N64
the texels are looked up in the palette anyway, because palettes are on; LibUltraShip only did that for CI tiles, until
the fix `cmake/patches/libultraship/0006-look-up-palettes-for-4-bit-and-8-bit-texels-of-every-format.patch`.

## The check: viewer --check

`viewer --check` draws each subject, reads its picture back from LibUltraShip's framebuffer, and compares it with a
reference that `tools/viewer_ref.py` computes from your ROM. That tool is a second, separate reader of the same data,
written in Python from the formats: it shares no code with the game's C. From the data it works out the colour each
pixel must have, following the way the N64 draws: where the camera puts each vertex, which triangle covers each pixel
(for the head the nearest one, which the viewer's depth buffer keeps; for the sprite the last one the list draws
there), the texel each pixel shows, and how the texel's colour is blended. The check compares:

1. For each part of the head, the vertices that the game's `func_8000486C` decoded with the tool's decode of the
   part's mesh, field by field.
2. For each part, the triangles in the game's triangle list with the tool's, in order. Unless every part's vertices
   and triangles match, nothing is drawn.
3. The camera the viewer fits over the vertices of all the parts with the tool's: its middle exactly, and its radius
   to within a hundred-thousandth of it (the two programs may round its square root differently). Unless it matches,
   nothing is drawn either.
4. Every pixel of the head at -60 degrees, and again at 120 degrees, the other side, so that every triangle the
   camera sees from behind in one picture faces it in the other; a line for each part then says how many of its
   triangles face the camera in at least one of the two. For the second picture Fast3D draws at 2.5 times the
   window's resolution instead of twice (or at 1.75 times, near its limit of 4320 rows), so that it sizes its
   framebuffer again, as when someone stretches the window (the case patch 0007 fixes); after the head it draws at
   twice again, so that the sprite and the picture are drawn after a resize too. The window keeps its size and place;
   the framebuffer lines of the output give each size.
5. Every pixel of the effect sprite.
6. Every pixel of the picture, which the tool works out tile by tile as the N64 draws it (its comments say how).
7. The picture again, after the check makes every entry of its palette red in memory, as the game changes palettes
   where they are. Fast3D must see that the palette changed and decode the picture again (patch 0009): every pixel
   that shows the picture must be red.

Each pixel must have the colour the tool works out, and pixels where the tool says the background shows must not be
drawn. Where a triangle's edge or a texel's edge passes close to a pixel's middle, the tool allows the colour on each
side, because the framebuffer pixel that is read back for a picture pixel does not have its middle exactly there (the
check needs a framebuffer of at least 960x480 pixels; it draws at twice the window's resolution, in the picture's 4:3
shape, so a window at least 480 pixels wide and 360 high gives one, and as Fast3D gives its framebuffer at most 4320
rows, the window must be at most 2160 high, or 2880 wide when it is taller than 4:3; the tool's comments give the
distances). In the same way, where two surfaces of the head are so near each other in depth at a pixel that either
could be in front where the GPU looks, as where the face and the lower part of the head cross, the tool allows the
colour of each. A colour may be 1 off in each of its red, green and blue (5 bits each as they read back), because
LibUltraShip scales 5-bit colours to 8 bits for the GPU. Each line of the check's output says what was compared and
whether it passed; the viewer ends with exit code 0 when all passed, and 2 when one failed. All subjects need the ROM,
and the check exits with code 77 when it finds none. A reference that is not what the check expects (written by an
older or newer tool, or changed by hand) ends the check with a message naming the part that is wrong.

The test `viewer_check` runs the check, after the test `viewer_reference` has run the tool. From this repository's
folder:

```
ctest --preset debug -R viewer
```

The same by hand, from this repository's folder:

```
python3 tools/viewer_ref.py ../source/baserom.z64 --out build/debug/viewer_ref.json
cd build/debug
./viewer --check --reference viewer_ref.json
```

The tool's options choose other subjects: `--head MESH TEX PAL [MESH TEX PAL ...]` (the parts of a head, three ids
for each; the ROM goes before this option, which takes every id after it), `--angle DEG` (the first of the head's two
angles, -60 when not given), `--sprite TEX PAL` and `--picture ID`. The reference holds data decoded from your ROM, so
`--out` must name a file in the build folder, which git ignores.

In the `asan` preset, `viewer_check` runs without LeakSanitizer, because the GPU driver and SDL's sound thread keep a
few blocks to the end that the program cannot free (hello shows the same ones). To see LeakSanitizer's report, run the
check by hand in `build/asan`; each block it lists names the library it came from.

## The frame's commands: viewer --dump

`viewer --dump` draws one frame, then prints every command of its display lists in the order Fast3D, LibUltraShip's
renderer, runs them: its address, its two words, and its name as Fast3D knows it. A list that a `G_DL` calls is
indented under it; a `G_DL` that branches (`DE01`) goes on in its target at the same depth, because the target's
`G_ENDDL` returns past the branch. The two words after a `G_TEXRECT` are its texture coordinates, which it reads
itself; they are marked so. From `build/debug`:

```
./viewer --dump
./viewer --dump --sprite
./viewer --dump --picture
```

For the head, the frame has a `G_VTX` and a `G_DL` to the game's material list for each part. A `G_DL` and the list's
first command, which names the palette's address, look like this (the addresses differ from run to run):

```
0x...  DE000000 0000...  G_DL  (call)
    0x...  FD100000 0000...  G_SETTIMG
```

`src/frame.cpp` (`WalkDisplayList`) walks the lists the way Fast3D's interpreter steps through them, so the dump lists
exactly the commands Fast3D runs.

## The debug menu and the Gfx Debugger

In the viewer's window, F1 shows or hides a menu bar. Its Debug menu opens and closes three of LibUltraShip's windows:

- **Console**: LibUltraShip's command console.
- **Stats**: the time each frame takes, and the frames per second.
- **Gfx Debugger**: the frame's display lists as a tree.

In the Gfx Debugger, the Debug button freezes the viewer on the frame it has just drawn: it shows that frame's commands
as a tree, in which a `G_DL` opens into the list it calls. Clicking a command draws the frame only up to that command,
so you can see what each one adds. Resume Game lets the viewer go on. Command names come from LibUltraShip; the texts
of the commands' fields need LibUltraShip's option `GFX_DEBUG_DISASSEMBLER`, which this build leaves off.

## Stepping through the game's functions

In VS Code (the README says how to set it up), with a Debug build:

1. Open one of the game's files with File, Open File, for example the decompilation's
   `src/main/buildCiTextureDisplayList.c`.
2. Click left of the line `obj->width = (u16)(1 << maskS);` to set a breakpoint.
3. In the Run and Debug view, choose "Debug asset viewer" and press F5. The viewer stops there while it builds the
   material list of the head's first part (and, each time F5 is pressed again, of the next part). The Variables view
   shows `obj`, the display record, and the texture's header bytes (`clampS`, `maskS` and the others); F10 runs one
   line, and the Call Stack view shows that `viewer_head_load` called the function.

The same from a terminal, with gdb, from `build/debug` (where the viewer then writes its log and settings). This stops
where the function starts, prints the texture's header, runs on by seven lines and prints the size the function has
worked out, then shows the calls that led there:

```
gdb -ex 'break buildCiTextureDisplayList' -ex run -ex 'print *obj->tex' -ex 'next 7' -ex 'print obj->width' \
    -ex 'print obj->height' -ex backtrace --args ./viewer --frames 100
```

gdb then waits at its `(gdb)` prompt: `continue` runs on, to the next part's material list, and `quit` ends.

## Where the viewer's code is

| File | What it holds |
|---|---|
| `viewer/main.cpp` | the program: its options, the frames, `--check` and `--dump` |
| `viewer/viewer_game.c` | the calls into the game's functions, compiled with the game's settings |
| `viewer/viewer_rom.c` | opening the ROM, reading ids, and checking each resource's kind |
| `viewer/viewer.h` | what the three files share, in plain C types |
| `tools/viewer_ref.py` | the check's reference, computed from the ROM |
| `src/context.cpp` | LibUltraShip's start-up, shared with hello |
| `src/frame.cpp` | matrices, reading the picture back, screenshots, and the walk through a frame's lists |
| `src/debug.cpp` | the F1 menu bar and the Gfx Debugger's frozen frame |
