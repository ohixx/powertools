#pragma once
#include "imgui.h"

namespace pt::w {

// Palette -------------------------------------------------------------------
namespace col {
inline ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
const ImU32 Bg = IM_COL32(23, 23, 26, 255);
const ImU32 Sidebar = IM_COL32(18, 18, 21, 255);
const ImU32 Card = IM_COL32(31, 31, 35, 255);
const ImU32 CardHover = IM_COL32(37, 37, 42, 255);
const ImU32 Border = IM_COL32(46, 46, 52, 255);
const ImU32 Text = IM_COL32(236, 238, 244, 255);
const ImU32 Muted = IM_COL32(141, 146, 163, 255);
const ImU32 Accent = IM_COL32(94, 129, 255, 255);
const ImU32 AccentHi = IM_COL32(120, 150, 255, 255);
const ImU32 AccentLo = IM_COL32(94, 129, 255, 40);
const ImU32 Good = IM_COL32(58, 201, 123, 255);
const ImU32 Warn = IM_COL32(245, 175, 60, 255);
const ImU32 Danger = IM_COL32(240, 85, 106, 255);
}  // namespace col

enum class BtnStyle { Primary, Secondary, Danger, Ghost };

void SetupStyle(float dpi);

bool Toggle(const char* id, bool on, bool enabled = true);  // returns true when clicked
bool Button(const char* label, BtnStyle style = BtnStyle::Secondary, ImVec2 size = ImVec2(0, 0), bool enabled = true);
bool Checkbox(const char* id, bool* v, bool enabled = true);
bool NavItem(const char* icon, const char* label, bool selected, float width);
void Pill(const char* text, ImU32 color);
void PillSameLine(const char* text, ImU32 color);
void Spinner(float radius, ImU32 color);
void Title(const char* text);
void Subtitle(const char* text);
void SectionLabel(const char* text);
void MutedText(const char* text);
void HSpace(float px);

// A rounded card row with a title, wrapped description and a toggle on the right.
// Returns true when the toggle was clicked.
struct RowInfo {
    const char* title;
    const char* desc;
    bool on;
    bool enabled;
    const char* tag1;
    ImU32 tag1Color;
    const char* tag2;
    ImU32 tag2Color;
};
bool ToggleRow(const char* id, const RowInfo& r);

// Fonts (set by ui.cpp)
extern ImFont* g_font;
extern ImFont* g_semibold;
extern ImFont* g_icons;

}  // namespace pt::w
