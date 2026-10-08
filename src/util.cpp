#include "util.h"

#include <psapi.h>
#include <shlobj.h>

#include <cwctype>

namespace pt {

std::wstring Widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string o(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), o.data(), n, nullptr, nullptr);
    return o;
}

std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t e = s.find('\n', start);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(start, e - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(std::move(line));
        start = e + 1;
    }
    return out;
}

std::string Trim(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n);
    CloseHandle(tok);
    return ok && e.TokenIsElevated;
}

std::wstring DataDir() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) dir = p;
    CoTaskMemFree(p);
    dir += L"\\PowerTools";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return std::wstring(buf, n);
}

size_t WorkingSetBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return pmc.WorkingSetSize;
    return 0;
}

void TrimWorkingSet() { EmptyWorkingSet(GetCurrentProcess()); }

double FreeRamNow() {
    auto avail = [] {
        MEMORYSTATUSEX m{sizeof(m)};
        GlobalMemoryStatusEx(&m);
        return static_cast<double>(m.ullAvailPhys);
    };
    double before = avail();
    DWORD pids[4096], bytes = 0;
    if (EnumProcesses(pids, sizeof(pids), &bytes)) {
        for (DWORD i = 0; i < bytes / sizeof(DWORD); ++i) {
            if (!pids[i]) continue;
            HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
            if (h) {
                EmptyWorkingSet(h);
                CloseHandle(h);
            }
        }
    }
    Sleep(300);
    double freed = (avail() - before) / 1048576.0;
    return freed > 0 ? freed : 0;
}

// ---- processes -------------------------------------------------------------

ProcResult RunHidden(const std::wstring& cmdline, const LineCallback& onLine) {
    ProcResult res;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return res;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nul;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdline;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        return res;
    }
    res.started = true;

    std::string pending;
    char buf[4096];
    DWORD got = 0;
    auto flushLines = [&](bool final) {
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending.erase(0, pos + 1);
            if (onLine) onLine(line);
        }
        if (final && !pending.empty()) {
            if (onLine) onLine(pending);
            pending.clear();
        }
    };
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        res.output.append(buf, got);
        pending.append(buf, got);
        flushLines(false);
    }
    flushLines(true);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &res.exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
}

static std::string Base64(const unsigned char* d, size_t n) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        o += (i + 2 < n) ? T[v & 63] : '=';
    }
    return o;
}

ProcResult RunPowerShell(const std::wstring& script, const LineCallback& onLine) {
    std::wstring full =
        L"[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);"
        L"$ProgressPreference='SilentlyContinue';$ErrorActionPreference='Continue';" +
        script;
    std::string b64 = Base64(reinterpret_cast<const unsigned char*>(full.data()), full.size() * sizeof(wchar_t));
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + sys +
                       L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive "
                       L"-ExecutionPolicy Bypass -EncodedCommand " +
                       Widen(b64);
    return RunHidden(cmd, onLine);
}

bool LaunchDeelevated(const std::wstring& commandLine) {
    // Parse "file args" (file may be quoted).
    std::wstring s = commandLine;
    size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return false;
    s = s.substr(a);
    std::wstring file, args;
    if (s[0] == L'"') {
        size_t e = s.find(L'"', 1);
        if (e == std::wstring::npos) return false;
        file = s.substr(1, e - 1);
        args = s.substr(e + 1);
    } else {
        size_t e = s.find_first_of(L" \t");
        file = s.substr(0, e);
        if (e != std::wstring::npos) args = s.substr(e);
    }
    std::wstring lower = file;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    auto ends = [&](const wchar_t* ext) {
        size_t n = wcslen(ext);
        return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
    };
    bool dot = lower.find(L'.') != std::wstring::npos;
    bool isExe = !dot || ends(L".exe") || ends(L".bat") || ends(L".cmd") || ends(L".com");

    if (isExe) {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetShellWindow(), &pid);
        HANDLE hp = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
        HANDLE tok = nullptr, prim = nullptr;
        bool ok = false;
        if (hp && OpenProcessToken(hp, TOKEN_DUPLICATE | TOKEN_QUERY, &tok) &&
            DuplicateTokenEx(tok, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &prim)) {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + file + L"\"" + args;
            if (CreateProcessWithTokenW(prim, 0, nullptr, cmd.data(), 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                ok = true;
            }
        }
        if (prim) CloseHandle(prim);
        if (tok) CloseHandle(tok);
        if (hp) CloseHandle(hp);
        if (ok) return true;
    }
    // Documents, folders, URLs, or a failed token launch: explorer.exe runs un-elevated.
    if (!isExe) {
        std::wstring target = L"\"" + file + L"\"";
        HINSTANCE r = ShellExecuteW(nullptr, L"open", L"explorer.exe", target.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(r) > 32;
    }
    HINSTANCE r = ShellExecuteW(nullptr, L"open", file.c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;
}

// ---- registry --------------------------------------------------------------
namespace reg {

std::optional<Value> Read(HKEY root, const wchar_t* key, const wchar_t* name) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return std::nullopt;
    Value v;
    DWORD type = 0, size = 0;
    std::optional<Value> out;
    if (RegQueryValueExW(h, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS) {
        if (type == REG_DWORD && size == sizeof(DWORD)) {
            DWORD d = 0;
            if (RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(&d), &size) == ERROR_SUCCESS) {
                v.type = REG_DWORD;
                v.dword = d;
                out = v;
            }
        } else if (type == REG_SZ || type == REG_EXPAND_SZ) {
            std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
            DWORD sz = size;
            if (RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(s.data()), &sz) == ERROR_SUCCESS) {
                s.resize(wcsnlen(s.c_str(), s.size()));
                v.type = REG_SZ;
                v.str = s;
                out = v;
            }
        } else {
            v.type = type;
            out = v;
        }
    }
    RegCloseKey(h);
    return out;
}

static HKEY OpenForWrite(HKEY root, const wchar_t* key) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return nullptr;
    return h;
}

bool WriteDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD v) {
    HKEY h = OpenForWrite(root, key);
    if (!h) return false;
    bool ok = RegSetValueExW(h, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)) == ERROR_SUCCESS;
    RegCloseKey(h);
    return ok;
}

bool WriteString(HKEY root, const wchar_t* key, const wchar_t* name, const wchar_t* v) {
    HKEY h = OpenForWrite(root, key);
    if (!h) return false;
    bool ok = RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v),
                             static_cast<DWORD>((wcslen(v) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    RegCloseKey(h);
    return ok;
}

bool DeleteValue(HKEY root, const wchar_t* key, const wchar_t* name) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return true;  // already gone
    LONG r = RegDeleteValueW(h, name);
    RegCloseKey(h);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

bool DeleteKeyTree(HKEY root, const wchar_t* key) {
    LONG r = RegDeleteTreeW(root, key);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

bool KeyExists(HKEY root, const wchar_t* key) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}

}  // namespace reg

// ---- services --------------------------------------------------------------
namespace svc {

std::optional<DWORD> GetStartType(const wchar_t* name) {
    auto v = reg::Read(HKEY_LOCAL_MACHINE, (std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + name).c_str(), L"Start");
    if (!v || v->type != REG_DWORD) return std::nullopt;
    return v->dword;
}

bool SetStartType(const wchar_t* name, DWORD startType) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE s = OpenServiceW(scm, name, SERVICE_CHANGE_CONFIG);
    bool ok = false;
    if (s) {
        ok = ChangeServiceConfigW(s, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr) != 0;
        CloseServiceHandle(s);
    } else if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
        ok = true;  // nothing to configure on this edition
    }
    CloseServiceHandle(scm);
    return ok;
}

void Stop(const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return;
    SC_HANDLE s = OpenServiceW(scm, name, SERVICE_STOP);
    if (s) {
        SERVICE_STATUS st;
        ControlService(s, SERVICE_CONTROL_STOP, &st);
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm);
}

}  // namespace svc

}  // namespace pt
