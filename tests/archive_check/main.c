/* archive_check: checks the game's archive (src/archive.h) against the user's ROM.
 *
 * 1. It writes the archive from the ROM (archive_write), into the build folder, and opens it again (archive_open),
 *    which builds the ROM image the game reads when it runs from the archive.
 * 2. It loads all 7657 resources with the game's own loader twice, once from the ROM and once from the archive, and
 *    compares one SHA-1 over each run: for each resource, the descriptor the loader writes (start, end, size, packed)
 *    and the bytes it leaves in its buffer, as tools/rom_loader does. The two must be the same.
 * 3. It compares the archive's ROM image with the ROM outside the resources' data and over every resource stored
 *    unpacked, where the two must be the same byte for byte, and checks each packed resource's first 4 bytes.
 * 4. It deletes the archive, which holds the game's data and is never kept: once the test ends, nothing of it is left.
 *
 * It needs the ROM, so CI never runs it; with no ROM it exits with code 77, which CTest reports as skipped.
 *
 * Usage: archive_check [--flip ID | --read-inside]
 * - --flip ID changes one byte of resource ID after it is loaded from the archive, so the two SHA-1s differ and the
 *   test must fail with "FAIL: the resources loaded from the archive"; archive_check_detects_change runs it this way.
 * - --read-inside reads 8 bytes inside a packed resource from the archive's image, which port_rom_read must refuse by
 *   stopping the program; archive_check_guard runs it this way.
 * Exit code: 0 passed, 1 failed, 2 wrong arguments, 77 skipped (no ROM). */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archive.h"
#include "game.h"
#include "resources.h"
#include "rom.h"
#include "sha1.h"

#define EXIT_SKIP 77

static void put_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/* Loads every resource of rom with the game's loader and returns the SHA-1 of their descriptors and bytes in hex.
 * flip is a resource id whose loaded bytes get one bit changed, or 0. */
static void scan(const Rom *rom, uint32_t flip, char out[41]) {
    static const char hex[] = "0123456789abcdef";
    Sha1 sha1;
    uint8_t digest[20];
    uint32_t id;
    int i;

    rom_attach(rom);
    res_init();
    sha1_init(&sha1);
    for (id = 1; id <= GAME_RES_COUNT; id++) {
        GameResDesc desc;
        uint8_t *buf = res_load(id, &desc);
        uint8_t head[16];

        if (id == flip && desc.size > 0) {
            buf[desc.size / 2] ^= 0x01;
        }
        put_be32(head, desc.start);
        put_be32(head + 4, desc.end);
        put_be32(head + 8, (uint32_t)desc.size);
        put_be32(head + 12, (uint32_t)desc.packed);
        sha1_update(&sha1, head, sizeof(head));
        sha1_update(&sha1, buf, (size_t)desc.size);
        free(buf);
    }
    res_shutdown();
    rom_attach(NULL);
    sha1_final(&sha1, digest);
    for (i = 0; i < 20; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[40] = '\0';
}

/* port_fatal ends with abort(), and CTest fails a test that aborts whatever it printed, so --read-inside, which must
 * stop, turns abort into a plain exit with code 3 and CTest judges it by its output (as tests/test_slots.c does). */
static void on_abort(int sig) {
    (void)sig;
    _Exit(3);
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int main(int argc, char **argv) {
    const char *archive = "archive_check.o2r";
    uint32_t flip = 0;
    int read_inside = 0;
    char why[512];
    char from_rom[41];
    char from_archive[41];
    Rom rom;
    Rom image;
    RomStatus status;
    uint32_t id;
    int ok = 1;

    if (argc == 3 && strcmp(argv[1], "--flip") == 0) {
        flip = (uint32_t)strtoul(argv[2], NULL, 0);
    } else if (argc == 2 && strcmp(argv[1], "--read-inside") == 0) {
        read_inside = 1;
    } else if (argc != 1) {
        fprintf(stderr, "usage: archive_check [--flip ID | --read-inside]\n");
        return 2;
    }
    status = rom_load(rom_find(NULL), &rom);
    if (status == ROM_NOT_FOUND) {
        printf("SKIP: no ROM (set TD_ROM)\n");
        return EXIT_SKIP;
    }
    if (status != ROM_OK) {
        printf("FAIL: the ROM: %s\n", rom_status_text(status));
        return 1;
    }
    if (archive_write(&rom, archive, why, sizeof(why)) != 0) {
        printf("FAIL: writing the archive: %s\n", why);
        rom_free(&rom);
        return 1;
    }
    if (archive_open(archive, &image, why, sizeof(why)) != 0) {
        printf("FAIL: opening the archive: %s\n", why);
        remove(archive);
        rom_free(&rom);
        return 1;
    }

    if (read_inside) {
        uint8_t bytes[8];

        remove(archive); /* the program stops below, and nothing of the archive may stay behind */
        signal(SIGABRT, on_abort);
#ifdef _MSC_VER
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        rom_attach(&image);
        for (id = 1; id <= GAME_RES_COUNT; id++) { /* the first packed resource; 8 bytes from its 9th byte on */
            const uint8_t *w = rom.data + TD_ROM_RES_TABLE + (id - 1) * 4;

            if (w[3] & 1u) {
                port_rom_read(bytes, TD_ROM_RES_DATA + (be32(w) & ~1u) + 8, sizeof(bytes)); /* must stop the program */
                break;
            }
        }
        printf("FAIL: port_rom_read read inside the resources' data of the archive's image\n");
        return 1;
    }

    /* The image against the ROM: the same outside the resources' data, and each resource's first 4 bytes inside. */
    if (image.size != rom.size || memcmp(image.data, rom.data, TD_ROM_RES_DATA) != 0 ||
        memcmp(image.data + TD_ROM_RES_TABLE, rom.data + TD_ROM_RES_TABLE, rom.size - TD_ROM_RES_TABLE) != 0) {
        printf("FAIL: the archive's ROM image differs from the ROM outside the resources' data\n");
        ok = 0;
    }
    for (id = 1; id <= GAME_RES_COUNT; id++) {
        uint32_t word = be32(rom.data + TD_ROM_RES_TABLE + (id - 1) * 4);
        uint32_t start = TD_ROM_RES_DATA + (word & ~1u);
        uint32_t end = TD_ROM_RES_DATA + (be32(rom.data + TD_ROM_RES_TABLE + id * 4) & ~1u);
        uint32_t compared = (word & 1u) ? 4 : end - start;

        if (memcmp(image.data + start, rom.data + start, compared) != 0) {
            printf("FAIL: resource 0x%04X differs in the archive's ROM image (%s)\n", id,
                   (word & 1u) ? "its first 4 bytes" : "it is stored unpacked, so all of it must be there");
            ok = 0;
            break;
        }
    }

    /* The resources: first from the archive (while it is open, the game's loader takes them from it), then, with the
     * archive closed, from the ROM. */
    scan(&image, flip, from_archive);
    archive_close();
    rom_free(&image);
    remove(archive);
    scan(&rom, 0, from_rom);
    rom_free(&rom);
    printf("ROM:     %s\n", from_rom);
    printf("Archive: %s\n", from_archive);
    if (strcmp(from_rom, from_archive) != 0) {
        printf("FAIL: the resources loaded from the archive differ from those loaded from the ROM\n");
        ok = 0;
    }
    if (ok) {
        printf("PASS: the archive gives the game the same %u resources as the ROM, and the same ROM outside them\n",
               GAME_RES_COUNT);
    }
    return ok ? 0 : 1;
}
