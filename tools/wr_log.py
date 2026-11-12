"""Reads the wrestler logs that turnbuckle-dive --dev-match --wr-log FILE writes (docs/GAME_CODE.md, "The developer
match"): one snapshot of the four wrestler records at every frame of a match.

    python3 tools/wr_log.py show FILE [--every N]   prints each wrestler's state, health and position every N frames
                                                    (default 300)
    python3 tools/wr_log.py diff A B [--from N]     compares two logs: prints "the same" and exits with 0, or names the
                                                    first frame, wrestler and field where they differ and exits with 1;
                                                    exits with 77 when a log is missing (its run was skipped). --from N
                                                    starts at the Nth entry (0 is the first): a second match's first
                                                    entry holds the joints the last match left (docs/GAME_CODE.md)

The format, in the PC's byte order (src/dev/dev_match.c writes it): "TDWRLOG1"; u32 record size, u32 number of
records, u32 number of fields; for each field u16 name length, the name, u32 offset, u32 size; then for every frame
u32 frame, u32 match frame (D_800E2FAC), u32 seed (xseed), i32 phase and the records. A log holds no bytes of the ROM,
but it is made from the game's data, so it stays in the build folder and is never committed.

It needs only Python 3.
"""
import struct
import sys


class Log:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        if self.data[:8] != b'TDWRLOG1':
            sys.exit('%s: not a wrestler log' % path)
        self.record_size, self.records, count = struct.unpack_from('<3I', self.data, 8)
        at = 20
        self.fields = []
        for _ in range(count):
            (n,) = struct.unpack_from('<H', self.data, at)
            name = self.data[at + 2:at + 2 + n].decode()
            offset, size = struct.unpack_from('<2I', self.data, at + 2 + n)
            self.fields.append((name, offset, size))
            at += 2 + n + 8
        self.start = at
        self.entry = 16 + self.records * self.record_size
        self.frames = (len(self.data) - at) // self.entry
        self.path = path

    def frame(self, i):
        """(frame, match frame, seed, phase, records bytes) of entry i."""
        at = self.start + i * self.entry
        frame, match, seed, phase = struct.unpack_from('<3Ii', self.data, at)
        return frame, match, seed, phase, self.data[at + 16:at + self.entry]

    def field(self, record, name):
        for n, offset, size in self.fields:
            if n == name:
                return record[offset:offset + size]
        raise KeyError(name)

    def field_at(self, offset):
        for n, o, size in self.fields:
            if o <= offset < o + size:
                return n, o, size
        return '(padding)', offset, 1


def show(path, every):
    log = Log(path)
    print('%s: %d frames, %d records of %d bytes' % (path, log.frames, log.records, log.record_size))
    for i in range(0, log.frames, every):
        frame, match, seed, phase, recs = log.frame(i)
        parts = []
        for r in range(2):
            rec = recs[r * log.record_size:(r + 1) * log.record_size]
            state = struct.unpack('<h', log.field(rec, 'state'))[0]
            health = struct.unpack('<h', log.field(rec, 'health'))[0]
            x = struct.unpack('<f', log.field(rec, 'x'))[0]
            z = struct.unpack('<f', log.field(rec, 'z'))[0]
            parts.append('state %3d health %3d at (%7.1f, %7.1f)' % (state, health, x, z))
        print('frame %5d  match %5d  phase %d  |  %s' % (frame, match, phase, '  |  '.join(parts)))


def diff(path_a, path_b, start=0):
    import os
    for path in (path_a, path_b):
        if not os.path.exists(path):
            # The run that writes it was skipped (no ROM) or failed; CTest reports exit code 77 as skipped.
            print('SKIP: %s does not exist' % path)
            return 77
    a, b = Log(path_a), Log(path_b)
    if (a.record_size, a.records, a.fields) != (b.record_size, b.records, b.fields):
        print('the two logs describe different records (made by different builds)')
        return 1
    for i in range(start, min(a.frames, b.frames)):
        fa, fb = a.frame(i), b.frame(i)
        if fa == fb:
            continue
        names = ('frame', 'match frame', 'seed', 'phase')
        for k in range(4):
            if fa[k] != fb[k]:
                print('frame %d: the %s differs: %d / %d' % (fa[0], names[k], fa[k], fb[k]))
                return 1
        for at in range(len(fa[4])):
            if fa[4][at] != fb[4][at]:
                slot, offset = divmod(at, a.record_size)
                name, o, size = a.field_at(offset)
                va = fa[4][slot * a.record_size + o:slot * a.record_size + o + size].hex()
                vb = fb[4][slot * a.record_size + o:slot * a.record_size + o + size].hex()
                print('frame %d (match frame %d): wrestler %d, field %s (+0x%X) differs: %s / %s'
                      % (fa[0], fa[1], slot, name, o, va, vb))
                return 1
    if a.frames != b.frames:
        print('the same for %d frames, then one log ends (%d / %d frames)' % (min(a.frames, b.frames), a.frames,
                                                                             b.frames))
        return 1
    print('the same: %d frames%s' % (a.frames - start, ' from entry %d' % start if start else ''))
    return 0


def main(argv):
    if len(argv) >= 2 and argv[0] == 'show':
        every = int(argv[argv.index('--every') + 1]) if '--every' in argv else 300
        show(argv[1], every)
        return 0
    if len(argv) in (3, 5) and argv[0] == 'diff':
        start = int(argv[4]) if len(argv) == 5 and argv[3] == '--from' else 0
        return diff(argv[1], argv[2], start)
    print(__doc__.split('\n\n')[1], file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
