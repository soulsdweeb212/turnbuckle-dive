#include "rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha1.h"

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

RomStatus rom_load(const char *path, Rom *rom) {
    FILE *f;
    long size;

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
    rom->data = malloc((size_t)size);
    if (rom->data == NULL) {
        fclose(f);
        return ROM_READ_ERROR;
    }
    if (fread(rom->data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        rom_free(rom);
        return ROM_READ_ERROR;
    }
    fclose(f);
    rom->size = (size_t)size;

    if (!detect_order(rom->data, &rom->order)) {
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
    free(rom->data);
    rom->data = NULL;
    rom->size = 0;
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
