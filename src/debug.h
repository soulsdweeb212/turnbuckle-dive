// The debugging tools in a LibUltraShip window, for the viewer and, later, the game: a menu bar that F1 shows and
// hides, whose Debug menu opens LibUltraShip's Console, Stats and Gfx Debugger windows, and the frozen frame that the
// Gfx Debugger's Debug button asks for. C++ only.
#ifndef TD_DEBUG_H
#define TD_DEBUG_H

#include <memory>

#include "libultraship/libultraship.h"

namespace Fast {
class Fast3dWindow;
} // namespace Fast

namespace td {

// Gives the window the menu bar. Call it once, after StartLibUltraShip (src/context.h). The bar starts hidden; F1 shows
// it, and LibUltraShip remembers in the program's settings file whether it was showing.
void AddDebugMenu(const std::shared_ptr<Fast::Fast3dWindow>& window);

// Call it after each frame is drawn, with the frame's display list. When the Gfx Debugger's Debug button was pressed,
// it hands the list to the Gfx Debugger and then keeps drawing that same frame, without returning, until the
// debugger's Resume Game button is pressed or the window closes. Meanwhile the debugger shows the frame's commands as a
// tree, and clicking a command draws the frame only up to it. The caller must leave the list and everything it points
// to unchanged until this returns.
void HoldForGfxDebugger(const std::shared_ptr<Fast::Fast3dWindow>& window, Gfx* frame);

} // namespace td

#endif
