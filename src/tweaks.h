#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "i18n.h"

namespace pt {

enum class Cat { Ads, Shell, Background, Memory, Performance, Customize };

struct RegOp {
    enum Kind { Dword, DwordDeleteOff, String, KeyDefault } kind = Dword;
    HKEY root = HKEY_CURRENT_USER;
    const wchar_t* key = L"";
    const wchar_t* name = L"";
    DWORD on = 0, off = 0;
    const wchar_t* sOn = L"";
    const wchar_t* sOff = L"";
};

struct SvcOp {
    const wchar_t* name;
    DWORD on;   // start type when the tweak is applied (SERVICE_DISABLED = 4)
    DWORD off;  // start type when reverted
};

struct Tweak {
    const char* id = "";
    Cat cat = Cat::Ads;
    Str name{"", ""};
    Str desc{"", ""};
    bool recommended = false;
    bool restartExplorer = false;
    bool reboot = false;
    std::vector<RegOp> regs;
    std::vector<SvcOp> svcs;
    std::vector<const wchar_t*> tasks;  // scheduled tasks disabled when applied
    std::function<bool(bool on, std::string& err)> custom;
    std::function<bool()> customState;  // combined with regs/svcs when present
};

const std::vector<Tweak>& AllTweaks();
const char* CatName(Cat c);
bool IsApplied(const Tweak& t);
bool Apply(const Tweak& t, bool on, std::string& err);

void RestartExplorer();
void BroadcastSettingChange();

}  // namespace pt
