// The splash screen (src/splash.cpp): shown in the window before the game boots, a cube with Sting's three painted
// faces turning above the port's name, drawn by LibUltraShip's renderer in the game's 480x240 picture. C++ only.
#ifndef TD_SPLASH_H
#define TD_SPLASH_H

#include <cstdint>
#include <functional>
#include <memory>

namespace Fast {
class Fast3dWindow;
} // namespace Fast

namespace td {

// Shows the splash for about four seconds, or until a button is pressed (pressed() says whether one is down; a button
// already down when the splash starts counts only once it has been let go). faces: the cube's pictures, as
// splash_faces_load makes them (src/splash_faces.h). Returns false if the window was closed.
bool RunSplash(const std::shared_ptr<Fast::Fast3dWindow>& window, const uint8_t* faces,
               const std::function<bool()>& pressed);

} // namespace td

#endif
