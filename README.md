<p align="center">
  <img src="assets/logo.png" width="112" alt="PowerTools">
</p>

<h1 align="center">PowerTools</h1>

<p align="center">
  <b>English</b> | <a href="README.ru.md">Русский</a>
</p>

---

PowerTools is a small native Windows 10/11 app for cleaning and customizing the system. It removes built-in apps and background junk, switches off ads and recommendations, tunes the power plan and visual effects for speed, and adds window management that Windows does not have: move and resize any window with the mouse from anywhere, and bind your own hotkeys.

It lives in the tray. With the window closed only the hotkey engine stays resident.

Tweaks are grouped by what they do:

- **UWP apps**: removes every Store/bundled app except Windows Terminal, Settings and the components Windows needs to run. GPU/audio driver apps, codecs, Security and the Store are protected unless you unlock them. Removed apps can be restored from the app.
- **Ads and suggestions**: Start "Recommended", taskbar search and Bing results, Widgets, Copilot and Recall, lock-screen tips, advertising ID, Explorer banners.
- **Background junk**: telemetry and the compatibility appraiser, activity history, background apps, Game Bar, Edge background mode, error reporting.
- **Memory (RAM)**: OneDrive, Delivery Optimization, SysMain, the search indexer, Xbox, location and other rarely needed services.
- **Performance**: the Ultimate Performance power plan, and visual effects set to best performance while keeping window contents while dragging, font smoothing and the translucent selection rectangle.
- **Customization**: file extensions, hidden files, classic context menu, mouse acceleration, Sticky Keys prompts.

Every tweak shows whether it is ON or OFF and can be reverted from the same switch.

Usage
-----

1. Run `PowerTools.exe` and accept the UAC prompt. Administrator rights are required to remove apps, change services and install the input hooks.
2. Turn tweaks on one by one, or press **Apply recommended** on a page. Some changes ask for an Explorer restart or a reboot.
3. Open **Hotkeys & gestures** to configure window gestures and shortcuts. Close the window; PowerTools keeps running in the tray. Turn on **Start with Windows** in Settings to have it at login without a UAC prompt.

Window gestures and hotkeys
---------------------------

| Action | Default |
| --- | --- |
| Move a window | `Win` + left mouse button, drag anywhere |
| Resize a window | `Win` + right mouse button, drag; the nearest corner or edge follows the cursor |
| Close window | `Win` + `Q` |
| Force-kill the window's process | `Win` + `Shift` + `Q` |
| Pin on top (orange frame) | `Win` + `Alt` + `T` |
| Maximize / restore | `Win` + `Alt` + `Enter` |

The gesture modifiers and all shortcuts can be changed. Other available actions: minimize, window opacity up/down, center window, move to the next monitor, launch a program or open a path or URL. Programs started from a shortcut run without administrator rights.

`Win+L`, `Ctrl+Alt+Del` and other secure-desktop shortcuts cannot be overridden by any program.

Technical overview
------------------

- C++20, Win32 API and Dear ImGui on Direct3D 11. No Electron, Qt or .NET.
- Single executable, static CRT, about 0.9 MB.
- The UI window is created on demand and destroyed on close, together with all GPU resources. Frames are rendered only on input or animation.
- Hotkeys and gestures use low-level keyboard and mouse hooks on a dedicated thread, so they keep working while the UI is busy.
- Tweaks are declarative tables of registry values, service start types and scheduled tasks, applied and reverted from the same data.
- UWP apps are removed per package with PowerShell. Removed names are recorded in `%APPDATA%\PowerTools\removed_apps.txt` so they can be re-registered later.
- Settings are stored in `%APPDATA%\PowerTools\config.ini`.

Status
------

Early alpha. It has been built and run on Windows 11 only. Some tweaks (for example the Start menu policies) may not take effect on every edition or build, and removing system-adjacent apps can break features that depend on them. Create a restore point first if you care about the machine. Pull requests and issues are welcome.

Build
-----

Requires Visual Studio 2022 (or Build Tools) with the C++ workload and CMake 3.20+.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The binary is `build/Release/PowerTools.exe`. Dear ImGui is vendored in `third_party/imgui`.

License
-------

BSD 3-Clause, see `LICENSE`. Dear ImGui is MIT licensed.
