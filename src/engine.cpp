#include "engine.h"

#include <dwmapi.h>

#include <algorithm>
#include <atomic>
#include <thread>

#include "util.h"

namespace pt {

const char* ActionName(Action a) {
    switch (a) {
        case Action::CloseWindow: return Tr("Close window", "Закрыть окно");
        case Action::KillProcess: return Tr("Force-kill app", "Убить процесс окна");
        case Action::Minimize: return Tr("Minimize window", "Свернуть окно");
        case Action::ToggleMaximize: return Tr("Maximize / restore", "Развернуть / восстановить");
        case Action::ToggleTopmost: return Tr("Toggle always on top", "Поверх всех окон");
        case Action::OpacityUp: return Tr("Window more opaque", "Окно менее прозрачное");
        case Action::OpacityDown: return Tr("Window more transparent", "Окно более прозрачное");
        case Action::CenterWindow: return Tr("Center window", "Центрировать окно");
        case Action::NextMonitor: return Tr("Move to next monitor", "На следующий монитор");
        case Action::Launch: return Tr("Launch program / open", "Запустить программу / открыть");
        default: return "?";
    }
}

bool ActionNeedsParam(Action a) { return a == Action::Launch; }

namespace engine {
namespace {

constexpr ULONG_PTR kMagic = 0x50545F31;  // marks our own injected input
constexpr WORD kDummyVk = 0xE8;           // unassigned VK, cancels the Start-menu tap

SRWLOCK g_lock = SRWLOCK_INIT;
EngineSettings g_s;
std::thread g_thread;
DWORD g_threadId = 0;
HHOOK g_kb = nullptr;
HHOOK g_ms = nullptr;

std::atomic<bool> g_capturing{false};
std::atomic<bool> g_captured{false};
std::atomic<uint32_t> g_capMods{0};
std::atomic<uint32_t> g_capVk{0};

bool g_swallowed[256] = {};  // keys whose down event we ate (hook thread only)

struct DragState {
    enum Mode { None, Move, Resize } mode = None;
    HWND hwnd = nullptr;
    POINT start{};
    RECT rect{};
    int edgeX = 0, edgeY = 0;  // resize: -1 left/top, +1 right/bottom, 0 untouched
    UINT button = 0;           // WM_LBUTTONDOWN or WM_RBUTTONDOWN
    ULONGLONG lastApply = 0;
    POINT last{};
    bool pending = false;
} g_drag;

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

uint32_t CurrentMods() {
    uint32_t m = 0;
    if (Down(VK_LWIN) || Down(VK_RWIN)) m |= Mod::Win;
    if (Down(VK_CONTROL)) m |= Mod::Ctrl;
    if (Down(VK_MENU)) m |= Mod::Alt;
    if (Down(VK_SHIFT)) m |= Mod::Shift;
    return m;
}

bool IsModifierVk(DWORD vk) {
    switch (vk) {
        case VK_LWIN: case VK_RWIN: case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
            return true;
    }
    return false;
}

// Set when Win/Alt was used for a gesture; the next Win/Alt release is swallowed and
// replayed as "Ctrl tap + release", which is what stops Start / the menu bar from opening.
bool g_maskRelease = false;

void InjectDummyKey() { g_maskRelease = true; }

void ReplayMaskedRelease(const KBDLLHOOKSTRUCT* kb) {
    INPUT in[3] = {};
    for (auto& i : in) {
        i.type = INPUT_KEYBOARD;
        i.ki.dwExtraInfo = kMagic;
    }
    in[0].ki.wVk = VK_LCONTROL;
    in[0].ki.wScan = 0x1D;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    in[2].ki.wVk = static_cast<WORD>(kb->vkCode);
    in[2].ki.wScan = static_cast<WORD>(kb->scanCode);
    in[2].ki.dwFlags = KEYEVENTF_KEYUP | ((kb->flags & LLKHF_EXTENDED) ? KEYEVENTF_EXTENDEDKEY : 0);
    in[2].ki.dwExtraInfo = kMagic;
    SendInput(3, in, sizeof(INPUT));
}

std::wstring ClassOf(HWND h) {
    wchar_t c[128] = {};
    GetClassNameW(h, c, ARRAYSIZE(c));
    return c;
}

bool IsShellWindow(HWND h) {
    static const wchar_t* skip[] = {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                                    L"XamlExplorerHostIslandWindow", L"TopLevelWindowForOverflowXamlIsland",
                                    L"NotifyIconOverflowWindow", L"Windows.UI.Core.CoreWindow"};
    std::wstring c = ClassOf(h);
    for (auto s : skip)
        if (c == s) return true;
    return false;
}

bool IsCloaked(HWND h) {
    BOOL cloaked = FALSE;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

HWND TopLevelAt(POINT pt) {
    HWND h = WindowFromPoint(pt);
    if (!h) return nullptr;
    h = GetAncestor(h, GA_ROOT);
    if (!h || !IsWindowVisible(h) || IsCloaked(h) || IsShellWindow(h)) return nullptr;
    return h;
}

HWND ForegroundTarget() {
    HWND h = GetForegroundWindow();
    if (!h) return nullptr;
    h = GetAncestor(h, GA_ROOT);
    if (!h || IsShellWindow(h)) return nullptr;
    return h;
}

// ---- drag / resize ---------------------------------------------------------

void ApplyDrag(POINT cur, bool async) {
    int dx = cur.x - g_drag.start.x, dy = cur.y - g_drag.start.y;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | (async ? SWP_ASYNCWINDOWPOS : 0);
    if (g_drag.mode == DragState::Move) {
        SetWindowPos(g_drag.hwnd, nullptr, g_drag.rect.left + dx, g_drag.rect.top + dy, 0, 0, flags | SWP_NOSIZE);
    } else {
        RECT r = g_drag.rect;
        if (g_drag.edgeX < 0) r.left += dx;
        if (g_drag.edgeX > 0) r.right += dx;
        if (g_drag.edgeY < 0) r.top += dy;
        if (g_drag.edgeY > 0) r.bottom += dy;
        const int minW = 140, minH = 80;
        if (r.right - r.left < minW) { if (g_drag.edgeX < 0) r.left = r.right - minW; else r.right = r.left + minW; }
        if (r.bottom - r.top < minH) { if (g_drag.edgeY < 0) r.top = r.bottom - minH; else r.bottom = r.top + minH; }
        SetWindowPos(g_drag.hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
    }
}

bool BeginDrag(DragState::Mode mode, UINT button, POINT pt, uint32_t mods) {
    HWND h = TopLevelAt(pt);
    if (!h || IsIconic(h)) return false;
    LONG style = GetWindowLongW(h, GWL_STYLE);
    RECT rc;
    if (!GetWindowRect(h, &rc)) return false;

    if (mode == DragState::Resize) {
        if (!(style & WS_THICKFRAME) || IsZoomed(h)) return false;
    } else if (IsZoomed(h)) {
        // Restore, then keep the cursor at the same relative spot on the title area.
        WINDOWPLACEMENT wp{sizeof(wp)};
        GetWindowPlacement(h, &wp);
        int w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        int hgt = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
        double rx = double(pt.x - rc.left) / std::max<LONG>(1, rc.right - rc.left);
        ShowWindow(h, SW_RESTORE);
        int nx = pt.x - int(rx * w);
        int ny = pt.y - 16;
        SetWindowPos(h, nullptr, nx, ny, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRect(h, &rc);
    }

    g_drag.mode = mode;
    g_drag.hwnd = h;
    g_drag.start = pt;
    g_drag.rect = rc;
    g_drag.button = button;
    g_drag.pending = false;
    g_drag.lastApply = 0;
    g_drag.edgeX = g_drag.edgeY = 0;

    if (mode == DragState::Resize) {
        int w = rc.right - rc.left, hh = rc.bottom - rc.top;
        int tx = rc.left + w / 3, tx2 = rc.right - w / 3;
        int ty = rc.top + hh / 3, ty2 = rc.bottom - hh / 3;
        g_drag.edgeX = pt.x < tx ? -1 : (pt.x > tx2 ? 1 : 0);
        g_drag.edgeY = pt.y < ty ? -1 : (pt.y > ty2 ? 1 : 0);
        if (g_drag.edgeX == 0 && g_drag.edgeY == 0) {  // centre: nearest corner
            g_drag.edgeX = pt.x < rc.left + w / 2 ? -1 : 1;
            g_drag.edgeY = pt.y < rc.top + hh / 2 ? -1 : 1;
        }
    }

    // Raise + focus like a normal click would.
    SetForegroundWindow(h);
    SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (mods & (Mod::Win | Mod::Alt)) InjectDummyKey();
    return true;
}

void EndDrag(POINT pt) {
    if (g_drag.mode == DragState::None) return;
    ApplyDrag(pt, true);
    g_drag = DragState{};
    if (Down(VK_LWIN) || Down(VK_RWIN) || Down(VK_MENU)) InjectDummyKey();  // right before the modifier is released
}

LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
    if (ms->flags & LLMHF_INJECTED) return CallNextHookEx(nullptr, code, wp, lp);

    switch (wp) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            if (g_drag.mode != DragState::None) break;
            AcquireSRWLockShared(&g_lock);
            EngineSettings s = {g_s.enabled, g_s.dragEnabled, g_s.dragMods, g_s.resizeEnabled, g_s.resizeMods, {}};
            ReleaseSRWLockShared(&g_lock);
            if (!s.enabled) break;
            uint32_t mods = CurrentMods();
            if (wp == WM_LBUTTONDOWN && s.dragEnabled && mods == s.dragMods && mods != 0) {
                if (BeginDrag(DragState::Move, WM_LBUTTONDOWN, ms->pt, mods)) return 1;
            } else if (wp == WM_RBUTTONDOWN && s.resizeEnabled && mods == s.resizeMods && mods != 0) {
                if (BeginDrag(DragState::Resize, WM_RBUTTONDOWN, ms->pt, mods)) return 1;
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (g_drag.mode != DragState::None) {
                ULONGLONG now = GetTickCount64();
                g_drag.last = ms->pt;
                if (now - g_drag.lastApply >= 6) {
                    ApplyDrag(ms->pt, true);
                    g_drag.lastApply = now;
                    g_drag.pending = false;
                } else {
                    g_drag.pending = true;
                }
            }
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            if (g_drag.mode != DragState::None && (wp == WM_LBUTTONUP) == (g_drag.button == WM_LBUTTONDOWN)) {
                EndDrag(ms->pt);
                return 1;
            }
            break;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// ---- actions ---------------------------------------------------------------

std::wstring ProcessName(DWORD pid) {
    std::wstring out;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        wchar_t buf[MAX_PATH];
        DWORD n = ARRAYSIZE(buf);
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
            out = buf;
            size_t p = out.find_last_of(L'\\');
            if (p != std::wstring::npos) out = out.substr(p + 1);
        }
        CloseHandle(h);
    }
    for (auto& c : out) c = static_cast<wchar_t>(towlower(c));
    return out;
}

void SetAlphaStep(HWND h, int delta) {
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    BYTE alpha = 255;
    if (ex & WS_EX_LAYERED) {
        COLORREF key;
        DWORD fl;
        GetLayeredWindowAttributes(h, &key, &alpha, &fl);
    }
    int a = std::clamp<int>(alpha + delta, 30, 255);
    if (a >= 255) {
        if (ex & WS_EX_LAYERED) SetWindowLongW(h, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        RedrawWindow(h, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        return;
    }
    if (!(ex & WS_EX_LAYERED)) SetWindowLongW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(h, 0, static_cast<BYTE>(a), LWA_ALPHA);
}

struct MonList {
    std::vector<HMONITOR> mons;
};
BOOL CALLBACK EnumMon(HMONITOR m, HDC, LPRECT, LPARAM p) {
    reinterpret_cast<MonList*>(p)->mons.push_back(m);
    return TRUE;
}

RECT WorkArea(HMONITOR m) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(m, &mi);
    return mi.rcWork;
}

// ---- "always on top" frames --------------------------------------------------
// A click-through accent ring is drawn around every window pinned with ToggleTopmost.

constexpr UINT WM_PT_TOPMOST = WM_APP + 1;
struct Pinned { HWND target; HWND frame; RECT last; };
std::vector<Pinned> g_pinned;  // engine thread only

LRESULT CALLBACK FrameProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) {
        HDC dc = reinterpret_cast<HDC>(w);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH b = CreateSolidBrush(RGB(255, 176, 56));
        FillRect(dc, &r, b);
        DeleteObject(b);
        return 1;
    }
    return DefWindowProcW(h, m, w, l);
}

HWND CreateFrame() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = FrameProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PowerToolsPinFrame";
        RegisterClassExW(&wc);
        reg = true;
    }
    HWND f = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                             L"PowerToolsPinFrame", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (f) SetLayeredWindowAttributes(f, 0, 235, LWA_ALPHA);
    return f;
}

void UpdateFrames() {
    for (size_t i = 0; i < g_pinned.size();) {
        Pinned& p = g_pinned[i];
        bool alive = IsWindow(p.target) && (GetWindowLongW(p.target, GWL_EXSTYLE) & WS_EX_TOPMOST);
        if (!alive) {
            DestroyWindow(p.frame);
            g_pinned.erase(g_pinned.begin() + i);
            continue;
        }
        if (!IsWindowVisible(p.target) || IsIconic(p.target) || IsCloaked(p.target)) {
            ShowWindow(p.frame, SW_HIDE);
            p.last = RECT{};
            ++i;
            continue;
        }
        RECT r;
        if (FAILED(DwmGetWindowAttribute(p.target, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(p.target, &r);
        bool shown = IsWindowVisible(p.frame);
        if (!shown || memcmp(&r, &p.last, sizeof(r)) != 0) {
            const int t = 3;
            int w = r.right - r.left, h = r.bottom - r.top;
            HRGN outer = CreateRectRgn(0, 0, w, h);
            HRGN inner = CreateRectRgn(t, t, w - t, h - t);
            CombineRgn(outer, outer, inner, RGN_DIFF);
            DeleteObject(inner);
            SetWindowPos(p.frame, nullptr, r.left, r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowRgn(p.frame, outer, TRUE);  // system owns the region now
            p.last = r;
        }
        if (!shown) ShowWindow(p.frame, SW_SHOWNOACTIVATE);
        if (GetWindow(p.frame, GW_HWNDNEXT) != p.target)  // keep the ring right above its window
            SetWindowPos(p.target, p.frame, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ++i;
    }
}

void OnTopmostChanged(HWND h, bool on) {
    for (size_t i = 0; i < g_pinned.size(); ++i)
        if (g_pinned[i].target == h) {
            if (!on) {
                DestroyWindow(g_pinned[i].frame);
                g_pinned.erase(g_pinned.begin() + i);
            }
            return;
        }
    if (on) {
        HWND f = CreateFrame();
        if (f) g_pinned.push_back({h, f, RECT{}});
    }
}

void RunAction(const Binding& b) {
    if (b.action == Action::Launch) {
        if (!b.param.empty()) LaunchDeelevated(b.param);
        return;
    }
    HWND h = ForegroundTarget();
    if (!h) return;
    switch (b.action) {
        case Action::CloseWindow:
            PostMessageW(h, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;
        case Action::KillProcess: {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (!pid || pid == GetCurrentProcessId()) break;
            std::wstring n = ProcessName(pid);
            static const wchar_t* never[] = {L"explorer.exe", L"csrss.exe", L"winlogon.exe", L"wininit.exe",
                                             L"dwm.exe",      L"lsass.exe", L"services.exe", L"smss.exe",
                                             L"svchost.exe",  L"system"};
            for (auto s : never)
                if (n == s) return;
            if (HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
                TerminateProcess(p, 1);
                CloseHandle(p);
            }
            break;
        }
        case Action::Minimize:
            PostMessageW(h, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            break;
        case Action::ToggleMaximize:
            PostMessageW(h, WM_SYSCOMMAND, IsZoomed(h) ? SC_RESTORE : SC_MAXIMIZE, 0);
            break;
        case Action::ToggleTopmost: {
            bool top = GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOPMOST;
            SetWindowPos(h, top ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            PostThreadMessageW(g_threadId, WM_PT_TOPMOST, reinterpret_cast<WPARAM>(h), top ? 0 : 1);
            break;
        }
        case Action::OpacityUp: SetAlphaStep(h, +25); break;
        case Action::OpacityDown: SetAlphaStep(h, -25); break;
        case Action::CenterWindow: {
            if (IsZoomed(h)) break;
            RECT r, wa = WorkArea(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST));
            GetWindowRect(h, &r);
            int w = r.right - r.left, hh = r.bottom - r.top;
            SetWindowPos(h, nullptr, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        }
        case Action::NextMonitor: {
            MonList ml;
            EnumDisplayMonitors(nullptr, nullptr, EnumMon, reinterpret_cast<LPARAM>(&ml));
            if (ml.mons.size() < 2) break;
            HMONITOR cur = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
            size_t idx = 0;
            for (size_t i = 0; i < ml.mons.size(); ++i)
                if (ml.mons[i] == cur) idx = i;
            HMONITOR next = ml.mons[(idx + 1) % ml.mons.size()];
            bool zoomed = IsZoomed(h);
            if (zoomed) ShowWindow(h, SW_RESTORE);
            RECT r, a = WorkArea(cur), b2 = WorkArea(next);
            GetWindowRect(h, &r);
            double sx = double(b2.right - b2.left) / std::max<LONG>(1, a.right - a.left);
            double sy = double(b2.bottom - b2.top) / std::max<LONG>(1, a.bottom - a.top);
            int nx = b2.left + int((r.left - a.left) * sx), ny = b2.top + int((r.top - a.top) * sy);
            int nw = int((r.right - r.left) * sx), nh = int((r.bottom - r.top) * sy);
            SetWindowPos(h, nullptr, nx, ny, nw, nh, SWP_NOZORDER | SWP_NOACTIVATE);
            if (zoomed) ShowWindow(h, SW_MAXIMIZE);
            break;
        }
        default: break;
    }
}

// ---- keyboard --------------------------------------------------------------

LRESULT CALLBACK KeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    if ((kb->flags & LLKHF_INJECTED) || kb->dwExtraInfo == kMagic) return CallNextHookEx(nullptr, code, wp, lp);

    bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    bool up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
    DWORD vk = kb->vkCode;
    if (vk > 255) return CallNextHookEx(nullptr, code, wp, lp);

    if (up && g_maskRelease && (vk == VK_LWIN || vk == VK_RWIN || vk == VK_LMENU || vk == VK_RMENU)) {
        g_maskRelease = false;
        ReplayMaskedRelease(kb);
        return 1;
    }
    if (up && g_swallowed[vk]) {
        g_swallowed[vk] = false;
        return 1;
    }
    if (!down || IsModifierVk(vk)) return CallNextHookEx(nullptr, code, wp, lp);
    if (g_swallowed[vk]) return 1;  // auto-repeat of a swallowed combo

    uint32_t mods = CurrentMods();

    if (g_capturing.load()) {
        if (vk == VK_ESCAPE && mods == 0) {
            g_capturing = false;
            g_swallowed[vk] = true;
            return 1;
        }
        g_capMods = mods;
        g_capVk = vk;
        g_captured = true;
        g_capturing = false;
        g_swallowed[vk] = true;
        if (mods & (Mod::Win | Mod::Alt)) InjectDummyKey();
        return 1;
    }

    if (mods == 0) return CallNextHookEx(nullptr, code, wp, lp);

    Binding hit;
    bool found = false;
    AcquireSRWLockShared(&g_lock);
    if (g_s.enabled) {
        for (const auto& b : g_s.bindings)
            if (b.enabled && b.vk == vk && b.mods == mods) {
                hit = b;
                found = true;
                break;
            }
    }
    ReleaseSRWLockShared(&g_lock);
    if (!found) return CallNextHookEx(nullptr, code, wp, lp);

    g_swallowed[vk] = true;
    if (mods & (Mod::Win | Mod::Alt)) InjectDummyKey();
    std::thread([hit] { RunAction(hit); }).detach();
    return 1;
}

void ThreadMain() {
    g_threadId = GetCurrentThreadId();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // create the queue
    g_kb = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    g_ms = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    SetTimer(nullptr, 1, 10, nullptr);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_PT_TOPMOST) {
            OnTopmostChanged(reinterpret_cast<HWND>(msg.wParam), msg.lParam != 0);
            UpdateFrames();
            continue;
        }
        if (msg.message == WM_TIMER) {
            if (!g_pinned.empty()) UpdateFrames();
            // Flush a throttled mouse position so the window never lags behind the cursor.
            if (g_drag.mode != DragState::None && g_drag.pending) {
                ApplyDrag(g_drag.last, true);
                g_drag.pending = false;
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_kb) UnhookWindowsHookEx(g_kb);
    if (g_ms) UnhookWindowsHookEx(g_ms);
    g_kb = g_ms = nullptr;
}

}  // namespace

void Start() {
    if (g_thread.joinable()) return;
    g_thread = std::thread(ThreadMain);
}

void Stop() {
    if (!g_thread.joinable()) return;
    while (!g_threadId) Sleep(1);
    PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
    g_thread.join();
    g_threadId = 0;
}

void Apply(const EngineSettings& s) {
    AcquireSRWLockExclusive(&g_lock);
    g_s = s;
    ReleaseSRWLockExclusive(&g_lock);
}

void BeginCapture() {
    g_captured = false;
    g_capturing = true;
}
void CancelCapture() { g_capturing = false; }
bool IsCapturing() { return g_capturing.load(); }
bool PollCapture(uint32_t& mods, uint32_t& vk) {
    if (!g_captured.exchange(false)) return false;
    mods = g_capMods;
    vk = g_capVk;
    return true;
}

std::string ModsName(uint32_t m) {
    std::string s;
    if (m & Mod::Win) s += "Win + ";
    if (m & Mod::Ctrl) s += "Ctrl + ";
    if (m & Mod::Alt) s += "Alt + ";
    if (m & Mod::Shift) s += "Shift + ";
    return s;
}

std::string ComboName(uint32_t mods, uint32_t vk) {
    if (!vk) return mods ? ModsName(mods).substr(0, ModsName(mods).size() - 3) : Tr("(not set)", "(не задано)");
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lparam = static_cast<LONG>(sc << 16);
    switch (vk) {  // keys whose scancode needs the "extended" bit
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
        case VK_END: case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            lparam |= 1 << 24;
    }
    wchar_t name[64] = {};
    if (!GetKeyNameTextW(lparam, name, ARRAYSIZE(name))) swprintf_s(name, L"VK %u", vk);
    return ModsName(mods) + Narrow(name);
}

}  // namespace engine
}  // namespace pt
