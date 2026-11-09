#include "context.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#include "fast/Fast3dWindow.h"
#include "frame.h"
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

namespace td {

namespace {

// The bitmask of connected controller ports. The control deck keeps a pointer to it, so it must outlive startup.
uint8_t sControllerBits;

} // namespace

void PreferWslGpu() {
#ifdef __linux__
    // /dev/dxg only exists in WSL with GPU support, and an explicit GALLIUM_DRIVER always wins.
    if (std::getenv("GALLIUM_DRIVER") == nullptr && access("/dev/dxg", F_OK) == 0) {
        setenv("GALLIUM_DRIVER", "d3d12", 0);
        std::printf("WSL: using the GPU through Direct3D 12 (set GALLIUM_DRIVER=llvmpipe to draw on the CPU)\n");
    }
#endif
}

// Starting the log and opening f3d.o2r are the steps a bad setup breaks (a folder the program cannot write to, a
// missing archive), and LibUltraShip reports both by throwing. They run before the window is added, because destroying
// a context that holds a window which never started crashes.
LibUltraShip StartLibUltraShip(const char* program, bool withCrashHandler) {
    std::string name = std::string("Turnbuckle Dive ") + program;

    // The logger is made before the context so that, when startup fails, it is destroyed after the context: the
    // context's destructor shuts the log down while the config, which logs as it goes away, is still alive.
    std::string logPath = Ship::Context::GetPathRelativeToAppDirectory(std::string("logs/") + program + ".log");
    auto logger = std::make_shared<Ship::Logger>(name, logPath);
    auto ctx = Ship::Context::CreateInstance(name, std::string("td") + program);
    ctx->GetChildren().Add(logger);
    try {
        logger->Init();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: could not start the log %s: %s\n", program, logPath.c_str(), e.what());
        return {};
    }

    auto config = std::make_shared<Ship::Config>(
        Ship::Context::GetPathRelativeToAppDirectory(std::string(program) + ".cfg.json"));
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
        std::fprintf(stderr, "%s: could not open %s: %s\n", program, archivePath.c_str(), e.what());
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

void ShowGamePicture(const std::shared_ptr<Fast::Fast3dWindow>& window, bool readBack) {
    GfxSetNativeDimensions(kGameWidth, kGameHeight);
    CVarSetInteger("gAdvancedResolution.Enabled", 1);
    CVarSetFloat("gAdvancedResolution.AspectRatioX", 4.0f);
    CVarSetFloat("gAdvancedResolution.AspectRatioY", 3.0f);
    window->SetTargetFps(60);
    if (readBack) {
        // When the picture's resolution equals its place in the window, Fast3D draws it straight to the window, which
        // cannot be read back. Drawing at twice that resolution sends it through Fast3D's own framebuffer instead,
        // whatever the window's shape.
        window->SetResolutionMultiplier(2.0f);
    }
}

void StopLibUltraShip(LibUltraShip* lus) {
    if (lus->window == nullptr) {
        return;
    }
    if (WindowIsRunning()) {
        lus->window->Close();
    }
    // Drop the bridge's global pointers before the parts they point to go away.
    lus->bridge->ClearCaches();
    *lus = {};
}

} // namespace td
