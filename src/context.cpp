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

#include <SDL3/SDL.h>

#include "fast/Fast3dWindow.h"
#include "frame.h"
#include "libultraship/bridge/UltraBridge.h"
#include "libultraship/controller/controldeck/ControlDeck.h"
#include "libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
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

// The OpenGL that LibUltraShip needs: its shaders are written for GLSL 1.30, which comes with OpenGL 3.0 (its GUI's
// renderer needs 2.0). kGlVersion is glGetString's name for the version, GL_VERSION in OpenGL's headers, which this
// file does not include.
constexpr int kOpenGlMajor = 3;
constexpr unsigned int kGlVersion = 0x1F02;

// Whether LibUltraShip's window really opened, with an OpenGL it can draw with; if not, prints what is missing. When
// SDL cannot create the window, LibUltraShip's SDL backend logs it and carries on without one; when SDL cannot create
// the window's OpenGL context, it carries on without logging anything; and an OpenGL older than 3.0 lets it start too.
// The first frame then crashes. So this asks SDL and OpenGL themselves. Only SDL's OpenGL backend, the one Linux
// uses, is asked: Windows' own backend, DXGI, opens its window without SDL.
bool WindowOpened(const char* program, const std::shared_ptr<Fast::Fast3dWindow>& window) {
    if (window->GetWindowBackend() != Fast::FAST3D_SDL_OPENGL) {
        return true;
    }
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_free(windows);
    if (count == 0) {
        std::fprintf(stderr, "%s: could not open a window (LibUltraShip's log says why)\n", program);
        return false;
    }
    if (SDL_GL_GetCurrentContext() == nullptr) {
        std::fprintf(stderr, "%s: could not start OpenGL in the window\n", program);
        return false;
    }
    using GetString = const unsigned char* (*)(unsigned int);
    auto getString = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    const char* version = getString != nullptr ? reinterpret_cast<const char*>(getString(kGlVersion)) : nullptr;
    if (version == nullptr || std::atoi(version) < kOpenGlMajor) {
        std::fprintf(stderr, "%s: needs OpenGL %d.0 or newer, and this screen offers %s\n", program, kOpenGlMajor,
                     version != nullptr ? version : "none");
        return false;
    }
    return true;
}

// The names of the N64's buttons, as LibUltraShip's own control deck gives them; its input editor shows them.
std::unordered_map<CONTROLLERBUTTONS_T, std::string> ButtonNames() {
    return { { BTN_A, "A" },          { BTN_B, "B" },           { BTN_L, "L" },          { BTN_R, "R" },
             { BTN_Z, "Z" },          { BTN_START, "Start" },   { BTN_CLEFT, "CLeft" },  { BTN_CRIGHT, "CRight" },
             { BTN_CUP, "CUp" },      { BTN_CDOWN, "CDown" },   { BTN_DLEFT, "DLeft" },  { BTN_DRIGHT, "DRight" },
             { BTN_DUP, "DUp" },      { BTN_DDOWN, "DDown" } };
}

// Controller 1's defaults, until the settings file holds mappings of its own (LibUltraShip uses these only then; the
// input editor changes them, and the settings file then keeps the changes).
// - Keyboard: the arrow keys the stick, W A S D the d-pad (up, left, down, right), H A, Y B, U L, O R, Z Z, I J K L
//   the C buttons (up, left, down, right), the space bar Start.
// - Gamepad, by an Xbox controller's names (SDL's south, east, west and north buttons): the right stick the stick;
//   the left stick and the d-pad both the d-pad; A A, X B, B C-down, Y C-up, LB C-left, RB C-right, LT L, RT R,
//   Menu Start, View Z.
// Every table is given: LibUltraShip fills an empty one with its own defaults.
std::shared_ptr<LUS::ControllerDefaultMappings> DefaultMappings() {
    using Ship::KbScancode;
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> keys = {
        { BTN_A, { KbScancode::LUS_KB_H } },          { BTN_B, { KbScancode::LUS_KB_Y } },
        { BTN_L, { KbScancode::LUS_KB_U } },          { BTN_R, { KbScancode::LUS_KB_O } },
        { BTN_Z, { KbScancode::LUS_KB_Z } },          { BTN_START, { KbScancode::LUS_KB_SPACE } },
        { BTN_CUP, { KbScancode::LUS_KB_I } },        { BTN_CLEFT, { KbScancode::LUS_KB_J } },
        { BTN_CDOWN, { KbScancode::LUS_KB_K } },      { BTN_CRIGHT, { KbScancode::LUS_KB_L } },
        { BTN_DUP, { KbScancode::LUS_KB_W } },        { BTN_DLEFT, { KbScancode::LUS_KB_A } },
        { BTN_DDOWN, { KbScancode::LUS_KB_S } },      { BTN_DRIGHT, { KbScancode::LUS_KB_D } },
    };
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, KbScancode>>> keyStick = {
        { Ship::LEFT_STICK,
          { { Ship::UP, KbScancode::LUS_KB_ARROWKEY_UP },
            { Ship::DOWN, KbScancode::LUS_KB_ARROWKEY_DOWN },
            { Ship::LEFT, KbScancode::LUS_KB_ARROWKEY_LEFT },
            { Ship::RIGHT, KbScancode::LUS_KB_ARROWKEY_RIGHT } } },
    };
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>> padButtons = {
        { BTN_A, { SDL_GAMEPAD_BUTTON_SOUTH } },
        { BTN_B, { SDL_GAMEPAD_BUTTON_WEST } },
        { BTN_CDOWN, { SDL_GAMEPAD_BUTTON_EAST } },
        { BTN_CUP, { SDL_GAMEPAD_BUTTON_NORTH } },
        { BTN_CLEFT, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER } },
        { BTN_CRIGHT, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER } },
        { BTN_START, { SDL_GAMEPAD_BUTTON_START } },
        { BTN_Z, { SDL_GAMEPAD_BUTTON_BACK } },
        { BTN_DUP, { SDL_GAMEPAD_BUTTON_DPAD_UP } },
        { BTN_DDOWN, { SDL_GAMEPAD_BUTTON_DPAD_DOWN } },
        { BTN_DLEFT, { SDL_GAMEPAD_BUTTON_DPAD_LEFT } },
        { BTN_DRIGHT, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT } },
    };
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>> padAxisButtons = {
        { BTN_L, { { SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1 } } },
        { BTN_R, { { SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1 } } },
        { BTN_DUP, { { SDL_GAMEPAD_AXIS_LEFTY, -1 } } },
        { BTN_DDOWN, { { SDL_GAMEPAD_AXIS_LEFTY, 1 } } },
        { BTN_DLEFT, { { SDL_GAMEPAD_AXIS_LEFTX, -1 } } },
        { BTN_DRIGHT, { { SDL_GAMEPAD_AXIS_LEFTX, 1 } } },
    };
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
        padStick = {
            { Ship::LEFT_STICK,
              { { Ship::LEFT, { SDL_GAMEPAD_AXIS_RIGHTX, -1 } },
                { Ship::RIGHT, { SDL_GAMEPAD_AXIS_RIGHTX, 1 } },
                { Ship::UP, { SDL_GAMEPAD_AXIS_RIGHTY, -1 } },
                { Ship::DOWN, { SDL_GAMEPAD_AXIS_RIGHTY, 1 } } } },
        };
    return std::make_shared<LUS::ControllerDefaultMappings>(
        keys, keyStick, padButtons,
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GamepadButton>>>(),
        padAxisButtons, padStick);
}

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

// Starting the log, finding a screen to draw on and opening f3d.o2r are the steps a bad setup breaks (a folder the
// program cannot write to, no display or no OpenGL for it, a missing archive). LibUltraShip reports a log or archive
// problem by throwing, and the program checks the screen itself. These steps run before the window is added, because
// destroying a context that holds a window which never started crashes; only whether the window really opened has to
// wait until it has started (WindowOpened).
LibUltraShip StartLibUltraShip(const char* program, bool withCrashHandler, int width, int height) {
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

#ifndef _WIN32
    // On Linux and macOS, SDL opens LibUltraShip's window, and it needs a screen to show it on (on Linux, an X11 or
    // Wayland display). Without one SDL cannot start its video, which LibUltraShip only logs before carrying on without
    // a window. So the program starts SDL's video first. SDL counts the starts, so LibUltraShip's own start of it later
    // only counts it again. This comes before f3d.o2r is opened, after which the resource manager's parts hold on to
    // each other, so that a program that ends here leaves none of them behind.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s: there is no screen to open a window on: %s\n", program, SDL_GetError());
        return {};
    }
#ifndef __APPLE__
    // On Linux the window always draws with OpenGL, which SDL must be able to load for that screen (an X11 display
    // without GLX has none, nor has SDL's dummy video driver). LibUltraShip would only log that too, and then ask
    // OpenGL for its limits without OpenGL, which leaves them unset. SDL counts this load as well. An X server that has
    // GLX but refuses OpenGL contexts makes Xlib end the program here, with Xlib's own message.
    if (!SDL_GL_LoadLibrary(nullptr)) {
        std::fprintf(stderr, "%s: cannot draw with OpenGL on this screen: %s\n", program, SDL_GetError());
        return {};
    }
#endif
#endif

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
    // The window's size, until the settings file remembers one: LibUltraShip saves the size the window had when the
    // program ended, and opens it at that size next time. (Config::Contains cannot tell: for a dotted key whose parts
    // are missing it answers true, so the value is read with a default instead.) It is set here, just before the window is
    // made, because the console variables reload the settings file when they are made, which would undo it.
    if (width > 0 && height > 0 && config->GetInt("Window.Width", 0) <= 0) {
        config->SetInt("Window.Width", width);
        config->SetInt("Window.Height", height);
    }
    auto window = std::make_shared<Fast::Fast3dWindow>(config, cvars, nullptr);
    ctx->GetChildren().Add(window);
    auto deck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>(), DefaultMappings(), ButtonNames(),
                                                   window, cvars);
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
    if (!WindowOpened(program, window)) {
        // The window is taken out of the context before the context is let go, which would otherwise save the size and
        // place of a window that does not exist in the settings file.
        ctx->GetChildren().Remove(window);
        return {};
    }
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
        window->SetResolutionMultiplier(kReadBackResolution);
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
