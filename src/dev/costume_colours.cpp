// The Costume Colours window (src/dev/costume_colours.h).
//
// How the game colours a costume: each body part's palette marks some of its colours with a key, green for colour 1
// and blue for colour 2, and func_8000B884 (in the decompilation) replaces each marked colour with a shade of the
// colour the Costume Change menu chose, one of 15 rows of 16 shades in the game's colour table (resource 0xA51), or
// leaves it as painted. So a colour is not one piece of the costume: it is every part the costume's artist marked
// with that key, which differs from costume to costume. On PC, func_8000B884 first asks the port for the player's own
// colour (port_costume_ramp, src/game_sdk.c), which the port turns into 16 shades shaped as the game's own; this window
// sets that colour, per wrestler, and has the wrestler's palettes coloured again at once (dev_records_recolour).
//
// The colours are kept in the settings file as gCostumeColour1_BNN and gCostumeColour2_BNN (the wrestler's character
// id in hexadecimal: bank B, entry NN), each 0xRRGGBB; a wrestler without one has no entry.
#include "costume_colours.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiWindow.h"

extern "C" {
#include "dev_records.h"
#include "game_sdk.h"
}

namespace td {

namespace {

// The settings file's name for a wrestler's own colour 1 or 2.
std::string CvarName(unsigned character, int colour) {
    char name[32];

    std::snprintf(name, sizeof(name), "gCostumeColour%d_%03X", colour, character & 0x1FFF);
    return name;
}

ImVec4 ToImGui(uint32_t rgb) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

uint32_t FromImGui(const float c[3]) {
    uint32_t rgb = 0;

    for (int i = 0; i < 3; i++) {
        float v = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];

        rgb = rgb << 8 | static_cast<uint32_t>(v * 255.0f + 0.5f);
    }
    return rgb;
}

class CostumeColoursWindow : public Ship::GuiWindow {
  public:
    CostumeColoursWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars, const std::shared_ptr<Ship::Window>& window)
        : Ship::GuiWindow(cvars, window, "gCostumeColoursShown", false, kCostumeColoursWindow, ImVec2(420, 360),
                          ImGuiWindowFlags_None),
          mCvars(cvars) {
    }

  protected:
    void UpdateElement() override {
    }

    void DrawElement() override {
        bool any = false;

        ImGui::TextWrapped("Colour 1 and colour 2 are the costume's two colours that the game's Costume Change menu "
                           "changes. Which parts each one covers depends on the costume. Your colour replaces the "
                           "menu's choice for that wrestler in every costume, and is shaded as the game's own.");
        ImGui::Separator();
        for (int n = 0; n < DEV_COSTUMES; n++) {
            DevCostume costume;

            if (!dev_records_costume(n, &costume)) {
                continue;
            }
            // The same wrestler twice (a mirror match) shares one setting; show it once.
            bool shown = false;

            for (int m = 0; m < n && !shown; m++) {
                DevCostume earlier;

                shown = dev_records_costume(m, &earlier) && earlier.character == costume.character;
            }
            if (shown) {
                continue;
            }
            any = true;
            DrawWrestler(n, costume);
        }
        if (!any) {
            ImGui::TextDisabled("No wrestler is loaded. Wrestlers appear here in a match or a menu that shows them.");
        }
        // The settings file is written once a change is let go, not on every frame of a drag.
        if (mUnsaved && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            mCvars->Save();
            mUnsaved = false;
        }
    }

  private:
    void DrawWrestler(int n, const DevCostume& costume) {
        ImGui::PushID(costume.character);
        ImGui::SeparatorText(("Player " + std::to_string(n + 1) + ": wrestler " + Hex(costume.character)).c_str());
        for (int colour = 1; colour <= 2; colour++) {
            ImGui::PushID(colour);
            if (!(costume.colours >> colour & 1)) {
                ImGui::TextDisabled("Colour %d: this costume has none", colour);
                ImGui::PopID();
                continue;
            }
            int32_t own = game_sdk_costume_colour(costume.character, colour);
            bool on = own >= 0;

            if (ImGui::Checkbox(("Colour " + std::to_string(colour)).c_str(), &on)) {
                // A new colour starts from the costume's painted one.
                Set(costume.character, colour, on ? static_cast<int32_t>(costume.original[colour]) : -1);
                own = game_sdk_costume_colour(costume.character, colour);
            }
            ImGui::SameLine();
            ImGui::ColorButton("##original", ToImGui(costume.original[colour]), ImGuiColorEditFlags_NoPicker);
            ImGui::SetItemTooltip("As painted: #%06X", costume.original[colour]);
            ImGui::SameLine();
            if (own >= 0) {
                ImVec4 c = ToImGui(static_cast<uint32_t>(own));
                float rgb[3] = { c.x, c.y, c.z };

                if (ImGui::ColorEdit3("##own", rgb,
                                      ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayHex |
                                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                    Set(costume.character, colour, static_cast<int32_t>(FromImGui(rgb)));
                }
                ImGui::SameLine();
                ImGui::TextDisabled("#%06X (click for the wheel)", static_cast<unsigned>(own));
            } else if (costume.level[colour] < 15) {
                ImGui::TextDisabled("the game's colour %d", costume.level[colour] + 1);
            } else {
                ImGui::TextDisabled("as painted");
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    // Sets a wrestler's own colour (-1: none), keeps it in the settings, and colours the wrestler again.
    void Set(unsigned character, int colour, int32_t rgb) {
        game_sdk_set_costume_colour(character, colour, rgb);
        if (rgb < 0) {
            mCvars->ClearVariable(CvarName(character, colour).c_str());
        } else {
            mCvars->SetInteger(CvarName(character, colour).c_str(), rgb);
        }
        mUnsaved = true;
        dev_records_recolour(character);
    }

    static std::string Hex(unsigned character) {
        char text[8];

        std::snprintf(text, sizeof(text), "0x%03X", character);
        return text;
    }

    std::shared_ptr<Ship::ConsoleVariable> mCvars;
    bool mUnsaved = false;
};

} // namespace

void AddCostumeColoursWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                             const std::shared_ptr<Ship::Window>& window) {
    // Every character id the game can have: bank 0 to 31, entry 1 to 31 (func_8000BB40 takes an id apart so).
    for (unsigned bank = 0; bank < 32; bank++) {
        for (unsigned entry = 1; entry < 32; entry++) {
            unsigned character = bank << 8 | entry;

            for (int colour = 1; colour <= 2; colour++) {
                int32_t rgb = cvars->GetInteger(CvarName(character, colour).c_str(), -1);

                if (rgb >= 0) {
                    game_sdk_set_costume_colour(character, colour, rgb);
                }
            }
        }
    }
    window->GetGui()->AddGuiWindow(std::make_shared<CostumeColoursWindow>(cvars, window));
}

} // namespace td
