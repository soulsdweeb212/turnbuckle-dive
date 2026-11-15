"""Writes the C file that defines the game's variables for the PC, from the decompilation's port/game_vars.tsv
(docs/GAME_CODE.md, "The game's variables"). CMake runs it when it configures; it needs only Python 3.

    python3 tools/game_vars.py MAP OUT.c LINK.txt FUNCTIONS.c

MAP is the decompilation's port/game_vars.tsv, which its tools/port_data.py writes and explains: one line per variable,
with its N64 address, its N64 section, its storage, its size and its swap pattern. This tool writes:

- OUT.c, compiled with the port's code:
  - one array per N64 section of main, and one for all the overlays' sections ("block" storage): the variables that
    hold no address sit in it at their N64 addresses. The two overlays share addresses on the N64 (overlay 1's BSS
    lies where overlay 2's data lies), and only one is loaded at a time, so their sections share one area by N64
    address, as the N64's memory does. A name that both overlays use there (as an s32 of overlay 1, an f64 of overlay
    2) is one name for one address in that area; each overlay's section has its own swap pattern for it.
    Each such variable is a name for an address inside the array, set in assembly (".set NAME, block + offset"),
    because C cannot give a name to the middle of an array;
  - an array of its own for each variable that holds an address ("own" storage), and for the few the port provides
    ("port" storage; their sizes and starting values are in PORT below);
  - the tables that src/game_vars.c reads to fill the sections from the ROM: each section's place in the ROM and in
    N64 memory, and each variable's swap pattern.
  - the address tables: for each variable that holds addresses and that the N64 starts from the ROM (and each array
    or record its addresses reach), how to build its PC layout from its N64 bytes (src/game_vars.h, GameVarFill);
  - the display lists of the ROM's data ("gfx" in the map): an array of 16 bytes per command for each, which
    src/game_vars.c fills with its PC copy (src/game_vars.h, GameVarGfx), and a name inside it for each of its entry
    points ("gfxentry": a list that starts in the middle of another, D_800C5D48 in D_800C5D10) at the PC command of the
    same number;
  - the swap-only ranges ("data" in the map): data that the tables' addresses point at;
  - the list of every object's N64 address and PC address, which turns an address the ROM holds into a PC address.
  It holds no byte of the game: the sections start as zeros and are filled from the user's ROM at start-up.
- FUNCTIONS.c: the functions the address tables hold ("entry" in the map), each with its N64 address. It names game
  functions, so only programs that link every game file compile it; they hand it to game_vars_use_functions.
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

# Variables with storage of their own that get zeroed bytes before them. The game uses -1 for "no wrestler" and reads
# fields of D_800F9D10[-1] where it does not use them (func_800BBA00 reads the flags of the partner -1); on the N64
# those bytes belong to other variables, on PC they are this guard, one record of zeros (0x3B8, sizeof(Wrestler)).
GUARD_BEFORE = {"D_800F9D10": 0x3B8}

SECTION_ORDER = ["main.data", "main.bss", "ovl1.data", "ovl1.bss", "ovl2.data", "ovl2.bss"]
# Each section's N64 address range, its ROM offset (None: zeros) and which overlay it belongs to (0: main). The same
# values as SECTIONS in the decompilation's tools/port_data.py, which says where they come from.
SECTIONS = {
    "main.data": (0x8002BE30, 0x8003BAD0, 0x2CA30, 0),
    "main.bss": (0x8003BAD0, 0x8007FCE0, None, 0),
    "ovl1.data": (0x800C5AD0, 0x800D6D30, 0x72240, 1),
    "ovl1.bss": (0x800D6D30, 0x800EA350, None, 1),
    "ovl2.data": (0x800E2B10, 0x800E77B0, 0xD5FB0, 2),
    "ovl2.bss": (0x800E77B0, 0x800FAFA0, None, 2),
}


# The overlays' sections share one area of PC memory, from the lowest to the highest N64 address of any of them.
OVERLAY_AREA = "td_overlays"
AREA_LO = min(lo for lo, _hi, _rom, ovl in SECTIONS.values() if ovl)
AREA_HI = max(hi for _lo, hi, _rom, ovl in SECTIONS.values() if ovl)


def c_name(section: str) -> str:
    return "td_" + section.replace(".", "_")


def block_of(section: str) -> tuple[str, int]:
    """The array that holds a section's bytes, and the N64 address of the array's first byte."""
    lo, _hi, _rom, ovl = SECTIONS[section]
    return (OVERLAY_AREA, AREA_LO) if ovl else (c_name(section), lo)


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    src, out, link, funcs = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4])
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

    c.append("/* The N64 sections of main, each as one block of PC memory, and the overlays' area, which holds all of")
    c.append(" * the overlays' sections at their N64 addresses (\"block\" storage). */")
    for s in SECTION_ORDER:
        lo, hi, _rom, ovl = SECTIONS[s]
        if not ovl:
            c.append(f"_Alignas(16) uint8_t {c_name(s)}[{hi - lo:#x}];")
    c.append(f"_Alignas(16) uint8_t {OVERLAY_AREA}[{AREA_HI - AREA_LO:#x}];")
    c.append("")

    owns: dict[str, list] = {s: [] for s in SECTION_ORDER}
    swaps: list[tuple[int, int]] = []
    var_rows: dict[str, list[str]] = {s: [] for s in SECTION_ORDER}
    fills: dict[str, list[str]] = {s: [] for s in SECTION_ORDER}
    gfx: dict[str, list[str]] = {s: [] for s in SECTION_ORDER}
    gfx_size: dict[str, int] = {}  # each display list's PC bytes, by name
    fields: list[str] = []
    targets: list[tuple[int, int, str]] = []
    placed: dict[str, int] = {}  # block names already given an address, by N64 address
    entries: list[tuple[int, str]] = []
    links = []

    def add_swaps(name, sec, addr, size, swap):
        lo = SECTIONS[sec][0]
        period, _, pattern = swap.partition(":")
        first = len(swaps)
        for item in filter(None, pattern.split(",")):
            o, w = item.split(".")
            swaps.append((int(o, 16), int(w)))
        var_rows[sec].append(f'    {{ "{name}", {addr - lo:#x}, {size}, {int(period, 16):#x}, {first}, '
                             f"{len(swaps) - first} }},")
    def overlay_of(sec):
        return SECTIONS[sec][3] if sec in SECTIONS else 0

    c.append("/* The variables, in N64 address order. */")
    for name, addr, sec, storage, size, swap in rows:
        if storage == "block":
            block, base = block_of(sec)
            if name in placed:
                # The other overlay's row for the same name: the same address in the same area.
                if placed[name] != addr or not SECTIONS[sec][3]:
                    sys.exit(f"{src}: {name} has two rows that are not one address in the overlays' area")
            else:
                placed[name] = addr
                c.append(f"TD_AT({name}, {block}, {addr - base:#x}, {int(size, 16):#x});")
            if swap != "-":
                add_swaps(name, sec, addr, size, swap)
        elif storage == "data":
            add_swaps(name, sec, addr, size, swap)
        elif storage == "own":
            n = int(size, 16)
            static = "static " if name.startswith("td_anon_") else ""
            if name in GUARD_BEFORE:
                g = GUARD_BEFORE[name]
                c.append(f"_Alignas(16) uint8_t td_guard_{name}[{g + max(n, 1):#x}];")
                c.append(f"TD_AT({name}, td_guard_{name}, {g:#x}, {max(n, 1):#x});")
                c.append(f"extern uint8_t {name}[];")
            else:
                c.append(f"{static}_Alignas(16) uint8_t {name}[{max(n, 1):#x}];")
            owns[sec].append((name, max(n, 1), swap != "-"))
            targets.append((addr, overlay_of(sec), name))
            if swap != "-":
                count, n64size, pcsize, spec = swap.split(":")
                first = len(fields)
                for item in spec.split(","):
                    o4, o8, kind = item.split(".")
                    fields.append(f"    {{ {int(o4, 16):#x}, {int(o8, 16):#x}, '{kind}' }},")
                fills[sec].append(f'    {{ "{name}", {name}, {addr:#x}, {int(count, 16)}, {int(n64size, 16):#x}, '
                                  f"{int(pcsize, 16):#x}, {first}, {len(fields) - first} }},")
        elif storage == "gfx":
            n = int(size, 16)
            if n != int(swap, 16) * 16:
                sys.exit(f"{src}: {name}: {size} bytes for {swap} commands; a PC command is 16 bytes")
            c.append(f"_Alignas(16) uint8_t {name}[{n:#x}];")
            gfx[sec].append(f'    {{ "{name}", {name}, {addr:#x}, {int(swap, 16)} }},')
            gfx_size[name] = n
            targets.append((addr, overlay_of(sec), name))
        elif storage == "gfxentry":
            # size names the display list it lies in, swap its offset there in bytes on PC
            if size not in gfx_size:
                sys.exit(f"{src}: {name}: an entry of {size}, which is not a display list of an earlier line")
            offset = int(swap, 16)
            c.append(f"TD_AT({name}, {size}, {offset:#x}, {gfx_size[size] - offset:#x});")
            c.append(f"extern uint8_t {name}[];")
            targets.append((addr, overlay_of(sec), name))
        elif storage == "entry":
            entries.append((addr, size))
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
            targets.append((addr, 0, name))
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
    c.append("/* How to build each address table from its N64 bytes (src/game_vars.h). */")
    c.append(f"const GameVarField game_var_fields[{max(len(fields), 1)}] = {{")
    c += fields or ["    { 0, 0, 0 },"]
    c.append("};")
    c.append("")
    c.append("/* Every object's N64 address and PC address, by N64 address: an address that the ROM holds becomes the PC")
    c.append(" * address of what it names. */")
    c.append(f"const GameVarTarget game_var_targets[{max(len(targets), 1)}] = {{")
    c += [f"    {{ {a:#x}, {o}, &{n} }}," for a, o, n in sorted(targets)] or ["    { 0, 0, 0 },"]
    c.append("};")
    c.append(f"const uint32_t game_var_target_count = {len(targets)};")
    c.append("")
    for s in SECTION_ORDER:
        if fills[s]:
            c.append(f"static const GameVarFill {c_name(s)}_fills[] = {{")
            c += fills[s]
            c.append("};")
        if var_rows[s]:
            c.append(f"static const GameVar {c_name(s)}_vars[] = {{")
            c += var_rows[s]
            c.append("};")
        if owns[s]:
            c.append(f"static const GameVarOwn {c_name(s)}_own[] = {{")
            c += [f'    {{ "{n}", {n}, {size:#x}, {int(rom)} }},' for n, size, rom in owns[s]]
            c.append("};")
        if gfx[s]:
            c.append(f"static const GameVarGfx {c_name(s)}_gfx[] = {{")
            c += gfx[s]
            c.append("};")
    c.append("")
    c.append("const GameVarSection game_var_sections[GAME_VAR_SECTIONS] = {")
    for s in SECTION_ORDER:
        lo, hi, rom, ovl = SECTIONS[s]
        vars_ = f"{c_name(s)}_vars, {len(var_rows[s])}" if var_rows[s] else "0, 0"
        own = f"{c_name(s)}_own, {len(owns[s])}" if owns[s] else "0, 0"
        fill = f"{c_name(s)}_fills, {len(fills[s])}" if fills[s] else "0, 0"
        gfx_ = f"{c_name(s)}_gfx, {len(gfx[s])}" if gfx[s] else "0, 0"
        block, base = block_of(s)
        c.append(f'    {{ "{s}", {block} + {lo - base:#x}, {lo:#x}, {hi - lo:#x}, '
                 f"{'GAME_VAR_NO_ROM' if rom is None else hex(rom)}, {ovl}, {vars_}, {own}, {fill}, {gfx_} }},")
    c.append("};")
    text = "\n".join(c) + "\n"
    if not out.exists() or out.read_text(encoding="utf-8") != text:
        out.write_text(text, encoding="utf-8", newline="\n")
    f = ["/* Written by turnbuckle-dive's tools/game_vars.py from the decompilation's port/game_vars.tsv; do not edit.",
         " * The functions that the game's address tables hold, by N64 address (src/game_vars.h). Each is declared",
         " * without parameters: only its address is taken here. */",
         '#include "game_vars.h"',
         ""]
    f += [f"void {n}(void);" for _, n in sorted(entries)]
    f += ["", f"const GameVarFunction game_var_functions[{max(len(entries), 1)}] = {{"]
    f += [f"    {{ {a:#x}, {n} }}," for a, n in sorted(entries)] or ["    { 0, 0 },"]
    f += ["};", f"const uint32_t game_var_function_count = {len(entries)};"]
    ftext = "\n".join(f) + "\n"
    if not funcs.exists() or funcs.read_text(encoding="utf-8") != ftext:
        funcs.write_text(ftext, encoding="utf-8", newline="\n")
    ltext = "".join(x + "\n" for x in links)
    if not link.exists() or link.read_text(encoding="utf-8") != ltext:
        link.write_text(ltext, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
