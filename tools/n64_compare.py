#!/usr/bin/env python3
"""Compares the developer match with the original game run in Project64 (docs/GAME_CODE.md, "Comparing with the
original game").

    tools/n64_compare.py records N64_DIR/lockstep.bin PORT.wrlog [--binary build/debug/turnbuckle-dive]
    tools/n64_compare.py frames N64_DIR PORT_DIR [--pictures DIR]

Project64 runs the original game with td_lockstep.js, which writes N64_DIR (C:/n64/out/lockstep): lockstep.bin, the
four wrestler records at each frame hand-off of the match, in the N64's layout and byte order; dl_NNNNN.gbi, the
display lists of match frame NNNNN; fb_NNNNN.png, its picture at the N64's 480x240. The port runs the same match with
--no-idle-randoms: --wr-log writes PORT.wrlog, and --capture-match-frames writes port_NNNNN.txt and port_NNNNN.png
into PORT_DIR.

records: compares every field of every wrestler (and the random seed) at every frame, in order, and prints the first
frame where something differs, with each field that differs there and its two values, then how many frames agree.
The fields and their types come from the debug information of the port's program (gdb), so the table follows
include/wrestler.h; on the N64 the 4-byte profile pointer makes every field after it 8 bytes nearer the start.

frames: for each captured match frame, the display lists (every command in order, with the words that hold addresses
left out, since those differ between the machines) and the pictures (the port's window cut to the game's 4:3 picture
and averaged down to 480x240, then each pixel's largest channel difference). GLideN64 leaves the HUD out of the
picture it copies back to the N64's memory, so the port's pictures are best taken in a second run with --hide-group 5,
whose folder --pictures names (the display lists still come from PORT_DIR). The port captures each frame and its
neighbours, because Project64 names a frame by the last hand-off before its tasks; the best match counts.

Exit code 0 when everything compared agrees (records), or when every frame's display lists agree and its picture is
within the tolerance (frames); 1 otherwise; 2 when it cannot run.
"""
import argparse
import difflib
import json
import os
import re
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N64_RECORD = 0x3B0
PROFILE_PC, ANIM_PC, SHIFT = 0x368, 0x370, 8

# ---- records ----

LEAVES_PY = r'''
import gdb, json
def walk(t, off, name, out):
    t = t.strip_typedefs()
    if t.code == gdb.TYPE_CODE_STRUCT:
        for f in t.fields():
            walk(f.type, off + f.bitpos // 8, name + '.' + f.name if name else f.name, out)
    elif t.code == gdb.TYPE_CODE_ARRAY:
        lo, hi = t.range()
        e = t.target()
        for i in range(lo, hi + 1):
            walk(e, off + i * e.strip_typedefs().sizeof, '%s[%d]' % (name, i), out)
    else:
        kind = 'f' if t.code == gdb.TYPE_CODE_FLT else 'p' if t.code == gdb.TYPE_CODE_PTR else (
            'u' if t.is_signed is False else 'i')
        out.append([name, off, t.sizeof, kind])
out = []
walk(gdb.lookup_type('Wrestler'), 0, '', out)
print('LEAVES ' + json.dumps(out))
'''


def leaves(binary):
    """[name, pc offset, n64 offset, size, kind] of every scalar field of Wrestler, the profile pointer left out."""
    try:
        out = subprocess.run(['gdb', '-q', '-batch', '-nx', '-ex', 'python exec(%r)' % LEAVES_PY, binary],
                             capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit('n64_compare.py: gdb could not read %s: %s' % (binary, e))
    line = next((l for l in out.splitlines() if l.startswith('LEAVES ')), None)
    if line is None:
        sys.exit('n64_compare.py: no Wrestler type in %s (a Debug build is needed)' % binary)
    result = []
    for name, off, size, kind in json.loads(line[7:]):
        if kind == 'p':
            if off != PROFILE_PC:
                sys.exit('n64_compare.py: Wrestler has a pointer other than profile (%s): update the layout rule' % name)
            continue
        result.append([name, off, off - SHIFT if off >= ANIM_PC else off, size, kind])
    return result


def value(raw, kind, big):
    size = len(raw)
    order = '>' if big else '<'
    if kind == 'f':
        return struct.unpack(order + ('f' if size == 4 else 'd'), raw)[0]
    code = {1: 'b', 2: 'h', 4: 'i', 8: 'q'}[size]
    return struct.unpack(order + (code.upper() if kind == 'u' else code), raw)[0]


def read_n64(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'TDN64LS1':
        sys.exit('%s: not a td_lockstep.js trace' % path)
    size, slots = struct.unpack_from('>2I', data, 8)
    entry = 12 + size * slots
    frames = []
    for at in range(16, len(data) - entry + 1, entry):
        handoff, tick, seed = struct.unpack_from('>3I', data, at)
        frames.append((tick, seed, [data[at + 12 + i * size:at + 12 + (i + 1) * size] for i in range(slots)]))
    return frames


def read_port(path):
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import wr_log
    log = wr_log.Log(path)
    frames = []
    for i in range(log.frames):
        frame, tick, seed, phase, recs = log.frame(i)
        frames.append((tick, seed, [recs[k * log.record_size:(k + 1) * log.record_size] for k in range(4)]))
    return frames


def records(args):
    fields = leaves(args.binary)
    n64 = read_n64(args.n64)
    port = read_port(args.port)
    # Both logs start at the first frame hand-off of the match; the port's may start earlier.
    start = next((i for i, f in enumerate(port) if f[0] == n64[0][0]), None) if n64 else None
    if start is None:
        print('no frame in common: the N64 log starts at match frame %s' % (n64[0][0] if n64 else '-'))
        return 1
    port = port[start:]
    count = min(len(n64), len(port))
    print('%d frames from Project64, %d from the port, compared from match frame %d' % (len(n64), len(port), n64[0][0]))
    for i in range(count):
        (t1, s1, r1), (t2, s2, r2) = n64[i], port[i]
        diffs = []
        if t1 != t2:
            diffs.append(('match frame', t1, t2))
        if s1 != s2:
            diffs.append(('random seed', '%08X' % s1, '%08X' % s2))
        for slot in range(4):
            if value(r1[slot][2:4], 'i', True) == -1 and value(r2[slot][2:4], 'i', False) == -1:
                continue
            for name, pc, n, size, kind in fields:
                a = r1[slot][n:n + size]
                b = r2[slot][pc:pc + size]
                if a != b[::-1]:
                    diffs.append(('wrestler %d %s' % (slot, name), value(a, kind, True), value(b, kind, False)))
        if diffs:
            print('first difference at frame %d of the match (match frame %d), %d values:' % (i, t1, len(diffs)))
            for what, x, y in diffs[:args.max]:
                print('    %-40s N64 %-24s port %s' % (what, x, y))
            if len(diffs) > args.max:
                print('    ... and %d more' % (len(diffs) - args.max))
            print('the %d frames before it agree in every field' % i)
            return 1
    print('the same: %d frames, every field of every wrestler and the random seed' % count)
    return 0

# ---- frames ----


def read_png(path):
    """(width, height, rows of RGB tuples) of an 8-bit RGB or RGBA PNG."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('%s: not a PNG' % path)
    at, idat, w = 8, b'', 0
    while at < len(data):
        n, kind = struct.unpack_from('>I4s', data, at)
        body = data[at + 8:at + 8 + n]
        if kind == b'IHDR':
            w, h, depth, colour, _, _, interlace = struct.unpack('>2I5B', body)
            if depth != 8 or colour not in (2, 6) or interlace:
                sys.exit('%s: only 8-bit RGB or RGBA PNGs without interlacing are read' % path)
            bpp = 3 if colour == 2 else 4
        elif kind == b'IDAT':
            idat += body
        at += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xFF
            elif f == 2:
                line[x] = (line[x] + b) & 0xFF
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(v for p in row for v in p) for row in rows)
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def port_picture(path, tw, th):
    """The port's window cut to the 4:3 picture and averaged down to tw x th."""
    w, h, rows = read_png(path)
    pw = min(w, h * 4 // 3)
    ph = pw * 3 // 4
    x0, y0 = (w - pw) // 2, (h - ph) // 2
    out = []
    for ty in range(th):
        ya, yb = y0 + ty * ph // th, y0 + (ty + 1) * ph // th
        line = []
        for tx in range(tw):
            xa, xb = x0 + tx * pw // tw, x0 + (tx + 1) * pw // tw
            n = (yb - ya) * (xb - xa)
            s = [0, 0, 0]
            for y in range(ya, yb):
                r = rows[y]
                for x in range(xa, xb):
                    p = r[x]
                    s[0] += p[0]; s[1] += p[1]; s[2] += p[2]
            line.append((s[0] // n, s[1] // n, s[2] // n))
        out.append(line)
    return out


ADDRESS_OPS = {0x01, 0xDA, 0xDC, 0xDE, 0xFD, 0xFE, 0xFF}


def norm(depth, w0, w1):
    op = w0 >> 24
    if op in ADDRESS_OPS or (op == 0xDB and (w0 >> 16) & 0xFF == 0x06):
        w1 = None
    return (depth, w0, w1)


def read_gbi(path):
    """The commands of a td_lockstep.js capture, without each task's closing G_ENDDL."""
    cmds = []
    for line in open(path):
        if line.startswith('#'):
            continue
        p = line.split()
        depth, w0, w1 = int(p[0]), int(p[-2], 16), int(p[-1], 16)
        if depth == 0 and w0 >> 24 == 0xDF:
            continue
        cmds.append(norm(depth, w0, w1))
    return cmds


def read_dump(path):
    """The commands of the port's frame dump (frame.h's DumpDisplayList), without the frame's closing G_ENDDL."""
    cmds = []
    for line in open(path):
        m = re.match(r'^( *)0x[0-9a-fA-F]+\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{16})', line)
        if not m:
            continue
        depth = len(m.group(1)) // 4
        w0, w1 = int(m.group(2), 16), int(m.group(3), 16)
        if depth == 0 and w0 >> 24 == 0xDF:
            continue
        cmds.append(norm(depth, w0, w1))
    return cmds


def frames(args):
    names = sorted(f for f in os.listdir(args.n64) if re.match(r'dl_\d{5}\.gbi$', f))
    if not names:
        print('no captured frames in %s' % args.n64)
        return 2
    failed = False
    for name in names:
        tick = int(name[3:8])
        n64 = read_gbi(os.path.join(args.n64, name))
        best = None
        for t in (tick, tick - 1, tick + 1):
            path = os.path.join(args.port, 'port_%05d.txt' % t)
            if not os.path.exists(path):
                continue
            port = read_dump(path)
            sm = difflib.SequenceMatcher(None, n64, port, autojunk=False)
            same = sum(b.size for b in sm.get_matching_blocks())
            if best is None or same > best[1]:
                best = (t, same, port, sm)
        if best is None:
            print('match frame %d: the port captured no frame near it' % tick)
            failed = True
            continue
        t, same, port, sm = best
        print('match frame %d (port frame %d): display lists, %d commands on the N64, %d on the port, %d the same in '
              'order' % (tick, t, len(n64), len(port), same))
        shown = 0
        for op, a1, a2, b1, b2 in sm.get_opcodes():
            if op == 'equal':
                continue
            # The N64's last task ends by setting the next framebuffer (G_SETCIMG) and clearing it; on PC that clear
            # would cover the frame, so submitGraphicsTask leaves it out. The frame's own G_SETCIMG, at its start, is
            # on both sides.
            if op == 'delete' and n64[a1][1] >> 24 == 0xFF:
                print('    the N64 then clears the next framebuffer (%d commands), which the port leaves out' % (a2 - a1))
                continue
            failed = True
            if shown < args.max:
                shown += 1
                print('    %s: N64 commands %d-%d %s / port commands %d-%d %s' % (
                    op, a1, a2, ['%08X' % c[1] for c in n64[a1:min(a2, a1 + 4)]], b1, b2,
                    ['%08X' % c[1] for c in port[b1:min(b2, b1 + 4)]]))
        fb = os.path.join(args.n64, 'fb_%05d.png' % tick)
        pp = os.path.join(args.pictures or args.port, 'port_%05d.png' % t)
        if os.path.exists(fb) and os.path.exists(pp):
            w, h, a = read_png(fb)
            b = port_picture(pp, w, h)
            diff = [[max(abs(a[y][x][k] - b[y][x][k]) for k in range(3)) for x in range(w)] for y in range(h)]
            flat = sorted(v for row in diff for v in row)
            within = sum(1 for v in flat if v <= args.tolerance) / len(flat)
            print('    picture: %.1f%% of pixels within %d of 255 in every channel; median %d, 99th percentile %d' % (
                100 * within, args.tolerance, flat[len(flat) // 2], flat[len(flat) * 99 // 100]))
            if args.out:
                os.makedirs(args.out, exist_ok=True)
                write_png(os.path.join(args.out, 'n64_%05d.png' % tick), w, h, a)
                write_png(os.path.join(args.out, 'port_%05d.png' % tick), w, h, b)
                write_png(os.path.join(args.out, 'diff_%05d.png' % tick), w, h,
                          [[(min(255, 4 * v),) * 3 for v in row] for row in diff])
            if within < args.share:
                failed = True
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('records')
    r.add_argument('n64')
    r.add_argument('port')
    r.add_argument('--binary', default=os.path.join(ROOT, 'build', 'debug', 'turnbuckle-dive'))
    r.add_argument('--max', type=int, default=20, help='differences to print')
    f = sub.add_parser('frames')
    f.add_argument('n64')
    f.add_argument('port')
    f.add_argument('--tolerance', type=int, default=32, help='largest channel difference a pixel may have (0-255)')
    f.add_argument('--share', type=float, default=0.95, help='share of pixels that must be within the tolerance')
    f.add_argument('--max', type=int, default=8, help='display-list differences to print per frame')
    f.add_argument('--out', help='folder to write the two pictures and their difference into, for each frame')
    f.add_argument('--pictures', help="folder of the port's pictures, when not PORT_DIR")
    args = ap.parse_args()
    return records(args) if args.cmd == 'records' else frames(args)


if __name__ == '__main__':
    sys.exit(main())
