/* The game's heap on PC: port_heap_alloc and port_heap_free, which the game's allocateHeap and releaseHeapBlock (the
 * decompilation's src/main) call on PC.
 *
 * Why the game's own heap cannot run on a 64-bit PC: every heap block starts with a header
 * {next pointer, u32 size, u64 magic}, which is 16 bytes on the N64 but 24 on a 64-bit PC, where the pointer is 8 bytes
 * and the u64 must start at a multiple of 8.
 * - releaseHeapBlock finds a block's header at the fixed address "payload - 0x10", which on the PC is 8 bytes inside
 *   the header, so the first release would corrupt the heap.
 * - allocateHeap counts in 16-byte units but steps through blocks of sizeof(header), and it sets the heap's size
 *   through the separate symbol D_801F0004, which is the first header's size field only in the N64 layout.
 * So on PC the two game functions hand the work to these, on top of calloc and free:
 * - port_heap_alloc returns a block of at least `bytes` bytes, aligned for any type like the game's 16-byte heap
 *   units, and filled with zeros: the N64 clears its whole heap the first time it allocates, so the first use of each
 *   block finds zeros there. It rounds `bytes` up to a whole number of 16-byte units, as the game's allocateHeap does,
 *   because the game reads into that rounding: the menus' text pictures are drawn in strips that load 128 bytes of each
 *   row from rows 120 bytes wide, so the last strip's load ends 8 bytes past the picture (a picture of 10200 bytes, in
 *   a block of 10208). Read past a PC allocation of exactly 10200 bytes, those bytes could be memory the program does
 *   not own. AddressSanitizer (the asan preset) reports an access past the rounded block. It has no size limit like
 *   the N64's 2 MiB heap; if the PC itself runs out of memory, it stops the program.
 * - port_heap_free frees a block of port_heap_alloc; the game's releaseHeapBlock has already ignored NULL. */
#include <stdlib.h>

#include "game.h"
#include "dev/order_check.h"

void *port_heap_alloc(uint32_t bytes) {
    size_t rounded = ((size_t)bytes + 15) & ~(size_t)15; /* the game's 16-byte units */
    void *block = calloc(1, rounded != 0 ? rounded : 16);

    if (block == NULL) {
        port_fatal("allocateHeap: no memory for 0x%X bytes", bytes);
    }
    ORDER_CHECK_FORGET(block, rounded);
    return block;
}

void port_heap_free(void *block) {
    free(block);
}
