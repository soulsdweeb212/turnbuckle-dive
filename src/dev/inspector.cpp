// The Match Inspector: the developer match's window (docs/GAME_CODE.md, "The developer match"). The game draws no
// picture yet, so the window shows the match as data: a table of the four wrestlers (state, animation, position,
// health, spirit, who holds whom) and a map of the ring from above, live, 30 frames a second. It can pause the match,
// run it one frame at a time and slow it down.
//
// How it runs: the developer match calls dev_inspector_frame after each frame the game hands over (port_present), on
// the game's own thread, while the game waits. It draws one window frame with LibUltraShip's GUI only (no game
// picture: RunGuiOnly), or several in slow motion, and while paused keeps drawing without returning, so the game stays
// where it is. The records are read through src/dev/dev_records.c, compiled with the game's settings; the inspector only
// reads them.
//
// Keys, in the window: F6 pauses and resumes, F7 runs one frame and pauses, F8 changes the slow motion (1, 2, 4, 8
// window frames per game frame). F1 shows the menu bar, whose Debug menu opens this window and LibUltraShip's Console,
// Stats and Gfx Debugger. The console also takes the commands pause, step [frames] and slowmo [factor].
#include "inspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <GL/gl.h>
#include <imgui.h>

#include "context.h"
#include "debug.h"
#include "dev_match.h"
#include "dev_records.h"
#include "fast/Fast3dWindow.h"
#include "frame.h"
#include "libultraship/libultraship.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/debug/Console.h"
#include "ship/window/gui/GameOverlay.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiWindow.h"

namespace {

// What the inspector asks of the match. Only the game's thread touches it: the window runs inside the game's hand-off.
struct Control {
    bool paused = false;
    int step = 0; // game frames still to run before pausing again
    int slow = 1; // window frames per game frame
};

Control gControl;
td::LibUltraShip gLus;
std::string gShotPath; // --screenshot: the file, and the game frame to take it at
unsigned gShotFrame;

// The colours of the four wrestlers, in the table's headers and on the ring map.
const ImVec4 kSlotColour[DEV_WRESTLERS] = { { 1.0f, 0.85f, 0.2f, 1.0f },
                                            { 0.4f, 0.75f, 1.0f, 1.0f },
                                            { 0.6f, 1.0f, 0.5f, 1.0f },
                                            { 1.0f, 0.5f, 0.6f, 1.0f } };

void TogglePause() {
    gControl.paused = !gControl.paused;
    gControl.step = 0;
}

void Step(int frames) {
    gControl.paused = true;
    gControl.step = frames > 0 ? frames : 1;
}

void CycleSlowMotion() {
    gControl.slow = gControl.slow >= 8 ? 1 : gControl.slow * 2;
}

class MatchInspector : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;

  protected:
    // LibUltraShip calls this for every registered window at every window frame, shown or not, so the keys work with
    // the inspector closed too.
    void UpdateElement() override {
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) {
            TogglePause();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) {
            Step(1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) {
            CycleSlowMotion();
        }
        if (gControl.paused) {
            // TextDraw draws only for the frame it is called in (a notification would fade), so it is called at each.
            gLus.window->GetGui()->GetGameOverlay()->TextDraw(20.0f, 20.0f, true, ImVec4(1, 1, 1, 1), "PAUSED");
        }
    }

    void DrawElement() override {
        DevWrestler w[DEV_WRESTLERS];
        DevMatch m;

        dev_records_read(w, &m);
        ImGui::Text("frame %u   match frame %u   phase %d (%s)   seed %08X", dev_match_frames(), m.match_frame,
                    static_cast<int>(m.phase), dev_phase_name(m.phase), m.seed);
        if (ImGui::Button(gControl.paused ? "Resume (F6)" : "Pause (F6)")) {
            TogglePause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Step (F7)")) {
            Step(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Slow motion (F8)")) {
            CycleSlowMotion();
        }
        ImGui::SameLine();
        ImGui::Text("x%d", gControl.slow);

        DrawTable(w);
        DrawRing(w);
    }

  private:
    template <typename Cell> static void Row(const char* name, const DevWrestler (&w)[DEV_WRESTLERS], Cell cell) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(name);
        for (int slot = 0; slot < DEV_WRESTLERS; slot++) {
            ImGui::TableNextColumn();
            if (w[slot].character != -1) {
                cell(w[slot]);
            }
        }
    }

    static void DrawTable(const DevWrestler (&w)[DEV_WRESTLERS]) {
        if (!ImGui::BeginTable("wrestlers", 1 + DEV_WRESTLERS, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            return;
        }
        ImGui::TableSetupColumn("");
        for (int slot = 0; slot < DEV_WRESTLERS; slot++) {
            ImGui::TableSetupColumn(("wrestler " + std::to_string(slot)).c_str());
        }
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableNextColumn();
        for (int slot = 0; slot < DEV_WRESTLERS; slot++) {
            ImGui::TableNextColumn();
            ImGui::TextColored(kSlotColour[slot], "wrestler %d", slot);
        }
        Row("character", w, [](const DevWrestler& v) { ImGui::Text("0x%03X", static_cast<unsigned>(v.character)); });
        Row("state", w, [](const DevWrestler& v) { ImGui::Text("%02X %s", v.state, dev_state_name(v.state)); });
        Row("step", w, [](const DevWrestler& v) { ImGui::Text("%d", v.step); });
        Row("animation", w, [](const DevWrestler& v) { ImGui::Text("%04X frame %d", v.anim & 0xFFFF, v.frame); });
        Row("x y z", w, [](const DevWrestler& v) { ImGui::Text("%.0f %.0f %.0f", v.x, v.y, v.z); });
        Row("heading", w, [](const DevWrestler& v) { ImGui::Text("%.0f deg", v.heading * 360.0f / 4096.0f); });
        Row("health", w, [](const DevWrestler& v) { ImGui::Text("%d / %d", v.health, v.health_max); });
        Row("spirit", w, [](const DevWrestler& v) { ImGui::Text("%d", v.spirit); });
        Row("held by", w, [](const DevWrestler& v) {
            if (v.held_by < 0) {
                ImGui::TextUnformatted("-");
            } else {
                ImGui::Text("%d", v.held_by);
            }
        });
        Row("opponent", w, [](const DevWrestler& v) { ImGui::Text("%d", v.opponent); });
        ImGui::EndTable();
    }

    // The ring from above: x to the right, z up. The ring's inside is -200..200 on both; the map shows -400..400, so the
    // apron and the floor around the ring are in it too. Each wrestler is a dot with a line where it faces (heading 0
    // faces +z, 0x400 faces +x).
    static void DrawRing(const DevWrestler (&w)[DEV_WRESTLERS]) {
        const float size = 260.0f;
        const float range = 400.0f;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        auto at = [&](float x, float z) {
            return ImVec2(origin.x + (x + range) / (2 * range) * size, origin.y + (range - z) / (2 * range) * size);
        };

        ImGui::Dummy(ImVec2(size, size));
        draw->AddRectFilled(at(-range, range), at(range, -range), IM_COL32(30, 30, 40, 255));
        draw->AddRectFilled(at(-200, 200), at(200, -200), IM_COL32(70, 70, 90, 255));
        draw->AddRect(at(-200, 200), at(200, -200), IM_COL32(220, 220, 220, 255));
        for (int slot = 0; slot < DEV_WRESTLERS; slot++) {
            const DevWrestler& v = w[slot];
            if (v.character == -1 || !std::isfinite(v.x) || !std::isfinite(v.z)) {
                continue;
            }
            float angle = v.heading * 6.2831853f / 4096.0f;
            ImU32 colour = ImGui::ColorConvertFloat4ToU32(kSlotColour[slot]);
            ImVec2 p = at(v.x, v.z);
            draw->AddLine(p, at(v.x + std::sin(angle) * 40.0f, v.z + std::cos(angle) * 40.0f), colour, 2.0f);
            draw->AddCircleFilled(p, 5.0f, colour);
        }
    }
};

// Reads the window's picture, just drawn and not shown yet, and saves it (--screenshot). OpenGL keeps the bottom row
// first, so the rows are turned over.
void SaveScreenshot() {
    std::string path = gShotPath;

    gShotPath.clear();
    if (gLus.window->GetWindowBackendName() != "OpenGL") {
        std::printf("dev-match: --screenshot needs the OpenGL backend, not %s\n",
                    gLus.window->GetWindowBackendName().c_str());
        return;
    }
    uint32_t w = gLus.window->GetWidth();
    uint32_t h = gLus.window->GetHeight();
    std::vector<uint8_t> bottomUp(static_cast<size_t>(w) * h * 3);
    std::vector<uint8_t> rgb(bottomUp.size());

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGB, GL_UNSIGNED_BYTE, bottomUp.data());
    for (uint32_t y = 0; y < h; y++) {
        std::copy_n(bottomUp.data() + static_cast<size_t>(h - 1 - y) * w * 3, w * 3, rgb.data() + static_cast<size_t>(y) * w * 3);
    }
    if (td::SaveRgbPng(rgb, w, h, path.c_str())) {
        std::printf("dev-match: saved the window at frame %u to %s\n", dev_match_frames(), path.c_str());
    }
}

// One window frame with LibUltraShip's GUI only, the way its own frame (DrawAndRunGraphicsCommands) is put together,
// but with RunGuiOnly in place of the game's display list. EndFrame waits for the next frame at the target rate.
void DrawWindowFrame() {
    auto window = gLus.window;

    window->HandleEvents();
    if (!WindowIsRunning()) {
        std::printf("dev-match: the window was closed\n");
        dev_inspector_stop();
        std::exit(0);
    }
    window->GetMouseStateManager()->StartFrame();
    window->GetGui()->StartDraw();
    window->StartFrame();
    window->RunGuiOnly();
    window->GetGui()->EndDraw();
    if (!gShotPath.empty() && dev_match_frames() >= gShotFrame) {
        SaveScreenshot();
    }
    window->EndFrame();
    dev_match_alive();
}

void AddConsoleCommands() {
    auto console = gLus.ctx->GetChildren().GetFirst<Ship::Console>();
    if (console == nullptr) {
        return;
    }
    console->AddCommand("pause", { [](std::shared_ptr<Ship::Console>, std::vector<std::string>, std::string*) {
                                      TogglePause();
                                      return 0;
                                  },
                                   "Pause or resume the match",
                                   {} });
    console->AddCommand("step", { [](std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string*) {
                                     Step(args.size() > 1 ? std::atoi(args[1].c_str()) : 1);
                                     return 0;
                                 },
                                  "Run some frames of the match, then pause",
                                  { { "frames", Ship::ArgumentType::NUMBER, true } } });
    console->AddCommand("slowmo", { [](std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string*) {
                                       int factor = args.size() > 1 ? std::atoi(args[1].c_str()) : 1;
                                       gControl.slow = factor < 1 ? 1 : factor > 16 ? 16 : factor;
                                       return 0;
                                   },
                                    "Show each frame of the match this many times (1 for normal speed)",
                                    { { "factor", Ship::ArgumentType::NUMBER, true } } });
}

} // namespace

extern "C" int dev_inspector_start(void) {
    td::PreferWslGpu();
    // A window big enough for the inspector, which LibUltraShip would otherwise move out into a window of its own.
    gLus = td::StartLibUltraShip("turnbuckle-dive", true, 1024, 720);
    if (gLus.window == nullptr) {
        return -1;
    }
    // The N64 runs a match at 30 frames a second; one window frame per game frame keeps that speed.
    gLus.window->SetTargetFps(30);
    auto cvars = gLus.ctx->GetChildren().GetFirst<Ship::ConsoleVariable>();
    gLus.window->GetGui()->AddGuiWindow(std::make_shared<MatchInspector>(
        cvars, gLus.window, "gMatchInspectorOpen", true, "Match Inspector", ImVec2(720, 560), ImGuiWindowFlags_None));
    td::AddDebugMenu(gLus.window, { "Match Inspector" });
    AddConsoleCommands();
    return 0;
}

extern "C" void dev_inspector_frame(void* user) {
    (void)user;
    if (gLus.window == nullptr) {
        return;
    }
    for (int i = 0; i < gControl.slow; i++) {
        DrawWindowFrame();
    }
    while (gControl.paused && gControl.step == 0) {
        DrawWindowFrame();
    }
    if (gControl.step > 0) {
        gControl.step--;
    }
}

extern "C" void dev_inspector_stop(void) {
    td::StopLibUltraShip(&gLus);
}

extern "C" void dev_inspector_screenshot(const char* path, unsigned frame) {
    gShotPath = path;
    gShotFrame = frame;
}
