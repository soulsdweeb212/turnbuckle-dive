/* rom_loader: runs the game's own ROM loader, compiled from the decompilation, on the user's ROM and checks what it
 * loads.
 *
 * 1. It loads all 7657 resources with the game's initializeAssetSizeTable, lookupResourceDescriptor and func_80003684
 *    (which unpacks the compressed ones with the game's func_800037F4), checks that the game reported each load to the
 *    port (port_res_loaded), and compares the resources with one known-good digest, RES_DIGEST below.
 * 2. It makes copies of the ROM in the .v64 and .n64 byte orders and checks that each, once the ROM check has turned
 *    it back into .z64 order, gives the same digest. Then it makes a copy with one byte of a resource changed, which
 *    the ROM check must refuse because its SHA-1 is wrong, and prints both SHA-1s, as the program does when it refuses
 *    a ROM. The copies only exist in memory (rom_load_memory in src/rom.c): the test writes no ROM data to disk.
 *
 * It needs the ROM, so CI never runs it: CMake builds it only with game code, and when it finds no ROM it exits with
 * code 77, which CTest reports as "skipped". It looks for the ROM like the program does (rom_find in src/rom.c):
 * $TD_ROM, then baserom.z64 in the current folder, then the decompilation's baserom.z64, in its folder TD_DECOMP_DIR.
 *
 * Usage: rom_loader [--flip ID]
 * - --flip ID changes one byte of resource ID after it is loaded, so the digest must not match and the test must fail
 *   with "FAIL: the digest". The rom_loader_detects_change test runs it this way.
 * - When the digest does not match and the environment variable TD_MANIFEST names a manifest, the test also prints the
 *   first resources that differ from it. tools/res_digest.py writes such a manifest from the ROM (docs/GAME_CODE.md
 *   says how): a text file with one line per resource, its id, ROM start and ROM end, 1 if it is compressed else 0,
 *   its size, and the SHA-1 of its bytes, with the id, the addresses and the size in hex. It lists hashes of game
 *   data, so it is never committed.
 * Exit code: 0 passed, 1 failed, 2 wrong arguments, 77 skipped (no ROM). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resources.h"
#include "rom.h"
#include "sha1.h"

/* The known-good digest of the supported ROM's resources: one SHA-1 over resources 1, 2, ..., 7657, in that order. For
 * each resource it takes in 16 bytes, the descriptor that the game's loader writes for it (GameResDesc: start, end,
 * size and packed, each as a big-endian 32-bit word), and then the resource's bytes: what func_80003684 leaves in its
 * buffer, size bytes, unpacked if the resource is compressed in ROM, else copied from ROM. With the descriptors in it,
 * the digest also changes when a resource starts or ends in the wrong place, even if all the bytes joined together
 * stay the same. tools/res_digest.py computes the same digest from the ROM with a decoder written in Python, which
 * shares no code with the game's loader, and the two agree. Like TD_ROM_SHA1 (src/rom.h) it is a fingerprint, not
 * game data. */
#define RES_DIGEST "4e8a5b8145e4f7bc0260f3e0bb5c2d963964dab1"

#define EXIT_SKIP 77

/* The resource that the changed copy of the ROM has one byte changed in. */
#define CHANGED_RES 1u

typedef struct {
    uint32_t loaded;          /* resources loaded */
    uint32_t compressed;      /* how many of them are compressed in ROM */
    unsigned long long bytes; /* their bytes in all */
    char digest[41];          /* see RES_DIGEST */
} Scan;

/* One resource's descriptor and the SHA-1 of its bytes, kept for the comparison with $TD_MANIFEST. */
typedef struct {
    GameResDesc desc;
    char sha1[41];
} ResResult;

/* Indexed by resource id; allocated only when TD_MANIFEST is set. */
static ResResult *g_results;

static void to_hex(const uint8_t digest[20], char out[41]) {
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 20; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[40] = '\0';
}

/* Writes value to p as a big-endian 32-bit word, the form in which the digest takes a descriptor's fields. */
static void put_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/* Loads every resource of rom with the game's loader and fills scan. flip is a resource id whose loaded bytes get one
 * bit changed (--flip), or 0. Returns 0 if a check failed, after saying which. */
static int scan_rom(const Rom *rom, uint32_t flip, Scan *scan) {
    Sha1 sha1;
    uint8_t digest[20];
    uint32_t id;
    int ok = 1;

    memset(scan, 0, sizeof(*scan));
    rom_attach(rom);
    res_init();
    sha1_init(&sha1);
    for (id = 1; id <= GAME_RES_COUNT; id++) {
        uint32_t reported = port_res_loaded_count();
        GameResDesc desc;
        uint8_t *buf = res_load(id, &desc);
        uint8_t head[16];

        if (port_res_loaded_count() != reported + 1 && ok) {
            printf("FAIL: the game's loader did not report resource 0x%04X to the port (port_res_loaded)\n", id);
            ok = 0;
        }
        if (id == flip && desc.size > 0) {
            buf[desc.size / 2] ^= 0x01;
        }
        put_be32(head, desc.start);
        put_be32(head + 4, desc.end);
        put_be32(head + 8, (uint32_t)desc.size);
        put_be32(head + 12, (uint32_t)desc.packed);
        sha1_update(&sha1, head, sizeof(head));
        sha1_update(&sha1, buf, (size_t)desc.size);
        if (g_results != NULL) {
            g_results[id].desc = desc;
            sha1_hex(buf, (size_t)desc.size, g_results[id].sha1);
        }
        scan->loaded++;
        if (desc.packed > 0) {
            scan->compressed++;
        }
        scan->bytes += (unsigned long long)desc.size;
        free(buf);
    }
    res_shutdown();
    rom_attach(NULL);
    sha1_final(&sha1, digest);
    to_hex(digest, scan->digest);
    if (scan->loaded != GAME_RES_COUNT || scan->bytes == 0) {
        printf("FAIL: %u resources with %llu bytes were loaded, expected %u\n", scan->loaded, scan->bytes,
               GAME_RES_COUNT);
        ok = 0;
    }
    return ok;
}

/* Prints the first resources whose results differ from the manifest that $TD_MANIFEST names, and how many differ. */
static void report_manifest_differences(void) {
    const char *path = getenv("TD_MANIFEST");
    static unsigned char listed[GAME_RES_COUNT + 1];
    char line[256];
    unsigned id, start, end, compressed, size, differing = 0, missing = 0;
    char sha1[41];
    FILE *f;

    if (path == NULL || path[0] == '\0' || g_results == NULL) {
        return;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        printf("TD_MANIFEST %s: file not found\n", path);
        return;
    }
    memset(listed, 0, sizeof(listed));
    while (fgets(line, sizeof(line), f) != NULL) {
        const ResResult *r;

        if (sscanf(line, "%x %x %x %u %x %40s", &id, &start, &end, &compressed, &size, sha1) != 6 || id < 1 ||
            id > GAME_RES_COUNT) {
            continue;
        }
        listed[id] = 1;
        r = &g_results[id];
        if (r->desc.start == start && r->desc.end == end && (r->desc.packed > 0) == (compressed != 0) &&
            (unsigned)r->desc.size == size && strcmp(r->sha1, sha1) == 0) {
            continue;
        }
        if (++differing <= 10) {
            printf("  resource 0x%04X differs: ROM 0x%07X..0x%07X (manifest 0x%07X..0x%07X), %s (manifest %s), "
                   "size 0x%X (manifest 0x%X), SHA-1 %s (manifest %s)\n",
                   id, r->desc.start, r->desc.end, start, end, r->desc.packed > 0 ? "compressed" : "not compressed",
                   compressed != 0 ? "compressed" : "not compressed", (unsigned)r->desc.size, size, r->sha1, sha1);
        }
    }
    fclose(f);
    for (id = 1; id <= GAME_RES_COUNT; id++) {
        missing += listed[id] == 0;
    }
    printf("TD_MANIFEST: %u of %u resources differ from %s", differing, GAME_RES_COUNT, path);
    if (missing != 0) {
        printf(", and %u are not in it", missing);
    }
    printf("\n");
}

/* Checks a scan's digest against RES_DIGEST. what names the ROM it came from. */
static int check_digest(const Scan *scan, const char *what) {
    if (strcmp(scan->digest, RES_DIGEST) == 0) {
        return 1;
    }
    printf("FAIL: the digest of the %u resources of %s is %s, expected %s\n", scan->loaded, what, scan->digest,
           RES_DIGEST);
    report_manifest_differences();
    return 0;
}

/* Copies rom's data into a new buffer in the given byte order, flipping one bit of the byte at changed_at first unless
 * changed_at is past the end, and checks the copy with rom_load_memory, as rom_load checks a file's contents. The test
 * swaps the bytes itself, so that it also tests the ROM check's conversion back to .z64 order. */
static RomStatus load_copy(const Rom *rom, RomByteOrder order, size_t changed_at, Rom *copy) {
    uint8_t *data = malloc(rom->size);
    size_t i;

    if (data == NULL) {
        printf("FAIL: out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(data, rom->data, rom->size);
    if (changed_at < rom->size) {
        data[changed_at] ^= 0x01;
    }
    /* .v64 swaps the two bytes of every 16-bit half, .n64 reverses the four bytes of every 32-bit word. */
    for (i = 0; i + 3 < rom->size && order != ROM_ORDER_Z64; i += 4) {
        uint8_t b0 = data[i], b1 = data[i + 1], b2 = data[i + 2], b3 = data[i + 3];

        if (order == ROM_ORDER_V64) {
            data[i] = b1;
            data[i + 1] = b0;
            data[i + 2] = b3;
            data[i + 3] = b2;
        } else {
            data[i] = b3;
            data[i + 1] = b2;
            data[i + 2] = b1;
            data[i + 3] = b0;
        }
    }
    return rom_load_memory(data, rom->size, copy);
}

/* Checks that a copy of rom in another byte order is read as that order and gives the same digest. */
static int check_order(const Rom *rom, RomByteOrder order) {
    char what[64];
    Rom copy;
    Scan scan;
    RomStatus status = load_copy(rom, order, SIZE_MAX, &copy);
    int ok = 0;

    snprintf(what, sizeof(what), "the copy in %s order", rom_order_text(order));
    if (status != ROM_OK) {
        printf("FAIL: %s: %s\n", what, rom_status_text(status));
    } else if (copy.order != order) {
        printf("FAIL: %s was read as %s\n", what, rom_order_text(copy.order));
    } else {
        ok = scan_rom(&copy, 0, &scan) && check_digest(&scan, what);
    }
    rom_free(&copy);
    return ok;
}

/* Checks that a copy of rom with one byte inside resource CHANGED_RES changed is refused, and prints the two SHA-1s
 * that the program prints when it refuses a ROM: the copy's, and the supported ROM's. */
static int check_changed(const Rom *rom) {
    GameResDesc desc;
    size_t changed_at;
    RomStatus status;
    Rom copy;
    int ok = 0;

    rom_attach(rom);
    res_init();
    lookupResourceDescriptor(&desc, CHANGED_RES);
    res_shutdown();
    rom_attach(NULL);
    changed_at = (size_t)desc.start + (size_t)(desc.end - desc.start) / 2;
    status = load_copy(rom, ROM_ORDER_Z64, changed_at, &copy);
    if (status != ROM_WRONG_HASH) {
        printf("FAIL: the copy with the byte at ROM 0x%07zX (inside resource 0x%04X) changed was not refused: %s\n",
               changed_at, CHANGED_RES, rom_status_text(status));
    } else {
        printf("The copy with the byte at ROM 0x%07zX changed is refused: SHA-1 %s, expected %s\n", changed_at,
               copy.sha1, TD_ROM_SHA1);
        ok = 1;
    }
    rom_free(&copy);
    return ok;
}

int main(int argc, char **argv) {
    const char *env = getenv("TD_ROM");
    const char *path;
    uint32_t flip = 0;
    RomStatus status;
    Scan scan;
    Rom rom;
    int i, ok;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--flip") == 0 && i + 1 < argc) {
            flip = (uint32_t)strtoul(argv[++i], NULL, 0);
            if (flip < 1 || flip > GAME_RES_COUNT) {
                fprintf(stderr, "--flip %s: a resource id is 1 to 0x%X\n", argv[i], GAME_RES_COUNT);
                return 2;
            }
        } else {
            fprintf(stderr, "Usage: rom_loader [--flip ID]\n");
            return 2;
        }
    }

    path = rom_find(NULL);
    status = rom_load(path, &rom);
    if (status == ROM_NOT_FOUND && (env == NULL || env[0] == '\0')) {
        printf("SKIP: no ROM found (set TD_ROM to your ROM, or put it in the decompilation's folder as "
               "baserom.z64)\n");
        return EXIT_SKIP;
    }
    if (status != ROM_OK) {
        printf("FAIL: ROM %s: %s\n", path, rom_status_text(status));
        rom_free(&rom);
        return EXIT_FAILURE;
    }
    printf("ROM: %s\n", path);
    if (getenv("TD_MANIFEST") != NULL && getenv("TD_MANIFEST")[0] != '\0') {
        g_results = calloc(GAME_RES_COUNT + 1, sizeof(*g_results));
        if (g_results == NULL) {
            printf("FAIL: out of memory\n");
            rom_free(&rom);
            return EXIT_FAILURE;
        }
    }

    ok = scan_rom(&rom, flip, &scan) && check_digest(&scan, path);
    if (ok && flip != 0) {
        printf("FAIL: --flip 0x%X did not change the digest\n", flip);
        ok = 0;
    }
    if (ok) {
        printf("%u resources loaded with the game's loader (%u compressed, %llu bytes), digest %s\n", scan.loaded,
               scan.compressed, scan.bytes, scan.digest);
        ok = check_order(&rom, ROM_ORDER_V64) && check_order(&rom, ROM_ORDER_N64) && check_changed(&rom);
    }
    if (ok) {
        printf("PASS: %u resources loaded and they match the known digest; the .v64 and .n64 copies give the same "
               "digest, and a copy with one byte changed is refused\n",
               scan.loaded);
    }
    free(g_results);
    rom_free(&rom);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
