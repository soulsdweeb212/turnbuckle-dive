/* The game's archive: an .o2r file (a zip archive, the kind LibUltraShip reads) that the port writes once from the
 * user's ROM, so that the port can then run without the ROM. C and C++.
 *
 * What it holds (docs/GAME_CODE.md, "The game's archive", says why):
 * - TD_ARCHIVE_RES_FILE: each resource whose bytes differ from every earlier one's, as the game's loader has it once
 *   unpacked (the game's LZSS, decompressLzss), in the N64's byte order. 6068 of the 7657 resources; the other 1589 are
 *   stored in the ROM twice and use the file of the first copy.
 * - TD_ARCHIVE_INDEX: for each resource id, 1 to GAME_RES_COUNT, six bytes: the id whose file holds its bytes
 *   (big-endian u16), then the first 4 bytes the ROM has at the resource's start, which the game reads by themselves.
 * - TD_ARCHIVE_ROM_LOW and TD_ARCHIVE_ROM_HIGH: the ROM before the resources (the code and the game's data) and after
 *   them (the resource table and the sound banks), as they are.
 * - TD_ARCHIVE_STAMP: a text file with the archive's format and the SHA-1 of the ROM it was written from, which the
 *   port checks when it opens the archive. There is no LibUltraShip "version" file: LibUltraShip accepts an archive
 *   that has one only through a full ResourceManager, and the stamp says more (the exact ROM, and the format). */
#ifndef TD_ARCHIVE_H
#define TD_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

#include "rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The archive's name, in the folder the port keeps its files in. */
#define TD_ARCHIVE_NAME "revenge.o2r"
/* The format of what the archive holds. The port writes the archive again when an archive has another. */
#define TD_ARCHIVE_FORMAT 1

#define TD_ARCHIVE_STAMP "revenge/extract.txt"
#define TD_ARCHIVE_INDEX "revenge/res/index"
#define TD_ARCHIVE_RES_FILE "revenge/res/%04X"
#define TD_ARCHIVE_ROM_LOW "revenge/rom/low"
#define TD_ARCHIVE_ROM_HIGH "revenge/rom/high"
#define TD_ARCHIVE_INDEX_ENTRY 6

/* The ROM's layout around the resources: their data runs from TD_ROM_RES_DATA up to their table, which holds
 * GAME_RES_COUNT + 1 big-endian words (each resource's start, with bit 0 set when the resource is packed, relative to
 * TD_ROM_RES_DATA, and the end of the last one). */
#define TD_ROM_RES_DATA 0xDAC50u
#define TD_ROM_RES_TABLE 0xCE2752u

/* Writes the archive at path from rom, which must be the supported ROM: to path's name with ".part" added, then renamed
 * to path, so that an archive is never left half-written. Returns 0, or -1 with the reason in why. In builds with the
 * game's code only (it unpacks with the game's decompressLzss). */
int archive_write(const Rom *rom, const char *path, char *why, size_t why_size);

/* Opens the archive at path with LibUltraShip and builds from it, in rom, the ROM image the game reads: the two ROM
 * ranges, every resource stored unpacked (the game streams the animations from the ROM itself), and of each packed
 * resource only its first 4 bytes; the packed ones then come from the archive through port_res_read, in func_80003684.
 * rom->sparse is set, so port_rom_read stops the program if the game reads anything else of a packed resource. The archive stays open until archive_close. Returns 0, or -1 with the reason in why:
 * no such file, not the game's archive, written from another ROM, or another format (then the port writes it again).
 * In programs that link LibUltraShip. */
int archive_open(const char *path, Rom *rom, char *why, size_t why_size);
void archive_close(void);

#ifdef __cplusplus
}
#endif

#endif
