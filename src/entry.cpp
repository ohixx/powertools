#include <windows.h>
#include <shellapi.h>

#include <string>

#include "config.h"
#include "engine.h"
#include "ui.h"
#include "util.h"

using namespace pt;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_PT_SHOW = WM_APP + 2;
constexpr UINT ID_OPEN = 1, ID_HOTKEYS = 2, ID_EXIT = 3;
const wchar_t* kTrayClass = L"PowerToolsTray";

HWND g_tray = nullptr;
UINT g_taskbarCreated = 0;

void AddTrayIcon() {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = g_tray;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON,
                                              GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(nid.szTip, L"PowerTools");
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = g_tray;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, Widen(Tr("Open PowerTools", "Открыть PowerTools")).c_str());
    AppendMenuW(m, MF_STRING | (Cfg().hotkeysEnabled ? MF_CHECKED : 0), ID_HOTKEYS,
                Widen(Tr("Hotkeys & gestures", "Хоткеи и жесты")).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, Widen(Tr("Exit", "Выход")).c_str());
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_tray);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_tray, nullptr);
    PostMessageW(g_tray, WM_NULL, 0, 0);
    DestroyMenu(m);
}

LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreated) {
        AddTrayIcon();
        return 0;
    }
    switch (msg) {
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_LBUTTONUP: case NIN_SELECT: ui::Open(); break;
                case WM_RBUTTONUP: case WM_CONTEXTMENU: ShowTrayMenu(); break;
            }
            return 0;
        case WM_PT_SHOW:
            ui::Open();
            return 0;
        case ui::WM_PT_WAKE:
            ui::RequestFrames(3);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_OPEN: ui::Open(); break;
                case ID_HOTKEYS:
                    Cfg().hotkeysEnabled = !Cfg().hotkeysEnabled;
                    SaveConfig();
                    PushEngineSettings();
                    ui::RequestFrames(3);
                    break;
                case ID_EXIT: PostQuitMessage(0); break;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\PowerTools.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kTrayClass, nullptr)) PostMessageW(other, WM_PT_SHOW, 0, 0);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool trayOnly = wcsstr(GetCommandLineW(), L"--tray") != nullptr;

    LoadConfig();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = TrayProc;
    wc.hInstance = inst;
    wc.lpszClassName = kTrayClass;
    RegisterClassExW(&wc);
    g_tray = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, L"PowerTools", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ui::SetWakeTarget(g_tray);
    AddTrayIcon();

    engine::Start();
    PushEngineSettings();

    if (!trayOnly) ui::Open();
    else TrimWorkingSet();

    MSG msg;
    bool quit = false;
    while (!quit) {
        DWORD timeout = ui::NeedsFrames() ? 8 : (ui::IsOpen() ? 1600 : INFINITE);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, timeout, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!quit) ui::Frame();
        // Keep our own footprint small: trim once the UI has been idle for a moment.
        static ULONGLONG lastBusy = 0;
        static bool trimmed = true;
        ULONGLONG now = GetTickCount64();
        if (ui::NeedsFrames()) { lastBusy = now; trimmed = false; }
        else if (!trimmed && now - lastBusy > 1500) { TrimWorkingSet(); trimmed = true; }
    }

    ui::Close();
    engine::Stop();
    RemoveTrayIcon();
    CloseHandle(mutex);
    return 0;
}
