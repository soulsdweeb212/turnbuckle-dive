/* The test game_calls_detects_crash (CMakeLists.txt): a small stand-in for the decompilation, with one call that
 * crashes on 64-bit Windows, which the decompilation's tools/port_calls.py must report. These are the N64's integer
 * and float types, with the names the game's code uses. */
#ifndef TYPES_H
#define TYPES_H

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef int s32;
typedef float f32;

#endif
