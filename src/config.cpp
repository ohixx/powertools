#include "config.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "util.h"

namespace pt {

static Config g_cfg;

Config& Cfg() { return g_cfg; }

static std::wstring ConfigPath() { return DataDir() + L"\\config.ini"; }

static void DefaultBindings(Config& c) {
    c.bindings.clear();
    Binding b;
    b.mods = Mod::Win; b.vk = 'Q'; b.action = Action::CloseWindow; c.bindings.push_back(b);
    b.mods = Mod::Win | Mod::Shift; b.vk = 'Q'; b.action = Action::KillProcess; c.bindings.push_back(b);
    b.mods = Mod::Win | Mod::Alt; b.vk = 'T'; b.action = Action::ToggleTopmost; c.bindings.push_back(b);
    b.mods = Mod::Win | Mod::Alt; b.vk = VK_RETURN; b.action = Action::ToggleMaximize; c.bindings.push_back(b);
}

static std::vector<std::string> Split(const std::string& s, char sep, size_t maxParts) {
    std::vector<std::string> out;
    size_t start = 0;
    while (out.size() + 1 < maxParts) {
        size_t e = s.find(sep, start);
        if (e == std::string::npos) break;
        out.push_back(s.substr(start, e - start));
        start = e + 1;
    }
    out.push_back(s.substr(start));
    return out;
}

static bool B(const std::string& v) { return v == "1" || v == "true"; }

void LoadConfig() {
    Config c;
    c.lang = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN) ? Lang::Ru : Lang::En;
    DefaultBindings(c);

    std::ifstream f(ConfigPath(), std::ios::binary);
    if (f) {
        bool bindingsSeen = false;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '#') continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "lang") c.lang = (v == "ru") ? Lang::Ru : Lang::En;
            else if (k == "hotkeys") c.hotkeysEnabled = B(v);
            else if (k == "drag") c.dragEnabled = B(v);
            else if (k == "drag_mods") c.dragMods = std::stoul(v);
            else if (k == "resize") c.resizeEnabled = B(v);
            else if (k == "resize_mods") c.resizeMods = std::stoul(v);
            else if (k == "ultimate_guid") c.ultimateGuid = v;
            else if (k == "previous_power_guid") c.previousPowerGuid = v;
            else if (k == "deprovision") c.deprovision = B(v);
            else if (k == "keep_app") c.keepApps.push_back(v);
            else if (k == "bind") {
                if (!bindingsSeen) { c.bindings.clear(); bindingsSeen = true; }
                auto p = Split(v, '|', 5);
                if (p.size() == 5) {
                    Binding b;
                    b.enabled = B(p[0]);
                    b.mods = std::stoul(p[1]);
                    b.vk = std::stoul(p[2]);
                    int a = std::stoi(p[3]);
                    if (a < 0 || a >= static_cast<int>(Action::Count)) continue;
                    b.action = static_cast<Action>(a);
                    b.param = Widen(p[4]);
                    c.bindings.push_back(b);
                }
            }
        }
    }
    g_cfg = std::move(c);
    g_lang = g_cfg.lang;
}

void SaveConfig() {
    const Config& c = g_cfg;
    std::ostringstream o;
    o << "# PowerTools configuration\n";
    o << "lang=" << (c.lang == Lang::Ru ? "ru" : "en") << "\n";
    o << "hotkeys=" << c.hotkeysEnabled << "\n";
    o << "drag=" << c.dragEnabled << "\ndrag_mods=" << c.dragMods << "\n";
    o << "resize=" << c.resizeEnabled << "\nresize_mods=" << c.resizeMods << "\n";
    o << "ultimate_guid=" << c.ultimateGuid << "\n";
    o << "previous_power_guid=" << c.previousPowerGuid << "\n";
    o << "deprovision=" << c.deprovision << "\n";
    for (auto& k : c.keepApps) o << "keep_app=" << k << "\n";
    for (auto& b : c.bindings)
        o << "bind=" << b.enabled << "|" << b.mods << "|" << b.vk << "|" << static_cast<int>(b.action) << "|"
          << Narrow(b.param) << "\n";
    std::ofstream f(ConfigPath(), std::ios::binary | std::ios::trunc);
    f << o.str();
}

void PushEngineSettings() {
    EngineSettings s;
    s.enabled = g_cfg.hotkeysEnabled;
    s.dragEnabled = g_cfg.dragEnabled;
    s.dragMods = g_cfg.dragMods;
    s.resizeEnabled = g_cfg.resizeEnabled;
    s.resizeMods = g_cfg.resizeMods;
    s.bindings = g_cfg.bindings;
    engine::Apply(s);
}

// ---- autostart via an elevated logon task (no UAC prompt at login) ----------

static const wchar_t* kTaskName = L"PowerTools";

bool AutostartEnabled() {
    ProcResult r = RunHidden(L"schtasks.exe /Query /TN PowerTools");
    return r.started && r.exitCode == 0;
}

bool SetAutostart(bool on) {
    if (!on) {
        ProcResult r = RunHidden(L"schtasks.exe /Delete /TN PowerTools /F");
        return r.started && r.exitCode == 0;
    }
    std::wstring exe = ExePath();
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"<RegistrationInfo><Description>PowerTools tray autostart</Description></RegistrationInfo>\r\n"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled></LogonTrigger></Triggers>\r\n"
        L"<Principals><Principal id=\"A\"><LogonType>InteractiveToken</LogonType>"
        L"<RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Enabled>true</Enabled></Settings>\r\n"
        L"<Actions Context=\"A\"><Exec><Command>" + exe + L"</Command><Arguments>--tray</Arguments></Exec></Actions>\r\n"
        L"</Task>\r\n";
    std::wstring path = DataDir() + L"\\task.xml";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        const unsigned char bom[2] = {0xFF, 0xFE};
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(xml.data()), static_cast<std::streamsize>(xml.size() * sizeof(wchar_t)));
    }
    ProcResult r = RunHidden(L"schtasks.exe /Create /TN PowerTools /XML \"" + path + L"\" /F");
    DeleteFileW(path.c_str());
    (void)kTaskName;
    return r.started && r.exitCode == 0;
}

}  // namespace pt
