/* The program's memory, for the developer match's report (src/dev/dev_match.c, docs/GAME_CODE.md, "The developer
 * match"). C and C++. */
#ifndef TD_DEV_MEMORY_H
#define TD_DEV_MEMORY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Bytes that malloc and new have handed out and not had back: on Linux what the C library says it holds for the
     * program (mallinfo2), in the asan preset what AddressSanitizer says, on Windows the busy blocks of the process's
     * heap, which the C library allocates from. 0 where the system cannot say. */
    uint64_t heap;
    /* Bytes of the program in memory now, and the most so far: the resident set on Linux, the working set on
     * Windows. 0 where the system cannot say. */
    uint64_t resident;
    uint64_t resident_peak;
} DevMemory;

/* Reads the program's memory now. On Windows it walks the heap, which takes a few milliseconds. */
void dev_memory_read(DevMemory *out);

#ifdef __cplusplus
}
#endif

#endif
