/* Writing the game's archive from the ROM (src/archive.h says what it holds). It reads the resource table the way the
 * game's initializeAssetSizeTable does, unpacks each packed resource with the game's own LZSS (decompressLzss, compiled
 * from the decompilation), and writes the files with libzip, the library LibUltraShip reads .o2r files with. */
#include "archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zip.h>

#include "game.h"
#include "sha1.h"

/* The game's LZSS: packed is a big-endian u32 size followed by the packed stream; out receives that many bytes. */
void decompressLzss(uint8_t *packed, uint8_t *out);

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void fail(char *why, size_t why_size, const char *what, const char *detail) {
    snprintf(why, why_size, "%s%s%s", what, detail != NULL ? ": " : "", detail != NULL ? detail : "");
}

/* Adds a file whose bytes libzip takes over (and frees once written). */
static int add_file(zip_t *z, const char *name, void *data, size_t size, char *why, size_t why_size) {
    zip_source_t *src = zip_source_buffer(z, data, size, 1);

    if (src == NULL) {
        free(data);
        fail(why, why_size, "could not add a file to the archive", zip_strerror(z));
        return -1;
    }
    if (zip_file_add(z, name, src, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
        zip_source_free(src);
        fail(why, why_size, "could not add a file to the archive", zip_strerror(z));
        return -1;
    }
    return 0;
}

static int add_copy(zip_t *z, const char *name, const void *data, size_t size, char *why, size_t why_size) {
    void *copy = malloc(size > 0 ? size : 1);

    if (copy == NULL) {
        fail(why, why_size, "out of memory", NULL);
        return -1;
    }
    memcpy(copy, data, size);
    return add_file(z, name, copy, size, why, why_size);
}

/* A resource's stored bytes in the ROM, and whether they are packed. */
typedef struct {
    uint32_t start;
    uint32_t end;
    int packed;
} Stored;

static Stored stored(const Rom *rom, uint32_t index) {
    const uint8_t *table = rom->data + TD_ROM_RES_TABLE;
    uint32_t a = be32(table + index * 4);
    uint32_t b = be32(table + index * 4 + 4);
    Stored s = { TD_ROM_RES_DATA + (a & ~1u), TD_ROM_RES_DATA + (b & ~1u), (int)(a & 1u) };

    return s;
}

static int same_bytes(const Rom *rom, Stored a, Stored b) {
    return a.packed == b.packed && a.end - a.start == b.end - b.start &&
           memcmp(rom->data + a.start, rom->data + b.start, a.end - a.start) == 0;
}

/* The id whose file holds each resource's bytes: the first resource stored with the same bytes, found through a hash
 * table of the stored bytes' SHA-1 (first 8 bytes), with the bytes compared on a match. */
static int find_owners(const Rom *rom, uint16_t owner[GAME_RES_COUNT + 1]) {
    enum { SLOTS = 16384 };
    uint16_t *slot = calloc(SLOTS, sizeof(*slot)); /* ids; 0 is free */
    uint32_t id;

    if (slot == NULL) {
        return -1;
    }
    for (id = 1; id <= GAME_RES_COUNT; id++) {
        Stored s = stored(rom, id - 1);
        Sha1 h;
        uint8_t digest[20];
        uint64_t key = 0;
        uint32_t i;

        sha1_init(&h);
        sha1_update(&h, rom->data + s.start, s.end - s.start);
        sha1_final(&h, digest);
        for (i = 0; i < 8; i++) {
            key = key << 8 | digest[i];
        }
        for (i = (uint32_t)(key % SLOTS);; i = (i + 1) % SLOTS) {
            if (slot[i] == 0) {
                slot[i] = (uint16_t)id;
                owner[id] = (uint16_t)id;
                break;
            }
            if (same_bytes(rom, stored(rom, slot[i] - 1u), s)) {
                owner[id] = slot[i];
                break;
            }
        }
    }
    free(slot);
    return 0;
}

int archive_write(const Rom *rom, const char *path, char *why, size_t why_size) {
    static uint16_t owner[GAME_RES_COUNT + 1];
    char part[4096];
    char name[64];
    char stamp[128];
    uint8_t *index;
    uint32_t id;
    int err = 0;
    zip_t *z;

    if (rom == NULL || rom->data == NULL || rom->sparse || strcmp(rom->sha1, TD_ROM_SHA1) != 0) {
        fail(why, why_size, "the archive is written only from the supported ROM", NULL);
        return -1;
    }
    if (snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part)) {
        fail(why, why_size, "the archive's path is too long", NULL);
        return -1;
    }
    if (find_owners(rom, owner) != 0) {
        fail(why, why_size, "out of memory", NULL);
        return -1;
    }
    z = zip_open(part, ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (z == NULL) {
        zip_error_t e;

        zip_error_init_with_code(&e, err);
        fail(why, why_size, "could not create the archive", zip_error_strerror(&e));
        zip_error_fini(&e);
        return -1;
    }

    snprintf(stamp, sizeof(stamp), "format %d\nrom %s\n", TD_ARCHIVE_FORMAT, rom->sha1);
    if (add_copy(z, TD_ARCHIVE_STAMP, stamp, strlen(stamp), why, why_size) != 0 ||
        add_copy(z, TD_ARCHIVE_ROM_LOW, rom->data, TD_ROM_RES_DATA, why, why_size) != 0 ||
        add_copy(z, TD_ARCHIVE_ROM_HIGH, rom->data + TD_ROM_RES_TABLE, rom->size - TD_ROM_RES_TABLE, why,
                 why_size) != 0) {
        zip_discard(z);
        remove(part);
        return -1;
    }

    index = malloc(GAME_RES_COUNT * TD_ARCHIVE_INDEX_ENTRY);
    if (index == NULL) {
        zip_discard(z);
        remove(part);
        fail(why, why_size, "out of memory", NULL);
        return -1;
    }
    for (id = 1; id <= GAME_RES_COUNT; id++) {
        Stored s = stored(rom, id - 1);
        uint8_t *entry = index + (id - 1) * TD_ARCHIVE_INDEX_ENTRY;
        uint8_t *bytes;
        uint32_t size;

        entry[0] = (uint8_t)(owner[id] >> 8);
        entry[1] = (uint8_t)owner[id];
        memcpy(entry + 2, rom->data + s.start, 4);
        if (owner[id] != id) {
            continue;
        }
        size = s.packed ? be32(rom->data + s.start) : s.end - s.start;
        bytes = malloc(size > 0 ? size : 1);
        if (bytes == NULL) {
            free(index);
            zip_discard(z);
            remove(part);
            fail(why, why_size, "out of memory", NULL);
            return -1;
        }
        if (s.packed) {
            decompressLzss(rom->data + s.start, bytes);
        } else {
            memcpy(bytes, rom->data + s.start, size);
        }
        snprintf(name, sizeof(name), TD_ARCHIVE_RES_FILE, id);
        if (add_file(z, name, bytes, size, why, why_size) != 0) {
            free(index);
            zip_discard(z);
            remove(part);
            return -1;
        }
    }
    if (add_file(z, TD_ARCHIVE_INDEX, index, GAME_RES_COUNT * TD_ARCHIVE_INDEX_ENTRY, why, why_size) != 0) {
        zip_discard(z);
        remove(part);
        return -1;
    }
    if (zip_close(z) != 0) {
        fail(why, why_size, "could not write the archive", zip_strerror(z));
        zip_discard(z);
        remove(part);
        return -1;
    }
    remove(path); /* rename does not replace an existing file on Windows */
    if (rename(part, path) != 0) {
        fail(why, why_size, "could not rename the finished archive into place", path);
        remove(part);
        return -1;
    }
    return 0;
}
