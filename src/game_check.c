/* A check, not code: this file is compiled with the game's settings and never linked. It stops the build if src/game.h
 * (the port's declarations of what it shares with the game) ever disagrees with the decompilation's include/port.h
 * (the game's declarations of the same).
 * - Each function that both headers declare is declared twice here, and the compiler rejects two declarations of one
 *   function with different types.
 * - The asserts below compare what the compiler does not: the two descriptor types (which must stay separate types,
 *   one per header), member by member, and the resource count. */

/* The game's types, then port.h. Game files include system headers before common.h, because port.h renames some C
 * library functions (bzero, for example); the headers below declare none of those, and coming after common.h lets
 * <stddef.h> replace common.h's NULL without a warning. */
#include "common.h"

#include <stddef.h>

#include "game.h"

_Static_assert(GAME_RES_COUNT == RES_COUNT, "src/game.h and port.h count a different number of resources");
_Static_assert(sizeof(GameResDesc) == sizeof(ResDesc), "GameResDesc (src/game.h) and ResDesc (port.h) differ in size");

/* A member must be at the same offset and have the same type in both. The type counts even where the size stays the
 * same: a size that became unsigned on one side only would make the port's "size < 0" checks dead code. GCC and Clang,
 * the only compilers that build game code, both have __builtin_types_compatible_p and __typeof__. */
#define TD_SAME_MEMBER(member)                                                                                         \
    (offsetof(GameResDesc, member) == offsetof(ResDesc, member) &&                                                     \
     __builtin_types_compatible_p(__typeof__(((GameResDesc *)0)->member), __typeof__(((ResDesc *)0)->member)))
_Static_assert(TD_SAME_MEMBER(start), "GameResDesc.start and ResDesc.start differ in offset or type");
_Static_assert(TD_SAME_MEMBER(end), "GameResDesc.end and ResDesc.end differ in offset or type");
_Static_assert(TD_SAME_MEMBER(size), "GameResDesc.size and ResDesc.size differ in offset or type");
_Static_assert(TD_SAME_MEMBER(packed), "GameResDesc.packed and ResDesc.packed differ in offset or type");
