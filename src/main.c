#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "rom.h"
#ifdef TD_GAME_CODE
#include "resources.h"
#endif

#ifndef TD_VERSION
#define TD_VERSION "dev"
#endif

/* Reads a resource id written in hex with 0x (0x1DE9) or in decimal (7657). Returns 0 if the text is not one. */
static uint32_t parse_res_id(const char *text) {
    int base = 10;
    char *end;
    unsigned long value;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    /* strtoul would also accept leading spaces and a minus sign. */
    if (!isxdigit((unsigned char)text[0])) {
        return 0;
    }
    value = strtoul(text, &end, base);
    if (*end != '\0' || value > GAME_RES_COUNT) {
        return 0;
    }
    return (uint32_t)value;
}

#ifdef TD_GAME_CODE
/* Runs the game's own ROM loader, compiled from the decompilation, on the checked ROM: with res_id 0 it loads every
 * resource and prints a summary, else only resource res_id, and prints its size and first bytes. A breakpoint in the
 * decompilation's src/main/func_800037F4.c (the game's LZSS decompressor) stops in here for each compressed one. */
static void run_game_loader(const Rom *rom, uint32_t res_id) {
    GameResDesc desc;
    uint8_t *buf;

    rom_attach(rom);
    res_init();
    if (res_id != 0) {
        int i, shown;

        buf = res_load(res_id, &desc);
        printf("Loaded: resource 0x%04X (%u), %d bytes", res_id, res_id, (int)desc.size);
        if (desc.packed > 0) {
            printf(", unpacked from %d bytes at ROM 0x%07X\n", (int)desc.packed, desc.start);
        } else {
            printf(", copied from ROM 0x%07X\n", desc.start);
        }
        shown = desc.size < 16 ? (int)desc.size : 16;
        printf("Bytes:  ");
        for (i = 0; i < shown; i++) {
            printf("%02X%s", buf[i], i + 1 < shown ? " " : "");
        }
        printf("%s\n", desc.size > 16 ? " ..." : "");
        free(buf);
    } else {
        uint32_t id, loaded = 0, compressed = 0;
        unsigned long long bytes = 0;

        for (id = 1; id <= GAME_RES_COUNT; id++) {
            buf = res_load(id, &desc);
            loaded++;
            if (desc.packed > 0) {
                compressed++;
            }
            bytes += (unsigned long long)desc.size;
            free(buf);
        }
        printf("Loaded: %u resources with the game's loader, %u of them compressed, %llu bytes\n", loaded, compressed,
               bytes);
    }
    res_shutdown();
}
#endif

int main(int argc, char **argv) {
    const char *rom_arg = NULL;
    const char *path;
    uint32_t res_id = 0;
    RomStatus status;
    Rom rom;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
            res_id = parse_res_id(argv[++i]);
            if (res_id == 0) {
                fprintf(stderr, "--res %s: a resource id is 1 to %u, in decimal or in hex such as 0x1DE9\n", argv[i],
                        GAME_RES_COUNT);
                return 1;
            }
#ifndef TD_GAME_CODE
            fprintf(stderr, "--res: this build has no game code (README.md, Building, says how to build it)\n");
            return 1;
#endif
        } else if (strncmp(argv[i], "--", 2) == 0 || rom_arg != NULL) {
            fprintf(stderr, "Usage: turnbuckle-dive [--res ID] [ROM]\n");
            return 1;
        } else {
            rom_arg = argv[i];
        }
    }
    path = rom_find(rom_arg);

    printf("Turnbuckle Dive %s\n", TD_VERSION);
    status = rom_load(path, &rom);
    if (status != ROM_OK && status != ROM_WRONG_HASH) {
        /* stdout is buffered when it is not a terminal; flush it so messages keep their order. */
        fflush(stdout);
        fprintf(stderr, "ROM %s: %s\n", path, rom_status_text(status));
        fprintf(stderr, "Pass the path to your ROM as the first argument, or set TD_ROM.\n");
        return 1;
    }
    printf("ROM:    %s\n", path);
    printf("Size:   %zu bytes (%zu MiB)\n", rom.size, rom.size >> 20);
    printf("Order:  %s\n", rom_order_text(rom.order));
    printf("Name:   %s\n", rom.name);
    printf("SHA-1:  %s\n", rom.sha1);
    if (status == ROM_WRONG_HASH) {
        fflush(stdout);
        fprintf(stderr, "This ROM is not supported. Expected SHA-1 %s.\n", TD_ROM_SHA1);
        rom_free(&rom);
        return 2;
    }
    printf("Status: supported ROM\n");
#ifdef TD_GAME_CODE
    run_game_loader(&rom, res_id);
#endif
    rom_free(&rom);
    return 0;
}
