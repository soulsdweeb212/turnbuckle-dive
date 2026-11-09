/* Stand-ins for the game's heap functions, allocateHeap and releaseHeapBlock (src/main in the decompilation), which the
 * game's ROM loader calls: initializeAssetSizeTable allocates its two tables, and func_80003684 allocates a temporary
 * block for each compressed resource.
 *
 * Why the game's own two functions are not compiled yet: every heap block starts with a header
 * {next pointer, u32 size, u64 magic}, which is 16 bytes on the N64 but 24 on a 64-bit PC, where the pointer is 8 bytes
 * and the u64 must start at a multiple of 8.
 * - releaseHeapBlock finds a block's header at the fixed address "payload - 0x10", which on the PC is 8 bytes inside
 *   the header, so the first release would corrupt the heap.
 * - allocateHeap counts in 16-byte units but steps through blocks of sizeof(header), and it sets the heap's size
 *   through the separate symbol D_801F0004, which is the first header's size field only in the N64 layout.
 * The port plan gives those files PC code in a later step (Increment 8). Until then these stand-ins keep the contract
 * the loader relies on, on top of malloc and free:
 * - allocateHeap returns a block of at least `bytes` bytes, aligned for any type like the game's 16-byte heap units.
 *   It allocates exactly `bytes`, without the game's rounding up to 16, so that AddressSanitizer (the asan preset)
 *   reports even a one-byte write past what the game asked for. It has no size limit like the N64's 2 MiB heap; if the
 *   PC itself runs out of memory, it stops the program.
 * - releaseHeapBlock frees a block from allocateHeap; NULL is ignored, as in the game. */
#include <stdlib.h>

#include "game.h"

void *allocateHeap(uint32_t bytes) {
    void *block = malloc(bytes != 0 ? bytes : 1);

    if (block == NULL) {
        port_fatal("allocateHeap: no memory for 0x%X bytes", bytes);
    }
    return block;
}

void releaseHeapBlock(void *block) {
    free(block);
}
