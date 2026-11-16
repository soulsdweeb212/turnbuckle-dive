// The Costume Colours window (F1, Settings, Costume Colours): a colour wheel for colour 1 and colour 2 of each wrestler
// the game has loaded, in place of the colours the game's Costume Change menu offers. C++ only.
#ifndef TD_COSTUME_COLOURS_H
#define TD_COSTUME_COLOURS_H

#include <memory>

namespace Ship {
class ConsoleVariable;
class Window;
} // namespace Ship

namespace td {

// The window's name, for the Settings menu.
inline constexpr const char* kCostumeColoursWindow = "Costume Colours";

// Gives the game the colours kept in the settings file, and adds the window to the program's GUI, hidden;
// "gCostumeColoursShown" in the settings file remembers whether it is open.
void AddCostumeColoursWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                             const std::shared_ptr<Ship::Window>& window);

} // namespace td

#endif
