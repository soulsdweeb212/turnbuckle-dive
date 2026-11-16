// The Sound Test window (src/dev/sound_test.h).
//
// A note's pitch is worked out as the game's music library does (__MusIntSetPitch in the decompilation): the sample is
// played 2 to the power (note + the sample's tuning) / 12 times as fast as the output's rate. The tuning of most samples
// is about -48, so note 48 plays them as recorded; the window starts each sample at the note that does.
#include "sound_test.h"

#include <cmath>

#include <imgui.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiWindow.h"

extern "C" {
#include "audio/sound.h"
}

namespace td {

namespace {

// The voice the window plays on: the last, which the music player will hand out last.
constexpr int kVoice = SYNTH_VOICES - 1;

class SoundTestWindow : public Ship::GuiWindow {
  public:
    SoundTestWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars, const std::shared_ptr<Ship::Window>& window)
        : Ship::GuiWindow(cvars, window, "gSoundTestShown", false, kSoundTestWindow, ImVec2(380, 300),
                          ImGuiWindowFlags_None) {
    }

  protected:
    void UpdateElement() override {
    }

    void DrawElement() override {
        Synth* synth = sound_synth();

        if (synth == nullptr) {
            ImGui::TextDisabled("Sound is off: the sound banks could not be read.");
            return;
        }
        static const char* kBanks[] = { "Music", "Sound effects" };
        bool changed = ImGui::Combo("Bank", &mBank, kBanks, 2);
        const SoundBank* bank = sound_bank(mBank);
        int last = static_cast<int>(bank->count) - 1;

        changed |= ImGui::SliderInt("Sample", &mSample, 0, last);
        if (mSample < 0 || mSample > last) {
            mSample = mSample < 0 ? 0 : last;
        }
        const SoundWave& wave = bank->waves[mSample];

        if (changed) {
            mNote = static_cast<int>(std::lround(-wave.tune));
        }
        ImGui::TextDisabled("%u samples (%.2f s as recorded), %s, tuning %.2f", sound_wave_samples(&wave),
                            sound_wave_samples(&wave) / static_cast<double>(SYNTH_RATE),
                            wave.has_loop ? "loops" : "no loop", wave.tune);
        ImGui::SliderInt("Note", &mNote, 0, 127);
        ImGui::SliderInt("Volume", &mVolume, 0, 127);
        ImGui::SliderInt("Pan", &mPan, 0, 127);
        ImGui::SetItemTooltip("0 left, 64 middle, 127 right");
        ImGui::SliderInt("Reverb", &mReverb, 0, 127);
        ImGui::SetItemTooltip("How much goes to the game's reverb (the big room)");

        float pitch = std::pow(2.0f, (mNote + wave.tune) / 12.0f);

        ImGui::TextDisabled("Pitch %.3f%s", pitch, pitch > SYNTH_MAX_PITCH ? " (too high: played at the limit, 2)" : "");
        if (ImGui::Button("Play")) {
            synth_voice_start(synth, kVoice, &wave, pitch, static_cast<int16_t>(mVolume * 32767 / 127), mPan, mReverb,
                              0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            synth_voice_stop(synth, kVoice);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(synth_voice_playing(synth, kVoice) ? "playing" : "");
        if (synth_voice_playing(synth, kVoice)) {
            // Changes are heard at once while it plays.
            synth_voice_set_pitch(synth, kVoice, pitch);
            synth_voice_set_volume(synth, kVoice, static_cast<int16_t>(mVolume * 32767 / 127), 0);
            synth_voice_set_pan(synth, kVoice, mPan);
            synth_voice_set_fxmix(synth, kVoice, mReverb);
        }
    }

  private:
    int mBank = SOUND_BANK_EFFECTS;
    int mSample = 0;
    int mNote = 48;
    int mVolume = 100;
    int mPan = 64;
    int mReverb = 0;
};

} // namespace

void AddSoundTestWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                        const std::shared_ptr<Ship::Window>& window) {
    window->GetGui()->AddGuiWindow(std::make_shared<SoundTestWindow>(cvars, window));
}

} // namespace td
