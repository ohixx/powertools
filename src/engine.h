#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "i18n.h"

namespace pt {

namespace Mod {
constexpr uint32_t Win = 1, Ctrl = 2, Alt = 4, Shift = 8;
}

enum class Action : int {
    CloseWindow = 0,
    KillProcess,
    Minimize,
    ToggleMaximize,
    ToggleTopmost,
    OpacityUp,
    OpacityDown,
    CenterWindow,
    NextMonitor,
    Launch,
    Count
};

const char* ActionName(Action a);
bool ActionNeedsParam(Action a);

struct Binding {
    bool enabled = true;
    uint32_t mods = Mod::Win;
    uint32_t vk = 0;
    Action action = Action::CloseWindow;
    std::wstring param;  // command line for Launch
};

struct EngineSettings {
    bool enabled = true;
    bool dragEnabled = true;
    uint32_t dragMods = Mod::Win;  // + left mouse button
    bool resizeEnabled = true;
    uint32_t resizeMods = Mod::Win;  // + right mouse button
    std::vector<Binding> bindings;
};

namespace engine {

void Start();
void Stop();
void Apply(const EngineSettings& s);  // thread-safe, may be called any time

// Key capture for the UI: the next non-modifier key press (with its modifiers)
// is swallowed and reported instead of being delivered to apps.
void BeginCapture();
void CancelCapture();
bool IsCapturing();
bool PollCapture(uint32_t& mods, uint32_t& vk);  // true once, when a combo was captured

std::string ComboName(uint32_t mods, uint32_t vk);
std::string ModsName(uint32_t mods);

}  // namespace engine
}  // namespace pt
