/* The planted call (include/types.h says what this folder is for). Two of the game's files declared
 * poseEntityAtAnimFrame this way and passed 0, "no look angles", as its fifth argument, a pointer the declaration
 * calls an s32. The N64 passes both in 32 bits. 64-bit Windows passes the fifth argument on the stack, in 8 bytes of
 * which the caller writes only the s32's 4: the pointer's upper half keeps what was there, so the function reads its
 * look angles through a broken address and crashes (frame 2,400 of the developer match, 2026-11-26). The port's own
 * files now give the definition's types on PC; here the mistake stays, and tools/port_calls.py must rate the call a
 * crash (class A3). */
#include "types.h"

void poseModel(s32, s16, s16, f32 *, s32, s8);

void poseCaller(f32 *pos) {
    poseModel(1, 2, 3, pos, 0, 0);
}
