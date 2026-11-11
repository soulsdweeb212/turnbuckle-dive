#include "debug.h"

#include <imgui.h>

#include "fast/Fast3dWindow.h"
#include "fast/debug/GfxDebugger.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiMenuBar.h"
#include "ship/window/gui/GuiWindow.h"

namespace td {

namespace {

// The menu bar. LibUltraShip shows and hides it on F1 (Gui::DrawMenu), and draws it inside its full-window "Main -
// Deck" window, which it opens with a menu bar of its own when this one is showing; so DrawElement fills that window's
// menu bar (ImGui::BeginMenuBar), not a bar of its own. A GuiMenuBar is given its ConsoleVariable and Window here, in
// its constructor: without them it cannot remember whether it is showing.
class DebugMenuBar : public Ship::GuiMenuBar {
  public:
    DebugMenuBar(std::shared_ptr<Ship::ConsoleVariable> cvars, std::shared_ptr<Ship::Window> window,
                 std::vector<std::string> names)
        : Ship::GuiMenuBar(std::move(cvars), window, "gOpenMenuBar", false), mWindow(std::move(window)),
          mNames(std::move(names)) {
    }

  protected:
    void UpdateElement() override {
    }

    // LibUltraShip makes these three windows when it starts. Each item shows whether its window is open; choosing it
    // opens or closes the window, and LibUltraShip remembers that in the CVar of the window ("gConsoleEnabled",
    // "gStatsEnabled", "gGfxDebuggerEnabled").
    void DrawElement() override {
        if (!ImGui::BeginMenuBar()) {
            return;
        }
        if (ImGui::BeginMenu("Debug")) {
            for (const std::string& name : mNames) {
                auto guiWindow = mWindow->GetGui()->GetGuiWindow(name);

                if (guiWindow != nullptr && ImGui::MenuItem(name.c_str(), nullptr, guiWindow->IsVisible())) {
                    guiWindow->ToggleVisibility();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

  private:
    std::shared_ptr<Ship::Window> mWindow;
    std::vector<std::string> mNames; // the program's windows, then LibUltraShip's three
};

} // namespace

void AddDebugMenu(const std::shared_ptr<Fast::Fast3dWindow>& window, std::vector<std::string> extraWindows) {
    auto cvars = window->GetContext()->GetChildren().GetFirst<Ship::ConsoleVariable>();

    for (const char* name : { "Console", "Stats", "Gfx Debugger" }) {
        extraWindows.push_back(name);
    }
    window->GetGui()->SetMenuBar(std::make_shared<DebugMenuBar>(cvars, window, std::move(extraWindows)));
}

// The Gfx Debugger window's Debug button only asks for a frame (GfxDebugger::RequestDebugging). The program answers by
// giving it a frame's list (DebugDisplayList), and from then on each run of Fast3D (Interpreter::Run) stops at the
// command chosen in the debugger's tree. So the frame is drawn again and again from the same list while the user looks
// at it. LibUltraShip's C helper functions for this (GfxDebuggerRequestDebugging and the rest) find no debugger in this
// setup, so the window's own GfxDebugger is used.
void HoldForGfxDebugger(const std::shared_ptr<Fast::Fast3dWindow>& window, Gfx* frame) {
    auto debugger = window->GetGfxDebugger();

    if (debugger == nullptr || !debugger->IsDebuggingRequested()) {
        return;
    }
    debugger->DebugDisplayList(reinterpret_cast<Fast::F3DGfx*>(frame));
    while (debugger->IsDebugging() && WindowIsRunning()) {
        window->HandleEvents();
        window->DrawAndRunGraphicsCommands(frame, {});
    }
}

} // namespace td
