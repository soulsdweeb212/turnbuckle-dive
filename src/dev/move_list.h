// The Move List window (F1, Debug, Move List): every wrestler's move table as the game stores it, entry by entry, with
// each entry's six 32-bit words, the fields the game unpacks from them (unpackAnimationParameters) and the animations
// it plays for it; and a button that saves the table as JSON beside the program. C++ only.
#ifndef TD_MOVE_LIST_H
#define TD_MOVE_LIST_H

#include <memory>

namespace Ship {
class ConsoleVariable;
class Window;
} // namespace Ship

namespace td {

// The window's name, for the Debug menu.
inline constexpr const char* kMoveListWindow = "Move List";

// Adds the window to the program's GUI, hidden; "gMoveListShown" in the settings file remembers whether it is open.
// It reads the ROM with the game's own resource loader, so the game must be running on the ROM (rom_attach).
void AddMoveListWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                       const std::shared_ptr<Ship::Window>& window);

} // namespace td

#endif
