/* The synthesizer's output to the sound device (src/audio/sound_output.cpp). C and C++. */
#ifndef TD_SOUND_OUTPUT_H
#define TD_SOUND_OUTPUT_H

#ifdef __cplusplus
#include <memory>

namespace Ship {
class Context;
}

namespace td {
// Hands the synthesizer's output (src/audio/sound.h, which must be started) to the audio player of LibUltraShip's
// context from now on. Returns 0, or -1 when the program has no working sound device (sound is then silent).
int OpenSoundOutput(const std::shared_ptr<Ship::Context>& ctx);
} // namespace td

extern "C" {
#endif

void sound_output_close(void);

#ifdef __cplusplus
}
#endif

#endif
