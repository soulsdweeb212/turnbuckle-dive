/* The meeting point of the port and the game's C (the files compiled from the decompilation, see docs/GAME_CODE.md):
 * the game functions the port calls, the functions the port provides to the game, and the port's stand-ins for game
 * parts that are not compiled yet.
 *
 * The decompilation's include/port.h declares the same functions for the game's side. Port code includes this file
 * instead, because port.h needs the game's own headers. src/game_check.c includes both, so a build with game code
 * stops with an error if the two ever declare a function differently. C only. */
#ifndef TD_GAME_H
#define TD_GAME_H

#include <stdint.h>

/* Lets GCC and Clang check the arguments of every port_fatal call against its format string, as they do for printf. */
#if defined(__GNUC__)
#define TD_PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define TD_PRINTF_FORMAT
#endif

/* Resource ids run from 1 to GAME_RES_COUNT (RES_COUNT in port.h). */
#define GAME_RES_COUNT 0x1DE9u

/* The 16-byte resource descriptor that the game's lookupResourceDescriptor and func_80003684 write (ResDesc in
 * port.h). The two addresses are ROM addresses, that is offsets into the ROM image, never pointers. */
typedef struct {
    uint32_t start;  /* ROM address of the resource's data */
    uint32_t end;    /* ROM address where the next resource starts */
    int32_t size;    /* size in RAM once loaded (unpacked) */
    int32_t packed;  /* end - start if the resource is LZSS-compressed in ROM, else 0 */
} GameResDesc;

/* ---- Game functions the port calls (the decompilation's src/main) ---- */

/* Reads the resource table from ROM and works out every resource's size (D_800482E0, D_800482E4). The game calls it
 * once at boot; every other loader function needs it first. */
void initializeAssetSizeTable(void);
/* Writes the GameResDesc of resource id to out. Only the low 16 bits of id count, as on the N64, where the parameter is
 * a u16: 0x10001 gives resource 1. If those 16 bits are not an id from 1 to GAME_RES_COUNT, it stops the program.
 * res_load (src/resources.c) checks the whole id before it calls the game's loader. */
void lookupResourceDescriptor(void *out, uint32_t id);
/* Loads resource id into buf, which must hold its size: a compressed resource is unpacked by the game's LZSS
 * decompressor func_800037F4, any other is copied. Writes the resource's GameResDesc to out and returns out. Like
 * lookupResourceDescriptor, it only uses the low 16 bits of id. */
void *func_80003684(void *out, int32_t id, void *buf);

/* The game's heap functions (the decompilation's src/main/allocateHeap.c and releaseHeapBlock.c). On PC they hand the
 * work to port_heap_alloc and port_heap_free. */
void *allocateHeap(uint32_t bytes);
void releaseHeapBlock(void *block);

/* ---- Functions the port provides to the game (port.h declares them for the game's side) ---- */

/* Prints the message and stops the program; the game's PC code calls it where the N64 would hang or crash
 * (src/port.c). */
_Noreturn void port_fatal(const char *fmt, ...) TD_PRINTF_FORMAT;
/* Copies size bytes of the ROM, starting at ROM address src, to dst (src/rom.c). */
void port_rom_read(void *dst, uint32_t src, uint32_t size);
/* func_80003684 calls this once a whole resource is in RAM (src/port.c). */
void port_res_loaded(uint32_t id, void *buf, int32_t size);
/* PTR_TO_SLOT and SLOT_TO_PTR in port.h (src/slots.c): turn an address into the 32-bit handle that a 4-byte address
 * field of an N64-layout record holds on the PC, and back. The same address always gives the same handle, NULL gives
 * 0, and port_slot_to_ptr stops the program on a handle that was never given out. */
uint32_t port_ptr_to_slot(void *p);
void *port_slot_to_ptr(uint32_t slot);

/* How many times the game has called port_res_loaded since the program started (src/port.c). */
uint32_t port_res_loaded_count(void);
/* The most handles the handle table holds, handle 0 (NULL) included: 2^20 (src/slots.c). */
#define PORT_SLOT_CAP 0x100000u
/* How many handles the table has given out so far, 0 not counted (src/slots.c). */
uint32_t port_slot_count(void);

/* The game's heap on PC (src/game_heap.c; port.h says why the game's own cannot run on a 64-bit PC). port_heap_alloc
 * returns a block of at least `bytes` bytes, filled with zeros, and stops the program when there is no memory left;
 * port_heap_free frees one. */
void *port_heap_alloc(uint32_t bytes);
void port_heap_free(void *block);

/* Game variables the port reads: the loader's two tables. Like every game variable, they are defined in the C file that
 * tools/game_vars.py writes into the build folder (src/game_vars.h). */
extern uint32_t *D_800482E0;
extern uint32_t *D_800482E4;

#endif
