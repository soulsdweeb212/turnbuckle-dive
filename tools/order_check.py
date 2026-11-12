#!/usr/bin/env python3
"""Turns the byte-order check's list (src/dev/order_check.c) into file names and lines.

    tools/order_check.py build/order/turnbuckle-dive order_check.txt [more lists...]
    tools/order_check.py --run build/order/turnbuckle-dive --dev-match --headless --no-watchdog

The check writes, for each place in the game code whose read gave a different value than the N64 would have read, the
code addresses of the read and of the write that put the bytes there. This prints one block per reading line, most
often first: how many times it read a value that differs from the N64's, the first frame it did, the widths, and the
lines that wrote the bytes. Several lists (from runs with other seeds) are added together. It needs addr2line, from
binutils. Exit code 0 when the lists are empty, 1 when they name a line, 2 when it cannot run.

With --run, it first runs the program with the arguments after it, the list going to order_check_run.txt next to the
program, and then reads that list. When the program fails, it says so and passes on the program's exit code (77 is
"no ROM", which CTest counts as skipped).
"""
import collections
import os
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    if sys.argv[1] == "--run":
        exe = sys.argv[2]
        path = os.path.join(os.path.dirname(os.path.abspath(exe)), "order_check_run.txt")
        if os.path.exists(path):
            os.remove(path)
        code = subprocess.run([exe] + sys.argv[3:], env=dict(os.environ, TD_ORDER_CHECK_OUT=path)).returncode
        if code != 0:
            print(f"order_check.py: {exe} ended with exit code {code}", file=sys.stderr)
            return code
        paths = [path]
    else:
        exe, paths = sys.argv[1], sys.argv[2:]
    rows = []
    for path in paths:
        try:
            with open(path, encoding="utf-8") as f:
                for line in f:
                    if line.startswith("#") or not line.strip():
                        continue
                    count, frame, rsite, rwidth, wsite, wwidth, woff, addr = line.split("\t")
                    rows.append((int(count), int(frame), int(rsite, 16), int(rwidth), int(wsite, 16), int(wwidth),
                                 int(woff), addr.strip()))
        except OSError as e:
            print(f"order_check.py: {e}", file=sys.stderr)
            return 2
    if not rows:
        print("no read differs from the N64")
        return 0
    # The sites are return addresses of the check's calls: one byte back is inside the call, on the line that made it.
    sites = sorted({r[2] for r in rows} | {r[4] for r in rows if r[4]})
    try:
        out = subprocess.run(["addr2line", "-e", exe, "-f", "-C"], input="".join(f"{s - 1:#x}\n" for s in sites),
                             capture_output=True, text=True, check=True).stdout.splitlines()
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"order_check.py: addr2line: {e}", file=sys.stderr)
        return 2
    where = {}
    for i, s in enumerate(sites):
        func, loc = out[2 * i], out[2 * i + 1]
        loc = loc.split(" (discriminator")[0]
        for root in (os.path.join(os.path.dirname(os.path.abspath(exe)), "..", "..", "..", "source"),):
            root = os.path.normpath(root) + os.sep
            if loc.startswith(root):
                loc = loc[len(root):]
        where[s] = f"{loc} ({func})"
    reads = collections.defaultdict(lambda: [0, None, set(), collections.Counter()])
    for count, frame, rsite, rwidth, wsite, wwidth, woff, addr in rows:
        r = reads[where[rsite]]
        r[0] += count
        r[1] = frame if r[1] is None else min(r[1], frame)
        r[2].add(rwidth)
        writer = where.get(wsite, "the port's conversion (src/res_convert.c or src/game_vars.c)")
        r[3][f"{writer}: a {wwidth}-byte value, byte {woff}"] += count
    for loc, (count, frame, widths, writers) in sorted(reads.items(), key=lambda kv: -kv[1][0]):
        print(f"{loc}: {count} reads of {'/'.join(map(str, sorted(widths)))} bytes, first at frame {frame}")
        for w, n in writers.most_common():
            print(f"    written by {w} ({n})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
