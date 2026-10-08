# PowerTools

Windows cleanup and customization tool: removes UWP bloat, ads and background junk, tunes power plan and visual effects, adds Win+drag window move/resize and custom hotkeys. Dear ImGui + Direct3D 11, tiny tray-resident footprint.

Build: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release`. Requires Windows 10/11 and MSVC. Runs elevated (admin manifest).

Status: early alpha. Many tweaks change system settings; use at your own risk. Licensed under BSD-3-Clause.
