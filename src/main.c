#include <stdio.h>
#include <stdlib.h>

#include "rom.h"

#ifndef TD_VERSION
#define TD_VERSION "dev"
#endif

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* The ROM path: the first argument, else $TD_ROM, else baserom.z64 in the current folder, else (developer builds) the
 * decompilation's baserom.z64 in the folder next to this repo. */
static const char *find_rom(int argc, char **argv) {
    static const char *const candidates[] = {
        "baserom.z64",
#ifdef TD_SOURCE_DIR
        TD_SOURCE_DIR "/../source/baserom.z64",
#endif
    };
    const char *env = getenv("TD_ROM");
    size_t i;

    if (argc > 1) {
        return argv[1];
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

int main(int argc, char **argv) {
    const char *path = find_rom(argc, argv);
    RomStatus status;
    Rom rom;

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
    rom_free(&rom);
    return 0;
}
