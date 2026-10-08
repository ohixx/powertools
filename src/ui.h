#pragma once
#include <windows.h>

namespace pt::ui {

void Open();       // creates the window (or focuses it)
void Close();      // destroys the window and every GPU resource
bool IsOpen();
bool NeedsFrames();  // true while something is animating or input is pending
void Frame();        // renders one frame if needed
void RequestFrames(int n = 3);
void Wake();         // thread-safe: ask the UI thread to redraw

// Main-thread message used by Wake() (posted to the tray window).
constexpr UINT WM_PT_WAKE = WM_APP + 41;
void SetWakeTarget(HWND h);

}  // namespace pt::ui
