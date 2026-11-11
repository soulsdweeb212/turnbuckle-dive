#!/usr/bin/env python3
"""Reads every resource of your ROM without any of the game's code, and prints the digest that the rom_loader test
(tests/rom_loader/main.c) compares the game's loader with, RES_DIGEST. With --manifest it also writes a manifest, one
line per resource, which the test reads through the environment variable TD_MANIFEST to say which resources differ.

It is a second, separate reader of the same data, written in Python from the format: the game's resource table, and
the compression that the game's func_800037F4 unpacks. When its digest and the test's agree, the game's loader,
compiled for the PC, reads every resource exactly as this script does. docs/GAME_CODE.md says when to run it.

Usage, from the repository folder:
    python3 tools/res_digest.py ROM [--manifest FILE]

ROM is your ROM, in any of the three byte orders (.z64, .v64, .n64). A manifest lists hashes of game data, so write it
into the build folder, which git ignores, and never commit it.
"""
import argparse
import hashlib
import pathlib
import re
import struct
import sys

# The one supported ROM, by the SHA-1 of its bytes in .z64 order (TD_ROM_SHA1 in src/rom.h).
ROM_SHA1 = 'e1711a2511394b9357b5f1ac8ca5cc17bd674836'

# Where the resources are, as ROM addresses (offsets into the ROM). The decompilation's include/port.h has the same
# values: ROM_RES_BASE, ROM_RES_TABLE and RES_COUNT.
RES_BASE = 0x000DAC50   # the resource archive; the table's offsets count from here
RES_TABLE = 0x00CE2752  # the table: one big-endian 32-bit word per resource id, plus one that ends the last resource
RES_COUNT = 0x1DE9      # resource ids run from 1 to RES_COUNT

# The test that this script checks, and the line in it that holds the known digest.
TEST_FILE = pathlib.Path(__file__).resolve().parent.parent / 'tests' / 'rom_loader' / 'main.c'
DIGEST_LINE = re.compile(r'#define RES_DIGEST "([0-9a-f]{40})"')


def to_z64(rom):
    """Returns the ROM in .z64 order (big-endian, as the cartridge stores it). The first word of every N64 ROM is
    0x80371240; the order its four bytes appear in tells the file's byte order."""
    if len(rom) % 4 != 0:
        return None
    head = rom[:4]
    if head == b'\x80\x37\x12\x40':
        return rom
    swapped = bytearray(len(rom))
    if head == b'\x37\x80\x40\x12':
        # .v64: the two bytes of every 16-bit half are swapped.
        swapped[0::2] = rom[1::2]
        swapped[1::2] = rom[0::2]
        return bytes(swapped)
    if head == b'\x40\x12\x37\x80':
        # .n64: the four bytes of every 32-bit word are in reverse order.
        for k in range(4):
            swapped[k::4] = rom[3 - k::4]
        return bytes(swapped)
    return None


def unpack(stream, size):
    """Unpacks one compressed resource: stream is its compressed data after the size word, and the result is exactly
    size bytes.

    The compression is LZSS with a 4 KiB window. The stream is a series of groups: a flag byte, then one item for each
    of its 8 bits, lowest bit first. A set bit is one byte, copied to the output as it is. A clear bit is a two-byte
    reference to output written earlier: 12 bits give a place in the 4 KiB window and 4 bits a length of 3 to 18 bytes.
    The output starts at place 0xFEE of the window, 0x12 bytes before its end (the layout of the classic LZSS), and a
    reference to a place before the output's start gives zero bytes.
    """
    out = bytearray()
    src = 0
    flags = 0
    while len(out) < size:
        # After the 8 bits of a flag byte are used, the marker bits above them are gone: read the next flag byte.
        flags >>= 1
        if not flags & 0x100:
            flags = stream[src] | 0xFF00
            src += 1
        if flags & 1:
            out.append(stream[src])
            src += 1
            continue
        low, high = stream[src], stream[src + 1]
        src += 2
        place = low | (high & 0xF0) << 4
        length = (high & 0x0F) + 3
        # How far back the reference reaches, from 1 to 0x1000 bytes before the output's end.
        back = ((place + 0x12) & 0xFFF) - (len(out) & 0xFFF)
        if back >= 0:
            back -= 0x1000
        for _ in range(length):
            at = len(out) + back
            out.append(out[at] if at >= 0 else 0)
    if len(out) != size:
        # The game's func_800037F4 would write these bytes past the end of the resource's buffer.
        raise ValueError('its last reference ends %d bytes past its size' % (len(out) - size))
    return bytes(out)


# The characters' resources, which the port turns into the PC's byte order as they load (src/res_convert.c explains
# the layout; this is the same conversion, written separately): triples of animation list, profile and move table from
# id 0x13F to 0x20E.
CHAR_FIRST, CHAR_LAST = 0x13F, 0x20E
# The move table every wrestler shares, read as 32-bit words like the characters' move tables.
SHARED_MOVES = 0x126


def swapped(data, width):
    """data with the bytes of each width-byte value reversed."""
    return b''.join(data[i:i + width][::-1] for i in range(0, len(data), width))


def convert(rid, data):
    """A resource's bytes as the port leaves them after port_res_loaded."""
    if rid == SHARED_MOVES:
        return swapped(data, 4)
    if not CHAR_FIRST <= rid <= CHAR_LAST:
        return data
    kind = (rid - CHAR_FIRST) % 3
    if kind == 0:  # animation list: 16-bit ids
        return swapped(data, 2)
    if kind == 1:  # profile: bytes, then 16-bit values from +0x24 to +0x37, then a 64-bit value at +0x38
        return data[:0x24] + swapped(data[0x24:0x38], 2) + data[0x38:0x40][::-1]
    return swapped(data, 4)  # move table: 32-bit words


def resources(rom):
    """Yields (id, start, end, size, packed, data) for every resource: the descriptor the game's loader writes for it
    (lookupResourceDescriptor) and the bytes it loads (func_80003684), after the port's conversion (convert)."""
    table = struct.unpack_from('>%dI' % (RES_COUNT + 1), rom, RES_TABLE)
    for rid in range(1, RES_COUNT + 1):
        # A resource ends where the next one starts. Bit 0 of a resource's own word says that it is compressed; it is
        # not part of the address.
        word, next_word = table[rid - 1], table[rid]
        start = (word & ~1) + RES_BASE
        end = (next_word & ~1) + RES_BASE
        if not start <= end <= len(rom):
            raise ValueError('resource 0x%04X: ROM 0x%07X..0x%07X is not inside the ROM' % (rid, start, end))
        if word & 1:
            # Compressed: the data starts with the unpacked size, a big-endian 32-bit word, then the compressed stream.
            packed = end - start
            size = struct.unpack_from('>I', rom, start)[0]
            try:
                data = unpack(rom[start + 4:end], size)
            except IndexError:
                raise ValueError('resource 0x%04X: its compressed data ends too early' % rid) from None
            except ValueError as error:
                raise ValueError('resource 0x%04X: %s' % (rid, error)) from None
        else:
            packed = 0
            size = end - start
            data = rom[start:end]
        yield rid, start, end, size, packed, convert(rid, data)


def main():
    parser = argparse.ArgumentParser(description='Computes the rom_loader test\'s digest from the ROM, without the '
                                                 'game\'s code.')
    parser.add_argument('rom', help='your ROM, in any byte order')
    parser.add_argument('--manifest', metavar='FILE', help='also write the manifest to FILE (in the build folder)')
    args = parser.parse_args()

    rom = to_z64(pathlib.Path(args.rom).read_bytes())
    if rom is None:
        sys.exit('%s: not an N64 ROM' % args.rom)
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        sys.exit('%s: an N64 ROM, but not the supported one (SHA-1 %s expected)' % (args.rom, ROM_SHA1))

    # The digest, as the comment on RES_DIGEST in the test defines it: for each resource, its descriptor (start, end,
    # size, packed) as four big-endian 32-bit words, then its bytes.
    digest = hashlib.sha1()
    lines = []
    compressed = 0
    total = 0
    try:
        for rid, start, end, size, packed, data in resources(rom):
            digest.update(struct.pack('>4I', start, end, size, packed))
            digest.update(data)
            compressed += 1 if packed else 0
            total += size
            lines.append('%04x %07x %07x %d %06x %s\n' % (rid, start, end, 1 if packed else 0, size,
                                                          hashlib.sha1(data).hexdigest()))
    except ValueError as error:
        sys.exit('%s: %s' % (args.rom, error))

    print('%d resources, %d of them compressed, %d bytes' % (RES_COUNT, compressed, total))
    print('digest %s' % digest.hexdigest())
    known = DIGEST_LINE.search(TEST_FILE.read_text()) if TEST_FILE.is_file() else None
    if known is not None:
        same = known.group(1) == digest.hexdigest()
        print('RES_DIGEST in %s: %s (%s)' % (TEST_FILE.relative_to(TEST_FILE.parents[2]), known.group(1),
                                             'the same' if same else 'DIFFERENT'))
    if args.manifest:
        pathlib.Path(args.manifest).write_text(''.join(lines))
        print('manifest: %d lines written to %s' % (len(lines), args.manifest))


if __name__ == '__main__':
    main()
