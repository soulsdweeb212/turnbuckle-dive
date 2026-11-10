/* The handle table behind port.h's PTR_TO_SLOT and SLOT_TO_PTR. Some of the game's records keep their N64 layout on the
 * PC, because many game files reach their fields at N64 offsets, and such a record has only 4 bytes for each address it
 * holds. A 64-bit pointer does not fit there, so on the PC the field holds a 32-bit handle instead: a number that this
 * table turns back into the address. The decompilation's docs/port.md says which fields hold handles.
 *
 * The rules:
 * - One handle per address. The first port_ptr_to_slot of an address gives it the next free number; every later call
 *   with the same address gives the same number back, for the whole run. A handle is never released: when the game
 *   frees a heap block and the heap later gives out the same address again, the old handle names the new block, just
 *   as the N64's old 32-bit address would.
 * - NULL is handle 0 and handle 0 is NULL, without the table.
 * - port_slot_to_ptr of a number the table never gave out stops the program with port_fatal. A handle field that holds
 *   a raw value or a cut pointer stops there, instead of being followed.
 * - The table holds at most PORT_SLOT_CAP handles, 0 included. One more stops the program with a message. A match is
 *   expected to use about 1400.
 *
 * Two threads use the table: the game's main thread and its render thread both turn addresses into handles, and the
 * render thread reads handles while the main thread adds new ones.
 * - Adding a handle takes a lock, so two threads never give out one number twice or one address two numbers.
 * - Reading a handle takes no lock as a rule. The array of addresses is one fixed block that never moves, and a
 *   handle's entry is written, by an atomic store, before the handle leaves the table. When the handle reached the
 *   reader through something that orders memory between threads (a queue, a lock), the reader's atomic load sees the
 *   entry. But the game also hands handles over through plain fields of its records: the main thread stores one, the
 *   render thread reads it, as the N64's single processor allowed. On ARM64 processors (Apple Silicon Macs, ARM
 *   Windows) the store of the field can then be seen before the store of the entry, and the reader finds the entry
 *   still empty. So when the reader finds an empty entry, it takes the lock and loads the entry again. The writer
 *   stored the entry before it let go of the lock, and gave the handle out only after that, so behind the lock the
 *   entry is there; only a number the table never gave out is still empty, and stops the program. On x86-64 the first
 *   load already sees the entry, and the lock is taken only for that stop.
 *
 * The lock is a pthread mutex, or an SRWLOCK on Windows. Both can be set up by an initialiser in the variable's
 * definition, so the table needs no start-up call and has no first-use race. C11's own threads (<threads.h>) would not
 * do: macOS's C library does not have them, Visual Studio has had them only since 2022, and their mutex needs a
 * start-up call. */
#include <stdlib.h>

#include "game.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

/* ---- The lock, and the atomic load and store of one entry ---- */

#ifdef _WIN32
static SRWLOCK g_lock = SRWLOCK_INIT;
static void table_lock(void) {
    AcquireSRWLockExclusive(&g_lock);
}
static void table_unlock(void) {
    ReleaseSRWLockExclusive(&g_lock);
}
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void table_lock(void) {
    pthread_mutex_lock(&g_lock);
}
static void table_unlock(void) {
    pthread_mutex_unlock(&g_lock);
}
#endif

/* GCC and Clang (clang-cl too) have the __atomic built-ins. Visual Studio's C compiler has C11's <stdatomic.h> only
 * behind an experimental option, so with it the entry is volatile: for 64-bit Windows on x86-64, the only Windows the
 * port is planned for (D7), Visual Studio gives a volatile store release order and a volatile load acquire order, as
 * the documentation of its /volatile:ms option says. CMakeLists.txt sets that option for this file, because on ARM
 * Windows it is not the default. */
#if defined(__GNUC__) || defined(__clang__)
static void store_release(void **entry, void *p) {
    __atomic_store_n(entry, p, __ATOMIC_RELEASE);
}
static void *load_acquire(void **entry) {
    return __atomic_load_n(entry, __ATOMIC_ACQUIRE);
}
#elif defined(_MSC_VER)
static void store_release(void **entry, void *p) {
    *(void *volatile *)entry = p;
}
static void *load_acquire(void **entry) {
    return *(void *volatile *)entry;
}
#else
#error "src/slots.c needs atomic loads and stores for this compiler"
#endif

/* ---- The table ---- */

/* The address of each handle; entry 0 stays NULL. A static array: the system gives it memory only for the pages the
 * table writes, so its 8 MiB cost little until they are used, and it never moves. */
static void *g_slots[PORT_SLOT_CAP];
/* The highest handle given out so far, which is also how many there are. Changed only under the lock. */
static uint32_t g_count;

/* The other direction, address to handle, used only under the lock: a hash table whose buckets each hold a handle (0
 * for an empty bucket), looked up by the handle's address in g_slots. It is kept at most half full, so a lookup finds
 * its bucket or an empty one within a few steps, and doubles when it would be fuller. */
static uint32_t *g_index;
static uint32_t g_index_size; /* number of buckets, a power of 2 */

/* Mixes the address's bits so that addresses a few bytes apart land in buckets far apart (the last step of the
 * MurmurHash3 hash function). */
static uint32_t hash_address(const void *p) {
    uint64_t x = (uint64_t)(uintptr_t)p;

    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDu;
    x ^= x >> 33;
    x *= 0xC4CEB9FE1A85EC53u;
    x ^= x >> 33;
    return (uint32_t)x;
}

/* The bucket that holds p's handle, or the empty bucket where it would go. */
static uint32_t *find_bucket(const void *p) {
    uint32_t mask = g_index_size - 1;
    uint32_t i = hash_address(p) & mask;

    while (g_index[i] != 0 && g_slots[g_index[i]] != p) {
        i = (i + 1) & mask;
    }
    return &g_index[i];
}

/* Makes the hash table twice as big (1024 buckets the first time) and puts every handle back into it. */
static void grow_index(void) {
    uint32_t size = g_index_size != 0 ? g_index_size * 2 : 1024;
    uint32_t *index = calloc(size, sizeof(*index));
    uint32_t handle;

    if (index == NULL) {
        port_fatal("PTR_TO_SLOT: no memory for a hash table of %u handles", size);
    }
    free(g_index);
    g_index = index;
    g_index_size = size;
    for (handle = 1; handle <= g_count; handle++) {
        *find_bucket(g_slots[handle]) = handle;
    }
}

uint32_t port_ptr_to_slot(void *p) {
    uint32_t *bucket;
    uint32_t handle;

    if (p == NULL) {
        return 0;
    }
    table_lock();
    if ((g_count + 1) * 2 > g_index_size) {
        grow_index();
    }
    bucket = find_bucket(p);
    handle = *bucket;
    if (handle == 0) {
        if (g_count + 1 >= PORT_SLOT_CAP) {
            port_fatal("PTR_TO_SLOT: the handle table is full: all 0x%X handles are in use, none is left for %p",
                       (unsigned)PORT_SLOT_CAP, p);
        }
        handle = g_count + 1;
        store_release(&g_slots[handle], p);
        g_count = handle;
        *bucket = handle;
    }
    table_unlock();
    return handle;
}

void *port_slot_to_ptr(uint32_t slot) {
    void *p;

    if (slot == 0) {
        return NULL;
    }
    p = slot < PORT_SLOT_CAP ? load_acquire(&g_slots[slot]) : NULL;
    if (p == NULL && slot < PORT_SLOT_CAP) {
        /* Empty: either a number never given out, or a new handle whose entry this thread cannot see yet (the header
         * comment says when). Every entry is written under the lock, so behind it the entry is there if it exists. */
        table_lock();
        p = g_slots[slot];
        table_unlock();
    }
    if (p == NULL) {
        port_fatal("SLOT_TO_PTR: handle 0x%X was never issued", slot);
    }
    return p;
}

uint32_t port_slot_count(void) {
    uint32_t count;

    table_lock();
    count = g_count;
    table_unlock();
    return count;
}
