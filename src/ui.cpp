#include "ui.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <atomic>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "pages.h"
#include "util.h"
#include "widgets.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace pt::ui {
namespace {

HWND g_hwnd = nullptr;
HWND g_wakeTarget = nullptr;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
std::atomic<int> g_frames{0};
bool g_rendering = false;
float g_dpi = 1.f;
const wchar_t* kClass = L"PowerToolsUI";

void CreateRtv() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void ReleaseRtv() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool InitD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    bool warp = wcsstr(GetCommandLineW(), L"--warp") != nullptr;
    HRESULT hr = E_FAIL;
    if (!warp)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) return false;
    CreateRtv();
    return true;
}

void ShutdownD3D() {
    ReleaseRtv();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

ImFont* LoadFont(const wchar_t* file, float size) {
    wchar_t dir[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    std::wstring path = std::wstring(dir) + L"\\Fonts\\" + file;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(Narrow(path).c_str(), size);
}

void LoadFonts() {
    w::g_font = LoadFont(L"segoeui.ttf", 16);
    if (!w::g_font) w::g_font = ImGui::GetIO().Fonts->AddFontDefault();
    w::g_semibold = LoadFont(L"seguisb.ttf", 16);
    if (!w::g_semibold) w::g_semibold = w::g_font;
    w::g_icons = LoadFont(L"SegoeIcons.ttf", 18);
    if (!w::g_icons) w::g_icons = LoadFont(L"segmdl2.ttf", 18);
}

void ApplyWindowChrome(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF cap = RGB(15, 16, 21), txt = RGB(236, 238, 244), border = RGB(15, 16, 21);
    DwmSetWindowAttribute(hwnd, 35 /*CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd, 36 /*TEXT_COLOR*/, &txt, sizeof(txt));
    DwmSetWindowAttribute(hwnd, 34 /*BORDER_COLOR*/, &border, sizeof(border));
}

void Render() {
    if (!g_hwnd || !g_rtv || g_rendering) return;
    g_rendering = true;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    pages::Draw(io.DisplaySize);
    ImGui::End();

    ImGui::Render();
    const float clear[4] = {20 / 255.f, 21 / 255.f, 27 / 255.f, 1.f};
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap->Present(1, 0);
    g_rendering = false;
}

void Teardown() {
    if (!g_hwnd) {  // window died before ImGui was initialised
        ShutdownD3D();
        return;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ShutdownD3D();
    g_hwnd = nullptr;
    w::g_font = w::g_semibold = w::g_icons = nullptr;
    TrimWorkingSet();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_hwnd && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) {
        RequestFrames(4);
        return TRUE;
    }
    switch (msg) {
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MOUSEWHEEL: case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_MOUSELEAVE: case WM_SETFOCUS:
        case WM_KILLFOCUS: case WM_ACTIVATE:
            RequestFrames(4);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = static_cast<LONG>(900 * g_dpi);
            mmi->ptMinTrackSize.y = static_cast<LONG>(600 * g_dpi);
            return 0;
        }
        case WM_SIZE:
            if (g_dev && wp != SIZE_MINIMIZED) {
                ReleaseRtv();
                g_swap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
                CreateRtv();
                Render();  // live repaint while the user drags the frame
            }
            return 0;
        case WM_PAINT:
            if (g_dev) {
                ValidateRect(hwnd, nullptr);
                Render();
                return 0;
            }
            break;
        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp) / 96.f;
            w::SetupStyle(g_dpi);
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            RequestFrames(4);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_KEYMENU) return 0;  // no Alt-menu beep
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            Teardown();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void SetWakeTarget(HWND h) { g_wakeTarget = h; }

void RequestFrames(int n) {
    int cur = g_frames.load();
    while (cur < n && !g_frames.compare_exchange_weak(cur, n)) {}
}

void Wake() {
    g_frames = std::max(g_frames.load(), 3);
    if (g_wakeTarget) PostMessageW(g_wakeTarget, WM_PT_WAKE, 0, 0);
}

bool IsOpen() { return g_hwnd != nullptr; }

bool NeedsFrames() { return g_hwnd && g_frames.load() > 0; }

void Frame() {
    if (!g_hwnd || g_frames.load() <= 0) return;
    if (IsIconic(g_hwnd)) { g_frames = 0; return; }
    g_frames.fetch_sub(1);
    Render();
}

void Open() {
    if (g_hwnd) {
        if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
        SetForegroundWindow(g_hwnd);
        RequestFrames(4);
        return;
    }
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(101));
        wc.hIconSm = wc.hIcon;
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        registered = true;
    }

    POINT cur;
    GetCursorPos(&cur);
    HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    g_dpi = dx / 96.f;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    int w = static_cast<int>(1040 * g_dpi), h = static_cast<int>(700 * g_dpi);
    int aw = mi.rcWork.right - mi.rcWork.left, ah = mi.rcWork.bottom - mi.rcWork.top;
    w = std::min(w, aw - 40);
    h = std::min(h, ah - 40);
    int x = mi.rcWork.left + (aw - w) / 2, y = mi.rcWork.top + (ah - h) / 2;

    HWND hwnd = CreateWindowExW(0, kClass, L"PowerTools", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return;
    ApplyWindowChrome(hwnd);
    if (!InitD3D(hwnd)) {
        DestroyWindow(hwnd);
        MessageBoxW(nullptr, L"Could not initialise Direct3D 11.", L"PowerTools", MB_ICONERROR);
        return;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    LoadFonts();
    w::SetupStyle(g_dpi);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    g_hwnd = hwnd;

    pages::OnOpen();
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    RequestFrames(6);
}

void Close() {
    if (g_hwnd) DestroyWindow(g_hwnd);
}

}  // namespace pt::ui
