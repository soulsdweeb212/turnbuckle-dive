/* The pictures on the splash screen's cube (src/splash.cpp): Sting's three painted faces, made from the textures of
 * the user's ROM when the program starts (src/splash_faces.c). Nothing of the ROM is kept in the repository. */
#ifndef TD_SPLASH_FACES_H
#define TD_SPLASH_FACES_H

#include <stdint.h>

#include "rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Three faces of 64x64 texels, RGBA16 in the N64's byte order (most significant byte first), as LibUltraShip's
 * renderer reads a texture: 8192 bytes each, one after the other. */
#define SPLASH_FACES 3
#define SPLASH_FACE_SIZE 64
#define SPLASH_FACE_BYTES (SPLASH_FACE_SIZE * SPLASH_FACE_SIZE * 2)

/* Loads the textures from the ROM with the game's loader and makes the three faces in out. Returns 0, or -1 after
 * printing why when a texture is not what the splash expects (out is then left as it is). */
int splash_faces_load(const Rom *rom, uint8_t out[SPLASH_FACES * SPLASH_FACE_BYTES]);

#ifdef __cplusplus
}
#endif

#endif
