// The Sound Player window (src/dev/sound_player.h).
//
// Three tabs list what the game can play: its songs (by song number, with the resource each is kept in), its sound
// effects (by number in the effect table) and the menus' sounds (by the ids the menus use, with the effect each plays).
// Each row has a play button and, while it plays, a stop button. Songs and effects are listed in the order they are
// stored in the ROM, each with its category (kSongs, and for an effect whether the menus play it), and a filter shows
// one category.
//
// A song is started as the game starts one, with the game's func_8000E55C (sp_request_song): it fades out the song
// playing, and once that is quiet a later call starts the new one; so the window asks again at every frame until the
// song has started, for at most kRequestFrames frames. The game's screens ask for their own song at every frame and
// would fade the player's out at once, so while the player has a song the game's own song calls wait
// (sp_take_songs); when the player's song is stopped or ends, the songs go back to the game (sp_give_songs) and the
// screen starts its own again.
#include "sound_player.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

#include <imgui.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiWindow.h"

extern "C" {
#include "audio/sound.h"
#include "sound_player_game.h"
}

namespace td {

namespace {

// How long the window keeps asking for a song: ten seconds of the game's frames (a fade is at most 255 frames).
constexpr int kRequestFrames = 600;

// What each song is, found by running the game through its screens and by reading the code that asks for songs
// (func_800ABE60 for the menus, func_800BCD30 for the scenes around a match, func_800AF978 for the match's song, which
// func_80090030 plays). seen: heard on that screen in a run; otherwise read from the code only.
enum class SongKind { Bgm, MenuBgm, Entrance, Unused, Unclear };

constexpr const char* kKindNames[] = { "BGM", "Menu BGM", "Entrance", "Unused", "Unclear" };

struct SongInfo {
    SongKind kind;
    const char* where;
    bool seen;
};

// By song number (0 is none).
constexpr std::array<SongInfo, SP_SONGS> kSongs = { {
    { SongKind::Unused, "", false },
    { SongKind::Bgm, "Match (the default match setting; Championship match 1)", true },
    { SongKind::Bgm, "Match (a match setting; Championship match 2)", false },
    { SongKind::Bgm, "Match (a match setting; Championship match 3)", false },
    { SongKind::Bgm, "Match (a match setting; Championship match 4)", false },
    { SongKind::Bgm, "Match (a match setting; Championship match 5)", false },
    { SongKind::Bgm, "Match (a match setting; Championship match 6)", false },
    { SongKind::Bgm, "Match (Championship match 7)", false },
    { SongKind::Bgm, "Match (Championship match 8)", false },
    { SongKind::Bgm, "Match (Championship title match)", false },
    { SongKind::Bgm, "Attract: the demo when the title screen sits idle", true },
    { SongKind::Bgm, "Ending credits", false },
    { SongKind::MenuBgm, "Main menu, mode menus, character select", true },
    { SongKind::MenuBgm, "Costume Change", true },
    { SongKind::MenuBgm, "Option", true },
    { SongKind::MenuBgm, "The screen after a match", false },
    { SongKind::Entrance, "The first wrestler's walk-out (and the third in a battle royal)", true },
    { SongKind::Entrance, "The second wrestler's walk-out (and the fourth in a battle royal)", true },
    { SongKind::Unclear, "An after-match scene that shows the opponents", false },
    { SongKind::Bgm, "Winning a title: the ceremony and the screen after it", false },
    { SongKind::MenuBgm, "The screen after a match in Championship", false },
    { SongKind::Unused, "The game never asks for it", false },
} };

// The filter's choices: all, then each kind.
constexpr const char* kFilterNames[] = { "All", "BGM", "Menu BGM", "Entrance", "Unused", "Unclear" };

class SoundPlayerWindow : public Ship::GuiWindow {
  public:
    SoundPlayerWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars, const std::shared_ptr<Ship::Window>& window)
        : Ship::GuiWindow(cvars, window, "gSoundPlayerShown", false, kSoundPlayerWindow, ImVec2(640, 560),
                          ImGuiWindowFlags_None) {
    }

  protected:
    // Every frame, open or not: a song asked for is asked for again until it has started, and the songs go back to
    // the game when the player's has ended.
    void UpdateElement() override {
        if (sound_music() == nullptr) {
            return;
        }
        if (mWanted > 0) {
            if (sp_request_song(mWanted, mFade)) {
                mPlaying = mWanted;
                mWanted = 0;
            } else if (--mRequestLeft <= 0) {
                sp_give_songs(1);
                mWanted = 0;
            }
        } else if (mPlaying > 0 && !sp_held_song_playing()) {
            sp_give_songs(1);
            mPlaying = 0;
        }
    }

    void DrawElement() override {
        if (sound_music() == nullptr) {
            ImGui::TextWrapped("Sound is off in this run. It is on in the developer boot's window (--dev-boot) when the "
                               "run has no pad script and no pad file to replay.");
            return;
        }
        DrawTop();
        if (ImGui::BeginTabBar("##sounds")) {
            if (ImGui::BeginTabItem("Songs")) {
                DrawSongs();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Sound effects")) {
                DrawEffects();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Menu sounds")) {
                DrawMenuSounds();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

  private:
    void DrawTop() {
        int current = sp_current_song();

        if (mPlaying > 0) {
            ImGui::Text("Song %d is playing", mPlaying);
            ImGui::SetItemTooltip("The game's own songs wait until it is stopped or ends");
        } else if (mWanted > 0) {
            ImGui::Text("Starting song %d", mWanted);
        } else if (sp_song_playing() && current > 0) {
            ImGui::Text("The game plays song %d", current);
        } else {
            ImGui::TextDisabled(sp_song_playing() ? "A song is fading out" : "No song is playing");
        }
        ImGui::BeginDisabled(mPlaying == 0 && mWanted == 0);
        if (ImGui::Button("Stop the song")) {
            StopSong();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Stops the song played here; the game then plays its own again");
        ImGui::SameLine();
        if (ImGui::Button("Stop the effects")) {
            sp_stop_effects();
        }
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("Fade", &mFade, 1, 255, "%d frames");
        ImGui::SetItemTooltip("How long the song playing fades out before the next starts (the game's menus use 1 to 16)");
        int volume = sp_music_volume();

        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderInt("Music volume", &volume, 0, 127)) {
            sp_set_music_volume(volume);
        }
        ImGui::SetItemTooltip("The game's own music volume (127 from the boot); it stays as set here");
        ImGui::Separator();
    }

    void PlaySong(int song) {
        sp_take_songs(mFade);
        mWanted = song;
        mPlaying = 0;
        mRequestLeft = kRequestFrames;
    }

    void StopSong() {
        sp_give_songs(mFade);
        mWanted = 0;
        mPlaying = 0;
    }

    void DrawSongs() {
        ImGui::SetNextItemWidth(160);
        ImGui::Combo("Category", &mSongFilter, kFilterNames, IM_ARRAYSIZE(kFilterNames));
        if (!ImGui::BeginTable("##songs", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            return;
        }
        ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Song", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Where it plays");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        // In the order they are stored in the ROM, which is the order of their resources.
        std::array<int, SP_SONGS - 1> order;
        for (int i = 0; i < SP_SONGS - 1; i++) {
            order[i] = i + 1;
        }
        std::stable_sort(order.begin(), order.end(),
                         [](int a, int b) { return sp_song_resource(a) < sp_song_resource(b); });
        for (int song : order) {
            int resource = sp_song_resource(song);
            bool on = song == mPlaying || song == mWanted;
            const SongInfo& info = kSongs[song];

            if (mSongFilter != 0 && static_cast<int>(info.kind) != mSongFilter - 1) {
                continue;
            }

            ImGui::PushID(song);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(resource == 0);
            if (on ? ImGui::SmallButton("Stop") : ImGui::SmallButton("Play")) {
                if (on) {
                    StopSong();
                } else {
                    PlaySong(song);
                }
            }
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (on) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%d", song);
            } else {
                ImGui::Text("%d", song);
            }
            ImGui::TableNextColumn();
            if (resource != 0) {
                ImGui::Text("0x%03X", resource);
            } else {
                ImGui::TextDisabled("none");
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kKindNames[static_cast<int>(info.kind)]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.where);
            ImGui::SetItemTooltip("%s\n(%s)", info.where,
                                  info.seen ? "heard on that screen in a run of the game"
                                            : "read from the game's code; not yet heard on that screen in a run");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void DrawEffects() {
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("Volume", &mEffectVolume, 0, 255);
        ImGui::SetItemTooltip("128 plays the effect as written; a match scales some by distance");
        static const char* kEffectFilters[] = { "All", "Menu sound", "Sound" };

        ImGui::SetNextItemWidth(160);
        ImGui::Combo("Category", &mEffectFilter, kEffectFilters, IM_ARRAYSIZE(kEffectFilters));
        const Music* music = sound_music();

        if (!ImGui::BeginTable("##effects", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            return;
        }
        ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Priority");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        // In the order their streams are stored in the ROM (the effect table points into the ROM's copy of the game's
        // data), which is not quite the order of their numbers; effects the table lacks go last.
        std::array<int, SP_EFFECTS> order;
        for (int i = 0; i < SP_EFFECTS; i++) {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), [music](int a, int b) {
            const uint8_t* pa = music->effect[a];
            const uint8_t* pb = music->effect[b];
            return pa != nullptr && (pb == nullptr || pa < pb);
        });
        for (int number : order) {
            bool exists = music->effect[number] != nullptr;
            bool menu = IsMenuEffect(number);

            if (!exists || (mEffectFilter == 1 && !menu) || (mEffectFilter == 2 && menu)) {
                continue;
            }
            ImGui::PushID(number);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!exists);
            PlayButton(mEffectHandle[number], [&] { return sp_play_effect(number, mEffectVolume); });
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ImGui::Text("%d", number);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(menu ? "Menu sound" : "Sound");
            ImGui::SetItemTooltip(menu ? "The menus play it (Menu sounds tab)" : "The menus do not play it");
            ImGui::TableNextColumn();
            if (exists) {
                ImGui::Text("%d", static_cast<int>(music->effect_priority[number]));
            } else {
                ImGui::TextDisabled("none");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void DrawMenuSounds() {
        if (!ImGui::BeginTable("##menu", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            return;
        }
        ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Menu id", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Effect");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < SP_MENU_COUNT; i++) {
            int id = SP_MENU_FIRST + i;
            int effect = sp_menu_sound_effect(id);
            // func_8000DEE4 plays effects 0 to 0x96 only.
            bool plays = effect >= 0 && effect < 0x97;

            ImGui::PushID(id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!plays);
            PlayButton(mMenuHandle[i], [&] { return sp_play_menu_sound(id); });
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ImGui::Text("0x%03X", id);
            ImGui::TableNextColumn();
            if (plays) {
                ImGui::Text("%d", effect);
            } else {
                ImGui::TextDisabled("none");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Whether one of the menus' sound ids plays effect `number`.
    static bool IsMenuEffect(int number) {
        for (int i = 0; i < SP_MENU_COUNT; i++) {
            if (sp_menu_sound_effect(SP_MENU_FIRST + i) == number) {
                return true;
            }
        }
        return false;
    }

    // Play, or Stop while the sound of `handle` plays.
    template <typename Play> void PlayButton(uint32_t& handle, Play play) {
        if (sp_effect_playing(handle)) {
            if (ImGui::SmallButton("Stop")) {
                sp_stop_effect(handle);
            }
        } else if (ImGui::SmallButton("Play")) {
            handle = play();
        }
    }

    int mWanted = 0;  // the song being started (0: none)
    int mPlaying = 0; // the song the player plays (0: none; the game's own then)
    int mRequestLeft = 0;
    int mFade = 16;
    int mEffectVolume = 128;
    int mSongFilter = 0;
    int mEffectFilter = 0;
    uint32_t mEffectHandle[SP_EFFECTS] = {};
    uint32_t mMenuHandle[SP_MENU_COUNT] = {};
};

} // namespace

void AddSoundPlayerWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                          const std::shared_ptr<Ship::Window>& window) {
    window->GetGui()->AddGuiWindow(std::make_shared<SoundPlayerWindow>(cvars, window));
}

} // namespace td
