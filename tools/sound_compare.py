#!/usr/bin/env python3
"""Compares the port's sound with the original game's (docs/GAME_CODE.md, "Comparing the sound with the original").

The recording DIR is made in Project64 by the comparison script (td_sound.js): calls.txt and audio.raw. The port's
--sound-replay DIR then adds port_calls.txt and port_audio.raw. This prints:

- the music player's calls to the synthesizer, side by side: how many match, and the first that differ, with the
  frame, the voice and both lines;
- the sound: the two recordings lined up (the offset that matches them best), and for each stretch of time the
  loudness of each and how alike they are (the correlation, 1.0 the same), so that a difference in level, timing or
  tone shows where it is.

With --wav, it also writes original.wav and port.wav to DIR, lined up, to listen to.

Usage: tools/sound_compare.py DIR [--wav] [--show N]
"""
import argparse
import array
import math
import os
import sys
import wave

RATE = 28800


def read_calls(path):
    """The 's' lines, grouped by player frame."""
    frames = {}
    with open(path) as f:
        for line in f:
            if line.startswith('s '):
                parts = line.split()
                frames.setdefault(int(parts[1]), []).append(' '.join(parts[2:]))
    return frames


def compare_calls(directory, show):
    original = read_calls(os.path.join(directory, 'calls.txt'))
    port = read_calls(os.path.join(directory, 'port_calls.txt'))
    total = same = 0
    shown = 0
    for frame in sorted(set(original) | set(port)):
        a = original.get(frame, [])
        b = port.get(frame, [])
        total += max(len(a), len(b))
        # Within a frame, the order of the voices' calls is the player's order; compare as lists.
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else '(none)'
            y = b[i] if i < len(b) else '(none)'
            if x == y:
                same += 1
            elif shown < show:
                print(f'  frame {frame}: original {x!r}, port {y!r}')
                shown += 1
    print(f'synthesizer calls: {same} of {total} the same')
    return same == total


def read_raw(path, big_endian):
    data = array.array('h')
    with open(path, 'rb') as f:
        data.frombytes(f.read())
    if big_endian != (sys.byteorder == 'big'):
        data.byteswap()
    return data


def mono(stereo):
    return [(stereo[2 * i] + stereo[2 * i + 1]) / 2 for i in range(len(stereo) // 2)]


def first_sound(samples, level=200):
    for i, v in enumerate(samples):
        if abs(v) > level:
            return i
    return -1


def best_offset(a, b, around, span=4096, search=2048):
    """The shift of b against a (b[i + shift] ~ a[i]) that matches them best near sample `around` of a."""
    best, best_score = 0, -1.0
    seg = a[around:around + span]
    for shift in range(-search, search + 1):
        start = around + shift
        if start < 0 or start + span > len(b):
            continue
        other = b[start:start + span]
        num = sum(x * y for x, y in zip(seg, other))
        den = math.sqrt(sum(x * x for x in seg) * sum(y * y for y in other)) or 1.0
        if num / den > best_score:
            best, best_score = shift, num / den
    return best, best_score


def compare_audio(directory, write_wav):
    a = read_raw(os.path.join(directory, 'audio.raw'), True)
    b = read_raw(os.path.join(directory, 'port_audio.raw'), False)
    ma, mb = mono(a), mono(b)
    sa, sb = first_sound(ma), first_sound(mb)
    if sa < 0 or sb < 0:
        print('sound: one of the recordings is silent')
        return
    coarse = sb - sa
    shift, score = best_offset(ma, mb[coarse:] if coarse >= 0 else [0.0] * -coarse + mb, sa)
    offset = coarse + shift
    print(f'sound: the port is {offset} samples ({offset / RATE * 1000:.1f} ms) after the original '
          f'(match {score:.3f} where the sound starts)')
    step = RATE * 5
    print('  from   original   port    level   alike')
    start = sa
    while start + step <= len(ma) and start + offset + step <= len(mb):
        x = ma[start:start + step]
        y = mb[start + offset:start + offset + step]
        rx = math.sqrt(sum(v * v for v in x) / step)
        ry = math.sqrt(sum(v * v for v in y) / step)
        num = sum(p * q for p, q in zip(x, y))
        den = math.sqrt(sum(p * p for p in x) * sum(q * q for q in y)) or 1.0
        level = 20 * math.log10(ry / rx) if rx > 0 and ry > 0 else float('nan')
        print(f'  {start / RATE:5.1f}s {rx:8.0f} {ry:8.0f} {level:+6.1f} dB {num / den:6.3f}')
        start += step
    if write_wav:
        n = min(len(a) // 2 - sa, len(b) // 2 - sa - offset)
        for name, data, first in (('original.wav', a, sa), ('port.wav', b, sa + offset)):
            with wave.open(os.path.join(directory, name), 'wb') as w:
                w.setnchannels(2)
                w.setsampwidth(2)
                w.setframerate(RATE)
                part = array.array('h', data[2 * first:2 * (first + n)])
                if sys.byteorder == 'big':
                    part.byteswap()
                w.writeframes(part.tobytes())
        print(f'wrote original.wav and port.wav ({n / RATE:.1f} s each, lined up) to {directory}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dir')
    parser.add_argument('--wav', action='store_true', help='write original.wav and port.wav, lined up')
    parser.add_argument('--show', type=int, default=20, help='how many differing calls to print')
    args = parser.parse_args()
    compare_calls(args.dir, args.show)
    compare_audio(args.dir, args.wav)


if __name__ == '__main__':
    main()
