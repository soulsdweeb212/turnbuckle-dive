// hello: the first Turnbuckle Dive window. LibUltraShip opens it, and every frame it draws a test picture made of N64
// display-list commands, at the game's resolution (480x240) shown at 4:3.
//
//   hello              run until the window is closed
//   hello --frames N   draw N frames, print how long they took, then exit
//   hello --check      draw at twice the window's resolution and, after the third frame, read the picture back and
//                      check it: the corner markers, and that each shape placed with gSPForceMatrix or
//                      gSPModifyVertex fills exactly its box (exit 2 if a check fails, or if hello stops before the
//                      third frame)
//
// The picture: a dark background, a shaded triangle drawn through the RSP path (viewport, matrices, vertices), three
// ring ropes, a square that moves one pixel per frame, and one-pixel white markers at (0,0) and (479,239). Above the
// ropes are the shapes that test LibUltraShip patches 0003 and 0004: on the left a cyan triangle placed only by a
// force matrix; on the right a yellow sprite built the way the game builds its sprites, in front of a green wall and
// behind a magenta one.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#include "fast/Fast3dWindow.h"
#include "fast/interpreter.h"
#include "libultraship/bridge/UltraBridge.h"
#include "libultraship/controller/controldeck/ControlDeck.h"
#include "libultraship/libultraship.h"
#include "ship/audio/Audio.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/debug/Console.h"
#include "ship/debug/CrashHandler.h"
#include "ship/events/Events.h"
#include "ship/log/Logger.h"
#include "ship/resource/ResourceManager.h"
#include "ship/thread/ThreadPool.h"
#include "ship/window/FileDrop.h"

namespace {

constexpr uint32_t kWidth = 480;
constexpr uint32_t kHeight = 240;

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

Gfx sDisplayList[256];
// The bitmask of connected controller ports. The control deck keeps a pointer to it, so it must outlive startup.
uint8_t sControllerBits;

// The N64's fixed-point matrix layout: 16 words of integer halves, then 16 of fractions, two elements per word.
void MatrixFromFloat(Mtx* m, const float f[4][4]) {
    int32_t* intWords = &m->m[0][0];
    int32_t* fracWords = &m->m[2][0];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t a = (int32_t)(f[i][j] * 65536.0f);
            int32_t b = (int32_t)(f[i][j + 1] * 65536.0f);

            *intWords++ = (int32_t)(((uint32_t)a & 0xFFFF0000u) | ((uint32_t)b >> 16));
            *fracWords++ = (int32_t)(((uint32_t)a << 16) | ((uint32_t)b & 0xFFFFu));
        }
    }
}

// Multiplies two matrices the N64 way round: a point (a row) is transformed by a first, then by b.
void MultiplyMatrices(const float a[4][4], const float b[4][4], float out[4][4]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
}

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

uint32_t Fill(uint32_t r, uint32_t g, uint32_t b) {
    uint32_t c = GPACK_RGBA5551(r, g, b, 1);

    return (c << 16) | c;
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

void BuildFrame(uint32_t frame) {
    Gfx* g = sDisplayList;
    uint32_t squareX = 20 + frame % 420;

    gDPPipeSync(g++);
    gDPSetColorImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kWidth, sColorImage);
    gDPSetDepthImage(g++, sDepthImage);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, kWidth, kHeight);

    // Fill mode: background, ropes, moving square.
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(g++, Fill(16, 24, 72));
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

    // The corner markers, last so nothing covers them.
    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(g++, Fill(255, 255, 255));
    gDPFillRectangle(g++, 0, 0, 0, 0);
    gDPFillRectangle(g++, kWidth - 1, kHeight - 1, kWidth - 1, kHeight - 1);

    gDPFullSync(g++);
    gSPEndDisplayList(g++);
}

#ifdef __linux__
// Under WSL, Mesa draws on the CPU (llvmpipe) unless it is told to use the GPU through Direct3D 12. /dev/dxg only
// exists in WSL with GPU support, and an explicit GALLIUM_DRIVER always wins.
void PreferWslGpu() {
    if (std::getenv("GALLIUM_DRIVER") == nullptr && access("/dev/dxg", F_OK) == 0) {
        setenv("GALLIUM_DRIVER", "d3d12", 0);
        std::printf("WSL: using the GPU through Direct3D 12 (set GALLIUM_DRIVER=llvmpipe to draw on the CPU)\n");
    }
}
#endif

// LibUltraShip's parts only hold the context weakly, so whoever starts it must keep the context alive.
struct LibUltraShip {
    std::shared_ptr<Ship::Context> ctx;
    std::shared_ptr<Fast::Fast3dWindow> window;
    std::shared_ptr<LUS::UltraBridge> bridge;
};

// Starting the log and opening f3d.o2r are the steps a bad setup breaks (a folder hello cannot write to, a missing
// archive), and LibUltraShip reports both by throwing. They run before the window is added, because destroying a
// context that holds a window which never started crashes.
//
// LibUltraShip's crash handler answers a crash with a message box and waits for someone to close it. A --check run
// (as on CI) has nobody to do that, so there it is left out, and a crash ends hello at once with the system's exit
// code for it.
LibUltraShip StartLibUltraShip(bool withCrashHandler) {
    // The logger is made before the context so that, when startup fails, it is destroyed after the context: the
    // context's destructor shuts the log down while the config, which logs as it goes away, is still alive.
    std::string logPath = Ship::Context::GetPathRelativeToAppDirectory("logs/hello.log");
    auto logger = std::make_shared<Ship::Logger>("Turnbuckle Dive hello", logPath);
    auto ctx = Ship::Context::CreateInstance("Turnbuckle Dive hello", "tdhello");
    ctx->GetChildren().Add(logger);
    try {
        logger->Init();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "hello: could not start the log %s: %s\n", logPath.c_str(), e.what());
        return {};
    }

    auto config = std::make_shared<Ship::Config>(Ship::Context::GetPathRelativeToAppDirectory("hello.cfg.json"));
    ctx->GetChildren().Add(config);
    auto cvars = std::make_shared<Ship::ConsoleVariable>(config);
    ctx->GetChildren().Add(cvars);
    auto threads = std::make_shared<Ship::ThreadPool>(std::max(1u, std::thread::hardware_concurrency() / 2));
    ctx->GetChildren().Add(threads);
    auto resources = std::make_shared<Ship::ResourceManager>(threads);
    ctx->GetChildren().Add(resources);
    if (withCrashHandler) {
        ctx->GetChildren().Add(std::make_shared<Ship::CrashHandler>());
    }
    auto console = std::make_shared<Ship::Console>();
    ctx->GetChildren().Add(console);

    std::string archivePath = Ship::Context::GetPathRelativeToAppBundle("f3d.o2r");
    nlohmann::json resourceArgs;
    resourceArgs["archivePaths"] = std::vector<std::string>{ archivePath };
    resourceArgs["validHashes"] = std::vector<uint32_t>{};
    try {
        resources->Init(resourceArgs);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "hello: could not open %s: %s\n", archivePath.c_str(), e.what());
        return {};
    }

    // The window comes before the control deck, which needs it; the deck must be in the context before the window
    // initialises, because the GUI looks it up then.
    auto window = std::make_shared<Fast::Fast3dWindow>(config, cvars, nullptr);
    ctx->GetChildren().Add(window);
    auto deck = std::make_shared<LUS::ControlDeck>(window, cvars);
    ctx->GetChildren().Add(deck);

    auto audio = std::make_shared<Ship::Audio>(Ship::AudioSettings{}, config);
    ctx->GetChildren().Add(audio);
    ctx->GetChildren().Add(std::make_shared<Ship::Events>());
    auto fileDrop = std::make_shared<Ship::FileDrop>(window);
    ctx->GetChildren().Add(fileDrop);
    // Without the bridge, the C functions (WindowIsRunning and the rest) cannot find the window.
    auto bridge = std::make_shared<LUS::UltraBridge>();
    ctx->GetChildren().Add(bridge);
    // All the parts are in place. The context's own Init() only marks it ready, and the window's GUI will not start
    // until it is.
    ctx->Init();

    console->Init();
    window->Init();
    fileDrop->Init();
    audio->Init();
    bridge->UpdateCaches(ctx);

    deck->Init(&sControllerBits);
    return { ctx, window, bridge };
}

const char* Describe(uint16_t px) {
    uint32_t r = px >> 11, g = (px >> 6) & 0x1F, b = (px >> 1) & 0x1F;

    if (r == 31 && g == 31 && b == 31) {
        return "white";
    }
    return (r < 8 && g < 8 && b > 4) ? "background" : "other";
}

// The game picture read back from Fast3D's framebuffer: w x h RGBA5551 pixels for the 480x240 picture, row after row
// in the order they were read. Fast3D's framebuffer may be stored upside down; CheckCorners finds out which way.
struct Picture {
    uint32_t w = 0;
    uint32_t h = 0;
    std::vector<uint16_t> px;
    bool topDown = false; // the first row read is the picture's top row
};

bool ReadPicture(const std::shared_ptr<Fast::Fast3dWindow>& window, Picture* pic) {
    auto interp = window->GetInterpreterWeak().lock();

    if (interp == nullptr || !interp->mRendersToFb) {
        std::printf("check: the picture is drawn straight to the window, so it cannot be read back\n");
        return false;
    }

    pic->w = interp->mCurDimensions.width;
    pic->h = interp->mCurDimensions.height;
    pic->px.resize(pic->w * pic->h);
    interp->mRapi->ReadFramebufferToCPU(interp->mGameFb, pic->w, pic->h, pic->px.data());
    std::printf("check: framebuffer %ux%u for a %ux%u picture (%.2fx)\n", pic->w, pic->h, kWidth, kHeight,
                (double)pic->h / kHeight);
    return true;
}

// Checks that the markers are in two opposite corners and the other two corners show the background. Either diagonal
// counts if it is consistent, and it tells which way up the rows were read.
bool CheckCorners(Picture* pic) {
    uint32_t w = pic->w;
    uint32_t h = pic->h;
    const std::vector<uint16_t>& px = pic->px;

    // Sample the middle of each corner pixel of the 480x240 picture as it lands in the w x h framebuffer.
    uint32_t dx = (uint32_t)(0.5f * w / kWidth);
    uint32_t dy = (uint32_t)(0.5f * h / kHeight);
    uint16_t lowLeft = px[dy * w + dx];
    uint16_t lowRight = px[dy * w + (w - 1 - dx)];
    uint16_t highLeft = px[(h - 1 - dy) * w + dx];
    uint16_t highRight = px[(h - 1 - dy) * w + (w - 1 - dx)];

    std::printf("check: corners, first row read: left %s, right %s; last row read: left %s, right %s\n",
                Describe(lowLeft), Describe(lowRight), Describe(highLeft), Describe(highRight));

    auto is = [](uint16_t p, const char* what) { return std::strcmp(Describe(p), what) == 0; };
    bool upright = is(highLeft, "white") && is(lowRight, "white") && is(lowLeft, "background") &&
                   is(highRight, "background");
    bool flipped = is(lowLeft, "white") && is(highRight, "white") && is(highLeft, "background") &&
                   is(lowRight, "background");

    if (upright || flipped) {
        pic->topDown = flipped;
        std::printf("check: PASS, markers at (0,0) and (479,239) are in the picture's corners (%s rows)\n",
                    upright ? "bottom-up" : "top-down");
        return true;
    }
    std::printf("check: FAIL, the markers are not in two opposite corners\n");
    return false;
}

// A colour as it reads back: 5 bits per channel, without the alpha bit.
constexpr uint16_t Rgb5(uint32_t r, uint32_t g, uint32_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1));
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

// Reads the picture back and runs every check on it.
bool CheckPicture(const std::shared_ptr<Fast::Fast3dWindow>& window) {
    Picture pic;

    if (!ReadPicture(window, &pic)) {
        return false;
    }
    if (!CheckCorners(&pic)) {
        std::printf("check: the shapes are not checked, because it is not known which way up the rows are\n");
        return false;
    }
    return CheckShapes(pic);
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

#ifdef __linux__
    PreferWslGpu();
#endif
    LibUltraShip lus = StartLibUltraShip(!check);
    if (lus.window == nullptr) {
        return 1;
    }
    auto& window = lus.window;

    GfxSetNativeDimensions(kWidth, kHeight);
    CVarSetInteger("gAdvancedResolution.Enabled", 1);
    CVarSetFloat("gAdvancedResolution.AspectRatioX", 4.0f);
    CVarSetFloat("gAdvancedResolution.AspectRatioY", 3.0f);
    window->SetTargetFps(60);
    if (check) {
        // When the picture's resolution equals its place in the window, Fast3D draws it straight to the window, which
        // cannot be read back. Drawing at twice that resolution sends it through Fast3D's own framebuffer instead,
        // whatever the window's shape. This is not saved in the settings.
        window->SetResolutionMultiplier(2.0f);
    }
    BuildMatrices();

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
    if (WindowIsRunning()) {
        window->Close();
    }
    // Drop the bridge's global pointers before the parts they point to go away.
    lus.bridge->ClearCaches();
    lus = {};
    return result;
}
