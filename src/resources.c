#include "resources.h"

#include <stdlib.h>

#include "game_vars.h"

void res_init(void) {
    /* The game's variables of main, as the N64 starts them, and those of overlay 1's data: the viewer draws pictures
     * with two of overlay 1's functions, and func_80096D8C reads its height scale there. */
    game_vars_load(0);
    game_vars_load(1);
    initializeAssetSizeTable();
}

/* The buffer is malloc'd at exactly the resource's size rather than taken from the game's heap, so that
 * AddressSanitizer (the asan preset) reports any write by the game's loader past the resource's end. */
uint8_t *res_load(uint32_t id, GameResDesc *desc) {
    uint8_t *buf;

    if (D_800482E4 == NULL) {
        port_fatal("res_load: resource 0x%X was asked for before res_init", id);
    }
    if (id < 1 || id > GAME_RES_COUNT) {
        port_fatal("res_load: resource id 0x%X is outside 1..0x%X", id, GAME_RES_COUNT);
    }
    lookupResourceDescriptor(desc, id);
    if (desc->size < 0) {
        port_fatal("res_load: resource 0x%X has a negative size (%d)", id, (int)desc->size);
    }
    buf = malloc(desc->size > 0 ? (size_t)desc->size : 1);
    if (buf == NULL) {
        port_fatal("res_load: no memory for resource 0x%X (%d bytes)", id, (int)desc->size);
    }
    func_80003684(desc, (int32_t)id, buf);
    return buf;
}

void res_shutdown(void) {
    releaseHeapBlock(D_800482E4);
    releaseHeapBlock(D_800482E0);
    D_800482E4 = NULL;
    D_800482E0 = NULL;
}
