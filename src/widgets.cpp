#include "widgets.h"

#include <algorithm>
#include <cmath>

#include "imgui_internal.h"
#include "i18n.h"
#include "ui.h"

namespace pt::w {

ImFont* g_font = nullptr;
ImFont* g_semibold = nullptr;
ImFont* g_icons = nullptr;

namespace {

float S(float px) { return px * ImGui::GetStyle().FontScaleDpi; }

ImU32 Lerp(ImU32 a, ImU32 b, float t) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t,
                                                 x.w + (y.w - x.w) * t));
}

ImU32 Alpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// Exponentially eased value stored per widget id; keeps requesting frames while it moves.
float Anim(ImGuiID id, float target, float speed = 16.f) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    float d = target - *v;
    if (std::fabs(d) < 0.003f) {
        *v = target;
        return target;
    }
    *v += d * std::min(1.f, ImGui::GetIO().DeltaTime * speed);
    ui::RequestFrames(2);
    return *v;
}

void DrawToggle(ImDrawList* dl, ImVec2 p, float t, bool hovered, bool enabled) {
    ImVec2 sz(S(40), S(22));
    ImU32 off = col::Rgb(48, 48, 55);
    ImU32 track = Lerp(off, col::Good, t);
    if (hovered && enabled) track = Lerp(track, IM_COL32_WHITE, 0.08f);
    float a = enabled ? 1.f : 0.45f;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), Alpha(track, a), sz.y * 0.5f);
    if (t < 0.99f) dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), Alpha(col::Rgb(92, 92, 104), a * (1.f - t)), sz.y * 0.5f, 0, 1.5f);
    float r = S(7);
    float cx = p.x + S(11) + t * S(18);
    ImU32 knob = Lerp(col::Rgb(150, 150, 162), IM_COL32_WHITE, t);
    dl->AddCircleFilled(ImVec2(cx, p.y + sz.y * 0.5f), r, Alpha(knob, a), 20);
}

}  // namespace

void SetupStyle(float dpi) {
    ImGuiStyle s;
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 0;
    s.ChildRounding = 10;
    s.FrameRounding = 8;
    s.PopupRounding = 10;
    s.GrabRounding = 8;
    s.ScrollbarRounding = 10;
    s.ScrollbarSize = 10;
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 8);
    s.WindowPadding = ImVec2(0, 0);
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.ChildBorderSize = 0;
    s.WindowBorderSize = 0;
    s.SeparatorTextBorderSize = 1;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col::V(col::Text);
    c[ImGuiCol_TextDisabled] = col::V(col::Muted);
    c[ImGuiCol_WindowBg] = col::V(col::Bg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = col::V(col::Rgb(30, 32, 41));
    c[ImGuiCol_Border] = col::V(col::Border);
    c[ImGuiCol_FrameBg] = col::V(col::Rgb(36, 39, 50));
    c[ImGuiCol_FrameBgHovered] = col::V(col::Rgb(42, 46, 59));
    c[ImGuiCol_FrameBgActive] = col::V(col::Rgb(48, 52, 67));
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = col::V(col::Rgb(70, 74, 92));
    c[ImGuiCol_ScrollbarGrabHovered] = col::V(col::Rgb(95, 100, 124));
    c[ImGuiCol_ScrollbarGrabActive] = col::V(col::Rgb(120, 126, 154));
    c[ImGuiCol_Header] = col::V(col::AccentLo);
    c[ImGuiCol_HeaderHovered] = col::V(col::Rgb(94, 129, 255, 70));
    c[ImGuiCol_HeaderActive] = col::V(col::Rgb(94, 129, 255, 100));
    c[ImGuiCol_Button] = col::V(col::Rgb(44, 48, 62));
    c[ImGuiCol_ButtonHovered] = col::V(col::Rgb(54, 59, 76));
    c[ImGuiCol_ButtonActive] = col::V(col::Rgb(64, 70, 90));
    c[ImGuiCol_Separator] = col::V(col::Border);
    c[ImGuiCol_TextSelectedBg] = col::V(col::Rgb(94, 129, 255, 90));
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);

    s.ScaleAllSizes(dpi);
    s.FontScaleDpi = dpi;
    ImGui::GetStyle() = s;
}

bool Toggle(const char* id, bool on, bool enabled) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(S(40), S(22));
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##toggle", sz);
    bool hovered = ImGui::IsItemHovered();
    float t = Anim(ImGui::GetID("##a"), on ? 1.f : 0.f);
    DrawToggle(ImGui::GetWindowDrawList(), p, t, hovered, enabled);
    ImGui::PopID();
    return clicked && enabled;
}

bool Button(const char* label, BtnStyle style, ImVec2 size, bool enabled) {
    ImGui::PushID(label);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 pad(S(16), S(9));
    if (size.x <= 0) size.x = ts.x + pad.x * 2;
    if (size.y <= 0) size.y = ts.y + pad.y * 2;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##btn", size);
    bool hovered = ImGui::IsItemHovered() && enabled;
    bool held = ImGui::IsItemActive() && enabled;
    float h = Anim(ImGui::GetID("##h"), hovered ? 1.f : 0.f, 20.f);

    ImU32 base, hot, txt = col::Text;
    switch (style) {
        case BtnStyle::Primary: base = col::Accent; hot = col::AccentHi; txt = IM_COL32_WHITE; break;
        case BtnStyle::Danger: base = col::Rgb(150, 52, 68); hot = col::Danger; txt = IM_COL32_WHITE; break;
        case BtnStyle::Ghost: base = col::Rgb(255, 255, 255, 0); hot = col::Rgb(255, 255, 255, 18); break;
        default: base = col::Rgb(44, 48, 62); hot = col::Rgb(58, 63, 82); break;
    }
    ImU32 bg = Lerp(base, hot, h);
    if (held) bg = Lerp(bg, IM_COL32_BLACK, 0.12f);
    float a = enabled ? 1.f : 0.4f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Alpha(bg, a), S(8));
    dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), Alpha(txt, a), label);
    ImGui::PopID();
    return clicked && enabled;
}

bool Checkbox(const char* id, bool* v, bool enabled) {
    ImGui::PushID(id);
    float sz = S(20);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##cb", ImVec2(sz, sz));
    bool hovered = ImGui::IsItemHovered();
    if (clicked && enabled) *v = !*v;
    float t = Anim(ImGui::GetID("##a"), *v ? 1.f : 0.f, 22.f);
    float a = enabled ? 1.f : 0.4f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill = Lerp(col::Rgb(40, 43, 56), col::Accent, t);
    if (hovered && enabled) fill = Lerp(fill, IM_COL32_WHITE, 0.07f);
    dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), Alpha(fill, a), S(6));
    if (t < 0.99f) dl->AddRect(p, ImVec2(p.x + sz, p.y + sz), Alpha(col::Rgb(80, 85, 108), a * (1.f - t)), S(6), 0, 1.5f);
    if (t > 0.05f) {
        ImVec2 a1(p.x + sz * 0.27f, p.y + sz * 0.52f), a2(p.x + sz * 0.44f, p.y + sz * 0.69f), a3(p.x + sz * 0.75f, p.y + sz * 0.33f);
        dl->PathLineTo(a1);
        dl->PathLineTo(a2);
        dl->PathLineTo(a3);
        dl->PathStroke(Alpha(IM_COL32_WHITE, a * t), 0, S(2.2f));
    }
    ImGui::PopID();
    return clicked && enabled;
}

bool NavItem(const char* icon, const char* label, bool selected, float width) {
    ImGui::PushID(label);
    float h = S(42);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##nav", ImVec2(width, h));
    bool hovered = ImGui::IsItemHovered();
    float hv = Anim(ImGui::GetID("##h"), hovered ? 1.f : 0.f, 20.f);
    float sel = Anim(ImGui::GetID("##s"), selected ? 1.f : 0.f, 18.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(p.x, p.y), b(p.x + width, p.y + h);
    if (sel > 0.01f) dl->AddRectFilled(a, b, Alpha(col::AccentLo, sel), S(9));
    if (hv > 0.01f) dl->AddRectFilled(a, b, Alpha(col::Rgb(255, 255, 255, 14), hv * (1.f - sel)), S(9));
    if (sel > 0.01f) dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.28f), ImVec2(p.x + S(3), p.y + h * 0.72f), Alpha(col::Accent, sel), S(2));
    ImU32 tc = Lerp(col::Muted, col::Text, std::max(sel, hv * 0.7f));
    ImU32 ic = Lerp(col::Muted, col::Accent, sel);
    if (hv > sel) ic = Lerp(ic, col::Text, hv * (1 - sel));

    if (g_icons) {
        ImGui::PushFont(g_icons, 18);
        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(p.x + S(16), p.y + (h - isz.y) * 0.5f), ic, icon);
        ImGui::PopFont();
    }
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + S(46), p.y + (h - ts.y) * 0.5f), tc, label);
    ImGui::PopID();
    return clicked;
}

void Pill(const char* text, ImU32 color) {
    ImGui::PushFont(g_font, 12);
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 pad(S(8), S(3));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), Alpha(color, 0.16f), sz.y * 0.5f);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), color, text);
    ImGui::Dummy(sz);
    ImGui::PopFont();
}

void PillSameLine(const char* text, ImU32 color) {
    ImGui::SameLine(0, S(8));
    Pill(text, color);
}

void Spinner(float radius, ImU32 color) {
    float r = S(radius);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + r, p.y + r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = static_cast<float>(ImGui::GetTime()) * 5.f;
    dl->PathClear();
    for (int i = 0; i <= 20; ++i) {
        float ang = t + (IM_PI * 1.5f) * i / 20.f;
        dl->PathLineTo(ImVec2(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r));
    }
    dl->PathStroke(color, 0, S(2.2f));
    ImGui::Dummy(ImVec2(r * 2, r * 2));
    ui::RequestFrames(2);
}

void Title(const char* text) {
    ImGui::PushFont(g_semibold, 26);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void Subtitle(const char* text) {
    ImGui::PushFont(g_font, 14);
    ImGui::PushStyleColor(ImGuiCol_Text, col::Muted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void SectionLabel(const char* text) {
    ImGui::PushFont(g_semibold, 13);
    ImGui::PushStyleColor(ImGuiCol_Text, col::Muted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void MutedText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col::Muted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void HSpace(float px) { ImGui::Dummy(ImVec2(0, S(px))); }

bool ToggleRow(const char* id, const RowInfo& r) {
    ImGui::PushID(id);
    float w = ImGui::GetContentRegionAvail().x;
    float pad = S(16), toggleW = S(40);
    float labelW = S(52);
    float textW = w - pad * 2 - toggleW - labelW - S(20);

    ImGui::PushFont(g_semibold, 15);
    ImVec2 tsz = ImGui::CalcTextSize(r.title);
    ImGui::PopFont();
    float descH = 0;
    bool hasDesc = r.desc && r.desc[0];
    if (hasDesc) {
        ImGui::PushFont(g_font, 13);
        descH = ImGui::CalcTextSize(r.desc, nullptr, false, textW).y;
        ImGui::PopFont();
    }
    float h = std::max(S(56), pad * 0.85f * 2 + tsz.y + (hasDesc ? S(4) + descH : 0));

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    float hv = Anim(ImGui::GetID("##h"), hovered ? 1.f : 0.f, 18.f);
    float t = Anim(ImGui::GetID("##t"), r.on ? 1.f : 0.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(p.x + w, p.y + h);
    dl->AddRectFilled(p, b, Lerp(col::Card, col::CardHover, hv), S(8));
    dl->AddRect(p, b, Lerp(col::Border, Alpha(col::Good, 0.55f), t), S(8), 0, 1.f);
    if (t > 0.01f)  // active marker on the left edge
        dl->AddRectFilled(ImVec2(p.x, p.y + S(10)), ImVec2(p.x + S(3), b.y - S(10)), Alpha(col::Good, t), S(2));

    float y = p.y + (h - (tsz.y + (hasDesc ? S(4) + descH : 0))) * 0.5f;
    ImGui::PushFont(g_semibold, 15);
    dl->AddText(ImVec2(p.x + pad, y), Lerp(col::Rgb(190, 192, 202), col::Text, t), r.title);
    ImGui::PopFont();

    // ON / OFF label
    {
        const char* lab = r.on ? Tr("ON", "ВКЛ") : Tr("OFF", "ВЫКЛ");
        ImGui::PushFont(g_semibold, 12);
        ImVec2 ls = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(b.x - pad - toggleW - S(12) - ls.x, p.y + (h - ls.y) * 0.5f), Lerp(col::Muted, col::Good, t), lab);
        ImGui::PopFont();
    }

    // tags next to the title
    float tx = p.x + pad + tsz.x + S(10);
    auto tag = [&](const char* text, ImU32 c) {
        if (!text || !text[0]) return;
        ImGui::PushFont(g_font, 11);
        ImVec2 ts = ImGui::CalcTextSize(text);
        ImVec2 pd(S(7), S(2));
        ImVec2 a(tx, y + (tsz.y - (ts.y + pd.y * 2)) * 0.5f + S(1));
        ImVec2 e(a.x + ts.x + pd.x * 2, a.y + ts.y + pd.y * 2);
        dl->AddRectFilled(a, e, Alpha(c, 0.16f), (e.y - a.y) * 0.5f);
        dl->AddText(ImVec2(a.x + pd.x, a.y + pd.y), c, text);
        tx = e.x + S(6);
        ImGui::PopFont();
    };
    tag(r.tag1, r.tag1Color);
    tag(r.tag2, r.tag2Color);

    if (hasDesc) {
        ImGui::PushFont(g_font, 13);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + pad, y + tsz.y + S(4)), col::Muted, r.desc, nullptr, textW);
        ImGui::PopFont();
    }

    DrawToggle(dl, ImVec2(b.x - pad - toggleW, p.y + (h - S(22)) * 0.5f), t, hovered, r.enabled);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, S(2)));
    return clicked && r.enabled;
}

}  // namespace pt::w
