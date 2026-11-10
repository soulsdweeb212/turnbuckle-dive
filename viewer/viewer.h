/* The viewer's three source files and what they share:
 * - main.cpp is the program: its options, the window (LibUltraShip), the frame it draws, --check and --dump. It uses
 *   LibUltraShip's headers, which a file that includes the game's headers must not (both define Gfx).
 * - viewer_rom.c opens the ROM, reads resource ids and checks that each resource is of the kind it is used as, before
 *   the game's code sees it. It is port code like src/, compiled with the port's strict settings.
 * - viewer_game.c calls the game's own functions, compiled from the decompilation, to turn a model, a sprite or a
 *   picture into display lists. It includes the game's headers to use the game's types, so it is compiled with the
 *   game's settings.
 * This header uses only C types, so all three can include it. The display-list commands the game writes are 16 bytes,
 * two pointer-sized words, the layout of LibUltraShip's Gfx (the decompilation's port.h checks it). */
#ifndef TD_VIEWER_H
#define TD_VIEWER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- viewer_rom.c ---- */

/* Exit code of a check that cannot run because there is no ROM; CTest reports such a test as skipped. */
#define VIEWER_EXIT_SKIP 77

/* Finds and checks the ROM, as turnbuckle-dive does (src/rom.h: the path given, else $TD_ROM, else baserom.z64 in
 * the current folder, else in the decompilation's folder), and starts the game's code on it (res_init). Returns 0, or
 * VIEWER_EXIT_SKIP when no ROM was given and none was found, or 1 after printing why the ROM cannot be used. */
int viewer_open_rom(const char *path);
/* Lets the ROM go again. */
void viewer_close_rom(void);

/* Reads a resource id written in decimal or in hex with 0x. Returns 1 and sets *id if it is one of the game's ids
 * (1 to 0x1DE9), else prints why not, naming the option it was given to, and returns 0. */
int viewer_parse_id(const char *option, const char *text, uint32_t *id);
/* The same check for an id that is already a number, read from where: 1 if it is one of the game's ids, else 0 after
 * printing why not. */
int viewer_check_id(const char *where, uint32_t id);

/* Each checks that a resource is of the kind named and prints why not: 1 if it is, 0 if not. A mesh: its counts of
 * vertices (at most 64) and triangles fit its size, and every triangle names vertices it has. A texture of the kind the
 * game draws on models (CI4 or CI8, with the 8-byte header buildCiTextureDisplayList reads): its size fits its header,
 * and it fits in the N64's texture memory. A sprite texture: the same header, always read as CI4
 * (buildCi4TextureDisplayList). A palette: big enough for the texture (16 entries for CI4, 256 for CI8). A picture: a
 * TEX resource whose texels and palette fit its size. */
int viewer_check_mesh(uint32_t id);
int viewer_check_texture(uint32_t id, uint32_t *paletteEntries);
int viewer_check_sprite_texture(uint32_t id);
int viewer_check_palette(uint32_t id, uint32_t entries);
int viewer_check_picture(uint32_t id);

/* ---- viewer_game.c ---- */

/* A model, such as one part of a head, made ready to draw by the game's own code, the way func_8000498C (load a mesh)
 * and func_80004F80 (give it a texture) do it in the game, with the list mode of func_80007020. The viewer loads one
 * for each part of a head. */
typedef struct {
    uint32_t mesh;
    uint32_t texture;
    uint32_t palette;
    void *record; /* the game's display record of the model (Nw2TextureDisplayObj in the decompilation's common.h) */

    /* What the frame needs: a G_VTX of the vertices into slots first_vertex and up, then a G_DL to material_list,
     * which loads the texture and goes on to the triangle list. */
    const void *vertices;   /* vertex_count Vtx, 16 bytes each, written by the game's func_8000486C */
    uint32_t vertex_count;
    uint32_t first_vertex;
    const void *material_list; /* written by the game's buildCiTextureDisplayList */
    const void *triangle_list; /* written by the game's buildVertexDisplayList; ends with G_ENDDL */
    uint32_t triangle_list_size; /* the commands its buffer holds */
    uint32_t triangle_count;
} ViewerHead;

/* Loads a mesh, a texture and its palette with the game's own functions and builds the model's display lists with
 * them. The ids must have passed the viewer_check_ functions first: the game's functions trust what they are given. */
void viewer_head_load(uint32_t mesh, uint32_t texture, uint32_t palette, ViewerHead *head);
void viewer_head_free(ViewerHead *head);

/* One of the game's effect sprites (the frames of the sprite animations it plays on the wrestlers in a match) made
 * ready to draw by the game's own code, the way initializeMatchResources loads it and func_80011764 draws it: a CI4
 * texture whose palette the game reads as IA16. */
typedef struct {
    uint32_t texture;
    uint32_t palette;
    void *pair; /* the texture's and the palette's addresses, which the game's func_8000670C loads them into */

    /* What the frame needs: the palette type IA16, a G_VTX of the four vertices into slots 0 to 3, and two triangles,
     * after a G_DL to material_list, which loads the palette and the texels. */
    const void *vertices;      /* 4 Vtx, 16 bytes each, written by the game's func_80007920 */
    const void *material_list; /* written by the game's buildCi4TextureDisplayList; ends with G_ENDDL */
} ViewerSprite;

/* Loads an effect sprite's texture and palette with the game's own functions and builds its material list with
 * them. The ids must have passed viewer_check_sprite_texture and viewer_check_palette (16 entries) first. */
void viewer_sprite_load(uint32_t texture, uint32_t palette, ViewerSprite *sprite);
void viewer_sprite_free(ViewerSprite *sprite);

/* A TEX picture loaded by the game's func_80004E08, and the parts of it the game draws: the palette of 16 or 256
 * big-endian RGBA5551 entries, and the texels, 4 or 8 bits each, row by row. */
typedef struct {
    uint32_t id;
    void *record; /* the game's display record the picture was loaded into */
    uint32_t width;
    uint32_t height;
    uint32_t bits;
    uint32_t palette_entries;
    const uint8_t *palette;
    const uint8_t *texels;
} ViewerPicture;

/* Loads a picture that passed viewer_check_picture. */
void viewer_picture_load(uint32_t id, ViewerPicture *picture);
void viewer_picture_free(ViewerPicture *picture);

/* The most commands viewer_picture_draw can write for picture. */
uint32_t viewer_picture_max_commands(const ViewerPicture *picture);
/* Writes the commands with which the game's func_80096D8C draws picture with its top left corner at (x, y) of the
 * 480x240 frame, into commands, which holds capacity of them, and returns how many it wrote. The caller loads the
 * picture's palette and sets the render state first, as the game does. */
uint32_t viewer_picture_draw(const ViewerPicture *picture, int32_t x, int32_t y, void *commands, uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif
