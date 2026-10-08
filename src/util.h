#pragma once
#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pt {

// ---- strings / paths -------------------------------------------------------
std::wstring Widen(std::string_view s);
std::string Narrow(std::wstring_view s);
std::vector<std::string> SplitLines(const std::string& s);
std::string Trim(std::string s);

bool IsElevated();
std::wstring DataDir();  // %APPDATA%\PowerTools (created on demand)
std::wstring ExePath();
size_t WorkingSetBytes();
void TrimWorkingSet();
// Empties the working set of every process we may touch; returns MB of RAM made available.
double FreeRamNow();

// ---- processes -------------------------------------------------------------
struct ProcResult {
    DWORD exitCode = static_cast<DWORD>(-1);
    std::string output;
    bool started = false;
};
using LineCallback = std::function<void(const std::string&)>;

// Runs a command line without a window, capturing stdout+stderr line by line.
ProcResult RunHidden(const std::wstring& cmdline, const LineCallback& onLine = {});
// Runs a PowerShell script (UTF-8 output). Keep scripts well below ~8k chars.
ProcResult RunPowerShell(const std::wstring& script, const LineCallback& onLine = {});
// Starts a program at the normal user level even though PowerTools is elevated.
bool LaunchDeelevated(const std::wstring& commandLine);

// ---- registry --------------------------------------------------------------
namespace reg {
struct Value {
    DWORD type = REG_NONE;  // REG_DWORD or REG_SZ
    DWORD dword = 0;
    std::wstring str;
};
std::optional<Value> Read(HKEY root, const wchar_t* key, const wchar_t* name);
bool WriteDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD v);
bool WriteString(HKEY root, const wchar_t* key, const wchar_t* name, const wchar_t* v);
bool DeleteValue(HKEY root, const wchar_t* key, const wchar_t* name);
bool DeleteKeyTree(HKEY root, const wchar_t* key);
bool KeyExists(HKEY root, const wchar_t* key);
}  // namespace reg

// ---- services --------------------------------------------------------------
namespace svc {
std::optional<DWORD> GetStartType(const wchar_t* name);  // SERVICE_AUTO_START etc.
bool SetStartType(const wchar_t* name, DWORD startType);
void Stop(const wchar_t* name);
}  // namespace svc

}  // namespace pt
