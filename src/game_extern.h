/* game_extern.h: read before each game file when Clang compiles it (CMakeLists.txt, td_game_code), never by GCC.
 *
 * Every "extern" declaration in a game file gets __attribute__((aligned(1))), so that Clang assumes nothing about where
 * the declared variable starts. The game's variables sit at their N64 addresses inside one block per N64 section
 * (docs/GAME_CODE.md, "The game's variables"): a variable is aligned on the PC as its N64 address is, often to 8 bytes
 * or fewer. Clang, though, takes any array of 16 bytes or more to start at a multiple of 16 (the rule x86-64's ABI sets
 * for the arrays a compiler defines), even an array it only declares, and then copies it with SSE instructions that
 * stop the program when the address is not a multiple of 16. This happened: the Windows build stopped in the game's
 * controller read (func_80003E78), which copies D_80048320[4] into D_80048338[4], 24 bytes at 0x80048338. GCC assumes
 * no more than the element type's alignment for a declared array, so GCC builds need none of this.
 *
 * An aligned attribute on a variable's declaration replaces the alignment Clang would assume, and a declaration that
 * comes later inherits it. Clang then uses the unaligned forms (movups instead of movaps), which are as fast on
 * current processors. A game file defines no variable of its own (the PC's variables are all in game_vars.c, written
 * by tools/game_vars.py), so this changes no variable's place. On a function declaration the attribute is harmless: a
 * function's code still starts at a multiple of 16. */
#ifndef GAME_EXTERN_H
#define GAME_EXTERN_H

/* -Wkeyword-macro: Clang warns that a macro named after a keyword hides it, which is the point here. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#define extern extern __attribute__((aligned(1)))
#pragma clang diagnostic pop

#endif
