// The Sound Player window (F1, Debug, Sound Player): every song, sound effect and menu sound of the game, each with a
// play button, played with the game's own sound functions (src/dev/sound_player_game.h), as the game's menus and
// matches play them. C++ only.
#ifndef TD_SOUND_PLAYER_H
#define TD_SOUND_PLAYER_H

#include <memory>

namespace Ship {
class ConsoleVariable;
class Window;
} // namespace Ship

namespace td {

// The window's name, for the Debug menu.
inline constexpr const char* kSoundPlayerWindow = "Sound Player";

// Adds the window to the program's GUI, hidden; "gSoundPlayerShown" in the settings file remembers whether it is open.
// The game must be running (the developer boot), and its sound on.
void AddSoundPlayerWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                          const std::shared_ptr<Ship::Window>& window);

} // namespace td

#endif
