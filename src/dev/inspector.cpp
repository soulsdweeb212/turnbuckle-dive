// The developer match's window (docs/GAME_CODE.md, "The developer match"): the game's own picture, drawn by Fast3D
// from the display lists the game builds, and over it the Match Inspector, which shows the match as data: a table of
// the four wrestlers (state, animation, position, health, spirit, who holds whom) and a map of the ring from above,
// live, 30 frames a second. It can pause the match, run it one frame at a time and slow it down.
//
// How it runs: the developer match calls dev_inspector_frame after each frame the game hands over (port_present), on
// the game's own thread, while the game waits. It draws one window frame from the frame's display list, or several in
// slow motion, and while paused keeps drawing the same list without returning, so the game stays where it is and the
// memory the list points to does not change. The records are read through src/dev/dev_records.c, compiled with the game's settings; the inspector only
// reads them.
//
// Keys, in the window: F5 shows and hides the inspector, F6 pauses and resumes, F7 runs one frame and pauses, F8
// changes the slow motion (1, 2, 4, 8 window frames per game frame). F1 shows the menu bar, whose Debug menu opens this window and LibUltraShip's Console,
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
#include "fast/interpreter.h"
#include "frame.h"
#include "game_sdk.h"
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
std::string gShotPrefix; // --screenshot-every: the files' prefix, the interval, and the next frame to save
unsigned gShotEvery;
unsigned gShotNext;
std::string gCaptureDir; // --capture-match-frames: the folder, and the match frames still to capture
std::vector<unsigned> gCaptureFrames;
std::string gDumpPath; // --dump-frame: the frame, and the file to list its display list in
unsigned gDumpFrame;

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
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
            ToggleVisibility();
        }
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
        ImGui::SameLine();
        ImGui::BeginGroup();
        DrawSwitches();
        ImGui::EndGroup();
        DrawOrder();
    }

  private:
    // The draw groups, as the match uses them (seen by hiding each one at a time), and the switches that hide them.
    // A switch takes effect from the next frame the game hands over: F7 shows it while paused.
    static constexpr struct {
        int group;
        const char* what;
    } kGroups[] = {
        { 0, "arena: crowd, stage, signs" }, { 3, "ring posts and turnbuckles" }, { 4, "wrestlers and ropes" },
        { 5, "HUD" },                        { 6, "ring mat" },
    };

    static void DrawSwitches() {
        ImGui::TextUnformatted("Draw groups");
        uint32_t mask = dev_match_hidden_groups();
        for (const auto& g : kGroups) {
            bool shown = !(mask & (1u << g.group));
            if (ImGui::Checkbox((std::to_string(g.group) + ": " + g.what).c_str(), &shown)) {
                mask = shown ? mask & ~(1u << g.group) : mask | (1u << g.group);
                dev_match_hide_groups(mask);
            }
        }
    }

    // The scene's work blocks in the game's depth order (dev_records_draw_blocks): what decides which object covers
    // which, since the match draws without a depth buffer.
    static void DrawOrder() {
        static DevDrawBlock blocks[512];
        if (!ImGui::CollapsingHeader("Draw order (depth sort)")) {
            return;
        }
        size_t n = dev_records_draw_blocks(blocks, 512);
        n = std::min<size_t>(n, 512);
        uint32_t mask = dev_match_hidden_groups();
        ImGui::Text("%zu work blocks, farthest first", n);
        if (!ImGui::BeginTable("order", 6,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                               ImVec2(0.0f, 240.0f))) {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char* name : { "#", "group", "kind", "depth", "z", "drawn" }) {
            ImGui::TableSetupColumn(name);
        }
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(n));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const DevDrawBlock& b = blocks[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i);
                ImGui::TableNextColumn();
                ImGui::Text("%u", b.group);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(b.mesh ? (b.flat ? "mesh 2D" : "mesh") : (b.flat ? "list 2D" : "list"));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", b.depth);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", b.eye_z);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(b.hidden ? "hidden" : (b.group < 32 && (mask & (1u << b.group))) ? "switched off"
                                                                                                      : "yes");
            }
        }
        ImGui::EndTable();
    }

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
void SaveScreenshot(const std::string& path) {
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

// One window frame: the game's last frame, drawn by Fast3D from the frame's display list (game_sdk_frame), with the
// GUI on top; or the GUI alone when the hand-off drew nothing (RunGuiOnly). EndFrame waits for the next frame at the
// target rate. Textures the game changes in place are decoded again by Fast3D itself (LibUltraShip patch 0009).
void DrawWindowFrame() {
    auto window = gLus.window;
    uint32_t count = 0;
    uint32_t tasks = 0;
    const void* frame = game_sdk_frame(&count, &tasks);

    window->HandleEvents();
    if (!WindowIsRunning()) {
        std::printf("dev-match: the window was closed\n");
        dev_inspector_stop();
        std::exit(0);
    }
    // The steps of LibUltraShip's own frame (Fast3dWindow::DrawAndRunGraphicsCommands), so that a screenshot can read the
    // window after the GUI is drawn and before EndFrame shows it.
    window->GetMouseStateManager()->StartFrame();
    window->GetGui()->StartDraw();
    window->StartFrame();
    if (count != 0 && !gDumpPath.empty() && dev_match_frames() >= gDumpFrame) {
        std::FILE* out = std::fopen(gDumpPath.c_str(), "w");

        if (out != nullptr) {
            uint32_t n = td::DumpDisplayList(static_cast<const Gfx*>(frame), out);
            std::fclose(out);
            std::printf("dev-match: frame %u: %u commands in %u tasks, listed in %s\n", dev_match_frames(), n, tasks,
                        gDumpPath.c_str());
        }
        gDumpPath.clear();
    }
    std::string capture;
    if (count != 0 && !gCaptureFrames.empty()) {
        unsigned m = dev_records_match_frame();
        auto it = std::find(gCaptureFrames.begin(), gCaptureFrames.end(), m);
        if (it != gCaptureFrames.end()) {
            char name[32];
            std::snprintf(name, sizeof(name), "/port_%05u", m);
            capture = gCaptureDir + name;
            gCaptureFrames.erase(it);
            std::FILE* out = std::fopen((capture + ".txt").c_str(), "w");
            if (out != nullptr) {
                td::DumpDisplayList(static_cast<const Gfx*>(frame), out);
                std::fclose(out);
            }
        }
    }
    if (count != 0) {
        window->GetInterpreterWeak().lock()->Run(static_cast<Gfx*>(const_cast<void*>(frame)), {});
    } else {
        window->RunGuiOnly();
    }
    window->GetGui()->EndDraw();
    if (!gShotPath.empty() && dev_match_frames() >= gShotFrame) {
        SaveScreenshot(gShotPath);
        gShotPath.clear();
    }
    if (!capture.empty()) {
        SaveScreenshot(capture + ".png");
    }
    if (gShotEvery != 0 && count != 0 && dev_match_frames() >= gShotNext) {
        char name[32];

        std::snprintf(name, sizeof(name), "-%05u.png", dev_match_frames());
        SaveScreenshot(gShotPrefix + name);
        gShotNext = dev_match_frames() - dev_match_frames() % gShotEvery + gShotEvery;
    }
    window->EndFrame();
    dev_match_alive();
    // The Gfx Debugger's Debug button (F1, Debug, Gfx Debugger) holds this frame until its Resume Game button.
    if (count != 0) {
        td::HoldForGfxDebugger(window, static_cast<Gfx*>(const_cast<void*>(frame)), dev_match_alive);
    }
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
    // The game's picture, 480x240 shown at 4:3 (src/context.h). The N64 runs a match at 30 frames a second; one window
    // frame per game frame keeps that speed.
    td::ShowGamePicture(gLus.window, false);
    gLus.window->SetTargetFps(30);
    auto cvars = gLus.ctx->GetChildren().GetFirst<Ship::ConsoleVariable>();
    gLus.window->GetGui()->AddGuiWindow(std::make_shared<MatchInspector>(
        cvars, gLus.window, "gMatchInspectorShown", false, "Match Inspector", ImVec2(560, 520), ImGuiWindowFlags_None));
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

extern "C" void dev_inspector_screenshots(const char* prefix, unsigned every) {
    gShotPrefix = prefix;
    gShotEvery = every;
    gShotNext = every;
}

extern "C" void dev_inspector_capture(const char* dir, const unsigned* frames, unsigned count) {
    gCaptureDir = dir;
    gCaptureFrames.assign(frames, frames + count);
}

extern "C" void dev_inspector_dump(const char* path, unsigned frame) {
    gDumpPath = path;
    gDumpFrame = frame;
}

extern "C" void dev_inspector_screenshot(const char* path, unsigned frame) {
    gShotPath = path;
    gShotFrame = frame;
}
