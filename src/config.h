#pragma once
#include <string>
#include <vector>

#include "engine.h"
#include "i18n.h"

namespace pt {

struct Config {
    Lang lang = Lang::En;
    bool hotkeysEnabled = true;
    bool dragEnabled = true;
    uint32_t dragMods = Mod::Win;
    bool resizeEnabled = true;
    uint32_t resizeMods = Mod::Win;
    std::vector<Binding> bindings;

    // Power plan bookkeeping so "off" can restore what the user had.
    std::string ultimateGuid;
    std::string previousPowerGuid;

    // UWP app names the user wants to keep (unchecked in the removal list).
    std::vector<std::string> keepApps;
    bool deprovision = false;  // also remove from the OS image
};

Config& Cfg();
void LoadConfig();
void SaveConfig();
void PushEngineSettings();  // Cfg() -> engine

bool AutostartEnabled();
bool SetAutostart(bool on);

}  // namespace pt
