#!/usr/bin/env python3
"""Computes, from your ROM, what the viewer's --check compares its pictures with (docs/VIEWER.md), and writes it to a
JSON file in the build folder.

It is a second, separate reader of the same data, written in Python from the formats, like tools/res_digest.py, whose
resource reader it uses: the mesh format (vertices and triangles), the textures and palettes of models and effect
sprites (CI4 and CI8 texels, RGBA16 and IA16 palettes), and the TEX picture format. It shares no code with the game's
C. For each of the check's subjects it works out the colour every pixel of the viewer's frame must have, the way the
N64 draws it: the head, seen from two opposite sides, and the effect sprite, from where the viewer's camera puts their
vertices and the texel each pixel shows; the picture from the tiles the game draws it in. For the head it also writes
the vertices and triangles the mesh holds, which the check compares with the ones the game's functions build.

Usage, from the repository folder, with the build folder of the preset you use:
    python3 tools/viewer_ref.py [ROM] --out build/debug/viewer_ref.json [--head MESH TEX PAL] [--angle DEG]
                                [--sprite TEX PAL] [--picture ID]

ROM is your ROM in any byte order. Without it, the tool uses $TD_ROM, else baserom.z64 in the current folder, else the
file given with --or-rom (CMake gives the decompilation's baserom.z64), and exits with code 77 when there is none,
which CTest reports as skipped. The file it writes holds data decoded from the game, so --out must name a file in the
build folder, which git ignores; never commit it.
"""
import argparse
import hashlib
import json
import math
import os
import pathlib
import struct
import sys

# Importing res_digest would otherwise leave a __pycache__ folder in tools/, inside the repository.
sys.dont_write_bytecode = True
import res_digest  # noqa: E402

EXIT_SKIP = 77

# The game's picture: 480x240 pixels, shown at 4:3.
WIDTH, HEIGHT = 480, 240
# The background of the viewer's frames (td::kBackgroundR, G and B in src/frame.h), in 8 bits per channel. In the
# reference, -1 stands for a pixel where the background shows.
BACKGROUND = (16, 24, 72)

# The subjects the viewer's --check draws unless told otherwise: a wrestler's face, with the mesh, texture and palette
# that the head record at 0x800319C0 gives its body part 5 (docs/VIEWER.md), turned -60 degrees, which shows it from the
# front and one side, and then the other way (120 degrees), so that every triangle the camera saw from behind faces it
# once; an effect sprite with an IA16 palette (one frame of the sprite animations the game plays on the wrestlers in a
# match); and the TEX picture 0x3.
HEAD = (0xA69, 0xA6A, 0xA68)
ANGLE = -60.0
SPRITE = (0x3EC, 0x3DF)
PICTURE = 0x3

# The viewer's camera (viewer/main.cpp, FitCamera): the ball around the model is 2 * FIT picture pixels high.
FIT = 100.0

# The effect sprite's four vertices, as the game's initializeMatchResources (src/main/initializeMatchResources.c in the
# decompilation) makes them with func_80007920: x and y, then s and t in 1/32 texel; z is 0. The game draws them as two
# triangles (func_80011764).
SPRITE_VERTICES = ((-40, -40, 16, 1008), (40, -40, 1007, 1008), (-40, 40, 16, 15), (40, 40, 1007, 15))
SPRITE_TRIANGLES = ((0, 1, 2), (2, 1, 3))

# How exact the reference is. The check reads each picture pixel back from the framebuffer pixel that holds its
# middle (td::Picture::At); with a framebuffer of at least 960x480 pixels, which the check requires, that pixel's own
# middle is at most 0.25 picture pixels away. SAMPLE adds a margin for the GPU's rounding of positions, and
# TEXEL_SLACK one for its rounding of texture coordinates. A pixel may have every colour that a point within SAMPLE
# of its middle could get, and one with more than MAX_CHOICES possible colours is not compared.
SAMPLE = 0.3
TEXEL_SLACK = 0.01
MAX_CHOICES = 6


# ---- The ROM's resources ----

def resource(rom, rid):
    """Returns resource rid's bytes, unpacked if it is compressed, read with tools/res_digest.py's reader of the
    resource table and the compression."""
    table = struct.unpack_from('>2I', rom, res_digest.RES_TABLE + (rid - 1) * 4)
    start = (table[0] & ~1) + res_digest.RES_BASE
    end = (table[1] & ~1) + res_digest.RES_BASE
    if table[0] & 1:
        size = struct.unpack_from('>I', rom, start)[0]
        return res_digest.unpack(rom[start + 4:end], size)
    return rom[start:end]


def checked(rid, decode, *args):
    """decode(*args), with the resource's id in front of the reason if it refuses them."""
    try:
        return decode(*args)
    except ValueError as error:
        raise ValueError('resource 0x%X: %s' % (rid, error)) from None


def s8(value):
    return value - 256 if value >= 128 else value


def s16(value):
    return ((value + 0x8000) & 0xFFFF) - 0x8000


def decode_mesh(data):
    """Decodes a mesh: its vertices as the 16-byte records the N64 draws (x, y, z, flag, s, t, red, green, blue,
    alpha) and its triangles as three vertex numbers each.

    The 8-byte header: byte 0 is the scale less one, byte 1 the number of vertices (its top bit is a flag), byte 2 the
    number of triangles, bytes 4 to 6 the mesh's origin in x, y and z as signed bytes in units of 16, and byte 7 the
    origin of the texture coordinates, s in its high 4 bits and t in its low 4, in units of 16 texels. Then 8 bytes per
    vertex: x, y and z as signed bytes from the origin, s and t as unsigned bytes from the texture origin, and red,
    green and blue. Then 3 bytes per triangle. A vertex's position is (offset + origin) times the scale, its texture
    coordinates are in 1/32 texel with a quarter texel added, and its alpha is 255; each is kept in 16 bits."""
    scale = data[0] + 1
    count = data[1] & 0x7F
    triangles = data[2]
    if not count or not triangles or not 0 <= len(data) - (8 + count * 8 + triangles * 3) <= 3:
        raise ValueError('it is not a mesh: %d vertices and %d triangles do not fit its %d bytes'
                         % (count, triangles, len(data)))
    origin = [s8(data[4 + axis]) * 16 for axis in range(3)]
    s_origin = (data[7] >> 4) * 16
    t_origin = (data[7] & 15) * 16
    vertices = []
    for i in range(count):
        v = data[8 + i * 8:16 + i * 8]
        vertices.append([s16((s8(v[0]) + origin[0]) * scale), s16((s8(v[1]) + origin[1]) * scale),
                         s16((s8(v[2]) + origin[2]) * scale), 0,
                         s16(((v[3] + s_origin) << 5) + 8), s16(((v[4] + t_origin) << 5) + 8),
                         v[5], v[6], v[7], 255])
    at = 8 + count * 8
    tris = [list(data[at + i * 3:at + i * 3 + 3]) for i in range(triangles)]
    return vertices, tris


class Texture:
    """A texture of the kind the game draws on models and effect sprites: an 8-byte header, then the texels row by
    row, 8 or 4 bits each (two to a byte, the first in the high bits), each a palette index. The header's bytes 4 and
    5 hold the clamp and mirror bits for s and t (the tile's G_TX_MIRROR = 1 and G_TX_CLAMP = 2), and bytes 6 and 7
    the width and height as powers of two. The game's function that loads it decides the bits: buildCiTextureDisplayList
    takes 8 when byte 2 is 1 and 4 otherwise, buildCi4TextureDisplayList always 4."""

    def __init__(self, data, bits):
        if len(data) < 8 or data[6] > 10 or data[7] > 10:
            raise ValueError('it is not a texture: its bytes 6 and 7 do not give a width and a height')
        self.bits = bits
        self.width, self.height = 1 << data[6], 1 << data[7]
        self.mode_s, self.mode_t = data[4], data[5]
        size = self.width * self.height * bits // 8
        if len(data) < 8 + size:
            raise ValueError('it is not a texture: its %dx%d texels of %d bits do not fit its %d bytes'
                             % (self.width, self.height, bits, len(data)))
        self.texels = data[8:8 + size]

    def indices(self):
        """Every palette index the texels use."""
        if self.bits == 8:
            return set(self.texels)
        return {b >> 4 for b in self.texels} | {b & 15 for b in self.texels}

    def index(self, s, t):
        """The palette index of texel (s, t), which may lie outside the texture: the tile repeats it, mirrored every
        other time when its mirror bit is set, or stops at its edges when its clamp bit is set."""
        s, t = wrap(s, self.width, self.mode_s), wrap(t, self.height, self.mode_t)
        at = t * self.width + s
        if self.bits == 8:
            return self.texels[at]
        return self.texels[at // 2] >> 4 if at % 2 == 0 else self.texels[at // 2] & 15


def wrap(i, size, mode):
    if mode & 2:
        return min(max(i, 0), size - 1)
    if mode & 1:
        i %= 2 * size
        return 2 * size - 1 - i if i >= size else i
    return i % size


def scale_5_8(value):
    """A 5-bit colour channel in 8 bits, as Fast3D widens it (SCALE_5_8)."""
    return value * 255 // 31


def palette(data, entries, ia16):
    """A palette's entries as red, green, blue and alpha, 8 bits each. An RGBA16 entry is RGBA5551: 5 bits each of
    red, green and blue, then one alpha bit (0 transparent, 1 opaque). An IA16 entry is an intensity in its high byte,
    which gives red, green and blue alike, and an alpha in its low byte. Entries are big-endian."""
    if len(data) < entries * 2:
        raise ValueError('its %d bytes are too few for the %d palette entries the texture needs' % (len(data), entries))
    colours = []
    for i in range(entries):
        entry = struct.unpack_from('>H', data, i * 2)[0]
        if ia16:
            colours.append((entry >> 8, entry >> 8, entry >> 8, entry & 0xFF))
        else:
            colours.append((scale_5_8(entry >> 11), scale_5_8((entry >> 6) & 31), scale_5_8((entry >> 1) & 31),
                            255 if entry & 1 else 0))
    return colours


# ---- Where the viewer's camera puts a model's vertices ----

def f32(x):
    """x rounded to a 32-bit float. The viewer and Fast3D work out the camera and the vertices in floats, which round
    after every step, and the reference rounds at the same steps."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def matrix_product(a, b):
    """a times b, the N64 way round, in floats added in the order C adds them (td::MultiplyMatrices in src/frame.cpp,
    MatrixMul in Fast3D)."""
    return [[f32(f32(f32(f32(a[i][0] * b[0][j]) + f32(a[i][1] * b[1][j])) + f32(a[i][2] * b[2][j]))
                 + f32(a[i][3] * b[3][j])) for j in range(4)] for i in range(4)]


def fixed_point(m):
    """m as the N64 is given it: 16.16 fixed point, each element cut toward zero (td::MatrixFromFloat), and turned
    back into floats as Fast3D reads it (the N64's RSP computes with the fixed-point numbers themselves)."""
    return [[f32(int(f32(v * 65536.0)) / 65536.0) for v in row] for row in m]


def place_vertices(vertices, angle):
    """Where the viewer's camera puts each vertex (x, y, z first): its x and y in picture pixels from the picture's top
    left corner.

    The camera (viewer/main.cpp, FitCamera and SetCamera) moves the middle of the vertices' box to the origin, turns
    the model by angle degrees about the vertical axis, and scales it so that the ball around the vertices is 2 * FIT
    pixels high. Its projection divides x by 160 and y by 120: the picture is 480x240 shown at 4:3, so a horizontal
    distance takes 1.5 times as many pixels as a vertical one, and the model keeps its shape on screen. There is no
    perspective, so every vertex keeps w = 1, and the viewport maps -1 to 1 onto the picture's width and height."""
    low = [min(v[axis] for v in vertices) for axis in range(3)]
    high = [max(v[axis] for v in vertices) for axis in range(3)]
    middle = [f32((low[axis] + high[axis]) / 2.0) for axis in range(3)]
    radius = 0.0
    for v in vertices:
        d = [f32(v[axis] - middle[axis]) for axis in range(3)]
        radius = max(radius, f32(math.sqrt(f32(f32(f32(d[0] * d[0]) + f32(d[1] * d[1])) + f32(d[2] * d[2])))))
    scale = f32(FIT / max(radius, 1.0))
    radians = f32(f32(f32(angle) * f32(3.14159265358979)) / 180.0)
    c, s = f32(math.cos(radians)), f32(math.sin(radians))
    move = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [-middle[0], -middle[1], -middle[2], 1]]
    turn = [[c, 0, -s, 0], [0, 1, 0, 0], [s, 0, c, 0], [0, 0, 0, 1]]
    size = [[scale, 0, 0, 0], [0, scale, 0, 0], [0, 0, scale, 0], [0, 0, 0, 1]]
    projection = [[f32(1 / 160), 0, 0, 0], [0, f32(1 / 120), 0, 0], [0, 0, -1 / 256, 0], [0, 0, 0, 1]]
    both = matrix_product(fixed_point(matrix_product(matrix_product(move, turn), size)), fixed_point(projection))
    placed = []
    for v in vertices:
        x, y, w = (f32(f32(f32(f32(v[0] * both[0][k]) + f32(v[1] * both[1][k])) + f32(v[2] * both[2][k]))
                       + both[3][k]) for k in (0, 1, 3))
        placed.append(((x / w + 1) * WIDTH / 2, (1 - y / w) * HEIGHT / 2))
    return placed


def texel_coordinates(s, t):
    """A vertex's texture coordinates in texels: its s and t (1/32 texel) times the scale 0xFFFF that the viewer's
    frames give gSPTexture, over 0x10000 and rounded down, as Fast3D and the N64's RSP scale them."""
    return ((s * 0xFFFF) >> 16) / 32.0, ((t * 0xFFFF) >> 16) / 32.0


# ---- The colours of a frame of textured triangles ----

def draw_triangles(placed, texcoords, triangles, texture, colours, blend, cull_back):
    """Works out the colours each pixel of the frame may have when the triangles are drawn in list order over the
    background, without a depth buffer, as in the viewer's frames: a later triangle covers an earlier one.

    A pixel shows a triangle when its middle is inside it, and then the texel its texture coordinates fall in there:
    they vary linearly across a triangle, because the camera has no perspective, and the viewer samples textures
    without filtering. With cull_back, a triangle whose vertices go clockwise on screen is not drawn (the N64's
    G_CULL_BACK, which the game's triangle list for a model turns on). A texel's colour comes from colours, the
    palette. blend False (the head): a texel whose alpha is 0 is left out, so what was drawn there before shows, and
    any other texel is drawn as it is (the render state the viewer gives models). blend True (the effect sprite): each
    texel is mixed with what is there by its alpha, colour x alpha + behind x (1 - alpha), in 8 bits per channel as
    the GPU's framebuffer keeps them.

    Each pixel gets the set of colours it may have: where a triangle's edge or a texel's edge passes within SAMPLE of
    the pixel's middle, each side's colour (see SAMPLE). Returns a dictionary from (x, y) to that set, for the pixels
    the triangles may reach (None where there are more than MAX_CHOICES), and the numbers of the triangles drawn."""
    state = {}
    drawn = []
    for number, (a, b, c) in enumerate(triangles):
        (x1, y1), (x2, y2), (x3, y3) = placed[a], placed[b], placed[c]
        # Twice the triangle's area, positive when its vertices go clockwise on screen, where y grows downwards.
        area2 = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1)
        if abs(area2) < 1e-9:
            continue
        maybe = False
        if cull_back:
            if area2 > 1e-3:
                continue
            # A sliver whose turn the GPU's rounding could decide either way: it may or may not be drawn.
            maybe = area2 > -1e-3
        drawn.append(number)
        sign = -1.0 if area2 > 0 else 1.0
        # Each edge as a line: (point, direction, length); a point is outside when the edge's function is positive.
        edges = []
        for (ax, ay), (bx, by) in (((x1, y1), (x2, y2)), ((x2, y2), (x3, y3)), ((x3, y3), (x1, y1))):
            length = math.hypot(bx - ax, by - ay)
            edges.append((ax, ay, bx - ax, by - ay, length, SAMPLE * (abs(bx - ax) + abs(by - ay)) / length))
        # The texture coordinates as linear functions of the picture position.
        (u1, v1), (u2, v2), (u3, v3) = texcoords[a], texcoords[b], texcoords[c]
        den = (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1)
        dudx = ((u2 - u1) * (y3 - y1) - (u3 - u1) * (y2 - y1)) / den
        dudy = ((u3 - u1) * (x2 - x1) - (u2 - u1) * (x3 - x1)) / den
        dvdx = ((v2 - v1) * (y3 - y1) - (v3 - v1) * (y2 - y1)) / den
        dvdy = ((v3 - v1) * (x2 - x1) - (v2 - v1) * (x3 - x1)) / den
        reach_u = SAMPLE * (abs(dudx) + abs(dudy)) + TEXEL_SLACK
        reach_v = SAMPLE * (abs(dvdx) + abs(dvdy)) + TEXEL_SLACK

        left = max(0, int(math.floor(min(x1, x2, x3) - SAMPLE)))
        right = min(WIDTH - 1, int(math.floor(max(x1, x2, x3) + SAMPLE)))
        top = max(0, int(math.floor(min(y1, y2, y3) - SAMPLE)))
        bottom = min(HEIGHT - 1, int(math.floor(max(y1, y2, y3) + SAMPLE)))
        for py in range(top, bottom + 1):
            cy = py + 0.5
            for px in range(left, right + 1):
                cx = px + 0.5
                # How far the middle is from each edge, outwards: the pixel is outside when its whole neighbourhood
                # is outside one edge, inside when it is inside all three, and on an edge otherwise.
                outside = False
                inside = not maybe
                for ax, ay, dx, dy, length, reach in edges:
                    distance = sign * (dx * (cy - ay) - dy * (cx - ax)) / length
                    if distance > reach:
                        outside = True
                        break
                    if distance > -reach:
                        inside = False
                if outside:
                    continue
                before = state.get((px, py), {BACKGROUND})
                if before is None:
                    continue
                u = u1 + dudx * (cx - x1) + dudy * (cy - y1)
                v = v1 + dvdx * (cx - x1) + dvdy * (cy - y1)
                texels = [(s, t) for t in range(math.floor(v - reach_v), math.floor(v + reach_v) + 1)
                          for s in range(math.floor(u - reach_u), math.floor(u + reach_u) + 1)]
                if len(texels) > MAX_CHOICES:
                    state[(px, py)] = None
                    continue
                after = set() if inside else set(before)
                for s, t in texels:
                    red, green, blue, alpha = colours[texture.index(s, t)]
                    if blend:
                        after |= {tuple(round(colour * alpha / 255 + behind * (1 - alpha / 255))
                                        for colour, behind in zip((red, green, blue), old)) for old in before}
                    elif alpha == 0:
                        after |= before
                    else:
                        after.add((red, green, blue))
                state[(px, py)] = after if len(after) <= MAX_CHOICES else None
    return state, drawn


def pixel_table(state):
    """The frame's pixels in the form the check reads: 'colour_sets', a list of sets of colours, and 'pixels', 240
    rows of 480 numbers, each the place of the pixel's set in that list. A colour is (red << 10) | (green << 5) | blue
    in 5 bits each, as the picture reads back (the top 5 of the 8 bits the GPU keeps); -1 means the background shows.
    An empty set means the pixel is not compared."""
    sets, where = [], {}
    rows = []
    for py in range(HEIGHT):
        row = []
        for px in range(WIDTH):
            possible = state.get((px, py), {BACKGROUND})
            if possible is None:
                key = ()
            else:
                key = tuple(sorted(-1 if c == BACKGROUND else ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3)
                                   for c in possible))
            if key not in where:
                where[key] = len(sets)
                sets.append(list(key))
            row.append(where[key])
        rows.append(row)
    return {'colour_sets': sets, 'pixels': rows}


# ---- The subjects ----

def head_reference(rom, mesh, tex, pal, angle):
    """The head: the mesh's vertices and triangles, and the frame seen at angle and at angle + 180 degrees. The game's
    buildCiTextureDisplayList loads the texture as CI8 or CI4 by its byte 2, and the viewer reads the palette as
    RGBA16, as the game does for its models."""
    vertices, triangles = checked(mesh, decode_mesh, resource(rom, mesh))
    data = resource(rom, tex)
    texture = checked(tex, Texture, data, 8 if len(data) > 2 and data[2] == 1 else 4)
    colours = checked(pal, palette, resource(rom, pal), 256 if texture.bits == 8 else 16, False)
    texcoords = [texel_coordinates(v[4], v[5]) for v in vertices]
    views = []
    for turn in (angle, angle + 180.0):
        state, drawn = draw_triangles(place_vertices(vertices, turn), texcoords, triangles, texture, colours,
                                      blend=False, cull_back=True)
        views.append(dict(angle=turn, front=drawn, **pixel_table(state)))
    return {'mesh': mesh, 'texture': tex, 'palette': pal, 'vertices': vertices, 'triangles': triangles,
            'views': views}


def sprite_reference(rom, tex, pal):
    """The effect sprite: the game's buildCi4TextureDisplayList loads the texture as CI4 with 16 palette entries, and
    the commands of func_80011764 around it read the palette as IA16 and draw the four vertices as two triangles,
    blended by alpha, without culling. The viewer gives it the primitive colour white, so the texel's colour and alpha
    are drawn as they are, and turns it to face the camera (angle 0)."""
    texture = checked(tex, Texture, resource(rom, tex), 4)
    colours = checked(pal, palette, resource(rom, pal), 16, True)
    vertices = [(x, y, 0) for x, y, _, _ in SPRITE_VERTICES]
    texcoords = [texel_coordinates(s, t) for _, _, s, t in SPRITE_VERTICES]
    state, _ = draw_triangles(place_vertices(vertices, 0.0), texcoords, SPRITE_TRIANGLES, texture, colours,
                              blend=True, cull_back=False)
    return dict(texture=tex, palette=pal, **pixel_table(state))


def picture_reference(rom, pid):
    """Every pixel of the frame in which the viewer draws TEX picture pid.

    A TEX picture starts with "TEX" and a zero byte, its width and height (big-endian 16-bit), its bits per texel
    (byte 8) and its number of palette entries (16-bit at byte 10); its palette of RGBA16 entries starts at byte 16, and
    its texels follow, row by row, two to a byte with the first in the high bits when they are 4-bit. The viewer puts
    the picture in the middle of the frame, or at its edge when the picture is taller or wider than the frame. The
    texel's alpha bit decides between its colour and the background, which shows through where it is 0.

    How the N64 draws it: the game draws a picture in tiles 64 texels wide and 48 rows high (24 for 8-bit pictures),
    each a texture rectangle from its top left corner up to, not including, the next tile's, and never past the
    picture's last column or row, which are left out. A tile's texels are loaded starting one texel to the left of the
    tile and one row above it (none at the picture's left and top edges). 4-bit texels are loaded two to a byte, from
    the byte that holds that first texel, so when that texel is the second of its byte, the load starts one texel
    further left still; the rectangle's texture coordinates count from the tile's first texel, so every texel of such
    a tile shows one place to the right of where the picture has it. This is the case for every tile but those at the
    left edge, because a tile starts at a multiple of 64 and so its first texel, one to the left, is odd. The game cuts
    every rectangle at the frame's right and bottom edges. A texture rectangle covers whole pixels, one texel each, so
    every pixel has exactly one colour."""
    data = resource(rom, pid)
    if data[:4] != b'TEX\0':
        raise ValueError('resource 0x%X is not a TEX picture' % pid)
    width, height = struct.unpack_from('>HH', data, 4)
    bits = data[8]
    entries = struct.unpack_from('>H', data, 10)[0]
    if (bits, entries) not in ((4, 16), (8, 256)):
        raise ValueError('resource 0x%X is a %dx%d picture of %d bits with %d colours; the reference handles 4-bit '
                         'pictures with 16 colours and 8-bit ones with 256' % (pid, width, height, bits, entries))
    colours = palette(data[16:], entries, False)
    texels = data[16 + entries * 2:]
    x0, y0 = max(0, (WIDTH - width) // 2), max(0, (HEIGHT - height) // 2)
    rows = 48 if bits == 4 else 24

    def index(s, t):
        if bits == 8:
            return texels[t * width + s]
        byte = texels[(t * width + s) // 2]
        return byte >> 4 if s % 2 == 0 else byte & 15

    state = {}
    for top in range(0, height, rows):
        for left in range(0, width, 64):
            shift = 1 if bits == 4 and left > 0 else 0
            for y in range(y0 + top, min(y0 + top + rows, y0 + height - 1, HEIGHT)):
                for x in range(x0 + left, min(x0 + left + 64, x0 + width - 1, WIDTH)):
                    red, green, blue, alpha = colours[index(x - x0 - shift, y - y0)]
                    if alpha:
                        state[(x, y)] = {(red, green, blue)}
    return dict(id=pid, x=x0, y=y0, width=width, height=height, **pixel_table(state))


# ---- The command line ----

def find_rom(given, fallback):
    """The ROM's path as the viewer finds it (src/rom.h): the one given, else $TD_ROM, else baserom.z64 in the current
    folder, else fallback. None when nothing was named and none of them exists."""
    if given:
        return given
    if os.environ.get('TD_ROM'):
        return os.environ['TD_ROM']
    for candidate in ('baserom.z64', fallback):
        if candidate and pathlib.Path(candidate).is_file():
            return candidate
    return None


def resource_id(text):
    value = int(text, 16) if text.lower().startswith('0x') else int(text, 10)
    if not 1 <= value <= res_digest.RES_COUNT:
        raise argparse.ArgumentTypeError('a resource id is 1 to 0x%X' % res_digest.RES_COUNT)
    return value


def main():
    parser = argparse.ArgumentParser(description='Computes the reference for viewer --check from the ROM, without the '
                                                 'game\'s code.')
    parser.add_argument('rom', nargs='?', help='your ROM, in any byte order')
    parser.add_argument('--or-rom', metavar='PATH', help='the ROM to use when none is given, set or in this folder')
    parser.add_argument('--out', metavar='FILE', required=True,
                        help='where to write the reference: a file in the build folder, such as '
                             'build/debug/viewer_ref.json')
    parser.add_argument('--head', metavar='ID', nargs=3, type=resource_id, default=list(HEAD),
                        help='the mesh, its texture and its palette')
    parser.add_argument('--angle', type=float, default=ANGLE,
                        help='the head\'s first angle in degrees; the second is 180 degrees on')
    parser.add_argument('--sprite', metavar='ID', nargs=2, type=resource_id, default=list(SPRITE),
                        help='the effect sprite\'s texture and its IA16 palette')
    parser.add_argument('--picture', metavar='ID', type=resource_id, default=PICTURE, help='the TEX picture')
    args = parser.parse_args()

    path = find_rom(args.rom, args.or_rom)
    if path is None:
        print('SKIP: no ROM found (give its path, set TD_ROM to it, or put it in the decompilation\'s folder as '
              'baserom.z64)')
        sys.exit(EXIT_SKIP)
    try:
        rom = res_digest.to_z64(pathlib.Path(path).read_bytes())
    except OSError as error:
        sys.exit('ROM %s: %s' % (path, error.strerror))
    if rom is None:
        sys.exit('%s: not an N64 ROM' % path)
    if hashlib.sha1(rom).hexdigest() != res_digest.ROM_SHA1:
        sys.exit('%s: an N64 ROM, but not the supported one (SHA-1 %s expected)' % (path, res_digest.ROM_SHA1))

    try:
        reference = {
            'tool': 'tools/viewer_ref.py',
            'head': head_reference(rom, *args.head, args.angle),
            'sprite': sprite_reference(rom, *args.sprite),
            'picture': picture_reference(rom, args.picture),
        }
    except (ValueError, IndexError, struct.error) as error:
        sys.exit('%s: %s' % (path, error))
    pathlib.Path(args.out).write_text(json.dumps(reference, separators=(',', ':')))
    head = reference['head']
    print('reference for mesh 0x%X (%d vertices, %d triangles), texture 0x%X, palette 0x%X at %g and %g degrees, '
          'sprite 0x%X with palette 0x%X, and picture 0x%X, from %s, written to %s'
          % (head['mesh'], len(head['vertices']), len(head['triangles']), head['texture'], head['palette'],
             head['views'][0]['angle'], head['views'][1]['angle'], args.sprite[0], args.sprite[1], args.picture, path,
             args.out))


if __name__ == '__main__':
    main()
