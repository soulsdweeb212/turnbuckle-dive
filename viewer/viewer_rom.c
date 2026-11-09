/* The viewer's port code in C: opening the ROM, reading resource ids, and checking that a resource is of the kind it is
 * used as. The game's functions trust the resources they are given (the game only ever passes them the right ones), so
 * a mesh id given as a picture, for example, would make them read past the resource's end. These checks look at the
 * same header bytes the game's functions read and refuse a resource whose sizes do not fit, with a message. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "resources.h"
#include "rom.h"
#include "viewer.h"

static Rom g_rom;
static int g_rom_open;

int viewer_open_rom(const char *given) {
    const char *env = getenv("TD_ROM");
    const char *path = rom_find(given);
    RomStatus status = rom_load(path, &g_rom);

    /* As in the rom_loader test: only a ROM nobody named may be missing; a ROM named on the command line or in TD_ROM
     * that is not there is an error. */
    if (status == ROM_NOT_FOUND && given == NULL && (env == NULL || env[0] == '\0')) {
        printf("No ROM found: give its path, set TD_ROM to it, or put it in the decompilation's folder as "
               "baserom.z64\n");
        return VIEWER_EXIT_SKIP;
    }
    if (status != ROM_OK) {
        if (status == ROM_WRONG_HASH) {
            printf("ROM %s: SHA-1 %s, expected %s\n", path, g_rom.sha1, TD_ROM_SHA1);
            rom_free(&g_rom);
        }
        printf("ROM %s: %s\n", path, rom_status_text(status));
        return 1;
    }
    printf("ROM: %s\n", path);
    rom_attach(&g_rom);
    res_init();
    g_rom_open = 1;
    return 0;
}

void viewer_close_rom(void) {
    if (g_rom_open) {
        res_shutdown();
        rom_free(&g_rom);
        g_rom_open = 0;
    }
}

int viewer_parse_id(const char *option, const char *text, uint32_t *id) {
    const char *digits = text;
    int base = 10;
    char *end;
    unsigned long value = 0;

    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        base = 16;
        digits += 2;
    }
    /* strtoul would also accept leading spaces and a minus sign. */
    if (isxdigit((unsigned char)digits[0])) {
        value = strtoul(digits, &end, base);
        if (*end != '\0') {
            value = 0;
        }
    }
    if (value < 1 || value > GAME_RES_COUNT) {
        fprintf(stderr, "%s %s: a resource id is 1 to 0x%X (%u), in decimal or in hex such as 0x3\n", option, text,
                GAME_RES_COUNT, GAME_RES_COUNT);
        return 0;
    }
    *id = (uint32_t)value;
    return 1;
}

int viewer_check_id(const char *where, uint32_t id) {
    if (id < 1 || id > GAME_RES_COUNT) {
        fprintf(stderr, "%s: resource id 0x%X is not one of the game's, which run from 1 to 0x%X\n", where, id,
                GAME_RES_COUNT);
        return 0;
    }
    return 1;
}

static uint32_t be16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8) | p[1];
}

/* func_8000498C's mesh header: byte 1 holds the number of vertices (its top bit is a flag), byte 2 the number of
 * triangles. 8 header bytes come first, then 8 bytes per vertex and 3 per triangle, each a vertex number
 * (func_8000486C and buildVertexDisplayList read them). */
static int mesh_ok(uint32_t id, const uint8_t *raw, uint32_t size) {
    uint32_t vertices, triangles, need, i;

    if (size < 8) {
        fprintf(stderr, "resource 0x%X is not a mesh: it is %u bytes, shorter than a mesh's 8-byte header\n", id, size);
        return 0;
    }
    vertices = raw[1] & 0x7F;
    triangles = raw[2];
    need = 8 + vertices * 8 + triangles * 3;
    if (vertices == 0 || triangles == 0 || size < need || size > need + 3) {
        fprintf(stderr,
                "resource 0x%X is not a mesh: a mesh with the %u vertices and %u triangles its bytes 1 and 2 give "
                "would be %u bytes (or up to 3 more, for padding), and the resource is %u\n",
                id, vertices, triangles, need, size);
        return 0;
    }
    /* The viewer loads all of a mesh's vertices with one G_VTX, as func_80007020 does, into Fast3D's 64 vertex slots
     * (the game's larger models use its second microcode, F3DLX.Rej, which has 64 too); Fast3D does not check that
     * they fit. The game keeps the number of triangles in a signed byte, and func_8000498C builds no triangle list
     * when it is negative. */
    if (vertices > 64 || triangles > 127) {
        fprintf(stderr,
                "resource 0x%X has %u vertices and %u triangles; the viewer draws meshes of up to 64 vertices and 127 "
                "triangles\n",
                id, vertices, triangles);
        return 0;
    }
    for (i = 0; i < triangles * 3; i++) {
        uint32_t index = raw[8 + vertices * 8 + i];

        if (index >= vertices) {
            fprintf(stderr, "resource 0x%X is not a mesh: its triangle %u names vertex %u, and it has %u vertices\n",
                    id, i / 3, index, vertices);
            return 0;
        }
    }
    return 1;
}

/* buildCiTextureDisplayList's texture header (Nw2TextureFormat in the decompilation's common.h): byte 2 is 1 for
 * 8-bit texels (CI8), else they are 4-bit (CI4); bytes 6 and 7 give the width and height as powers of two. The texels
 * follow the 8 header bytes. ci8 says which of the two the function that loads the texture takes them for. */
static int texture_ok(uint32_t id, const uint8_t *raw, uint32_t size, int ci8) {
    uint32_t texels, bytes;

    if (size < 8) {
        fprintf(stderr, "resource 0x%X is not a texture: it is %u bytes, shorter than a texture's 8-byte header\n", id,
                size);
        return 0;
    }
    if (raw[6] > 10 || raw[7] > 10) {
        fprintf(stderr,
                "resource 0x%X is not a texture: its bytes 6 and 7 (%u and %u) would make it wider or taller than "
                "1024 texels\n",
                id, raw[6], raw[7]);
        return 0;
    }
    texels = (1u << raw[6]) * (1u << raw[7]);
    bytes = ci8 ? texels : texels / 2;
    if (size < 8 + bytes) {
        fprintf(stderr,
                "resource 0x%X is not a texture: its header gives %ux%u texels of %u bits, %u bytes after the "
                "header, and the resource is %u bytes\n",
                id, 1u << raw[6], 1u << raw[7], ci8 ? 8u : 4u, bytes, size);
        return 0;
    }
    /* With a palette in use, the N64 keeps the palette in the upper half of its 4 KiB texture memory, and the texels
     * must fit in the lower half. */
    if (bytes > 2048) {
        fprintf(stderr,
                "resource 0x%X: its %ux%u texels of %u bits are %u bytes, more than the 2048 bytes of texture memory "
                "the N64 has for them\n",
                id, 1u << raw[6], 1u << raw[7], ci8 ? 8u : 4u, bytes);
        return 0;
    }
    return 1;
}

/* A TEX picture starts with a 16-byte header: "TEX" and a zero byte, the width and the height (big-endian 16-bit
 * numbers), the bits per texel (byte 8) and the number of palette entries (16-bit, at byte 10). The palette follows,
 * 2 bytes per entry, then the texels row by row (the overlay-1 functions func_800B6F88 and func_8009EC58 read it this
 * way). */
static int picture_ok(uint32_t id, const uint8_t *raw, uint32_t size) {
    uint32_t width, height, bits, entries;
    uint64_t need;

    if (size < 16 || memcmp(raw, "TEX", 4) != 0) {
        fprintf(stderr, "resource 0x%X is not a TEX picture: it does not start with \"TEX\" and a zero byte\n", id);
        return 0;
    }
    width = be16(raw + 4);
    height = be16(raw + 6);
    bits = raw[8];
    entries = be16(raw + 10);
    if (!((bits == 4 && entries == 16) || (bits == 8 && entries == 256)) || width == 0 || height == 0 ||
        (bits == 4 && width % 2 != 0)) {
        fprintf(stderr,
                "resource 0x%X is a %ux%u TEX picture of %u bits per texel with %u palette entries; the game draws "
                "4-bit pictures of an even width with 16 colours, and 8-bit ones with 256\n",
                id, width, height, bits, entries);
        return 0;
    }
    /* 64-bit, because a width and a height of up to 65535 could overflow 32 bits. */
    need = 16 + (uint64_t)entries * 2 + (uint64_t)width * height * bits / 8;
    if (size < need) {
        fprintf(stderr, "resource 0x%X is a %ux%u TEX picture that needs %llu bytes, and it has %u\n", id, width,
                height, (unsigned long long)need, size);
        return 0;
    }
    return 1;
}

/* Each check loads the resource with the game's loader into a buffer of exactly its size, looks at it, and frees it. */
int viewer_check_mesh(uint32_t id) {
    GameResDesc desc;
    uint8_t *raw = res_load(id, &desc);
    int ok = mesh_ok(id, raw, (uint32_t)desc.size);

    free(raw);
    return ok;
}

int viewer_check_texture(uint32_t id, uint32_t *paletteEntries) {
    GameResDesc desc;
    uint8_t *raw = res_load(id, &desc);
    int ci8 = desc.size >= 3 && raw[2] == 1;
    int ok = texture_ok(id, raw, (uint32_t)desc.size, ci8);

    *paletteEntries = ci8 ? 256 : 16;
    free(raw);
    return ok;
}

/* buildCi4TextureDisplayList reads the same header, but loads every texture as CI4, whatever its byte 2 says. */
int viewer_check_sprite_texture(uint32_t id) {
    GameResDesc desc;
    uint8_t *raw = res_load(id, &desc);
    int ok = texture_ok(id, raw, (uint32_t)desc.size, 0);

    free(raw);
    return ok;
}

int viewer_check_palette(uint32_t id, uint32_t entries) {
    GameResDesc desc;
    uint8_t *raw = res_load(id, &desc);
    /* buildCiTextureDisplayList loads a palette of 256 entries for a CI8 texture and 16 for a CI4 one, 2 bytes
     * each. */
    int ok = (uint32_t)desc.size >= entries * 2;

    if (!ok) {
        fprintf(stderr,
                "resource 0x%X is %d bytes, too short for the palette of %u entries (%u bytes) the texture needs\n", id,
                (int)desc.size, entries, entries * 2);
    }
    free(raw);
    return ok;
}

int viewer_check_picture(uint32_t id) {
    GameResDesc desc;
    uint8_t *raw = res_load(id, &desc);
    int ok = picture_ok(id, raw, (uint32_t)desc.size);

    free(raw);
    return ok;
}
