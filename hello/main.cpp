// hello: the first Turnbuckle Dive window. LibUltraShip opens it, and every frame it draws a test picture made of N64
// display-list commands, at the game's resolution (480x240) shown at 4:3.
//
//   hello              run until the window is closed
//   hello --frames N   draw N frames, print how long they took, then exit
//   hello --check      draw at twice the window's resolution and, after the third frame, read the picture back and
//                      check the corner markers (exit 2 if they are wrong, or if hello stops before the third frame)
//
// The picture: a dark background, a shaded triangle drawn through the RSP path (viewport, matrices, vertices), three
// ring ropes, a square that moves one pixel per frame, and one-pixel white markers at (0,0) and (479,239).

#include <algorithm>
#include <chrono>
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
Gfx sDisplayList[128];
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

// An orthographic projection that maps pixel coordinates (0,0 top left) straight onto the screen.
void BuildMatrices() {
    float ortho[4][4] = {};
    float ident[4][4] = {};

    ortho[0][0] = 2.0f / kWidth;
    ortho[1][1] = -2.0f / kHeight;
    ortho[2][2] = -1.0f / 10.0f;
    ortho[3][0] = -1.0f;
    ortho[3][1] = 1.0f;
    ortho[3][3] = 1.0f;
    for (int i = 0; i < 4; i++) {
        ident[i][i] = 1.0f;
    }
    MatrixFromFloat(&sProjection, ortho);
    MatrixFromFloat(&sIdentity, ident);
}

uint32_t Fill(uint32_t r, uint32_t g, uint32_t b) {
    uint32_t c = GPACK_RGBA5551(r, g, b, 1);

    return (c << 16) | c;
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
LibUltraShip StartLibUltraShip() {
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
    ctx->GetChildren().Add(std::make_shared<Ship::CrashHandler>());
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

// Reads the game picture back and checks that the markers are in two opposite corners and the other two corners show
// the background. Fast3D's framebuffer may be stored upside down, so either diagonal counts if it is consistent.
bool CheckCorners(const std::shared_ptr<Fast::Fast3dWindow>& window) {
    auto interp = window->GetInterpreterWeak().lock();

    if (interp == nullptr || !interp->mRendersToFb) {
        std::printf("check: the picture is drawn straight to the window, so it cannot be read back\n");
        return false;
    }

    uint32_t w = interp->mCurDimensions.width;
    uint32_t h = interp->mCurDimensions.height;
    std::vector<uint16_t> px(w * h);
    interp->mRapi->ReadFramebufferToCPU(interp->mGameFb, w, h, px.data());

    // Sample the middle of each corner pixel of the 480x240 picture as it lands in the w x h framebuffer.
    uint32_t dx = (uint32_t)(0.5f * w / kWidth);
    uint32_t dy = (uint32_t)(0.5f * h / kHeight);
    uint16_t lowLeft = px[dy * w + dx];
    uint16_t lowRight = px[dy * w + (w - 1 - dx)];
    uint16_t highLeft = px[(h - 1 - dy) * w + dx];
    uint16_t highRight = px[(h - 1 - dy) * w + (w - 1 - dx)];

    std::printf("check: framebuffer %ux%u for a %ux%u picture (%.2fx)\n", w, h, kWidth, kHeight, (double)h / kHeight);
    std::printf("check: corners, first row read: left %s, right %s; last row read: left %s, right %s\n",
                Describe(lowLeft), Describe(lowRight), Describe(highLeft), Describe(highRight));

    auto is = [](uint16_t p, const char* what) { return std::strcmp(Describe(p), what) == 0; };
    bool upright = is(highLeft, "white") && is(lowRight, "white") && is(lowLeft, "background") &&
                   is(highRight, "background");
    bool flipped = is(lowLeft, "white") && is(highRight, "white") && is(highLeft, "background") &&
                   is(lowRight, "background");

    if (upright || flipped) {
        std::printf("check: PASS, markers at (0,0) and (479,239) are in the picture's corners (%s rows)\n",
                    upright ? "bottom-up" : "top-down");
        return true;
    }
    std::printf("check: FAIL, the markers are not in two opposite corners\n");
    return false;
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
    LibUltraShip lus = StartLibUltraShip();
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
            if (!CheckCorners(window)) {
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
