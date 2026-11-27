/* The program's memory (src/dev/dev_memory.h). */
#include "dev_memory.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* after windows.h, which it needs */
#include <psapi.h>
#else
#if defined(__SANITIZE_ADDRESS__) /* GCC */
#define TD_ASAN 1
#elif defined(__has_feature) /* Clang */
#if __has_feature(address_sanitizer)
#define TD_ASAN 1
#endif
#endif
#if defined(TD_ASAN)
#include <stddef.h>
/* AddressSanitizer's count of the bytes the program has allocated and not freed. Both GCC's and Clang's runtimes have
 * it, but only Clang ships its header (sanitizer/allocator_interface.h), so it is declared here. */
size_t __sanitizer_get_current_allocated_bytes(void);
#elif defined(__GLIBC__)
#include <malloc.h>
#endif
#endif

#if defined(_WIN32)

/* The C library (the UCRT) allocates from the process's heap: the sum of its busy blocks. HeapLock keeps the other
 * threads from changing the heap during the walk. */
static uint64_t heap_in_use(void) {
    HANDLE heap = GetProcessHeap();
    PROCESS_HEAP_ENTRY entry;
    uint64_t total = 0;

    if (!HeapLock(heap)) {
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    while (HeapWalk(heap, &entry)) {
        if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
            total += entry.cbData;
        }
    }
    HeapUnlock(heap);
    return total;
}

void dev_memory_read(DevMemory *out) {
    PROCESS_MEMORY_COUNTERS counters;

    memset(out, 0, sizeof(*out));
    out->heap = heap_in_use();
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        out->resident = counters.WorkingSetSize;
        out->resident_peak = counters.PeakWorkingSetSize;
    }
}

#else

/* The value of a "Name:   123 kB" line of /proc/self/status, in bytes; 0 if there is none. */
static uint64_t status_value(const char *text, const char *name) {
    const char *line = strstr(text, name);
    unsigned long long kib = 0;

    if (line != NULL && sscanf(line + strlen(name), " %llu", &kib) == 1) {
        return (uint64_t)kib * 1024u;
    }
    return 0;
}

void dev_memory_read(DevMemory *out) {
    char text[4096];
    size_t got = 0;
    FILE *status;

    memset(out, 0, sizeof(*out));
#if defined(TD_ASAN)
    out->heap = __sanitizer_get_current_allocated_bytes();
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    {
        struct mallinfo2 info = mallinfo2();

        out->heap = info.uordblks + info.hblkhd; /* blocks in use in the heap, and those mapped on their own */
    }
#endif
    status = fopen("/proc/self/status", "r");
    if (status != NULL) {
        got = fread(text, 1, sizeof(text) - 1, status);
        fclose(status);
    }
    text[got] = '\0';
    out->resident = status_value(text, "VmRSS:");
    out->resident_peak = status_value(text, "VmHWM:");
}

#endif
