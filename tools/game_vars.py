"""Writes the C file that defines the game's variables for the PC, from the decompilation's port/game_vars.tsv
(docs/GAME_CODE.md, "The game's variables"). CMake runs it when it configures; it needs only Python 3.

    python3 tools/game_vars.py MAP OUT.c LINK.txt

MAP is the decompilation's port/game_vars.tsv, which its tools/port_data.py writes and explains: one line per variable,
with its N64 address, its N64 section, its storage, its size and its swap pattern. This tool writes:

- OUT.c, compiled with the port's code:
  - one array per N64 section ("block" storage): the variables that hold no address sit in it at their N64 offsets.
    Each such variable is a name for an address inside the array, set in assembly (".set NAME, block + offset"),
    because C cannot give a name to the middle of an array;
  - an array of its own for each variable that holds an address ("own" storage), and for the few the port provides
    ("port" storage; their sizes and starting values are in PORT below);
  - the tables that src/game_vars.c reads to fill the sections from the ROM: each section's place in the ROM and in
    N64 memory, and each variable's swap pattern.
  It holds no byte of the game: the sections start as zeros and are filled from the user's ROM at start-up.
- LINK.txt: one linker option per line, which CMake adds to the programs that link the game code. A function's second
  name (an old name that some game files still call, "function" in the map) cannot be set in C or in an object file,
  so the linker sets it.
"""
from __future__ import annotations

import sys
from pathlib import Path

# The variables the port provides: N64 hardware or boot state, not game data. Size in bytes and starting value.
PORT = {
    # Set by the N64's boot code from the console: 1 is NTSC, the only region of the supported ROM.
    "osTvType": (4, "1"),
    # The RSP's boot code, whose address and size the game puts in each graphics task. The PC runs no RSP code, so only
    # the address is used; the size is the N64's (0xD0).
    "rspbootTextStart": (0xD0, None),
    # The three framebuffers. func_80003098 clears each as 0xE100 words, so each gets that many bytes.
    "D_80147000": (0x38400, None),
    "D_8017F400": (0x38400, None),
    "D_801B7800": (0x38400, None),
}

SECTION_ORDER = ["main.data", "main.bss", "ovl1.data", "ovl2.data", "ovl2.bss"]
# Each section's N64 address range, its ROM offset (None: zeros) and which overlay it belongs to (0: main). The same
# values as SECTIONS in the decompilation's tools/port_data.py, which says where they come from.
SECTIONS = {
    "main.data": (0x8002BE30, 0x8003BAD0, 0x2CA30, 0),
    "main.bss": (0x8003BAD0, 0x8007FCE0, None, 0),
    "ovl1.data": (0x800C5AD0, 0x800D6D30, 0x72240, 1),
    "ovl2.data": (0x800E2B10, 0x800E77B0, 0xD5FB0, 2),
    "ovl2.bss": (0x800E77B0, 0x800FAFA0, None, 2),
}


def c_name(section: str) -> str:
    return "td_" + section.replace(".", "_")


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    src, out, link = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    rows = []
    for number, line in enumerate(src.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) != 6:
            sys.exit(f"{src}:{number}: expected 6 columns, found {len(f)}")
        rows.append((f[0], int(f[1], 16), f[2], f[3], f[4], f[5]))

    c = ["/* Written by turnbuckle-dive's tools/game_vars.py from the decompilation's port/game_vars.tsv; do not edit.",
         " * docs/GAME_CODE.md, \"The game's variables\", explains it. */",
         "#include <stdint.h>",
         "",
         '#include "game_vars.h"',
         "",
         "/* An assembly name: Mach-O (macOS) puts an underscore before every C name. */",
         "#if defined(__APPLE__)",
         '#define TD_ASM(name) "_" #name',
         "#else",
         "#define TD_ASM(name) #name",
         "#endif",
         "/* A name for the address block + offset. On ELF (Linux) it also gets the type and size a variable has, which",
         " * debuggers and tools such as nm show. */",
         "#if defined(__ELF__)",
         "#define TD_AT(name, block, offset, size) __asm__(\".globl \" TD_ASM(name) \"\\n.set \" TD_ASM(name) \", \" \\",
         "    TD_ASM(block) \" + \" #offset \"\\n.type \" TD_ASM(name) \", @object\\n.size \" TD_ASM(name) \", \" #size)",
         "#else",
         "#define TD_AT(name, block, offset, size) __asm__(\".globl \" TD_ASM(name) \"\\n.set \" TD_ASM(name) \", \" \\",
         "    TD_ASM(block) \" + \" #offset)",
         "#endif",
         ""]

    c.append("/* The N64 sections, each as one block of PC memory (\"block\" storage). */")
    for s in SECTION_ORDER:
        lo, hi, _rom, _ovl = SECTIONS[s]
        c.append(f"_Alignas(16) uint8_t {c_name(s)}[{hi - lo:#x}];")
    c.append("")

    owns: dict[str, list] = {s: [] for s in SECTION_ORDER}
    swaps: list[tuple[int, int]] = []
    var_rows: dict[str, list[str]] = {s: [] for s in SECTION_ORDER}
    links = []
    c.append("/* The variables, in N64 address order. */")
    for name, addr, sec, storage, size, swap in rows:
        if storage == "block":
            lo = SECTIONS[sec][0]
            c.append(f"TD_AT({name}, {c_name(sec)}, {addr - lo:#x}, {int(size, 16):#x});")
            if swap != "-":
                period, _, pattern = swap.partition(":")
                first = len(swaps)
                for item in filter(None, pattern.split(",")):
                    o, w = item.split(".")
                    swaps.append((int(o, 16), int(w)))
                var_rows[sec].append(f'    {{ "{name}", {addr - lo:#x}, {size}, {int(period, 16):#x}, {first}, '
                                     f"{len(swaps) - first} }},")
        elif storage == "own":
            n = int(size, 16)
            note = "" if n else "  /* the map gives 0 bytes */"
            c.append(f"_Alignas(16) uint8_t {name}[{max(n, 1):#x}];{note}")
            owns[sec].append((name, max(n, 1), swap == "rom"))
        elif storage == "alias":
            c.append(f"__asm__(\".globl \" TD_ASM({name}) \"\\n.set \" TD_ASM({name}) \", \" TD_ASM({size}));")
        elif storage == "port":
            if name not in PORT:
                sys.exit(f"{src}: {name} is provided by the port, but tools/game_vars.py's PORT does not list it")
            n, value = PORT[name]
            if value is None:
                c.append(f"_Alignas(16) uint8_t {name}[{n:#x}];")
            else:
                c.append(f"int32_t {name} = {value};")
        elif storage == "function":
            links.append(f"LINKER:--defsym={name}={size}")
        else:
            sys.exit(f"{src}: {name}: unknown storage {storage}")
    c.append("")

    c.append("/* The swap patterns of the variables in sections the ROM fills: a w-byte value at offset o, again every")
    c.append(" * period bytes of the variable (src/game_vars.h). */")
    c.append(f"const GameVarSwap game_var_swaps[{max(len(swaps), 1)}] = {{")
    c += [f"    {{ {o:#x}, {w} }}," for o, w in swaps] or ["    { 0, 0 },"]
    c.append("};")
    c.append("")
    for s in SECTION_ORDER:
        if var_rows[s]:
            c.append(f"static const GameVar {c_name(s)}_vars[] = {{")
            c += var_rows[s]
            c.append("};")
        if owns[s]:
            c.append(f"static const GameVarOwn {c_name(s)}_own[] = {{")
            c += [f'    {{ "{n}", {n}, {size:#x}, {int(rom)} }},' for n, size, rom in owns[s]]
            c.append("};")
    c.append("")
    c.append("const GameVarSection game_var_sections[GAME_VAR_SECTIONS] = {")
    for s in SECTION_ORDER:
        lo, hi, rom, ovl = SECTIONS[s]
        vars_ = f"{c_name(s)}_vars, {len(var_rows[s])}" if var_rows[s] else "0, 0"
        own = f"{c_name(s)}_own, {len(owns[s])}" if owns[s] else "0, 0"
        c.append(f'    {{ "{s}", {c_name(s)}, {lo:#x}, {hi - lo:#x}, '
                 f"{'GAME_VAR_NO_ROM' if rom is None else hex(rom)}, {ovl}, {vars_}, {own} }},")
    c.append("};")
    text = "\n".join(c) + "\n"
    if not out.exists() or out.read_text(encoding="utf-8") != text:
        out.write_text(text, encoding="utf-8", newline="\n")
    ltext = "".join(x + "\n" for x in links)
    if not link.exists() or link.read_text(encoding="utf-8") != ltext:
        link.write_text(ltext, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
