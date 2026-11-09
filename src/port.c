/* Functions the game's C calls in the port, as the decompilation's include/port.h declares them. port_rom_read is in
 * rom.c, next to the ROM image it reads. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"

static uint32_t g_res_loaded;

/* stdout is flushed first so that everything the program printed before stays in order with the message. abort()
 * rather than exit(): under a debugger the program stops right here, and the call stack shows which game function
 * gave up. */
_Noreturn void port_fatal(const char *fmt, ...) {
    va_list args;

    fflush(stdout);
    fputs("Turnbuckle Dive stopped: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}

/* func_80003684 (the game's resource loader) calls this once a whole resource is in RAM: the one place every loaded
 * resource passes. Converting resources to the PC's byte order will happen here, in a later step; until then it checks
 * what the loader passes and counts the call. */
void port_res_loaded(uint32_t id, void *buf, int32_t size) {
    if (id < 1 || id > GAME_RES_COUNT) {
        port_fatal("port_res_loaded: resource id 0x%X is outside 1..0x%X", id, GAME_RES_COUNT);
    }
    if (size < 0 || (size > 0 && buf == NULL)) {
        port_fatal("port_res_loaded: resource 0x%X has size %d at %p", id, (int)size, buf);
    }
    g_res_loaded++;
}

uint32_t port_res_loaded_count(void) {
    return g_res_loaded;
}
