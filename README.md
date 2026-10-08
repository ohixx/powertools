# PowerTools

A small native Windows app for cleaning and customizing Windows 10/11. It strips the junk out of the system, tunes it for speed, and adds window-management tweaks that Windows never shipped. It sits in the tray and stays out of your RAM.

> **Status:** early alpha. Many tweaks change system settings. Everything is reversible from the UI, but use it at your own risk.

## Features

- **UWP cleanup:** removes every built-in app except Windows Terminal, Settings and the system components Windows needs. Hardware vendor apps (NVIDIA, Realtek, AMD, Intel) and codecs are protected by default. Removed apps can be restored from inside the app.
- **Ads and suggestions:** Start "Recommended", taskbar search/Bing, Widgets, Copilot/Recall, lock-screen tips, advertising ID, Explorer banners.
- **Background junk:** telemetry and CompatTelRunner, activity history, background apps, Game Bar, Edge background mode, error reporting.
- **Memory:** OneDrive, Delivery Optimization, SysMain, Search indexer, Xbox, location and other rarely needed services.
- **Performance:** Ultimate Performance power plan; visual effects set to best performance while keeping window contents on drag, font smoothing and the translucent selection rectangle.
- **Window gestures:** `Win + LMB` moves a window from anywhere, `Win + RMB` resizes it from the nearest corner or edge. The modifier is configurable.
- **Hotkeys:** your own shortcuts for closing, force-killing, minimizing, maximizing, pinning on top (with a visible frame), changing opacity, centering, moving to the next monitor, or launching a program.
- Every tweak shows a clear ON / OFF state. English and Russian UI.

## Build

Requires Windows 10/11, MSVC (Visual Studio 2022 Build Tools) and CMake 3.20+.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The binary is `build/Release/PowerTools.exe`. It asks for administrator rights on launch.

## Design notes

- C++20, Win32 and Dear ImGui on Direct3D 11. No Electron, Qt or .NET.
- The window is created on demand and destroyed when closed, so only the hotkey engine and tray icon stay resident.
- Hotkeys use low-level keyboard and mouse hooks on their own thread.
- Autostart uses a scheduled task, so there is no UAC prompt at login.
- Settings live in `%APPDATA%\PowerTools`.

## License

BSD 3-Clause. Dear ImGui is MIT licensed (see `third_party/imgui`).
