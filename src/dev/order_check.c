/* The byte-order check (the "order" preset, TD_ORDER_CHECK; docs/TESTING.md, "The byte-order check").
 *
 * The N64 stores a number's bytes big end first, a PC little end first. Game code that writes a value at one width
 * and reads its bytes at another (a byte of an s32, two u16 fields as one u32) gets different values on the two
 * machines, and the PC then plays differently. The compiler cannot see it when the two widths are in different files
 * or behind a pointer cast, so this check watches the game run instead.
 *
 * GCC's -fsanitize=thread, given to the game files only, puts a call before every read and write of memory with its
 * address and width (__tsan_read4 and the like). This file provides those calls; the thread sanitizer's own library
 * is not linked. For every byte the game code writes, the check records the width and place of the value it is part
 * of (its shape) and the line that wrote it. When game code reads bytes whose shapes differ from the read, the check
 * works out both values: the one the PC reads, and the one the N64 would read, whose bytes are each value's bytes in
 * the other order. When the two differ, it counts the read's line and the line that wrote the bytes. At the end of
 * the program it writes the counts to a file (TD_ORDER_CHECK_OUT, or order_check.txt in the current folder), which
 * tools/order_check.py turns into file names and lines.
 *
 * Copies are not reports: a read of whole values, followed soon by a write of the same width and the same bytes, is
 * a copy, and the copy's bytes keep their shapes. So is a read of one half of an 8-byte value as 4 bytes, the way the
 * N64's 32-bit code moves a 64-bit value in two halves; the halves arrive in the same order. A read of part of a value
 * otherwise differs from the N64's at the read itself, and is reported there. memcpy, memmove and memset (which the game's bcopy
 * and bzero call) are wrapped by the linker (-Wl,--wrap) to carry or clear shapes, and a new heap block starts with
 * none (calloc clears memory without memset). Bytes no game code has written
 * (the ROM's data, resources the port converted) have no shape and are taken to match any read. The stack is left out:
 * a function's locals reuse the bytes of earlier calls.
 *
 * Only the thread that first runs game code is watched; the program runs all game code on that thread. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "order_check.h"

void *__real_memcpy(void *dst, const void *src, size_t n);
void *__real_memmove(void *dst, const void *src, size_t n);
void *__real_memset(void *dst, int c, size_t n);

/* A byte's shape: 0 for none, else SHAPE_SET | log2(width) << 3 | the byte's place in its value. */
#define SHAPE_SET 0x80
#define SHAPE(lg, off) ((uint8_t)(SHAPE_SET | (lg) << 3 | (off)))
#define SHAPE_WIDTH(s) (1u << (((s) >> 3) & 3))
#define SHAPE_OFF(s) ((s) & 7)

/* Shapes and writers of the watched bytes, in chunks of 64 KiB of addresses found through a hash table. */
#define CHUNK_BITS 16
#define CHUNK_SIZE (1u << CHUNK_BITS)
#define TABLE_BITS 14
typedef struct {
    uintptr_t key;
    uint8_t shape[CHUNK_SIZE];
    uint32_t site[CHUNK_SIZE]; /* the writer's code address, from the program's start */
} Chunk;
static Chunk *g_table[1u << TABLE_BITS];
static Chunk *g_last;

static uintptr_t g_stack_lo, g_stack_hi;
static uintptr_t g_base; /* where the program is loaded: code addresses are written from here */
static int g_ready;

static Chunk *chunk_of(uintptr_t a, int make) {
    uintptr_t key = a >> CHUNK_BITS;
    size_t h;

    if (g_last != NULL && g_last->key == key) {
        return g_last;
    }
    h = (size_t)((key * 0x9E3779B97F4A7C15ull) >> (64 - TABLE_BITS));
    for (;;) {
        Chunk *c = g_table[h];

        if (c == NULL) {
            if (!make) {
                return NULL;
            }
            c = calloc(1, sizeof(*c));
            if (c == NULL) {
                fprintf(stderr, "order check: out of memory\n");
                abort();
            }
            c->key = key;
            g_table[h] = c;
        }
        if (c->key == key) {
            g_last = c;
            return c;
        }
        h = (h + 1) & ((1u << TABLE_BITS) - 1);
    }
}

static uint8_t shape_at(uintptr_t a, uint32_t *site) {
    Chunk *c = chunk_of(a, 0);

    if (c == NULL) {
        return 0;
    }
    if (site != NULL) {
        *site = c->site[a & (CHUNK_SIZE - 1)];
    }
    return c->shape[a & (CHUNK_SIZE - 1)];
}

static void set_shape(uintptr_t a, uint8_t shape, uint32_t site) {
    Chunk *c = chunk_of(a, shape != 0);

    if (c != NULL) {
        c->shape[a & (CHUNK_SIZE - 1)] = shape;
        c->site[a & (CHUNK_SIZE - 1)] = site;
    }
}

static int watched(uintptr_t a) {
    return g_ready && !(a >= g_stack_lo && a < g_stack_hi);
}

static uint64_t load(uintptr_t a, uint32_t w) {
    uint64_t v = 0;

    __real_memcpy(&v, (const void *)a, w);
    return v;
}

static uint32_t site_of(void *pc) {
    return (uint32_t)((uintptr_t)pc - g_base);
}

/* ---- reports ---- */

typedef struct {
    uint32_t read_site, write_site;
    uint8_t read_width, write_shape;
    uint32_t count, frame;
    uintptr_t addr;
} Report;
static Report *g_reports;
static size_t g_report_count, g_report_cap;

unsigned dev_match_frames(void) __attribute__((weak));

static void add_report(uint32_t read_site, uint32_t read_width, uint8_t write_shape, uint32_t write_site,
                       uintptr_t addr) {
    size_t i;

    for (i = 0; i < g_report_count; i++) {
        Report *r = &g_reports[i];

        if (r->read_site == read_site && r->write_site == write_site && r->read_width == read_width &&
            r->write_shape == write_shape) {
            r->count++;
            return;
        }
    }
    if (g_report_count == g_report_cap) {
        g_report_cap = g_report_cap ? g_report_cap * 2 : 256;
        g_reports = realloc(g_reports, g_report_cap * sizeof(*g_reports));
        if (g_reports == NULL) {
            abort();
        }
    }
    g_reports[g_report_count++] = (Report){ read_site, write_site, (uint8_t)read_width, write_shape, 1,
                                            dev_match_frames ? dev_match_frames() : 0, addr };
}

/* ---- reads ---- */

/* A read whose bytes hold whole values of other shapes (or half of an 8-byte value), kept for a few accesses in case
 * a write copies it. */
typedef struct {
    uintptr_t a;
    uint64_t v;
    uint32_t w, site, write_site;
    uint8_t live, differs, write_shape;
} Read;
#define READS 4
static Read g_reads[READS];
static unsigned g_read_next;

/* The write announced last: its bytes are in memory at the next call. */
static struct {
    uintptr_t a;
    uint32_t w, site;
    int live;
} g_write;

static void drop_read(Read *r) {
    if (r->live && r->differs) {
        add_report(r->site, r->w, r->write_shape, r->write_site, r->a);
    }
    r->live = 0;
}

static void settle_write(void) {
    uint64_t v;
    unsigned k;
    uint32_t i;

    if (!g_write.live) {
        return;
    }
    g_write.live = 0;
    v = load(g_write.a, g_write.w);
    for (k = 0; k < READS; k++) {
        Read *r = &g_reads[(g_read_next + READS - 1 - k) % READS];

        if (r->live && r->w == g_write.w && r->v == v && r->a != g_write.a) {
            /* A copy: the bytes keep the shapes they had where they were read. */
            for (i = 0; i < g_write.w; i++) {
                uint32_t site = 0;
                uint8_t s = shape_at(r->a + i, &site);
                set_shape(g_write.a + i, s, site);
            }
            r->live = 0;
            return;
        }
    }
    for (i = 0; i < g_write.w; i++) {
        set_shape(g_write.a + i, SHAPE(__builtin_ctz(g_write.w), i), g_write.site);
    }
}

static void on_read(uintptr_t a, uint32_t w, void *pc) {
    uint8_t n64[8];
    uint64_t pc_value, n64_value = 0;
    int same = 1, whole = 1, half = (w == 4);
    uint8_t first_shape = 0;
    uint32_t first_site = 0, i;

    settle_write();
    if (!watched(a)) {
        return;
    }
    for (i = 0; i < w; i++) {
        uint32_t site = 0;
        uint8_t s = shape_at(a + i, &site);

        if (s == 0) {
            n64[i] = *(const uint8_t *)(a + w - 1 - i);
            continue;
        }
        {
            uint32_t sw = SHAPE_WIDTH(s), off = SHAPE_OFF(s);
            uintptr_t start = a + i - off;

            if (sw != w || start != a) {
                if (same) {
                    first_shape = s;
                    first_site = site;
                }
                same = 0;
            }
            if (start < a || start + sw > a + w) {
                whole = 0;
            }
            if (sw != 8 || (a - start) % 4 != 0) {
                half = 0;
            }
            n64[i] = *(const uint8_t *)(start + sw - 1 - off);
        }
    }
    if (same) {
        return;
    }
    pc_value = load(a, w);
    for (i = 0; i < w; i++) {
        n64_value = n64_value << 8 | n64[i];
    }
    if (!whole && !half) {
        if (pc_value != n64_value) {
            add_report(site_of(pc), w, first_shape, first_site, a);
        }
        return;
    }
    {
        Read *r = &g_reads[g_read_next];

        drop_read(r);
        *r = (Read){ a, pc_value, w, site_of(pc), first_site, 1, pc_value != n64_value, first_shape };
        g_read_next = (g_read_next + 1) % READS;
    }
}

static void on_write(uintptr_t a, uint32_t w, void *pc) {
    settle_write();
    if (!watched(a)) {
        return;
    }
    g_write.a = a;
    g_write.w = w;
    g_write.site = site_of(pc);
    g_write.live = 1;
}

/* A copy of n bytes from src to dst (or of nothing known, src 0): dst takes src's shapes. */
static void copy_shapes(uintptr_t dst, uintptr_t src, size_t n) {
    size_t i;

    if (!g_ready) {
        return;
    }
    settle_write();
    if (dst < src) {
        for (i = 0; i < n; i++) {
            uint32_t site = 0;
            uint8_t s = (src != 0 && watched(src + i)) ? shape_at(src + i, &site) : 0;
            if (watched(dst + i)) {
                set_shape(dst + i, s, site);
            }
        }
    } else {
        for (i = n; i-- > 0;) {
            uint32_t site = 0;
            uint8_t s = (src != 0 && watched(src + i)) ? shape_at(src + i, &site) : 0;
            if (watched(dst + i)) {
                set_shape(dst + i, s, site);
            }
        }
    }
}

/* ---- the port's conversions (order_check.h) ---- */

void order_check_value(const void *p, unsigned width) {
    unsigned i;

    if (!g_ready || !watched((uintptr_t)p)) {
        return;
    }
    settle_write();
    for (i = 0; i < width; i++) {
        set_shape((uintptr_t)p + i, SHAPE(__builtin_ctz(width), i), 0);
    }
}

void order_check_forget(const void *p, size_t n) {
    copy_shapes((uintptr_t)p, 0, n);
}

/* ---- the end ---- */

static void write_reports(void) {
    const char *path = getenv("TD_ORDER_CHECK_OUT");
    FILE *f;
    size_t i;
    unsigned k;

    settle_write();
    for (k = 0; k < READS; k++) {
        drop_read(&g_reads[k]);
    }
    if (path == NULL || path[0] == '\0') {
        path = "order_check.txt";
    }
    f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "order check: cannot write %s\n", path);
        return;
    }
    fprintf(f, "# count\tframe\tread_site\tread_width\twrite_site\twrite_width\twrite_offset\taddress\n");
    for (i = 0; i < g_report_count; i++) {
        Report *r = &g_reports[i];

        fprintf(f, "%u\t%u\t%#x\t%u\t%#x\t%u\t%u\t%#lx\n", r->count, r->frame, r->read_site, r->read_width,
                r->write_site, r->write_shape ? SHAPE_WIDTH(r->write_shape) : 0, SHAPE_OFF(r->write_shape),
                (unsigned long)r->addr);
    }
    fclose(f);
    fprintf(stderr, "order check: %zu read sites that differ from the N64, listed in %s\n", g_report_count, path);
}

/* ---- the calls GCC's -fsanitize=thread puts into the game files ---- */

void __tsan_init(void) {
    pthread_attr_t attr;
    Dl_info info;

    if (g_ready) {
        return;
    }
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *lo;
        size_t size;

        if (pthread_attr_getstack(&attr, &lo, &size) == 0) {
            g_stack_lo = (uintptr_t)lo;
            g_stack_hi = (uintptr_t)lo + size;
        }
        pthread_attr_destroy(&attr);
    }
    if (dladdr((void *)(uintptr_t)__tsan_init, &info) != 0) {
        g_base = (uintptr_t)info.dli_fbase;
    }
    g_ready = 1;
    atexit(write_reports);
}

void __tsan_func_entry(void *pc) {
    (void)pc;
}

void __tsan_func_exit(void) {
}

#define ACCESS(n)                                                                                                     \
    void __tsan_read##n(void *a) {                                                                                    \
        on_read((uintptr_t)a, n, __builtin_return_address(0));                                                        \
    }                                                                                                                 \
    void __tsan_write##n(void *a) {                                                                                   \
        on_write((uintptr_t)a, n, __builtin_return_address(0));                                                       \
    }
ACCESS(1)
ACCESS(2)
ACCESS(4)
ACCESS(8)
#define UNALIGNED(n)                                                                                                  \
    void __tsan_unaligned_read##n(void *a) {                                                                          \
        on_read((uintptr_t)a, n, __builtin_return_address(0));                                                        \
    }                                                                                                                 \
    void __tsan_unaligned_write##n(void *a) {                                                                         \
        on_write((uintptr_t)a, n, __builtin_return_address(0));                                                       \
    }
UNALIGNED(2)
UNALIGNED(4)
UNALIGNED(8)

/* A whole record read and written (a struct assignment): GCC calls these before it copies the bytes. */
static uintptr_t g_range_src;
static size_t g_range_size;

void __tsan_read_range(void *a, unsigned long n) {
    settle_write();
    g_range_src = (uintptr_t)a;
    g_range_size = n;
}

void __tsan_write_range(void *a, unsigned long n) {
    copy_shapes((uintptr_t)a, g_range_size == n ? g_range_src : 0, n);
    g_range_src = 0;
    g_range_size = 0;
}

void __tsan_read16(void *a) {
    __tsan_read_range(a, 16);
}

void __tsan_write16(void *a) {
    __tsan_write_range(a, 16);
}

void __tsan_unaligned_read16(void *a) {
    __tsan_read_range(a, 16);
}

void __tsan_unaligned_write16(void *a) {
    __tsan_write_range(a, 16);
}

/* ---- the C library's copies, through the linker's --wrap ---- */

void *__wrap_memcpy(void *dst, const void *src, size_t n) {
    copy_shapes((uintptr_t)dst, (uintptr_t)src, n);
    return __real_memcpy(dst, src, n);
}

void *__wrap_memmove(void *dst, const void *src, size_t n) {
    copy_shapes((uintptr_t)dst, (uintptr_t)src, n);
    return __real_memmove(dst, src, n);
}

void *__wrap_memset(void *dst, int c, size_t n) {
    copy_shapes((uintptr_t)dst, 0, n);
    return __real_memset(dst, c, n);
}
