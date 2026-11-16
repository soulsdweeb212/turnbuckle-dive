// The synthesizer's output to the sound device (src/audio/sound_output.h): LibUltraShip's audio player, opened at the
// synthesizer's rate (src/context.cpp), which converts it to the device's own.
//
// At each of the game's frames, the synthesizer makes sub-frames until the player has as much queued as it wants
// (its "desired buffered" amount, about a tenth of a second): the sound runs on the device's clock, as the N64's ran on
// its audio interface's, and the queue stays short, so what the game starts is heard soon after.
#include "sound_output.h"

#include <cstdint>

#include "ship/core/Context.h"
#include "ship/audio/Audio.h"

extern "C" {
#include "sound.h"
}

namespace {

std::shared_ptr<Ship::AudioPlayer> gPlayer;

void Feed() {
    if (gPlayer == nullptr || !gPlayer->IsInitialized()) {
        return;
    }
    Synth* synth = sound_synth();
    int32_t queued = gPlayer->Buffered();
    int32_t wanted = gPlayer->GetDesiredBuffered();
    int16_t block[SYNTH_SUBFRAME * 2];

    // At most a second's worth at once, should the device have stalled.
    for (int n = 0; queued < wanted && n < SYNTH_RATE / SYNTH_SUBFRAME; n++) {
        synth_render(synth, block, SYNTH_SUBFRAME);
        gPlayer->Play(reinterpret_cast<const uint8_t*>(block), sizeof(block));
        queued += SYNTH_SUBFRAME;
    }
}

} // namespace

namespace td {

int OpenSoundOutput(const std::shared_ptr<Ship::Context>& ctx) {
    auto audio = ctx != nullptr ? ctx->GetChildren().GetFirst<Ship::Audio>() : nullptr;

    gPlayer = audio != nullptr ? audio->GetAudioPlayer() : nullptr;
    if (gPlayer == nullptr || !gPlayer->IsInitialized()) {
        gPlayer = nullptr;
        return -1;
    }
    sound_set_output(Feed);
    return 0;
}

} // namespace td

extern "C" void sound_output_close(void) {
    sound_set_output(nullptr);
    gPlayer = nullptr;
}
