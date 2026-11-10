/* The viewer's calls into the game's own code (viewer.h says how the viewer's files fit together). This file includes
 * the game's headers, because it fills in the game's display record, whose layout on the PC only those headers know
 * (Nw2TextureDisplayObj in the decompilation's common.h). So CMake compiles it with the game's settings, like the game
 * files themselves. It includes no C library header before common.h, whose NULL would then redefine the library's:
 * it takes its memory from the game's heap, like the game, and clears records by assignment. viewer.h comes after
 * common.h, and its <stddef.h> replaces common.h's NULL with its own, which is the same. */
#include "common.h"

#include "viewer.h"

/* The game's functions this file calls, compiled from the decompilation. Their files declare some parameters with
 * structs of their own, which give each the layout of the display record; void * stands for those here: a pointer is
 * passed the same way whatever it points to. */
void func_8000486C(void *vertices, u8 *raw);
void buildVertexDisplayList(void *record, u8 *raw);
void buildObjectDisplayListTail(void *record);
Nw2RdpCmd *buildCiTextureDisplayList(Nw2TextureDisplayObj *record, s16 mode);
void func_80004E08(void *record, s16 id1, s16 id2);
void func_8000670C(void *pair, s16 id1, s16 id2);
void func_80007920(void *vertex, s16 x, s16 y, s16 s, u16 t, u8 red, u8 green, u8 blue, u8 alpha);
Gfx *buildCi4TextureDisplayList(void *pair, Gfx *out, u16 mode);
void func_80096D8C(Gfx **out, void *texels, s32 fmt, s32 x, s32 y, s32 texw, s32 h, s32 w, s32 dh);
/* The game's heap (the port's stand-in in src/game_heap.c until the game's own runs on the PC). */
void *allocateHeap(u32 bytes);
void releaseHeapBlock(void *block);

/* The room for the record's material list (its dl): the game gives it 256 bytes, 32 of the N64's 8-byte commands. Here
 * a command is 16 bytes, so 32 of them keep the same room. buildCiTextureDisplayList writes at most 16. */
#define MATERIAL_COMMANDS 32

/* The game's func_8000498C loads a mesh into a display record and builds its untextured display lists; then
 * func_80004F80 (for a wrestler's body part) or func_80007020 (for the meshes of a scene) gives the mesh a texture.
 * The viewer cannot call func_8000498C or func_80007020 yet: on the PC they would keep the record's pointers in 32-bit
 * integers, and size their buffers for 8-byte commands. So this function does the same steps itself, in the same order,
 * and calls the game's functions that do the work. */
void viewer_head_load(uint32_t mesh, uint32_t texture, uint32_t palette, ViewerHead *head) {
    ResDesc desc;
    Nw2TextureDisplayObj *record;
    u8 *raw;
    s32 triangles;

    *head = (ViewerHead){ 0 };
    record = allocateHeap(sizeof(*record));
    *record = (Nw2TextureDisplayObj){ 0 };

    /* func_8000498C: load the mesh into a buffer of its size from the game's heap. */
    lookupResourceDescriptor(&desc, mesh);
    raw = allocateHeap((u32)desc.size);
    func_80003684(&desc, (s32)mesh, raw);

    /* Copy the header into the record: byte 0 marks it in use, byte 1 gets the number of triangles, byte 2 (count)
     * the number of vertices without the flag in its top bit, which goes to byte 6, and bytes 4 and 5 the two halves
     * of the mesh's byte 3. The first vertex slot (byte 3) stays 0. The game's func_8000486C reads the vertex count
     * from the mesh itself, so the flag is cleared there too. */
    record->pad4[2] = raw[1] & 0x80;
    raw[1] &= 0x7F;
    record->pad0[0] = 1;
    record->count = (s8)raw[1];
    record->pad0[1] = raw[2];
    record->pad4[0] = raw[3] >> 4;
    record->pad4[1] = raw[3] & 15;
    triangles = (s8)record->pad0[1];

    /* The vertices, 16 bytes each, which func_8000486C decodes from the mesh's 8-byte ones. */
    record->colorPtr = allocateHeap((u32)record->count * 16);
    func_8000486C(record->colorPtr, raw);

    /* The triangle list (next), which buildVertexDisplayList writes: 2 geometry-mode commands, one command per two
     * triangles (plus one for an odd last triangle), a pipe sync and the end, so (triangles / 2 + 5) commands, as the
     * game counts it. Then the material list (dl) with the untextured settings of buildObjectDisplayListTail, which
     * branches to the triangle list. */
    record->next = allocateHeap((u32)(triangles / 2 + 5) * sizeof(Gfx));
    record->dl = allocateHeap(MATERIAL_COMMANDS * sizeof(Gfx));
    buildVertexDisplayList(record, raw);
    buildObjectDisplayListTail(record);
    releaseHeapBlock(raw);

    /* Load the texture and its palette into the record with func_80004E08, as func_80004F80 does for a wrestler's body
     * part, then let buildCiTextureDisplayList write the material list again, now loading the palette and the texels
     * before it branches to the triangles. Mode 1 is the one func_80007020 uses for its models; it leaves the render
     * state to the caller, and the viewer's head frame relies on that: the depth buffer it draws the parts with is
     * part of the render state it sets. A wrestler's body part uses mode 0, whose list starts with the wrestlers'
     * render state: a render mode that neither tests nor keeps depths, and a colour combiner that LibUltraShip cannot
     * draw yet, which multiplies each texel by the vertex's alpha, where the game keeps its lighting. */
    func_80004E08(record, (s16)texture, (s16)palette);
    buildCiTextureDisplayList(record, 1);

    head->mesh = mesh;
    head->texture = texture;
    head->palette = palette;
    head->record = record;
    head->vertices = record->colorPtr;
    head->vertex_count = (u32)record->count;
    head->first_vertex = (u32)record->first;
    head->material_list = record->dl;
    head->triangle_list = record->next;
    head->triangle_list_size = (u32)(triangles / 2 + 5);
    head->triangle_count = (u32)triangles;
}

void viewer_head_free(ViewerHead *head) {
    Nw2TextureDisplayObj *record = head->record;

    if (record != NULL) {
        releaseHeapBlock(record->colorPtr);
        releaseHeapBlock(record->dl);
        releaseHeapBlock(record->next);
        releaseHeapBlock(record->tex);
        releaseHeapBlock(record->palette);
        releaseHeapBlock(record);
    }
    *head = (ViewerHead){ 0 };
}

/* The pair of addresses that func_8000670C loads an effect sprite's texture and palette into, and that
 * buildCi4TextureDisplayList reads them from (Nw2TwoBuf and Nw2Ci4Object in those files): 16 bytes on the PC. */
typedef struct {
    void *texture;
    void *palette;
} SpritePair;

/* The room for a sprite's material list: the game gives each of its sprites' lists 0x200 bytes, 64 of the N64's 8-byte
 * commands. Here a command is 16 bytes, so 64 of them keep the same room. buildCi4TextureDisplayList writes at most
 * 17. */
#define SPRITE_COMMANDS 64

/* The game's initializeMatchResources loads each effect sprite with func_8000670C, and makes the four vertices all its
 * sprites share with func_80007920; func_80011764 then draws a sprite with buildCi4TextureDisplayList and a few
 * commands of its own. The viewer cannot call those two yet (they keep the sprites in arrays sized for the N64's
 * 4-byte pointers), so this function does their steps itself and calls the game's functions that do the work. */
void viewer_sprite_load(uint32_t texture, uint32_t palette, ViewerSprite *sprite) {
    SpritePair *pair;
    u8 *vertices;
    Gfx *list;

    *sprite = (ViewerSprite){ 0 };
    pair = allocateHeap(sizeof(*pair));
    *pair = (SpritePair){ 0 };
    func_8000670C(pair, (s16)texture, (s16)palette);

    /* The four vertices, with initializeMatchResources' values: a square 80 units across around the origin, in the
     * plane z = 0, whose texture coordinates (in 1/32 texel) run from about half a texel inside one edge of the 32x32
     * texture to half a texel inside the other, and whose colour is black and opaque. A vertex is 16 bytes. */
    vertices = allocateHeap(4 * 16);
    func_80007920(vertices + 0 * 16, -40, -40, 16, 1008, 0, 0, 0, 255);
    func_80007920(vertices + 1 * 16, 40, -40, 1007, 1008, 0, 0, 0, 255);
    func_80007920(vertices + 2 * 16, -40, 40, 16, 15, 0, 0, 0, 255);
    func_80007920(vertices + 3 * 16, 40, 40, 1007, 15, 0, 0, 0, 255);

    /* The material list: the render state of effect sprites, then the palette's and the texels' loads, and G_ENDDL,
     * which func_80011764 writes over with its own commands (the viewer's frame has them after its call of this list
     * instead). The last argument is the sprite's kind, which only decides whether the list turns on the N64's
     * dithered alpha test: the game's kind 6 does without it, and the viewer draws every sprite that way, because
     * Fast3D makes that test random noise, which the check could not compare. */
    list = allocateHeap(SPRITE_COMMANDS * sizeof(Gfx));
    buildCi4TextureDisplayList(pair, list, 6);

    sprite->texture = texture;
    sprite->palette = palette;
    sprite->pair = pair;
    sprite->vertices = vertices;
    sprite->material_list = list;
}

void viewer_sprite_free(ViewerSprite *sprite) {
    SpritePair *pair = sprite->pair;

    if (pair != NULL) {
        releaseHeapBlock(pair->texture);
        releaseHeapBlock(pair->palette);
        releaseHeapBlock(pair);
        releaseHeapBlock((void *)sprite->vertices);
        releaseHeapBlock((void *)sprite->material_list);
    }
    *sprite = (ViewerSprite){ 0 };
}

/* The game loads a picture with func_80004E08, as the texture of a display record, with no palette resource: a TEX
 * picture holds its palette. It finds the palette 16 bytes in, after the header, and the texels after the palette's
 * entries (the static function hdr in the overlay-1 file func_800B6F88.c does this). */
void viewer_picture_load(uint32_t id, ViewerPicture *picture) {
    Nw2TextureDisplayObj *record;
    const u8 *data;

    *picture = (ViewerPicture){ 0 };
    record = allocateHeap(sizeof(*record));
    *record = (Nw2TextureDisplayObj){ 0 };
    func_80004E08(record, (s16)id, 0);
    data = (const u8 *)record->tex;

    picture->id = id;
    picture->record = record;
    picture->width = READ_BE16(data + 4);
    picture->height = READ_BE16(data + 6);
    picture->bits = data[8];
    picture->palette_entries = READ_BE16(data + 10);
    picture->palette = data + 0x10;
    picture->texels = picture->palette + picture->palette_entries * 2;
}

void viewer_picture_free(ViewerPicture *picture) {
    Nw2TextureDisplayObj *record = picture->record;

    if (record != NULL) {
        releaseHeapBlock(record->tex);
        releaseHeapBlock(record);
    }
    *picture = (ViewerPicture){ 0 };
}

/* func_80096D8C draws a picture in tiles 64 texels wide and 48 rows high (4-bit pictures) or 24 (8-bit ones), with 10
 * commands per tile. */
uint32_t viewer_picture_max_commands(const ViewerPicture *picture) {
    uint32_t rows = picture->bits == 4 ? 48 : 24;

    return ((picture->width + 63) / 64) * ((picture->height + rows - 1) / rows) * 10;
}

/* The game's 2D code (func_8009EC58 in overlay 1, for example) calls func_80096D8C with format 0 for a 4-bit picture
 * with 16 colours and 1 for an 8-bit one with 256, with the picture's own width and height for the width and height
 * it draws, so it is drawn at its size. */
uint32_t viewer_picture_draw(const ViewerPicture *picture, int32_t x, int32_t y, void *commands, uint32_t capacity) {
    Gfx *start = commands;
    Gfx *end = start;
    s32 format = picture->bits == 4 ? 0 : 1;

    if (capacity < viewer_picture_max_commands(picture)) {
        port_fatal("viewer_picture_draw: room for %u commands, and a %ux%u picture can take %u", capacity,
                   picture->width, picture->height, viewer_picture_max_commands(picture));
    }
    func_80096D8C(&end, (void *)picture->texels, format, x, y, (s32)picture->width, (s32)picture->height,
                  (s32)picture->width, (s32)picture->height);
    return (uint32_t)(end - start);
}
