// hello: the first Turnbuckle Dive window. LibUltraShip opens it, and every frame it draws a test picture made of N64
// display-list commands, at the game's resolution (480x240) shown at 4:3.
//
//   hello              run until the window is closed
//   hello --frames N   draw N frames, print how long they took, then exit
//   hello --check      draw at twice the window's resolution and, after the third frame, read the picture back and
//                      check it: the corner markers, that each shape placed with gSPForceMatrix or gSPModifyVertex
//                      fills exactly its box, and that the eight palette squares have the colours their texels and
//                      palettes give (exit 2 if a check fails, or if hello stops before the third frame)
//
// The picture: a dark background, a shaded triangle drawn through the RSP path (viewport, matrices, vertices), three
// ring ropes, a square that moves one pixel per frame, and one-pixel white markers at (0,0) and (479,239). Above the
// ropes are the shapes that test LibUltraShip patches 0003 and 0004: on the left a cyan triangle placed only by a
// force matrix; on the right a yellow sprite built the way the game builds its sprites, in front of a green wall and
// behind a magenta one. Between them, on either side of the shaded triangle's tip, eight squares test patches 0005 and
// 0006. In the upper row, two textures made of palette indices (CI4 on the left, CI8 on the right), each drawn once
// with its palette read as IA16 (grey levels with their own transparency, over a red backdrop that shows through the
// left half) and once, beside it, with the same palette read as RGBA16 (colours). In the lower row, smaller, two
// textures whose texels are tagged IA4 and IA8 (intensity and alpha), the way the game loads its 2D pictures, each
// drawn once with palettes on, when the N64 looks their texels up in the palette like CI ones, and once with palettes
// off, when they are grey levels.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "context.h"
#include "fast/Fast3dWindow.h"
#include "frame.h"
#include "libultraship/libultraship.h"

namespace {

// LibUltraShip's start-up, the matrices, the corner markers and the read-back are shared with the viewer (src/context.h
// and src/frame.h).
using td::CheckCorners;
using td::Fill;
using td::MatrixFromFloat;
using td::MultiplyMatrices;
using td::Picture;
using td::ReadPicture;
using td::Rgb5;

constexpr uint32_t kWidth = td::kGameWidth;
constexpr uint32_t kHeight = td::kGameHeight;

// Distinct colour and depth image addresses. Fast3D treats a fill rectangle as a depth clear when the two are equal,
// and both start out null.
uint16_t sColorImage[1];
uint16_t sDepthImage[1];

Vp sViewport = { { { kWidth * 2, kHeight * 2, G_MAXZ / 2, 0 }, { kWidth * 2, kHeight * 2, G_MAXZ / 2, 0 } } };
Mtx sProjection;
Mtx sIdentity;
Vtx sTriangle[3] = {
    { { { 240, 30, 0 }, 0, { 0, 0 }, { 255, 40, 40, 255 } } },
    { { { 150, 200, 0 }, 0, { 0, 0 }, { 40, 255, 40, 255 } } },
    { { { 330, 200, 0 }, 0, { 0, 0 }, { 40, 80, 255, 255 } } },
};

// The force-matrix triangle: a right triangle one unit on each side, in a space of its own. sForceMatrix, which hello
// multiplies out itself as the game does with its matrices, puts it at (40,20), (120,20) and (40,100). Through the
// G_MTX matrices it would be a speck at (0,0), under the corner marker.
Mtx sForceMatrix;
Vtx sForceTriangle[3] = {
    { { { 0, 0, 0 }, 0, { 0, 0 }, { 0, 255, 255, 255 } } },
    { { { 1, 0, 0 }, 0, { 0, 0 }, { 0, 255, 255, 255 } } },
    { { { 0, 1, 0 }, 0, { 0, 0 }, { 0, 255, 255, 255 } } },
};

// Two walls for the sprite's depth test, placed by the G_MTX matrices. The projection gives a point at z the screen
// depth 511 - 51.1 z (0 is nearest, 1022 farthest), so the magenta wall (z = -2) is at 613 and the green one (z = -6)
// at 818.
Vtx sWalls[8] = {
    { { { 360, 20, -2 }, 0, { 0, 0 }, { 255, 0, 255, 255 } } },
    { { { 400, 20, -2 }, 0, { 0, 0 }, { 255, 0, 255, 255 } } },
    { { { 360, 90, -2 }, 0, { 0, 0 }, { 255, 0, 255, 255 } } },
    { { { 400, 90, -2 }, 0, { 0, 0 }, { 255, 0, 255, 255 } } },
    { { { 420, 20, -6 }, 0, { 0, 0 }, { 0, 255, 0, 255 } } },
    { { { 460, 20, -6 }, 0, { 0, 0 }, { 0, 255, 0, 255 } } },
    { { { 420, 90, -6 }, 0, { 0, 0 }, { 0, 255, 0, 255 } } },
    { { { 460, 90, -6 }, 0, { 0, 0 }, { 0, 255, 0, 255 } } },
};

// The sprite. Like the game's sprite vertices, its four vertices all sit at the origin, so G_VTX gives each one the
// last row of the current matrix as its clip coordinates. sSpriteMatrix's last row is x -8, y 0, z 3, w 4: left of
// the view volume (x/w = -2) and at screen depth 894, behind both walls. Only the gSPModifyVertex commands bring the
// sprite on screen at kSpriteDepth, between the walls, and turn its grey colour and zero texture coordinates into
// yellow and the texture's white half.
Mtx sSpriteMatrix;
Vtx sSpriteCorners[4] = {
    { { { 0, 0, 0 }, 0, { 0, 0 }, { 128, 128, 128, 255 } } },
    { { { 0, 0, 0 }, 0, { 0, 0 }, { 128, 128, 128, 255 } } },
    { { { 0, 0, 0 }, 0, { 0, 0 }, { 128, 128, 128, 255 } } },
    { { { 0, 0, 0 }, 0, { 0, 0 }, { 128, 128, 128, 255 } } },
};
constexpr uint32_t kSpriteDepth = 715;

// The sprite's texture: 4x4 RGBA16 texels, black on the left half and white on the right, stored as bytes in the N64's
// byte order (big-endian), which is how Fast3D reads texels. 0x0001 is opaque black and 0xFFFF opaque white.
const uint8_t sSpriteTexture[4 * 4 * 2] = {
    0x00, 0x01, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x01, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
};

// The palette squares. A CI4 or CI8 texel is not a colour but an index into a palette, which G_LOADTLUT copies into
// TMEM, and gDPSetTextureLUT (the TEXTLUT field of other_mode_h) tells the RDP how to read the palette's 16-bit
// entries: G_TT_RGBA16 as RGBA5551 colours, G_TT_IA16 as an intensity (high byte) and an alpha (low byte). Both
// textures are 16x16 texels, the left half one palette entry and the right half another, and both palettes hold the
// same two entries:
//   kEntryLeft   IA16: intensity 0x40 with alpha 0x81, a dark grey that lets about half of what is behind it through.
//                RGBA16: an opaque dark brown (red 8, green 2 and blue 0, out of 31).
//   kEntryRight  IA16: intensity 0xC0 with alpha 0xFF, an opaque light grey. RGBA16: an opaque violet (24, 3, 31).
// They are entries 1 and 2 of the CI4 palette (16 entries), and entries 0x21 and 0xA2 of the CI8 palette (256 entries,
// which Fast3D keeps in two halves of 128).
//
// The lower row's texels are values whose IA4 or IA8 meaning differs from their palette entry's colour, and every
// entry they index holds kEntryLeft or kEntryRight too:
//   kIa4Left  0x7   IA4: intensity 3 of 7 with its alpha bit set, an opaque dark grey; palette entry 7 of 16.
//   kIa4Right 0xD   IA4: intensity 6 of 7, opaque, a light grey; palette entry 13.
//   kIa8Left  0x5F  IA8: intensity 5 of 15 with alpha 15 of 15, an opaque dark grey; palette entry 0x5F of 256.
//   kIa8Right 0xCF  IA8: intensity 12 of 15, opaque, a light grey; palette entry 0xCF.
constexpr uint16_t kEntryLeft = 0x4081;
constexpr uint16_t kEntryRight = 0xC0FF;
constexpr uint8_t kCi4Left = 1;
constexpr uint8_t kCi4Right = 2;
constexpr uint8_t kCi8Left = 0x21;
constexpr uint8_t kCi8Right = 0xA2;
constexpr uint8_t kIa4Left = 0x7;
constexpr uint8_t kIa4Right = 0xD;
constexpr uint8_t kIa8Left = 0x5F;
constexpr uint8_t kIa8Right = 0xCF;
constexpr uint32_t kPaletteTexels = 16; // each texture's width and height in texels
alignas(8) uint8_t sCi4Texels[kPaletteTexels * kPaletteTexels / 2];
alignas(8) uint8_t sCi8Texels[kPaletteTexels * kPaletteTexels];
alignas(8) uint8_t sIa4Texels[kPaletteTexels * kPaletteTexels / 2];
alignas(8) uint8_t sIa8Texels[kPaletteTexels * kPaletteTexels];
alignas(8) uint8_t sPalette16[16 * 2];
alignas(8) uint8_t sPalette256[256 * 2];

// How a square's texels are read when it is drawn: as indices into the palette, read as IA16 or as RGBA16, or, with
// palettes off, as the IA4 or IA8 values they are.
enum class Read { PaletteIa16, PaletteRgba16, Ia4, Ia8 };

// The eight squares in the order they are drawn, with their top left corners and sizes in picture pixels. The upper
// row's textures (CI4, CI8) are each drawn once with their palette read as IA16, over a red backdrop 4 pixels larger
// than the square all round, and once, beside it, with the same palette at the same address read as RGBA16; they are
// triangles, two to a square, whose four vertices are in sPaletteQuads. The lower row's (IA4, IA8) are texture
// rectangles, one texel to a pixel, each drawn with palettes on and off.
struct PaletteSquare {
    const char* name;
    uint32_t left, top, size;
    Read read;
};
const PaletteSquare kPaletteSquares[8] = {
    { "CI4 texels, IA16 palette", 132, 40, 32, Read::PaletteIa16 },
    { "CI4 texels, RGBA16 palette", 176, 40, 32, Read::PaletteRgba16 },
    { "IA4 texels, RGBA16 palette", 136, 88, 16, Read::PaletteRgba16 },
    { "IA4 texels, no palette", 160, 88, 16, Read::Ia4 },
    { "CI8 texels, RGBA16 palette", 320, 40, 32, Read::PaletteRgba16 },
    { "CI8 texels, IA16 palette", 276, 40, 32, Read::PaletteIa16 },
    { "IA8 texels, no palette", 304, 88, 16, Read::Ia8 },
    { "IA8 texels, RGBA16 palette", 328, 88, 16, Read::PaletteRgba16 },
};
Vtx sPaletteQuads[8][4];

Gfx sDisplayList[256];

// An orthographic projection that maps pixel coordinates (0,0 top left) straight onto the screen, and the two
// matrices given with gSPForceMatrix.
void BuildMatrices() {
    float ortho[4][4] = {};
    float ident[4][4] = {};
    float model[4][4] = {};
    float force[4][4];
    float sprite[4][4] = {};

    ortho[0][0] = 2.0f / kWidth;
    ortho[1][1] = -2.0f / kHeight;
    ortho[2][2] = -1.0f / 10.0f;
    ortho[3][0] = -1.0f;
    ortho[3][1] = 1.0f;
    ortho[3][3] = 1.0f;
    for (int i = 0; i < 4; i++) {
        ident[i][i] = 1.0f;
        sprite[i][i] = 1.0f;
    }

    // The force matrix is the whole chain multiplied out: the triangle's own space to pixels (scale by 80, then move
    // by (40,20)), then the projection.
    model[0][0] = 80.0f;
    model[1][1] = 80.0f;
    model[2][2] = 1.0f;
    model[3][0] = 40.0f;
    model[3][1] = 20.0f;
    model[3][3] = 1.0f;
    MultiplyMatrices(model, ortho, force);

    // The sprite matrix only matters through its last row (see sSpriteCorners).
    sprite[3][0] = -8.0f;
    sprite[3][2] = 3.0f;
    sprite[3][3] = 4.0f;

    MatrixFromFloat(&sProjection, ortho);
    MatrixFromFloat(&sIdentity, ident);
    MatrixFromFloat(&sForceMatrix, force);
    MatrixFromFloat(&sSpriteMatrix, sprite);
}

// Fills in the palette squares' texels, palettes and vertices (see kEntryLeft).
void BuildPaletteSquares() {
    // A CI4 texel is 4 bits, two to a byte with the first in the high bits, so a row is 8 bytes and its left half 4 of
    // them. A CI8 texel is a byte.
    for (uint32_t i = 0; i < sizeof(sCi4Texels); i++) {
        uint8_t index = (i % (kPaletteTexels / 2) < kPaletteTexels / 4) ? kCi4Left : kCi4Right;

        sCi4Texels[i] = (uint8_t)((index << 4) | index);
    }
    for (uint32_t i = 0; i < sizeof(sCi8Texels); i++) {
        sCi8Texels[i] = (i % kPaletteTexels < kPaletteTexels / 2) ? kCi8Left : kCi8Right;
    }
    for (uint32_t i = 0; i < sizeof(sIa4Texels); i++) {
        uint8_t value = (i % (kPaletteTexels / 2) < kPaletteTexels / 4) ? kIa4Left : kIa4Right;

        sIa4Texels[i] = (uint8_t)((value << 4) | value);
    }
    for (uint32_t i = 0; i < sizeof(sIa8Texels); i++) {
        sIa8Texels[i] = (i % kPaletteTexels < kPaletteTexels / 2) ? kIa8Left : kIa8Right;
    }

    // Palette entries are stored in the N64's byte order (big-endian), which is how Fast3D reads them.
    auto setEntry = [](uint8_t* palette, uint32_t index, uint16_t entry) {
        palette[index * 2] = (uint8_t)(entry >> 8);
        palette[index * 2 + 1] = (uint8_t)entry;
    };
    setEntry(sPalette16, kCi4Left, kEntryLeft);
    setEntry(sPalette16, kCi4Right, kEntryRight);
    setEntry(sPalette256, kCi8Left, kEntryLeft);
    setEntry(sPalette256, kCi8Right, kEntryRight);
    setEntry(sPalette16, kIa4Left, kEntryLeft);
    setEntry(sPalette16, kIa4Right, kEntryRight);
    setEntry(sPalette256, kIa8Left, kEntryLeft);
    setEntry(sPalette256, kIa8Right, kEntryRight);

    // Each square's corners in the order top left, top right, bottom left, bottom right, in picture pixels (the
    // orthographic projection maps them straight onto the screen), with texture coordinates in texels times 32 (5
    // fraction bits) that stretch the whole texture over the square.
    for (int i = 0; i < 8; i++) {
        for (uint32_t corner = 0; corner < 4; corner++) {
            uint32_t right = corner & 1;
            uint32_t bottom = corner >> 1;
            Vtx_t* v = &sPaletteQuads[i][corner].v;

            v->ob[0] = (short)(kPaletteSquares[i].left + right * kPaletteSquares[i].size);
            v->ob[1] = (short)(kPaletteSquares[i].top + bottom * kPaletteSquares[i].size);
            v->ob[2] = 0;
            v->flag = 0;
            v->tc[0] = (short)(right * kPaletteTexels * 32);
            v->tc[1] = (short)(bottom * kPaletteTexels * 32);
            v->cn[0] = 255;
            v->cn[1] = 255;
            v->cn[2] = 255;
            v->cn[3] = 255;
        }
    }
}

// gSPModifyVertex values, built the way the game's sprite code builds them. A screen position is in pixels times 4
// (2 fraction bits), x in the high half; texture coordinates are in texels times 32 (5 fraction bits), s in the high
// half; a screen depth has 16 fraction bits.
uint32_t ScreenXY(uint32_t x, uint32_t y) {
    return ((x * 4) << 16) | (y * 4);
}

uint32_t TexelST(uint32_t s, uint32_t t) {
    return ((s * 32) << 16) | (t * 32);
}

// A lower-row square as a texture rectangle of the loaded texture, one texel to a picture pixel (the scale 1 << 10 that
// func_80096D8C uses), from its top left corner to its bottom right one, which in one-cycle mode is not drawn.
Gfx* PaletteRectangle(Gfx* g, const PaletteSquare& square) {
    gSPTextureRectangle(g++, square.left << 2, square.top << 2, (square.left + square.size) << 2,
                        (square.top + square.size) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    return g;
}

void BuildFrame(uint32_t frame) {
    Gfx* g = sDisplayList;
    uint32_t squareX = 20 + frame % 420;

    gDPPipeSync(g++);
    gDPSetColorImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kWidth, sColorImage);
    gDPSetDepthImage(g++, sDepthImage);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, kWidth, kHeight);

    // Fill mode: background, ropes, moving square, and the red backdrops of the IA16 palette squares.
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(g++, Fill(td::kBackgroundR, td::kBackgroundG, td::kBackgroundB));
    gDPFillRectangle(g++, 0, 0, kWidth - 1, kHeight - 1);
    gDPPipeSync(g++);
    gDPSetFillColor(g++, Fill(230, 40, 40));
    gDPFillRectangle(g++, 40, 120, 439, 123);
    gDPPipeSync(g++);
    gDPSetFillColor(g++, Fill(230, 230, 230));
    gDPFillRectangle(g++, 40, 150, 439, 153);
    gDPPipeSync(g++);
    gDPSetFillColor(g++, Fill(40, 80, 230));
    gDPFillRectangle(g++, 40, 180, 439, 183);
    gDPPipeSync(g++);
    gDPSetFillColor(g++, Fill(250, 210, 40));
    gDPFillRectangle(g++, squareX, 206, squareX + 15, 221);
    gDPPipeSync(g++);
    gDPSetFillColor(g++, Fill(255, 0, 0));
    for (const PaletteSquare& square : kPaletteSquares) {
        if (square.read == Read::PaletteIa16) {
            gDPFillRectangle(g++, square.left - 4, square.top - 4, square.left + square.size + 3,
                             square.top + square.size + 3);
        }
    }

    // One-cycle mode through the RSP: a shaded triangle.
    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetRenderMode(g++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(g++, 0xFFFFFFFF);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gSPViewport(g++, &sViewport);
    gSPMatrix(g++, &sProjection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, &sIdentity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    __gSPVertex(g++, sTriangle, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);

    // The force-matrix triangle: gSPForceMatrix (a G_MOVEMEM of the matrix to G_MV_MATRIX, then G_MOVEWORD
    // G_MW_FORCEMTX) hands the RSP sForceMatrix as the finished modelview x projection, with no G_MTX.
    gSPForceMatrix(g++, &sForceMatrix);
    __gSPVertex(g++, sForceTriangle, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);

    // The walls, through the G_MTX matrices again. Loading only the modelview rebuilds the combined matrix from it and
    // the projection loaded above, which the force matrix must have left alone. G_ZBUFFER and a Z render mode make
    // them test and write depth.
    gSPMatrix(g++, &sIdentity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPSetGeometryMode(g++, G_ZBUFFER);
    gDPPipeSync(g++);
    gDPSetRenderMode(g++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
    __gSPVertex(g++, sWalls, 8, 0);
    gSP1Quadrangle(g++, 0, 1, 3, 2, 0);
    gSP1Quadrangle(g++, 4, 5, 7, 6, 0);

    // The sprite, in the game's order: VTX and four ZSCREEN (the game's list D_800C95B8), then the texture and four
    // XYSCREEN (a sprite emitter such as buildTiledTextureDisplayList), four RGBA (as in the game's list D_800C9678),
    // four ST and one QUAD. The point-sampled texture is multiplied by the vertex colour.
    gSPForceMatrix(g++, &sSpriteMatrix);
    gDPPipeSync(g++);
    __gSPVertex(g++, sSpriteCorners, 4, 0);
    for (int i = 0; i < 4; i++) {
        gSPModifyVertex(g++, i, G_MWO_POINT_ZSCREEN, kSpriteDepth << 16);
    }
    gDPPipeSync(g++);
    gDPSetCombineMode(g++, G_CC_MODULATERGB, G_CC_MODULATERGB);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPLoadTextureBlock(g++, sSpriteTexture, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 4, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK,
                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPModifyVertex(g++, 0, G_MWO_POINT_XYSCREEN, ScreenXY(380, 40));
    gSPModifyVertex(g++, 1, G_MWO_POINT_XYSCREEN, ScreenXY(440, 40));
    gSPModifyVertex(g++, 2, G_MWO_POINT_XYSCREEN, ScreenXY(380, 70));
    gSPModifyVertex(g++, 3, G_MWO_POINT_XYSCREEN, ScreenXY(440, 70));
    for (int i = 0; i < 4; i++) {
        gSPModifyVertex(g++, i, G_MWO_POINT_RGBA, 0xFFFF00FF);
    }
    gSPModifyVertex(g++, 0, G_MWO_POINT_ST, TexelST(2, 0));
    gSPModifyVertex(g++, 1, G_MWO_POINT_ST, TexelST(4, 0));
    gSPModifyVertex(g++, 2, G_MWO_POINT_ST, TexelST(2, 4));
    gSPModifyVertex(g++, 3, G_MWO_POINT_ST, TexelST(4, 4));
    gSP1Quadrangle(g++, 0, 1, 3, 2, 0);

    // The upper row's palette squares, drawn the way the game draws its effect sprites (func_80011764). First the
    // palette and the texels are loaded with the commands of the game's buildCi4TextureDisplayList (CI4) and
    // buildCiTextureDisplayList (CI8): gDPLoadTLUT_pal16 or gDPLoadTLUT_pal256 copies the palette into TMEM 0x100
    // through the load tile, and gDPLoadTextureBlock_4b or gDPLoadTextureBlock copies the texels into TMEM 0 with one
    // LOADBLOCK and sets up the render tile. Then gDPSetTextureLUT sets the palette type to IA16 just before the
    // triangles and back to RGBA16 after them, with the game's words (G_SETOTHERMODE_H E3001001 0000C000, then
    // E3001001 00008000). The second square of each texture uses the texture that is still loaded, with the other
    // palette type, so Fast3D must notice the type change by itself and must not take the texture decoded with the
    // first type from its cache. The CI8 pair is drawn the other way round, RGBA16 first, and switches to IA16 with
    // G_RDPSETOTHERMODE instead (gDPSetOtherMode, which sets all of other_mode_h and other_mode_l at once): its words
    // hold every field as the commands above left it, but with the palette type IA16. The texels' colour and alpha are
    // drawn as they are (G_CC_DECALRGBA) and blended over what is behind them (G_RM_XLU_SURF).
    gSPMatrix(g++, &sIdentity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(g++);
    gDPSetRenderMode(g++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPLoadTLUT_pal16(g++, 0, sPalette16);
    gDPLoadTextureBlock_4b(g++, sCi4Texels, G_IM_FMT_CI, kPaletteTexels, kPaletteTexels, 0, G_TX_CLAMP, G_TX_CLAMP, 4,
                           4, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetTextureLUT(g++, G_TT_IA16);
    __gSPVertex(g++, sPaletteQuads[0], 4, 0);
    gSP1Quadrangle(g++, 0, 1, 3, 2, 0);
    gDPPipeSync(g++);
    gDPSetTextureLUT(g++, G_TT_RGBA16);
    __gSPVertex(g++, sPaletteQuads[1], 4, 0);
    gSP1Quadrangle(g++, 0, 1, 3, 2, 0);

    // The lower row's IA4 pair, loaded the way the game's func_80096A6C loads a tile of a 4-bit picture
    // (gDPLoadTextureTile_4b with the IA format: the texels go in as 8-bit ones, two to a byte, with one LOADTILE) and
    // drawn the way its func_80096D8C draws one, as a texture rectangle, one texel to a pixel. First with palettes on
    // (RGBA16, as the square before left them), so the texels are indices into the CI4 palette, still loaded; then with
    // palettes off, so they are IA4 grey levels. The palette type's change makes Fast3D import the texels again.
    gDPPipeSync(g++);
    gDPLoadTextureTile_4b(g++, sIa4Texels, G_IM_FMT_IA, kPaletteTexels, kPaletteTexels, 0, 0, kPaletteTexels - 1,
                          kPaletteTexels - 1, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                          G_TX_NOLOD);
    g = PaletteRectangle(g, kPaletteSquares[2]);
    gDPPipeSync(g++);
    gDPSetTextureLUT(g++, G_TT_NONE);
    g = PaletteRectangle(g, kPaletteSquares[3]);

    gDPPipeSync(g++);
    gDPSetTextureLUT(g++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(g++, sPalette256);
    gDPLoadTextureBlock(g++, sCi8Texels, G_IM_FMT_CI, G_IM_SIZ_8b, kPaletteTexels, kPaletteTexels, 0, G_TX_CLAMP,
                        G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
    __gSPVertex(g++, sPaletteQuads[4], 4, 0);
    gSP1Quadrangle(g++, 0, 1, 3, 2, 0);
    gDPPipeSync(g++);
    gDPSetOtherMode(g++, G_CYC_1CYCLE | G_TF_POINT | G_TT_IA16,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    __gSPVertex(g++, sPaletteQuads[5], 4, 0);
    gSP1Quadrangle(g++, 0, 1, 3, 2, 0);

    // The IA8 pair, loaded the way func_80096A6C loads a tile of an 8-bit picture (gDPLoadTextureTile with the IA
    // format), and drawn as texture rectangles: first with palettes off, so the texels are IA8 grey levels, then with
    // palettes on (RGBA16), so they are indices into the CI8 palette, still loaded.
    gDPPipeSync(g++);
    gDPSetTextureLUT(g++, G_TT_NONE);
    gDPLoadTextureTile(g++, sIa8Texels, G_IM_FMT_IA, G_IM_SIZ_8b, kPaletteTexels, kPaletteTexels, 0, 0,
                       kPaletteTexels - 1, kPaletteTexels - 1, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                       G_TX_NOLOD, G_TX_NOLOD);
    g = PaletteRectangle(g, kPaletteSquares[6]);
    gDPPipeSync(g++);
    gDPSetTextureLUT(g++, G_TT_RGBA16);
    g = PaletteRectangle(g, kPaletteSquares[7]);
    // hello's other textures have no palette, so the next frame starts without a palette type.
    gDPPipeSync(g++);
    gDPSetTextureLUT(g++, G_TT_NONE);

    // The corner markers, last so nothing covers them.
    g = td::AddCornerMarkers(g);

    gDPFullSync(g++);
    gSPEndDisplayList(g++);
}

// A shape of the test picture, found by its colour, which nothing else in the picture has: the box its pixels must
// fill (edge to edge, in picture pixels) and the area they cover. An edge may be 1 pixel off and the area 10%, for the
// way the GPU rounds edges to framebuffer pixels.
struct Shape {
    const char* name;
    uint16_t colour;
    float left, top, right, bottom;
    float area;
};

// The sprite's corners are (380,40) and (440,70). The magenta wall hides the sprite's left third, and the sprite hides
// the part of the green wall it overlaps, so the three shapes together show where the sprite's depth puts it.
const Shape kShapes[] = {
    { "force-matrix triangle (cyan)", Rgb5(0, 255, 255), 40, 20, 120, 100, 3200 },
    { "magenta wall (depth 613, in front of the sprite), whole", Rgb5(255, 0, 255), 360, 20, 400, 90, 2800 },
    { "green wall (depth 818, behind the sprite), less the sprite's 20x30", Rgb5(0, 255, 0), 420, 20, 460, 90, 2200 },
    { "sprite (yellow, depth 715), less the third behind the magenta wall", Rgb5(255, 255, 0), 400, 40, 440, 70, 1200 },
};

// Finds each shape's pixels anywhere in the picture and checks that they fill its box and that none lie outside it.
bool CheckShapes(const Picture& pic) {
    // The width and height of one framebuffer pixel in picture pixels.
    float sx = (float)kWidth / (float)pic.w;
    float sy = (float)kHeight / (float)pic.h;
    bool pass = true;

    for (const Shape& shape : kShapes) {
        uint32_t minX = pic.w, minY = pic.h, maxX = 0, maxY = 0;
        uint64_t count = 0;

        for (uint32_t row = 0; row < pic.h; row++) {
            uint32_t y = pic.topDown ? row : pic.h - 1 - row; // counted from the picture's top
            for (uint32_t x = 0; x < pic.w; x++) {
                if ((pic.px[row * pic.w + x] & 0xFFFE) == shape.colour) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                    count++;
                }
            }
        }
        if (count == 0) {
            std::printf("check: FAIL, %s is not in the picture (expected x %g-%g, y %g-%g)\n", shape.name,
                        shape.left, shape.right, shape.top, shape.bottom);
            pass = false;
            continue;
        }

        float left = (float)minX * sx;
        float right = (float)(maxX + 1) * sx;
        float top = (float)minY * sy;
        float bottom = (float)(maxY + 1) * sy;
        float area = (float)count * sx * sy;
        bool ok = std::fabs(left - shape.left) <= 1.0f && std::fabs(right - shape.right) <= 1.0f &&
                  std::fabs(top - shape.top) <= 1.0f && std::fabs(bottom - shape.bottom) <= 1.0f &&
                  std::fabs(area - shape.area) <= 0.1f * shape.area;

        std::printf("check: %s, %s: x %.1f-%.1f, y %.1f-%.1f, area %.0f (expected x %g-%g, y %g-%g, area %g)\n",
                    ok ? "PASS" : "FAIL", shape.name, left, right, top, bottom, area, shape.left, shape.right,
                    shape.top, shape.bottom, shape.area);
        pass = pass && ok;
    }
    return pass;
}

// A colour with 8 bits per channel, as the N64 works it out.
struct Colour {
    float r, g, b;
};

// The colour a palette entry gives over the backdrop behind it. IA16: the intensity in the high byte, blended over the
// backdrop by the alpha in the low byte (the blend G_RM_XLU_SURF sets up). RGBA16: 5 bits each of red, green and blue,
// scaled to 8 bits, then an alpha bit: 1 is opaque and 0 lets the backdrop show instead.
Colour EntryColour(uint16_t entry, bool ia16, Colour backdrop) {
    if (ia16) {
        float intensity = (float)(entry >> 8);
        float alpha = (float)(entry & 0xFF) / 255.0f;

        return { intensity * alpha + backdrop.r * (1.0f - alpha), intensity * alpha + backdrop.g * (1.0f - alpha),
                 intensity * alpha + backdrop.b * (1.0f - alpha) };
    }
    if ((entry & 1) == 0) {
        return backdrop;
    }
    return { (float)(entry >> 11) * 255.0f / 31.0f, (float)((entry >> 6) & 0x1F) * 255.0f / 31.0f,
             (float)((entry >> 1) & 0x1F) * 255.0f / 31.0f };
}

// One half of a palette square as it reads back, in the 5 bits per channel of the read-back picture (an 8-bit value
// divided by 8, rounded down): the average colour of its pixels, the colour they must have, how many pixels were read
// and how many of them are wrong.
struct HalfRead {
    float r = 0, g = 0, b = 0;
    int wantR = 0, wantG = 0, wantB = 0;
    uint32_t off = 0;
    uint32_t total = 0;
};

// Reads the half of a palette square that lies between left and right and between top and bottom (picture pixels).
// Only the pixels at least 2 picture pixels inside it count, so that the way the GPU rounds edges to framebuffer pixels
// does not matter, and a pixel is wrong only when a channel is more than 1 off, which leaves room for the way the GPU
// rounds the blend.
HalfRead ReadHalf(const Picture& pic, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, Colour want) {
    // The width and height of one picture pixel in framebuffer pixels.
    float sx = (float)pic.w / (float)kWidth;
    float sy = (float)pic.h / (float)kHeight;
    uint32_t x0 = (uint32_t)std::ceil((float)(left + 2) * sx);
    uint32_t x1 = (uint32_t)std::floor((float)(right - 2) * sx);
    uint32_t y0 = (uint32_t)std::ceil((float)(top + 2) * sy);
    uint32_t y1 = (uint32_t)std::floor((float)(bottom - 2) * sy);
    HalfRead read;
    uint64_t sumR = 0, sumG = 0, sumB = 0;

    read.wantR = (int)(want.r / 8.0f);
    read.wantG = (int)(want.g / 8.0f);
    read.wantB = (int)(want.b / 8.0f);
    for (uint32_t y = y0; y < y1; y++) {
        uint32_t row = pic.topDown ? y : pic.h - 1 - y; // y is counted from the picture's top
        for (uint32_t x = x0; x < x1; x++) {
            uint16_t px = pic.px[row * pic.w + x];
            int r = px >> 11;
            int g = (px >> 6) & 0x1F;
            int b = (px >> 1) & 0x1F;

            sumR += (uint64_t)r;
            sumG += (uint64_t)g;
            sumB += (uint64_t)b;
            if (std::abs(r - read.wantR) > 1 || std::abs(g - read.wantG) > 1 || std::abs(b - read.wantB) > 1) {
                read.off++;
            }
            read.total++;
        }
    }
    if (read.total > 0) {
        read.r = (float)sumR / (float)read.total;
        read.g = (float)sumG / (float)read.total;
        read.b = (float)sumB / (float)read.total;
    }
    return read;
}

// A half's colours for the check's report, with the number of wrong pixels when there are any.
std::string DescribeHalf(const HalfRead& read) {
    char text[128];

    std::snprintf(text, sizeof(text), "(%.0f,%.0f,%.0f), expected (%d,%d,%d)", read.r, read.g, read.b, read.wantR,
                  read.wantG, read.wantB);
    std::string description = text;
    if (read.off > 0) {
        std::snprintf(text, sizeof(text), ", %u of %u pixels wrong", read.off, read.total);
        description += text;
    }
    return description;
}

// The colour one half of a square must have. The upper row's halves show palette entries kEntryLeft and kEntryRight
// (an IA16 one blended over the red backdrop), and so do the lower row's squares drawn with palettes on, whose texels
// index entries that hold the same two values. With palettes off, an IA4 texel is a 3-bit intensity and an alpha bit,
// and an IA8 texel a 4-bit intensity and a 4-bit alpha, widened to 8 bits as Fast3D widens them (times 36 and 17);
// the lower row's texels are all opaque.
Colour HalfColour(const PaletteSquare& square, bool right) {
    const Colour red = { 255.0f, 0.0f, 0.0f };
    float grey;

    switch (square.read) {
        case Read::PaletteIa16:
        case Read::PaletteRgba16:
            return EntryColour(right ? kEntryRight : kEntryLeft, square.read == Read::PaletteIa16, red);
        case Read::Ia4:
            grey = (float)(((right ? kIa4Right : kIa4Left) >> 1) * 36);
            break;
        default:
            grey = (float)(((right ? kIa8Right : kIa8Left) >> 4) * 17);
            break;
    }
    return { grey, grey, grey };
}

// Checks that each palette square has the colours its texels and palette type give: one colour on its left half and
// another on its right half (HalfColour).
bool CheckPalettes(const Picture& pic) {
    bool pass = true;

    for (const PaletteSquare& square : kPaletteSquares) {
        uint32_t half = square.size / 2;
        uint32_t bottom = square.top + square.size;
        HalfRead left =
            ReadHalf(pic, square.left, square.top, square.left + half, bottom, HalfColour(square, false));
        HalfRead right = ReadHalf(pic, square.left + half, square.top, square.left + square.size, bottom,
                                  HalfColour(square, true));

        std::printf("check: palette square, %s: left half %s; right half %s\n", square.name,
                    DescribeHalf(left).c_str(), DescribeHalf(right).c_str());
        pass = pass && left.total > 0 && right.total > 0 && left.off == 0 && right.off == 0;
    }
    if (pass) {
        std::printf("check: PASS, the palette squares have the colours of IA16 and RGBA16 palettes, for CI4 and CI8 "
                    "texels, and for IA4 and IA8 texels with palettes on and off (colours as read back, 5 bits per "
                    "channel)\n");
    } else {
        std::printf("check: FAIL, a palette square does not have the colours of its texels and palette type (colours "
                    "as read back, 5 bits per channel; each may be 1 off)\n");
    }
    return pass;
}

// Reads the picture back and runs every check on it.
bool CheckPicture(const std::shared_ptr<Fast::Fast3dWindow>& window) {
    Picture pic;

    if (!ReadPicture(window, &pic)) {
        return false;
    }
    if (!CheckCorners(&pic)) {
        std::printf("check: the shapes and the palette squares are not checked, because it is not known which way up "
                    "the rows are\n");
        return false;
    }
    bool shapes = CheckShapes(pic);
    bool palettes = CheckPalettes(pic);
    return shapes && palettes;
}

} // namespace

int main(int argc, char** argv) {
    long frameLimit = 0;
    bool check = false;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char* end;
            frameLimit = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || frameLimit <= 0) {
                std::fprintf(stderr, "hello: --frames needs a whole number above 0\n");
                return 1;
            }
        } else if (std::strcmp(argv[i], "--check") == 0) {
            check = true;
        } else {
            std::fprintf(stderr, "usage: hello [--frames N] [--check]\n");
            return 1;
        }
    }

    td::PreferWslGpu();
    // A --check run (as on CI) has nobody to close the crash handler's message box, so there it is left out, and a
    // crash ends hello at once with the system's exit code for it.
    td::LibUltraShip lus = td::StartLibUltraShip("hello", !check);
    if (lus.window == nullptr) {
        return 1;
    }
    auto& window = lus.window;

    td::ShowGamePicture(window, check);
    BuildMatrices();
    BuildPaletteSquares();

    int result = 0;
    bool checked = false;
    long frame = 0;
    auto start = std::chrono::steady_clock::now();

    while (WindowIsRunning() && (frameLimit == 0 || frame < frameLimit)) {
        window->HandleEvents();
        BuildFrame((uint32_t)frame);
        // A backend that paces frames itself (DXGI on Windows) skips drawing a frame that is already late. Count only
        // the frames it drew.
        if (!window->DrawAndRunGraphicsCommands(sDisplayList, {})) {
            continue;
        }
        frame++;
        if (check && frame == 3) {
            checked = true;
            if (!CheckPicture(window)) {
                result = 2;
            }
        }
    }

    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("hello: %ld frames in %.3f s (%.2f per second)\n", frame, seconds, frame / seconds);
    if (check && !checked) {
        std::printf("check: not run, hello stopped before the third frame\n");
        result = 2;
    }
    td::StopLibUltraShip(&lus);
    return result;
}
