/* Loading and checking the user's ROM. No game data is stored in this repository; everything comes from this file. */
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

RomStatus rom_load(const char *path, Rom *rom);
void rom_free(Rom *rom);
const char *rom_status_text(RomStatus status);
const char *rom_order_text(RomByteOrder order);

#endif
