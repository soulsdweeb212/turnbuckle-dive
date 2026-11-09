// Starting and stopping LibUltraShip (LUS) for the port's programs: hello and the viewer use this file, and the game
// will too. LUS is made of parts (the log, the settings, the resource archive, the window, the GUI, the controllers,
// sound and others) that must be created and started in a fixed order; StartLibUltraShip does that once, the same way
// for every program, and says why each step is where it is. C++ only.
#ifndef TD_CONTEXT_H
#define TD_CONTEXT_H

#include <memory>

namespace Ship {
class Context;
} // namespace Ship
namespace Fast {
class Fast3dWindow;
} // namespace Fast
namespace LUS {
class UltraBridge;
} // namespace LUS

namespace td {

// LibUltraShip's parts only hold the context weakly, so whoever starts it must keep the context alive.
struct LibUltraShip {
    std::shared_ptr<Ship::Context> ctx;
    std::shared_ptr<Fast::Fast3dWindow> window;
    std::shared_ptr<LUS::UltraBridge> bridge;
};

// Under WSL, Mesa draws on the CPU (llvmpipe) unless it is told to use the GPU through Direct3D 12. This sets that up
// before the window opens, unless GALLIUM_DRIVER is already set. It does nothing outside Linux.
void PreferWslGpu();

// Starts LibUltraShip for the program named program ("hello", "viewer"). The name goes into the log's file name,
// logs/<program>.log, the settings file, <program>.cfg.json, both in the folder the program starts from, and the
// window's title. withCrashHandler adds LUS's crash handler, which answers a crash with a message box that waits for
// someone to close it: leave it out in checks that run with nobody watching, as on CI. If a step fails, for example
// when there is no screen to open the window on, it prints why and returns a LibUltraShip whose window is null.
LibUltraShip StartLibUltraShip(const char* program, bool withCrashHandler);

// Shows the game's picture, 480x240 pixels, at 4:3 in the window, at 60 frames per second. readBack: Fast3D draws the
// picture at twice the window's resolution, into a framebuffer of its own that a check can read back (ReadPicture in
// src/frame.h); this is not saved in the settings.
void ShowGamePicture(const std::shared_ptr<Fast::Fast3dWindow>& window, bool readBack);

// Closes the window if it is still open and lets LibUltraShip go, in the order its parts need.
void StopLibUltraShip(LibUltraShip* lus);

} // namespace td

#endif
