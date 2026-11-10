/* Tests of the handle table behind port.h's PTR_TO_SLOT and SLOT_TO_PTR (src/slots.c).
 *
 * Without arguments it checks what the game relies on, and prints "FAIL" and a reason for each check that does not
 * hold: NULL and handle 0, the same handle for the same address, different handles for different addresses (also after
 * the table has grown), and several threads adding and reading handles at once.
 *
 * With an argument it does one thing that must stop the program, and CTest passes it only when the output holds the
 * table's message (CMakeLists.txt):
 * - --never-issued reads handle 0x3039, which the table has not given out;
 * - --out-of-range reads handle 0x80123456, an N64 address left in a handle field, far beyond the table's end;
 * - --full gives out every handle the table holds, then asks for one more.
 * If the program goes on, it prints "FAIL" and ends with exit code 1. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

/* ---- Threads: pthreads, or Windows' own ---- */

typedef struct Job Job;
struct Job {
    void (*run)(Job *job);
    uint32_t first;    /* where in the shared addresses this thread starts */
    uint32_t *handles; /* the handle it got for each of the shared addresses */
    int failures;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
};

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID arg) {
    Job *job = arg;

    job->run(job);
    return 0;
}
static void start(Job *job) {
    job->thread = CreateThread(NULL, 0, thread_main, job, 0, NULL);
    if (job->thread == NULL) {
        printf("FAIL: could not start a thread\n");
        exit(1);
    }
}
static void join(Job *job) {
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
}
#else
static void *thread_main(void *arg) {
    Job *job = arg;

    job->run(job);
    return NULL;
}
static void start(Job *job) {
    if (pthread_create(&job->thread, NULL, thread_main, job) != 0) {
        printf("FAIL: could not start a thread\n");
        exit(1);
    }
}
static void join(Job *job) {
    pthread_join(job->thread, NULL);
}
#endif

/* ---- The checks ---- */

/* Addresses to give handles to. They are never written or read through: the table only keeps them. Neighbouring bytes
 * make neighbouring addresses, the hardest case for the table's hash. */
#define MANY 5000u
#define SHARED 20000u
#define WRITERS 4u
static char g_one[2];
static char g_many[MANY];
static char g_shared[SHARED];
static uint32_t g_many_handles[MANY];
static uint32_t g_writer_handles[WRITERS][SHARED];

static int check(int ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        return 1;
    }
    return 0;
}

/* A writer: gives each of the shared addresses a handle, starting at its own place in them, and checks right away that
 * each handle leads back to its address. */
static void run_writer(Job *job) {
    uint32_t n;

    for (n = 0; n < SHARED; n++) {
        uint32_t i = (job->first + n) % SHARED;
        uint32_t handle = port_ptr_to_slot(&g_shared[i]);

        job->handles[i] = handle;
        if (port_slot_to_ptr(handle) != &g_shared[i]) {
            job->failures++;
        }
    }
}

/* The reader: while the writers add handles, reads the handles of g_many over and over, without the table's lock. */
static void run_reader(Job *job) {
    uint32_t pass;
    uint32_t i;

    for (pass = 0; pass < 100; pass++) {
        for (i = 0; i < MANY; i++) {
            if (port_slot_to_ptr(g_many_handles[i]) != &g_many[i]) {
                job->failures++;
            }
        }
    }
}

static int check_threads(void) {
    static Job writers[WRITERS];
    static Job reader;
    static unsigned char seen[PORT_SLOT_CAP];
    uint32_t before = port_slot_count();
    uint32_t w;
    uint32_t i;
    int failures = 0;

    reader.run = run_reader;
    start(&reader);
    for (w = 0; w < WRITERS; w++) {
        writers[w].run = run_writer;
        writers[w].first = w * (SHARED / WRITERS);
        writers[w].handles = g_writer_handles[w];
        start(&writers[w]);
    }
    for (w = 0; w < WRITERS; w++) {
        join(&writers[w]);
        failures += check(writers[w].failures == 0, "threads: a writer's handle did not lead back to its address");
    }
    join(&reader);
    failures += check(reader.failures == 0, "threads: the reader got a wrong address while the writers added handles");

    /* Each address got one handle, the same in every thread, and no two addresses got the same handle. */
    for (i = 0; i < SHARED; i++) {
        uint32_t handle = g_writer_handles[0][i];

        for (w = 1; w < WRITERS; w++) {
            if (g_writer_handles[w][i] != handle) {
                return check(0, "threads: two threads got different handles for one address");
            }
        }
        if (handle <= before || handle > before + SHARED || seen[handle]) {
            return check(0, "threads: a handle was given to two addresses, or is out of the expected range");
        }
        seen[handle] = 1;
    }
    failures += check(port_slot_count() == before + SHARED, "threads: the count is not one per new address");
    return failures;
}

static int check_all(void) {
    uint32_t a;
    uint32_t b;
    uint32_t i;
    int failures = 0;

    failures += check(port_slot_count() == 0, "a new table already has handles");
    failures += check(port_ptr_to_slot(NULL) == 0, "NULL does not give handle 0");
    failures += check(port_slot_to_ptr(0) == NULL, "handle 0 does not give NULL");
    failures += check(port_slot_count() == 0, "NULL took a handle");

    a = port_ptr_to_slot(&g_one[0]);
    failures += check(a == 1, "the first address does not get handle 1");
    failures += check(port_slot_to_ptr(a) == &g_one[0], "a handle does not lead back to its address");
    failures += check(port_ptr_to_slot(&g_one[0]) == a, "the same address gives a second handle");
    failures += check(port_slot_count() == 1, "asking again for one address took another handle");

    b = port_ptr_to_slot(&g_one[1]);
    failures += check(b != 0 && b != a, "two addresses one byte apart share a handle");
    failures += check(port_slot_to_ptr(b) == &g_one[1] && port_slot_to_ptr(a) == &g_one[0],
                      "two handles do not lead back to their own addresses");
    failures += check(port_slot_count() == 2, "two addresses do not count as two handles");

    /* Enough addresses to make the table's hash grow several times; each handle must stay the same across. */
    for (i = 0; i < MANY; i++) {
        g_many_handles[i] = port_ptr_to_slot(&g_many[i]);
    }
    for (i = 0; i < MANY; i++) {
        if (g_many_handles[i] != i + 3 || port_ptr_to_slot(&g_many[i]) != g_many_handles[i] ||
            port_slot_to_ptr(g_many_handles[i]) != &g_many[i]) {
            failures += check(0, "many addresses: a handle is not the next number, or changed as the table grew");
            break;
        }
    }
    failures += check(port_slot_count() == MANY + 2, "many addresses: the count is not one per address");
    failures += check(port_ptr_to_slot(&g_one[0]) == a && port_ptr_to_slot(&g_one[1]) == b,
                      "the first two handles changed as the table grew");

    failures += check_threads();
    return failures;
}

/* ---- The cases that must stop ---- */

/* port_fatal ends with abort(), and CTest fails a test that aborts whatever it printed, so a case that must stop turns
 * abort into a plain exit with code 3, and CTest judges it by its output. On Windows, abort() would also first show a
 * dialog or report the crash to Windows, which would hold up a test run; _set_abort_behavior turns both off. */
static void on_abort(int sig) {
    (void)sig;
    _Exit(3);
}

static int must_stop(const char *mode) {
    signal(SIGABRT, on_abort);
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (strcmp(mode, "--never-issued") == 0) {
        /* A few handles exist; 0x3039 is above them. */
        port_ptr_to_slot(&g_one[0]);
        port_ptr_to_slot(&g_one[1]);
        port_slot_to_ptr(0x3039);
    } else if (strcmp(mode, "--out-of-range") == 0) {
        /* Beyond PORT_SLOT_CAP: the table must stop before it reads outside its array. */
        port_ptr_to_slot(&g_one[0]);
        port_slot_to_ptr(0x80123456u);
    } else if (strcmp(mode, "--full") == 0) {
        /* Handles 1 to PORT_SLOT_CAP - 1, one for each byte of a block, fill the table; the next address must stop. */
        char *block = malloc(PORT_SLOT_CAP);
        uint32_t i;

        if (block == NULL) {
            printf("FAIL: no memory for the test's addresses\n");
            return 1;
        }
        for (i = 0; i < PORT_SLOT_CAP - 1; i++) {
            if (port_ptr_to_slot(&block[i]) != i + 1) {
                printf("FAIL: address %u of the block did not get handle %u\n", i, i + 1);
                free(block);
                return 1;
            }
        }
        printf("slots: %u handles given out, the table is full\n", port_slot_count());
        fflush(stdout);
        port_ptr_to_slot(&block[PORT_SLOT_CAP - 1]);
        free(block);
    } else {
        printf("unknown option %s (use --never-issued, --out-of-range or --full)\n", mode);
        return 1;
    }
    printf("FAIL: %s did not stop the program\n", mode);
    return 1;
}

int main(int argc, char **argv) {
    int failures;

    if (argc > 1) {
        return must_stop(argv[1]);
    }
    failures = check_all();
    if (failures == 0) {
        printf("slots: all checks passed, %u handles given out\n", port_slot_count());
    }
    return failures != 0;
}
