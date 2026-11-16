// The debugging tools in a LibUltraShip window, for the viewer and the game: a menu bar that F1 shows and hides, with a
// Settings menu (the game's Input Editor), a Debug menu that opens LibUltraShip's Console, Stats and Gfx Debugger
// windows, and Reset and Quit at its right-hand end; and the frozen frame that the Gfx Debugger's Debug button asks
// for. C++ only.
#ifndef TD_DEBUG_H
#define TD_DEBUG_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "libultraship/libultraship.h"

namespace Fast {
class Fast3dWindow;
} // namespace Fast

namespace td {

// Gives the window the menu bar. Call it once, after StartLibUltraShip (src/context.h). The bar starts hidden; F1 shows
// it, and LibUltraShip remembers in the program's settings file whether it was showing. extraWindows: the names of the
// program's own GUI windows to list first in the Debug menu (the developer match's "Match Inspector").
// settingsWindows: those of the Settings menu ("Input Editor"); drawSettings, when given, draws the menu's own items
// after them (ImGui calls, such as the game's settings); with neither, the bar has no Settings menu. At the bar's
// right-hand end, Quit closes the window; Reset, shown only when onReset is given, calls it.
void AddDebugMenu(const std::shared_ptr<Fast::Fast3dWindow>& window, std::vector<std::string> extraWindows = {},
                  std::vector<std::string> settingsWindows = {}, std::function<void()> onReset = {},
                  std::function<void()> drawSettings = {});

// Call it after each frame is drawn, with the frame's display list. When the Gfx Debugger's Debug button was pressed,
// it hands the list to the Gfx Debugger and then keeps drawing that same frame, without returning, until the
// debugger's Resume Game button is pressed or the window closes. Meanwhile the debugger shows the frame's commands as a
// tree, and clicking a command draws the frame only up to it. The caller must leave the list and everything it points
// to unchanged until this returns. eachFrame, when given, is called after each frame drawn meanwhile (the developer
// match tells its watchdog that the program is still running).
void HoldForGfxDebugger(const std::shared_ptr<Fast::Fast3dWindow>& window, Gfx* frame,
                        const std::function<void()>& eachFrame = {});

} // namespace td

#endif
