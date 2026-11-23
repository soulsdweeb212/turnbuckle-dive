// The Player Console window (src/dev/player_console.h).
#include "player_console.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <iterator>
#include <string>
#include <vector>

#include <imgui.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiWindow.h"

#include "dev_records.h"

namespace td {

namespace {

constexpr size_t kMaxLines = 5000;

struct Line {
    int kind; // DEV_EVENT_*
    int slot;
    std::string text;
};

std::deque<Line> gLines;
DevPlayer gPlayers[DEV_WRESTLERS]; // the last read, for the names in the log
bool gScrollToEnd = true;

const char* ZoneName(int zone) {
    switch (zone) {
        case DEV_ZONE_RING:
            return "in the ring";
        case DEV_ZONE_APRON:
            return "on the apron";
        case DEV_ZONE_FLOOR:
            return "on the floor";
        case DEV_ZONE_RING | DEV_ZONE_APRON:
            return "between ring and apron";
        case DEV_ZONE_APRON | DEV_ZONE_FLOOR:
            return "between apron and floor";
        case 0:
            return "no zone";
        default:
            return "several zones";
    }
}

const char* ZoneShort(int zone) {
    switch (zone) {
        case DEV_ZONE_RING:
            return "RING";
        case DEV_ZONE_APRON:
            return "apron";
        case DEV_ZONE_FLOOR:
            return "floor";
        case DEV_ZONE_RING | DEV_ZONE_APRON:
            return "ring+apron";
        case DEV_ZONE_APRON | DEV_ZONE_FLOOR:
            return "apron+floor";
        default:
            return "-";
    }
}

const char* SideName(int side) {
    static const char* const kSides[] = { "+Z", "+X", "-Z", "-X" };
    return side >= 0 && side < 4 ? kSides[side] : "?";
}

// "P1 Sting (0x10C)": the slot, the game's short name and the character id.
std::string Who(int slot, int character) {
    char buf[64];
    const char* name = gPlayers[slot].character == character ? gPlayers[slot].short_name : "";

    std::snprintf(buf, sizeof(buf), "P%d %s (0x%03X)", slot + 1, name, static_cast<unsigned>(character) & 0xFFFF);
    return buf;
}

void AddLine(int kind, int slot, std::string text) {
    gLines.push_back({ kind, slot, std::move(text) });
    while (gLines.size() > kMaxLines) {
        gLines.pop_front();
    }
}

void AddEvent(const DevEvent& e) {
    char buf[256];
    std::string who = Who(e.slot, e.character);

    switch (e.kind) {
        case DEV_EVENT_ANIM:
            std::snprintf(buf, sizeof(buf), "[%6u] %-24s anim 0x%04X <- 0x%04X  mode %d  at (%.1f, %.1f, %.1f)",
                          e.match_frame, who.c_str(), static_cast<unsigned>(e.value) & 0xFFFF,
                          static_cast<unsigned>(e.previous) & 0xFFFF, e.mode, e.x, e.y, e.z);
            break;
        case DEV_EVENT_ANIM_DIRECT:
            std::snprintf(buf, sizeof(buf),
                          "[%6u] %-24s anim 0x%04X <- 0x%04X  set directly  at (%.1f, %.1f, %.1f)", e.match_frame,
                          who.c_str(), static_cast<unsigned>(e.value) & 0xFFFF,
                          static_cast<unsigned>(e.previous) & 0xFFFF, e.x, e.y, e.z);
            break;
        default:
            std::snprintf(buf, sizeof(buf), "[%6u] %-24s now %s (was %s)  at (%.1f, %.1f, %.1f)", e.match_frame,
                          who.c_str(), ZoneName(e.value), ZoneName(e.previous), e.x, e.y, e.z);
            break;
    }
    AddLine(e.kind, e.slot, buf);
}

void TakeEvents() {
    DevEvent events[256];
    uint32_t lost = 0;
    size_t n;

    bool named = false;

    while ((n = dev_players_take(events, 256, &lost)) > 0 || lost > 0) {
        // The lines name each player with the game's name: read the players once, when there is something to log.
        if (!named) {
            DevPlayer players[DEV_WRESTLERS];
            uint32_t frame;

            if (dev_players_read(players, &frame) != 0) {
                std::copy(std::begin(players), std::end(players), std::begin(gPlayers));
            }
            named = true;
        }
        if (lost > 0) {
            AddLine(-1, -1, "(" + std::to_string(lost) + " events lost: more came in one frame than the log holds)");
        }
        for (size_t i = 0; i < n; i++) {
            AddEvent(events[i]);
        }
        if (n < 256) {
            break;
        }
    }
}

std::string AllLines(const bool (&kinds)[3], const bool (&slots)[DEV_WRESTLERS]) {
    std::string all;

    for (const Line& l : gLines) {
        if ((l.kind >= 0 && !kinds[l.kind]) || (l.slot >= 0 && !slots[l.slot])) {
            continue;
        }
        all += l.text;
        all += '\n';
    }
    return all;
}

class PlayerConsoleWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;

  protected:
    void UpdateElement() override {
    }

    void DrawElement() override {
        uint32_t frame = 0;
        bool match = dev_players_read(gPlayers, &frame) != 0;

        if (!match) {
            ImGui::TextWrapped("No match is running: the players appear once a match starts.");
        } else {
            ImGui::Text("match frame %u", frame);
            DrawPlayers();
        }
        ImGui::Separator();
        DrawLog();
    }

  private:
    bool mKinds[3] = { true, true, true }; // DEV_EVENT_ANIM, _ANIM_DIRECT, _ZONE
    bool mSlots[DEV_WRESTLERS] = { true, true, true, true };
    std::string mStatus;
    std::vector<size_t> mShown; // the lines that pass the filters, by index in gLines

    static void DrawPlayers() {
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("players", 9, flags)) {
            for (const char* name : { "P", "name", "game id", "bank/entry", "costume", "where", "x, y, z", "side",
                                      "anim  frame  state" }) {
                ImGui::TableSetupColumn(name);
            }
            ImGui::TableHeadersRow();
            for (int i = 0; i < DEV_WRESTLERS; i++) {
                const DevPlayer& p = gPlayers[i];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i + 1);
                if (p.character == -1) {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("empty");
                    continue;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.name);
                ImGui::TableNextColumn();
                ImGui::Text("0x%03X", static_cast<unsigned>(p.character));
                ImGui::TableNextColumn();
                ImGui::Text("%u / %u", p.bank, p.entry);
                ImGui::TableNextColumn();
                if (p.costume >= 0) {
                    ImGui::Text("%d", p.costume);
                } else {
                    ImGui::TextDisabled("?");
                }
                ImGui::TableNextColumn();
                if (p.zone == DEV_ZONE_RING) {
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", ZoneShort(p.zone));
                } else {
                    ImGui::TextUnformatted(ZoneShort(p.zone));
                }
                ImGui::TableNextColumn();
                ImGui::Text("%7.1f %5.1f %7.1f", p.x, p.y, p.z);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(SideName(p.ring_side));
                ImGui::TableNextColumn();
                ImGui::Text("0x%04X  %3d/%-3d  %02X %s", static_cast<unsigned>(p.anim) & 0xFFFF, p.anim_frame,
                            p.anim_frames, p.state & 0xFF, dev_state_name(p.state));
            }
            ImGui::EndTable();
        }
        if (ImGui::TreeNode("Memory")) {
            for (int i = 0; i < DEV_WRESTLERS; i++) {
                const DevPlayer& p = gPlayers[i];

                ImGui::Text("P%d  record N64 0x%08X  PC %p   characterId at +0x%03X: N64 0x%08X = 0x%04X   "
                            "D_8006A2A4[%d] at N64 0x%08X = 0x%04X",
                            i + 1, p.n64_record, p.pc_record, p.character_offset, p.n64_character,
                            static_cast<unsigned>(p.character) & 0xFFFF, i, p.n64_entity_character,
                            p.entity_character);
            }
            ImGui::TreePop();
        }
    }

    void DrawLog() {
        ImGui::Checkbox("Animations (func_800D427C)", &mKinds[DEV_EVENT_ANIM]);
        ImGui::SameLine();
        ImGui::Checkbox("Set directly", &mKinds[DEV_EVENT_ANIM_DIRECT]);
        ImGui::SameLine();
        ImGui::Checkbox("Zone", &mKinds[DEV_EVENT_ZONE]);
        for (int i = 0; i < DEV_WRESTLERS; i++) {
            char label[8];

            std::snprintf(label, sizeof(label), "P%d", i + 1);
            ImGui::SameLine();
            ImGui::Checkbox(label, &mSlots[i]);
        }
        if (ImGui::Button("Clear")) {
            gLines.clear();
            mStatus.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            ImGui::SetClipboardText(AllLines(mKinds, mSlots).c_str());
            mStatus = "Copied";
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            std::string path = Ship::Context::GetPathRelativeToAppDirectory("player_console.txt");
            FILE* f = std::fopen(path.c_str(), "w");

            if (f != nullptr) {
                std::string all = AllLines(mKinds, mSlots);
                std::fwrite(all.data(), 1, all.size(), f);
                std::fclose(f);
                mStatus = "Saved " + path;
            } else {
                mStatus = "Could not write " + path;
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("Follow", &gScrollToEnd);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu lines  %s", gLines.size(), mStatus.c_str());

        ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        // Only the lines in view are drawn (ImGuiListClipper), so a full log costs little.
        mShown.clear();
        for (size_t i = 0; i < gLines.size(); i++) {
            const Line& l = gLines[i];
            if ((l.kind < 0 || mKinds[l.kind]) && (l.slot < 0 || mSlots[l.slot])) {
                mShown.push_back(i);
            }
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(mShown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const Line& l = gLines[mShown[row]];
                if (l.kind == DEV_EVENT_ZONE) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", l.text.c_str());
                } else if (l.kind == DEV_EVENT_ANIM_DIRECT) {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", l.text.c_str());
                } else {
                    ImGui::TextUnformatted(l.text.c_str());
                }
            }
        }
        if (gScrollToEnd && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
};

} // namespace

void PlayerConsoleGameFrame() {
    dev_players_frame();
    TakeEvents();
}

void AddPlayerConsoleWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                            const std::shared_ptr<Ship::Window>& window) {
    dev_players_listen();
    window->GetGui()->AddGuiWindow(std::make_shared<PlayerConsoleWindow>(cvars, window, "gPlayerConsoleShown", false,
                                                                         kPlayerConsoleWindow, ImVec2(760, 520),
                                                                         ImGuiWindowFlags_None));
}

} // namespace td
