/* Loading the game's resources with the game's own loader (initializeAssetSizeTable, lookupResourceDescriptor and
 * func_80003684, compiled from the decompilation), for the program and the tests. Only in builds with game code. */
#ifndef TD_RESOURCES_H
#define TD_RESOURCES_H

#include <stdint.h>

#include "game.h"

/* Starts the game's code on the attached ROM (rom_attach): fills the game's variables of main and of overlay 1's data
 * from the ROM (game_vars_load), then runs the game's initializeAssetSizeTable, which reads the resource table. */
void res_init(void);

/* Loads resource id (1 to GAME_RES_COUNT) with the game's func_80003684 into a new buffer of exactly the resource's
 * size, fills desc, and returns the buffer, which the caller frees with free(). */
uint8_t *res_load(uint32_t id, GameResDesc *desc);

/* Releases the tables res_init made. The N64 builds them once at boot and keeps them; the port's tests load several
 * ROMs in one run, so each run gives them back. */
void res_shutdown(void);

#endif
