// The Sound Test window (F1, Debug, Sound Test): plays any sample of the game's two sound banks on the port's
// synthesizer, at a note, volume, pan and reverb send of one's choosing. It is there to hear the synthesizer while the
// music player is not yet ported. C++ only.
#ifndef TD_SOUND_TEST_H
#define TD_SOUND_TEST_H

#include <memory>

namespace Ship {
class ConsoleVariable;
class Window;
} // namespace Ship

namespace td {

// The window's name, for the Debug menu.
inline constexpr const char* kSoundTestWindow = "Sound Test";

// Adds the window to the program's GUI, hidden; "gSoundTestShown" in the settings file remembers whether it is open.
void AddSoundTestWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                        const std::shared_ptr<Ship::Window>& window);

} // namespace td

#endif
