// The splash screen (src/splash.h). Each window frame is one display list that LibUltraShip's renderer (Fast3D) runs,
// as it runs the game's: a dark background in bands, the cube, and the text, drawn in the game's 480x240 picture,
// which the window shows at 4:3 (td::ShowGamePicture). The picture's pixels are 1.5 times as tall as they are wide.
//
// The cube: six squares of 100 units around the origin, two of each of Sting's three faces (src/splash_faces.c), on
// opposite sides. Every frame it turns a little about the vertical axis and rocks about the horizontal one. Each
// side's vertices get a grey level from how much the side faces a light in front of it, above and to the left, and
// the texels are multiplied by it (G_CC_MODULATEIDECALA), so the sides turning away grow darker. The depth buffer
// keeps the nearest side at every pixel.
//
// The text is drawn with a small pixel font of the port's own, 5x7 dots for each letter: each dot is a rectangle
// filled in fill mode: 4x3 picture pixels for the name and 3x2 for the line under it, which look square on screen, and
// 2x1 for "PRESS START".
// The splash fades in from black and out to black: every colour of the frame is scaled by how far the fade has gone.
#include "splash.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "fast/Fast3dWindow.h"
#include "fast/interpreter.h"
#include "frame.h"
#include "libultraship/libultraship.h"
#include "ship/window/gui/Gui.h"
#include "splash_faces.h"

namespace td {
namespace {

constexpr int kFrames = 240; // four seconds at 60 frames a second, then the fade out
constexpr int kFade = 20;    // frames of each fade
constexpr int kHalf = 50;    // half a side of the cube, in model units

// The font: 5x7 dots for each letter, a row of the picture per string ('#' a dot). Letters missing here are drawn as
// spaces; lower case is drawn as upper case.
struct Glyph {
    char c;
    const char* rows[7];
};
constexpr Glyph kFont[] = {
    { 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
    { 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
    { 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
    { 'D', { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
    { 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
    { 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
    { 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" } },
    { 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
    { 'I', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" } },
    { 'J', { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
    { 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
    { 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
    { 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
    { 'N', { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" } },
    { 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
    { 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
    { 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
    { 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
    { 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
    { 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
    { 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
    { 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
    { 'W', { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#." } },
    { 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
    { 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
    { 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
};

const Glyph* FindGlyph(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const Glyph& glyph : kFont) {
        if (glyph.c == c) {
            return &glyph;
        }
    }
    return nullptr;
}

// What the frame's commands point at. The colour and depth images' addresses only matter to Fast3D's rules for them.
uint16_t sColorImage[1];
uint16_t sDepthImage[1];
Vp sViewport = { { { kGameWidth * 2, kGameHeight * 2, G_MAXZ / 2, 0 },
                   { kGameWidth * 2, kGameHeight * 2, G_MAXZ / 2, 0 } } };
Mtx sProjection;
Mtx sModelview;
Vtx sCube[24];
Gfx sFrame[4096];

// A colour scaled by the fade (0 to 1), as a fill colour.
uint32_t FadedFill(int r, int g, int b, float fade) {
    return Fill(static_cast<uint32_t>(r * fade), static_cast<uint32_t>(g * fade), static_cast<uint32_t>(b * fade));
}

// Draws text centred on the picture, moved right by shift picture pixels, its top at y, each dot dx by dy picture
// pixels, in fill mode.
Gfx* DrawText(Gfx* g, const char* text, int y, int dx, int dy, uint32_t fill, int shift = 0) {
    int width = static_cast<int>(std::strlen(text)) * 6 * dx - dx;
    int x = (static_cast<int>(kGameWidth) - width) / 2 + shift;

    gDPPipeSync(g++);
    gDPSetFillColor(g++, fill);
    for (const char* p = text; *p != '\0'; p++, x += 6 * dx) {
        const Glyph* glyph = FindGlyph(*p);

        if (glyph == nullptr) {
            continue;
        }
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph->rows[row][col] == '#') {
                    int left = x + col * dx, top = y + row * dy;
                    gDPFillRectangle(g++, left, top, left + dx - 1, top + dy - 1);
                }
            }
        }
    }
    return g;
}

// Text with a shadow one dot down and to the right.
Gfx* DrawShadowedText(Gfx* g, const char* text, int y, int dx, int dy, const int colour[3], const int shadow[3],
                      float fade) {
    g = DrawText(g, text, y + dy, dx, dy, FadedFill(shadow[0], shadow[1], shadow[2], fade), dx);
    return DrawText(g, text, y, dx, dy, FadedFill(colour[0], colour[1], colour[2], fade));
}

// Fills the cube's 24 vertices for this frame: 4 for each side, which has its normal n and its up direction u; its
// right direction is u x n. The texture runs from the side's top left (s, t = 0) to its bottom right (64 texels, in
// 1/32 texel). turn: the cube's rotation (a point p goes to p * turn), which gives each side its grey level.
void FillCube(const float turn[3][3], float fade) {
    static const float kSides[6][2][3] = {
        { { 0, 0, 1 }, { 0, 1, 0 } },  { { 1, 0, 0 }, { 0, 1, 0 } },  { { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1, 0, 0 }, { 0, 1, 0 } }, { { 0, 1, 0 }, { 0, 0, -1 } }, { { 0, -1, 0 }, { 0, 0, 1 } },
    };
    const float light[3] = { -0.35f, 0.55f, 0.76f }; // towards the light: in front, above and to the left

    for (int side = 0; side < 6; side++) {
        const float* n = kSides[side][0];
        const float* u = kSides[side][1];
        float r[3] = { u[1] * n[2] - u[2] * n[1], u[2] * n[0] - u[0] * n[2], u[0] * n[1] - u[1] * n[0] };
        float turned[3], facing;
        uint8_t grey;

        for (int j = 0; j < 3; j++) {
            turned[j] = n[0] * turn[0][j] + n[1] * turn[1][j] + n[2] * turn[2][j];
        }
        facing = std::max(0.0f, turned[0] * light[0] + turned[1] * light[1] + turned[2] * light[2]);
        grey = static_cast<uint8_t>(std::min(255.0f, 255.0f * (0.3f + 0.75f * facing) * fade));
        for (int corner = 0; corner < 4; corner++) {
            int sx = corner == 0 || corner == 3 ? -1 : 1; // corners: top left, top right, bottom right, bottom left
            int sy = corner < 2 ? 1 : -1;
            Vtx* v = &sCube[side * 4 + corner];

            for (int j = 0; j < 3; j++) {
                v->v.ob[j] = static_cast<short>(kHalf * (n[j] + sx * r[j] + sy * u[j]));
            }
            v->v.flag = 0;
            v->v.tc[0] = static_cast<short>(sx < 0 ? 0 : SPLASH_FACE_SIZE << 5);
            v->v.tc[1] = static_cast<short>(sy > 0 ? 0 : SPLASH_FACE_SIZE << 5);
            v->v.cn[0] = v->v.cn[1] = v->v.cn[2] = grey;
            v->v.cn[3] = 0xFF;
        }
    }
}

// The matrices: the cube turned by yaw about the vertical axis and pitch about the horizontal one, 400 units in front
// of the camera; a perspective of 40 degrees from top to bottom, for a picture of 4:3, moved up so that the cube sits
// above the text. Points are rows, as on the N64: a point p goes to p * modelview * projection.
void SetMatrices(float yaw, float pitch, float turn[3][3]) {
    float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    float spin[4][4] = { { cy, 0, -sy, 0 }, { 0, 1, 0, 0 }, { sy, 0, cy, 0 }, { 0, 0, 0, 1 } };
    float tilt[4][4] = { { 1, 0, 0, 0 }, { 0, cp, sp, 0 }, { 0, -sp, cp, 0 }, { 0, 0, 0, 1 } };
    float away[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, -400, 1 } };
    float turned[4][4], modelview[4][4];
    const float f = 1.0f / std::tan(20.0f * 3.14159265f / 180.0f);
    const float nearZ = 100.0f, farZ = 1000.0f;
    float projection[4][4] = { { f * 3.0f / 4.0f, 0, 0, 0 },
                               { 0, f, 0, 0 },
                               { 0, -0.3f, (farZ + nearZ) / (nearZ - farZ), -1 },
                               { 0, 0, 2 * farZ * nearZ / (nearZ - farZ), 0 } };

    MultiplyMatrices(spin, tilt, turned);
    MultiplyMatrices(turned, away, modelview);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            turn[i][j] = turned[i][j];
        }
    }
    MatrixFromFloat(&sModelview, modelview);
    MatrixFromFloat(&sProjection, projection);
}

// The frame's display list for splash frame `frame`, faded by fade (0 to 1).
void BuildFrame(int frame, float fade, const uint8_t* faces) {
    Gfx* g = sFrame;
    float turn[3][3];
    float t = static_cast<float>(frame) / 60.0f;

    // The background: 16 bands, from a dark blue at the top to black at the bottom.
    gDPPipeSync(g++);
    gDPSetColorImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kGameWidth, sColorImage);
    gDPSetDepthImage(g++, sDepthImage);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, kGameWidth, kGameHeight);
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    for (int band = 0; band < 16; band++) {
        int top = band * static_cast<int>(kGameHeight) / 16;
        int bottom = (band + 1) * static_cast<int>(kGameHeight) / 16 - 1;
        int k = 15 - band;

        gDPPipeSync(g++);
        gDPSetFillColor(g++, FadedFill(2 * k, 3 * k, 8 + 5 * k, fade));
        gDPFillRectangle(g++, 0, top, kGameWidth - 1, bottom);
    }

    // The cube.
    SetMatrices(t * 1.6f, 0.45f + 0.25f * std::sin(t * 1.3f), turn);
    FillCube(turn, fade);
    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPClearGeometryMode(g++, 0xFFFFFFFF);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetTextureLUT(g++, G_TT_NONE);
    gDPSetTexturePersp(g++, G_TP_PERSP);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(g++, G_CC_MODULATEIDECALA, G_CC_MODULATEIDECALA);
    gSPViewport(g++, &sViewport);
    gSPMatrix(g++, &sProjection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, &sModelview, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    // Front and back, right and left, top and bottom (FillCube's order) wear the same face.
    static const int kFaceOfSide[6] = { 0, 1, 0, 1, 2, 2 };
    for (int side = 0; side < 6; side++) {
        gDPLoadTextureBlock(g++, faces + kFaceOfSide[side] * SPLASH_FACE_BYTES, G_IM_FMT_RGBA, G_IM_SIZ_16b,
                            SPLASH_FACE_SIZE, SPLASH_FACE_SIZE, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        __gSPVertex(g++, &sCube[side * 4], 4, 0);
        gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
    }
    gSPClearGeometryMode(g++, G_ZBUFFER);

    // The text.
    static const int kGold[3] = { 255, 204, 64 };
    static const int kWhite[3] = { 224, 224, 232 };
    static const int kShadow[3] = { 90, 16, 16 };
    static const int kDark[3] = { 8, 8, 20 };
    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    g = DrawShadowedText(g, "TURNBUCKLE DIVE", 168, 4, 3, kGold, kShadow, fade);
    g = DrawShadowedText(g, "A NATIVE PORT", 196, 3, 2, kWhite, kDark, fade);
    if (frame > kFade && (frame / 30) % 2 == 0) {
        g = DrawText(g, "PRESS START", 226, 2, 1, FadedFill(kWhite[0], kWhite[1], kWhite[2], fade));
    }
    gDPPipeSync(g++);
    gDPFullSync(g++);
    gSPEndDisplayList(g++);
}

} // namespace

bool RunSplash(const std::shared_ptr<Fast::Fast3dWindow>& window, const uint8_t* faces,
               const std::function<bool()>& pressed) {
    bool armed = !pressed(); // a button held from before the splash does not end it
    int end = kFrames;       // the frame the fade out ends at

    for (int frame = 0; frame < end; frame++) {
        float fade = std::min(1.0f, static_cast<float>(frame) / kFade);

        fade = std::min(fade, static_cast<float>(end - frame) / kFade);
        window->HandleEvents();
        if (!WindowIsRunning()) {
            return false;
        }
        bool down = pressed();
        if (armed && down && end > frame + kFade) {
            end = frame + kFade; // fade out from here
        }
        armed = armed || !down;
        BuildFrame(frame, fade, faces);
        window->GetMouseStateManager()->StartFrame();
        window->GetGui()->StartDraw();
        window->StartFrame();
        window->GetInterpreterWeak().lock()->Run(sFrame, {});
        window->GetGui()->EndDraw();
        window->EndFrame(); // shows the frame, and waits for the next one at the target rate
    }
    return true;
}

} // namespace td
