/* Loading and checking the user's ROM, and holding the ROM that the game's code reads. No game data is stored in this
 * repository; everything comes from this file. */
#ifndef TD_ROM_H
#define TD_ROM_H

#include <stddef.h>
#include <stdint.h>

/* SHA-1 of the one supported ROM, in big-endian (.z64) byte order. */
#define TD_ROM_SHA1 "e1711a2511394b9357b5f1ac8ca5cc17bd674836"

typedef enum {
    ROM_OK,
    ROM_NOT_FOUND,
    ROM_READ_ERROR,
    ROM_NOT_N64,     /* too small, or the first word is not an N64 header in any byte order */
    ROM_WRONG_HASH,  /* an N64 ROM, but not the supported one; the data is kept so the caller can report it */
} RomStatus;

/* Byte order of the file on disk. In memory the ROM is always converted to .z64 order. */
typedef enum {
    ROM_ORDER_Z64,   /* big-endian, as the cartridge stores it */
    ROM_ORDER_N64,   /* 32-bit words little-endian */
    ROM_ORDER_V64,   /* 16-bit halves swapped */
} RomByteOrder;

typedef struct {
    uint8_t *data;
    size_t size;
    RomByteOrder order;
    char sha1[41];   /* hex SHA-1 of data (after conversion to .z64 order) */
    char name[21];   /* internal name from the header (0x20..0x33), trailing spaces removed */
} Rom;

/* The ROM path to use: given if it is not NULL, else $TD_ROM, else baserom.z64 in the current folder, else (builds made
 * with this repository's CMakeLists.txt) the decompilation's baserom.z64, in its folder TD_DECOMP_DIR. If none of the
 * files exists, the result is "baserom.z64", which rom_load then reports as not found. */
const char *rom_find(const char *given);

/* Reads the ROM file at path and checks it with rom_load_memory. */
RomStatus rom_load(const char *path, Rom *rom);
/* Checks a ROM image that is already in memory: data holds size bytes in any of the three byte orders and was
 * allocated with malloc. rom takes data over and converts it to .z64 order in place. With ROM_OK and ROM_WRONG_HASH,
 * rom keeps the data until rom_free; with any other result data is already freed. The rom_loader test checks its
 * copies of the ROM in other byte orders with it, so that no copy of the ROM is ever written to disk. */
RomStatus rom_load_memory(uint8_t *data, size_t size, Rom *rom);
/* Frees the ROM's data. If the game was reading this ROM (rom_attach), it no longer can. */
void rom_free(Rom *rom);
const char *rom_status_text(RomStatus status);
const char *rom_order_text(RomByteOrder order);

/* Makes rom the ROM that the game's code reads, through port_rom_read (src/game.h), or none if rom is NULL. It must be
 * the supported ROM: the game's code is only known to work on that one. The ROM stays the game's until rom_free. */
void rom_attach(const Rom *rom);

#endif
