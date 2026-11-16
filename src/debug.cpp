#include "debug.h"

#include <imgui.h>

#include <fstream>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "fast/Fast3dWindow.h"
#include "fast/debug/GfxDebugger.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiMenuBar.h"
#include "ship/window/gui/GuiWindow.h"

namespace td {

namespace {

// Whether a debugger runs the program (on Linux, /proc/self/status names its tracer; on Windows, IsDebuggerPresent).
bool UnderDebugger() {
#ifdef _WIN32
    return IsDebuggerPresent() != 0;
#else
    std::ifstream status("/proc/self/status");
    std::string line;

    while (std::getline(status, line)) {
        if (line.rfind("TracerPid:", 0) == 0) {
            return std::stoi(line.substr(10)) != 0;
        }
    }
    return false;
#endif
}

// The menu bar. LibUltraShip shows and hides it on F1 (Gui::DrawMenu), and draws it inside its full-window "Main -
// Deck" window, which it opens with a menu bar of its own when this one is showing; so DrawElement fills that window's
// menu bar (ImGui::BeginMenuBar), not a bar of its own. A GuiMenuBar is given its ConsoleVariable and Window here, in
// its constructor: without them it cannot remember whether it is showing.
class DebugMenuBar : public Ship::GuiMenuBar {
  public:
    DebugMenuBar(std::shared_ptr<Ship::ConsoleVariable> cvars, std::shared_ptr<Ship::Window> window,
                 std::vector<std::string> settings, std::vector<std::string> names, std::function<void()> onReset,
                 std::function<void()> drawSettings)
        : Ship::GuiMenuBar(std::move(cvars), window, "gOpenMenuBar", false), mWindow(std::move(window)),
          mSettings(std::move(settings)), mNames(std::move(names)), mOnReset(std::move(onReset)),
          mDrawSettings(std::move(drawSettings)) {
    }

  protected:
    // LibUltraShip calls this every frame, with the bar showing or not. While the bar shows, the mouse cursor stays
    // visible; otherwise LibUltraShip hides it a few seconds after the mouse last moved (MouseStateManager), and when
    // the bar closes that countdown starts again. Under a debugger the cursor always stays visible: LibUltraShip shows
    // a hidden cursor again only in a frame where the mouse moved, and while the debugger holds the program at a
    // breakpoint no frame runs, so a cursor hidden before it stopped would stay hidden over the window.
    void UpdateElement() override {
        bool showing = IsVisible() || mUnderDebugger;

        if (showing == mCursorKept) {
            return;
        }
        mCursorKept = showing;
        mWindow->SetForceCursorVisibility(showing);
        if (showing) {
            mWindow->SetCursorVisibility(true);
        } else {
            mWindow->GetMouseStateManager()->ResetCursorVisibilityTimer();
        }
    }

    // The Settings menu (the program's settings windows, such as the Input Editor, then its own items; left out when
    // there are none), the Debug menu, then Reset and Quit at the bar's right-hand end.
    void DrawElement() override {
        if (!ImGui::BeginMenuBar()) {
            return;
        }
        DrawWindowMenu("Settings", mSettings, mDrawSettings);
        DrawWindowMenu("Debug", mNames, {});
        DrawActions();
        ImGui::EndMenuBar();
    }

  private:
    // A menu of GUI windows by name. Each item shows whether its window is open; choosing it opens or closes the
    // window, and LibUltraShip remembers that in the window's CVar (for its own three: "gConsoleEnabled",
    // "gStatsEnabled", "gGfxDebuggerEnabled").
    // drawItems, when given, draws more items after the windows'.
    void DrawWindowMenu(const char* label, const std::vector<std::string>& names, const std::function<void()>& drawItems) {
        if ((names.empty() && !drawItems) || !ImGui::BeginMenu(label)) {
            return;
        }
        for (const std::string& name : names) {
            auto guiWindow = mWindow->GetGui()->GetGuiWindow(name);

            if (guiWindow != nullptr && ImGui::MenuItem(name.c_str(), nullptr, guiWindow->IsVisible())) {
                guiWindow->ToggleVisibility();
            }
        }
        if (drawItems) {
            if (!names.empty()) {
                ImGui::Separator();
            }
            drawItems();
        }
        ImGui::EndMenu();
    }

    // Reset (when the program gave a way to reset) and Quit, at the right-hand end of the bar. An item in a menu bar
    // takes its text's width and one item spacing (ImGui's MenuItem), so the two start that far from the bar's end.
    // Quit closes the window, as its close button does: the program sees it at its next frame and ends.
    void DrawActions() {
        const ImGuiStyle& style = ImGui::GetStyle();
        float width = ImGui::CalcTextSize("Quit").x + style.ItemSpacing.x;

        if (mOnReset) {
            width += ImGui::CalcTextSize("Reset").x + style.ItemSpacing.x;
        }
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - width - style.WindowPadding.x);
        if (mOnReset && ImGui::MenuItem("Reset")) {
            mOnReset();
        }
        if (ImGui::MenuItem("Quit")) {
            mWindow->Close();
        }
    }

    std::shared_ptr<Ship::Window> mWindow;
    std::vector<std::string> mSettings; // the Settings menu's windows
    std::vector<std::string> mNames;    // the Debug menu's: the program's windows, then LibUltraShip's three
    std::function<void()> mOnReset;  // empty: no Reset item
    std::function<void()> mDrawSettings; // the Settings menu's own items, after its windows
    bool mCursorKept = false;        // the cursor is being kept visible (the bar shows, or a debugger runs us)
    bool mUnderDebugger = UnderDebugger();
};

} // namespace

void AddDebugMenu(const std::shared_ptr<Fast::Fast3dWindow>& window, std::vector<std::string> extraWindows,
                  std::vector<std::string> settingsWindows, std::function<void()> onReset,
                  std::function<void()> drawSettings) {
    auto cvars = window->GetContext()->GetChildren().GetFirst<Ship::ConsoleVariable>();

    for (const char* name : { "Console", "Stats", "Gfx Debugger" }) {
        extraWindows.push_back(name);
    }
    window->GetGui()->SetMenuBar(std::make_shared<DebugMenuBar>(cvars, window, std::move(settingsWindows),
                                                                 std::move(extraWindows), std::move(onReset),
                                                                 std::move(drawSettings)));
}

// The Gfx Debugger window's Debug button only asks for a frame (GfxDebugger::RequestDebugging). The program answers by
// giving it a frame's list (DebugDisplayList), and from then on each run of Fast3D (Interpreter::Run) stops at the
// command chosen in the debugger's tree. So the frame is drawn again and again from the same list while the user looks
// at it. LibUltraShip's C helper functions for this (GfxDebuggerRequestDebugging and the rest) find no debugger in this
// setup, so the window's own GfxDebugger is used.
void HoldForGfxDebugger(const std::shared_ptr<Fast::Fast3dWindow>& window, Gfx* frame,
                        const std::function<void()>& eachFrame) {
    auto debugger = window->GetGfxDebugger();

    if (debugger == nullptr || !debugger->IsDebuggingRequested()) {
        return;
    }
    debugger->DebugDisplayList(reinterpret_cast<Fast::F3DGfx*>(frame));
    while (debugger->IsDebugging() && WindowIsRunning()) {
        window->HandleEvents();
        window->DrawAndRunGraphicsCommands(frame, {});
        if (eachFrame) {
            eachFrame();
        }
    }
}

} // namespace td
