#include "rom.h"
#include "archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "sha1.h"

/* The ROM the game's code reads (rom_attach): its data in .z64 order, and its size. */
static const uint8_t *g_game_rom;
static size_t g_game_rom_size;
static int g_game_rom_sparse;

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

const char *rom_find(const char *given) {
    static const char *const candidates[] = {
        "baserom.z64",
#ifdef TD_DECOMP_ROM
        TD_DECOMP_ROM,
#endif
    };
    const char *env = getenv("TD_ROM");
    size_t i;

    if (given != NULL) {
        return given;
    }
    if (env != NULL && env[0] != '\0') {
        return env;
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (file_exists(candidates[i])) {
            return candidates[i];
        }
    }
    return candidates[0];
}

/* The first word of every N64 ROM is 0x80371240 in big-endian order. */
static int detect_order(const uint8_t *p, RomByteOrder *order) {
    if (p[0] == 0x80 && p[1] == 0x37 && p[2] == 0x12 && p[3] == 0x40) {
        *order = ROM_ORDER_Z64;
        return 1;
    }
    if (p[0] == 0x40 && p[1] == 0x12 && p[2] == 0x37 && p[3] == 0x80) {
        *order = ROM_ORDER_N64;
        return 1;
    }
    if (p[0] == 0x37 && p[1] == 0x80 && p[2] == 0x40 && p[3] == 0x12) {
        *order = ROM_ORDER_V64;
        return 1;
    }
    return 0;
}

static void convert_to_z64(uint8_t *p, size_t size, RomByteOrder order) {
    size_t i;

    if (order == ROM_ORDER_V64) {
        for (i = 0; i + 1 < size; i += 2) {
            uint8_t t = p[i];

            p[i] = p[i + 1];
            p[i + 1] = t;
        }
    } else if (order == ROM_ORDER_N64) {
        for (i = 0; i + 3 < size; i += 4) {
            uint8_t b0 = p[i], b1 = p[i + 1];

            p[i] = p[i + 3];
            p[i + 1] = p[i + 2];
            p[i + 2] = b1;
            p[i + 3] = b0;
        }
    }
}

static void read_name(const uint8_t *header, char name[21]) {
    int i, end = 0;

    for (i = 0; i < 20; i++) {
        uint8_t c = header[0x20 + i];

        name[i] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
        if (name[i] != ' ') {
            end = i + 1;
        }
    }
    memset(name + end, 0, 21 - end);
}

/* Reads the whole file into memory, then leaves the checks to rom_load_memory. A file smaller than 4 KiB cannot be an
 * N64 ROM, which starts with a 4 KiB header and boot code, so it is not read at all. */
RomStatus rom_load(const char *path, Rom *rom) {
    FILE *f;
    long size;
    uint8_t *data;

    memset(rom, 0, sizeof(*rom));
    f = fopen(path, "rb");
    if (f == NULL) {
        return ROM_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return ROM_READ_ERROR;
    }
    if (size < 0x1000) {
        fclose(f);
        return ROM_NOT_N64;
    }
    data = malloc((size_t)size);
    if (data == NULL) {
        fclose(f);
        return ROM_READ_ERROR;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return ROM_READ_ERROR;
    }
    fclose(f);
    return rom_load_memory(data, (size_t)size, rom);
}

RomStatus rom_load_memory(uint8_t *data, size_t size, Rom *rom) {
    memset(rom, 0, sizeof(*rom));
    rom->data = data;
    rom->size = size;
    if (data == NULL || size < 0x1000 || !detect_order(rom->data, &rom->order)) {
        rom_free(rom);
        return ROM_NOT_N64;
    }
    convert_to_z64(rom->data, rom->size, rom->order);
    read_name(rom->data, rom->name);
    sha1_hex(rom->data, rom->size, rom->sha1);
    if (strcmp(rom->sha1, TD_ROM_SHA1) != 0) {
        return ROM_WRONG_HASH;
    }
    return ROM_OK;
}

void rom_free(Rom *rom) {
    if (rom->data != NULL && rom->data == g_game_rom) {
        rom_attach(NULL);
    }
    free(rom->data);
    rom->data = NULL;
    rom->size = 0;
}

void rom_attach(const Rom *rom) {
    if (rom == NULL) {
        g_game_rom = NULL;
        g_game_rom_size = 0;
        g_game_rom_sparse = 0;
        return;
    }
    if (rom->data == NULL || strcmp(rom->sha1, TD_ROM_SHA1) != 0) {
        port_fatal("rom_attach: the game's code only reads the supported ROM (SHA-1 %s)", TD_ROM_SHA1);
    }
    g_game_rom = rom->data;
    g_game_rom_size = rom->size;
    g_game_rom_sparse = rom->sparse;
}

/* In an image built from the game's archive, the resources' data holds every resource stored unpacked (the game
 * streams the animations from it a piece at a time, initializeResourceStream) and, of each packed one, only its first
 * 4 bytes, its unpacked size, which the game reads by itself; the rest of a packed resource comes from the archive
 * through the loader. A read there is allowed when the part of it inside each packed resource lies within those first
 * 4 bytes. The resource table's starts rise from one resource to the next; a binary search finds the first resource
 * the read touches. */
static uint32_t res_word(uint32_t index) {
    const uint8_t *w = g_game_rom + TD_ROM_RES_TABLE + index * 4;

    return (uint32_t)w[0] << 24 | (uint32_t)w[1] << 16 | (uint32_t)w[2] << 8 | w[3];
}

static uint32_t res_start(uint32_t index) {
    return TD_ROM_RES_DATA + (res_word(index) & ~1u);
}

static int sparse_read_ok(uint32_t src, uint32_t size) {
    uint32_t end = src + size;
    uint32_t lo = 0;
    uint32_t hi = GAME_RES_COUNT - 1;
    uint32_t i;

    if (end <= TD_ROM_RES_DATA || src >= TD_ROM_RES_TABLE) {
        return 1;
    }
    if (src < TD_ROM_RES_DATA) {
        return 0;
    }
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo + 1) / 2;

        if (res_start(mid) <= src) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    for (i = lo; i < GAME_RES_COUNT && res_start(i) < end; i++) {
        if (res_word(i) & 1u) {
            uint32_t next = res_start(i + 1); /* the table's last word is the end of the last resource */
            uint32_t part_end = end < next ? end : next;

            if (part_end > res_start(i) + 4) {
                return 0;
            }
        }
    }
    return 1;
}

/* The game's code reads ROM only through this function: on the PC, its readRomSynchronousChunks (the N64's PI DMA
 * transfer) calls it. The game passes ROM addresses, which are offsets into the image. A read that does not lie wholly
 * inside the attached ROM stops the program; it is never shortened or allowed past the end. The range test compares
 * size with the room left after src, because src + size, added in 32 bits, could wrap around to a small number. */
void port_rom_read(void *dst, uint32_t src, uint32_t size) {
    if (g_game_rom == NULL) {
        port_fatal("port_rom_read: the game read 0x%X bytes at ROM address 0x%07X, but no ROM is attached", size, src);
    }
    if (src > g_game_rom_size || size > g_game_rom_size - src) {
        port_fatal("port_rom_read: the game read 0x%X bytes at ROM address 0x%07X, past the end of the ROM "
                   "(0x%zX bytes)",
                   size, src, g_game_rom_size);
    }
    if (size == 0) {
        return;
    }
    if (dst == NULL) {
        port_fatal("port_rom_read: the game read 0x%X bytes at ROM address 0x%07X into a null pointer", size, src);
    }
    if (g_game_rom_sparse && !sparse_read_ok(src, size)) {
        port_fatal("port_rom_read: the game read 0x%X bytes at ROM address 0x%07X, inside the resources' data, which "
                   "the game's archive keeps as files, not as ROM bytes, for a packed resource; only the loader (func_80003684) "
                   "reads those",
                   size, src);
    }
    memcpy(dst, g_game_rom + src, size);
}

const char *rom_status_text(RomStatus status) {
    switch (status) {
    case ROM_OK: return "ok";
    case ROM_NOT_FOUND: return "file not found";
    case ROM_READ_ERROR: return "could not read the file";
    case ROM_NOT_N64: return "not an N64 ROM";
    case ROM_WRONG_HASH: return "an N64 ROM, but not the supported one";
    }
    return "unknown";
}

const char *rom_order_text(RomByteOrder order) {
    switch (order) {
    case ROM_ORDER_Z64: return "z64 (big-endian)";
    case ROM_ORDER_N64: return "n64 (little-endian words)";
    case ROM_ORDER_V64: return "v64 (byte-swapped)";
    }
    return "unknown";
}
