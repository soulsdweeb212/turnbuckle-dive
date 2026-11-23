// The Player Console (F1, Debug, Player Console): each player of the match as the game holds it, live, and a log of
// what changes. C++ only.
//
// The top table has one row per player: the game's name for the character (its own buildWrestlerNameRecord), the
// character id as the game stores it (0xBNN) and its bank and entry, the costume, whether the player is in the ring, on
// the apron or on the floor (the game's zone flags), the position, the side of the ring, the animation and its frame,
// and the action state. "Memory" shows where those ids sit: the wrestler record's N64 and PC addresses, the character
// id's offset and address in it, and the slot's entry of the game's character table D_8006A2A4.
//
// The log adds a line each time a player starts an animation, as the game's own func_800D427C does it (with the
// animation it replaces and the caller's mode), when the game changes an animation id without that function (found
// at the end of the frame), and when a player changes zone. Everything is read from the game's records and functions,
// in the game's frame hand-off (src/dev/dev_records.h). The log keeps its last 5000 lines; Copy and Save take them.
#ifndef TD_PLAYER_CONSOLE_H
#define TD_PLAYER_CONSOLE_H

#include <memory>

namespace Ship {
class ConsoleVariable;
class Window;
} // namespace Ship

namespace td {

// The window's name, for the Debug menu.
inline constexpr const char* kPlayerConsoleWindow = "Player Console";

// Adds the window to the program's GUI, hidden ("gPlayerConsoleShown" in the settings file remembers whether it is
// open), and starts listening to the game's animations.
void AddPlayerConsoleWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                            const std::shared_ptr<Ship::Window>& window);

// Call it once per game frame, in the hand-off: notes the changes that did not pass through the game's
// func_800D427C, and moves the frame's events into the log.
void PlayerConsoleGameFrame();

} // namespace td

#endif
