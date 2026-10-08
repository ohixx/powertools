#pragma once
#include "imgui.h"

namespace pt::pages {

void OnOpen();            // refresh system state when the window appears
void Draw(ImVec2 size);   // whole window contents (sidebar + page + status bar)

}  // namespace pt::pages
