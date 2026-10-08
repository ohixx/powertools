#include "pages.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <functional>

#include "appx.h"
#include "config.h"
#include "engine.h"
#include "imgui_internal.h"
#include "tasks.h"
#include "tweaks.h"
#include "ui.h"
#include "util.h"
#include "widgets.h"

namespace pt::pages {
namespace {

using namespace pt::w;

float S(float px) { return px * ImGui::GetStyle().FontScaleDpi; }

// Segoe Fluent Icons code points (see C:\Windows\Fonts\SegoeIcons.ttf).
const char* kIcOverview = "\ue80f";
const char* kIcApps = "\ue71d";
const char* kIcAds = "\ue72e";
const char* kIcShell = "\ue8a1";
const char* kIcBackground = "\ue9d9";
const char* kIcPerf = "\ue945";
const char* kIcMem = "\ue950";
const char* kIcCustom = "\ue771";
const char* kIcKeys = "\ue765";
const char* kIcSettings = "\ue713";

enum class Page { Overview, Apps, Ads, Shell, Background, Memory, Performance, Customize, Hotkeys, Settings };
Page g_page = Page::Overview;

// ---- shared state -----------------------------------------------------------

std::vector<char> g_applied;  // per tweak, cached system state
int g_gen = -1;
std::atomic<bool> g_needExplorer{false};
std::atomic<bool> g_needReboot{false};

void RefreshStates() {
    const auto& all = AllTweaks();
    g_applied.assign(all.size(), 0);
    for (size_t i = 0; i < all.size(); ++i) g_applied[i] = IsApplied(all[i]) ? 1 : 0;
}

void SyncState() {
    int gen = tasks::Generation();
    if (gen != g_gen) {
        g_gen = gen;
        RefreshStates();
    }
}

struct ConfirmDlg {
    std::string title, body, yes;
    std::function<void()> onYes;
    bool open = false;
    bool danger = true;
} g_confirm;

void AskConfirm(const std::string& title, const std::string& body, const std::string& yes, std::function<void()> fn,
                bool danger = true) {
    g_confirm = {title, body, yes, std::move(fn), true, danger};
}

void DrawConfirm() {
    if (g_confirm.open) {
        ImGui::OpenPopup("##confirm");
        g_confirm.open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(26), S(22)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(14));
    if (ImGui::BeginPopupModal("##confirm", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) {
        ImGui::PushFont(g_semibold, 20);
        ImGui::TextUnformatted(g_confirm.title.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::PushTextWrapPos(S(460));
        ImGui::PushStyleColor(ImGuiCol_Text, col::Muted);
        ImGui::TextWrapped("%s", g_confirm.body.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(14)));
        if (Button(g_confirm.yes.c_str(), g_confirm.danger ? BtnStyle::Danger : BtnStyle::Primary)) {
            auto fn = g_confirm.onYes;
            ImGui::CloseCurrentPopup();
            if (fn) fn();
        }
        ImGui::SameLine();
        if (Button(Tr("Cancel", "Отмена"), BtnStyle::Secondary)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// ---- tweak operations -----------------------------------------------------

void StartApply(size_t idx, bool on) {
    const Tweak& t = AllTweaks()[idx];
    g_applied[idx] = on ? 1 : 0;
    std::string label = std::string(on ? Tr("Applying: ", "Применяю: ") : Tr("Reverting: ", "Откатываю: ")) + t.name.get();
    tasks::Run(label, [idx, on](tasks::Reporter&) {
        const Tweak& tw = AllTweaks()[idx];
        std::string err;
        bool ok = Apply(tw, on, err);
        BroadcastSettingChange();
        if (ok) {
            if (tw.restartExplorer) g_needExplorer = true;
            if (tw.reboot) g_needReboot = true;
        }
        tasks::SetResult(ok, ok ? std::string(Tr("Done: ", "Готово: ")) + tw.name.get() : err);
    });
}

void StartBulk(const std::vector<size_t>& idxs, bool on) {
    if (idxs.empty()) {
        tasks::SetResult(true, Tr("Nothing to change.", "Нечего менять."));
        return;
    }
    tasks::Run(on ? Tr("Applying tweaks…", "Применяю твики…") : Tr("Reverting tweaks…", "Откатываю твики…"),
               [idxs, on](tasks::Reporter& rep) {
                   int ok = 0, n = 0;
                   std::string lastErr;
                   for (size_t i : idxs) {
                       const Tweak& tw = AllTweaks()[i];
                       rep.Text(std::string(on ? Tr("Applying: ", "Применяю: ") : Tr("Reverting: ", "Откатываю: ")) + tw.name.get());
                       std::string err;
                       if (Apply(tw, on, err)) {
                           ++ok;
                           if (tw.restartExplorer) g_needExplorer = true;
                           if (tw.reboot) g_needReboot = true;
                       } else {
                           lastErr = err;
                       }
                       rep.Progress(static_cast<float>(++n) / idxs.size());
                   }
                   BroadcastSettingChange();
                   char buf[64];
                   snprintf(buf, sizeof(buf), " %d/%d", ok, static_cast<int>(idxs.size()));
                   std::string msg = std::string(Tr("Tweaks changed:", "Твиков изменено:")) + buf;
                   if (ok < static_cast<int>(idxs.size())) msg += " — " + lastErr;
                   tasks::SetResult(ok == static_cast<int>(idxs.size()), msg);
               });
}

std::vector<size_t> RecommendedPending(bool applied, int catFilter = -1) {
    std::vector<size_t> v;
    const auto& all = AllTweaks();
    for (size_t i = 0; i < all.size(); ++i) {
        if (!all[i].recommended) continue;
        if (catFilter >= 0 && static_cast<int>(all[i].cat) != catFilter) continue;
        if ((g_applied[i] != 0) == applied) v.push_back(i);
    }
    return v;
}

// ---- layout helpers ---------------------------------------------------------

bool BeginCard(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Card);
    ImGui::PushStyleColor(ImGuiCol_Border, col::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
    return ImGui::BeginChild(id, ImVec2(0, 0),
                             ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, S(6)));
}

void PageHeader(const char* title, const char* subtitle) {
    Title(title);
    if (subtitle && subtitle[0]) {
        ImGui::Dummy(ImVec2(0, S(2)));
        Subtitle(subtitle);
    }
    ImGui::Dummy(ImVec2(0, S(14)));
}

// ---- pages ----------------------------------------------------------------

void DrawTweakPage(Cat cat, const char* title, const char* subtitle) {
    PageHeader(title, subtitle);
    const auto& all = AllTweaks();
    bool busy = tasks::Busy();

    auto pending = RecommendedPending(false, static_cast<int>(cat));
    char label[96];
    snprintf(label, sizeof(label), "%s (%d)", Tr("Apply recommended", "Применить рекомендуемые"), static_cast<int>(pending.size()));
    if (Button(label, BtnStyle::Primary, ImVec2(0, 0), !busy && !pending.empty())) StartBulk(pending, true);
    ImGui::Dummy(ImVec2(0, S(8)));

    for (size_t i = 0; i < all.size(); ++i) {
        const Tweak& t = all[i];
        if (t.cat != cat) continue;
        RowInfo r{};
        r.title = t.name.get();
        r.desc = t.desc.get();
        r.on = g_applied[i] != 0;
        r.enabled = !busy;
        r.tag1 = t.recommended ? Tr("recommended", "рекомендуется") : nullptr;
        r.tag1Color = col::Good;
        r.tag2 = t.reboot ? Tr("reboot", "перезагрузка") : nullptr;
        r.tag2Color = col::Warn;
        if (ToggleRow(t.id, r)) StartApply(i, !r.on);
    }
}

void StatCard(const char* id, float width, const char* big, const char* label, ImU32 accent) {
    ImGui::BeginChild(id, ImVec2(width, S(96)), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(g_semibold, 30);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted(big);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    MutedText(label);
    ImGui::EndChild();
}

void DrawOverview() {
    const auto& all = AllTweaks();
    int rec = 0, recOn = 0;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].recommended) {
            ++rec;
            recOn += g_applied[i];
        }
    bool busy = tasks::Busy();

    PageHeader("PowerTools", Tr("Clean Windows from junk, ads and background noise — and make it yours.",
                                "Очистите Windows от мусора, рекламы и фонового шума — и настройте под себя."));

    float avail = ImGui::GetContentRegionAvail().x;
    float gap = S(12);
    float cw = (avail - gap * 2) / 3;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Card);
    ImGui::PushStyleColor(ImGuiCol_Border, col::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(14)));
    char b1[32], b2[32], b3[32];
    snprintf(b1, sizeof(b1), "%d / %d", recOn, rec);
    auto removable = appx::List();
    int nRemovable = 0;
    for (auto& p : removable)
        if (p.reason.empty()) ++nRemovable;
    snprintf(b2, sizeof(b2), "%d", nRemovable);
    snprintf(b3, sizeof(b3), "%d", static_cast<int>(std::count_if(Cfg().bindings.begin(), Cfg().bindings.end(),
                                                                   [](const Binding& b) { return b.enabled; })));
    StatCard("##s1", cw, b1, Tr("recommended tweaks applied", "рекомендуемых твиков применено"), recOn == rec ? col::Good : col::Accent);
    ImGui::SameLine(0, gap);
    StatCard("##s2", cw, appx::Loading() && removable.empty() ? "…" : b2, Tr("removable UWP apps found", "UWP-приложений можно удалить"), col::Warn);
    ImGui::SameLine(0, gap);
    StatCard("##s3", cw, b3, Tr("active keyboard shortcuts", "активных сочетаний клавиш"), col::AccentHi);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, S(10)));

    if (BeginCard("##quick")) {
        ImGui::PushFont(g_semibold, 17);
        ImGui::TextUnformatted(Tr("One-click clean-up", "Очистка в один клик"));
        ImGui::PopFont();
        MutedText(Tr("Applies every recommended tweak: ads and suggestions off, background junk off, Ultimate Performance power plan, lean visual effects. Everything can be reverted.",
                     "Применяет все рекомендуемые твики: реклама и рекомендации, фоновый мусор, схема «Максимальная производительность», облегчённые эффекты. Всё можно откатить."));
        ImGui::Dummy(ImVec2(0, S(6)));
        auto pending = RecommendedPending(false);
        char lbl[96];
        snprintf(lbl, sizeof(lbl), "%s (%d)", Tr("Apply recommended tweaks", "Применить рекомендуемые твики"), static_cast<int>(pending.size()));
        if (Button(lbl, BtnStyle::Primary, ImVec2(0, 0), !busy && !pending.empty())) StartBulk(pending, true);
        ImGui::SameLine();
        if (Button(Tr("Revert recommended", "Откатить рекомендуемые"), BtnStyle::Secondary, ImVec2(0, 0), !busy && recOn > 0))
            AskConfirm(Tr("Revert recommended tweaks?", "Откатить рекомендуемые твики?"),
                       Tr("All recommended tweaks that are currently on will be set back to Windows defaults.",
                          "Все включённые рекомендуемые твики будут возвращены к настройкам Windows по умолчанию."),
                       Tr("Revert", "Откатить"), [] { StartBulk(RecommendedPending(true), false); });
        EndCard();
    }
    if (BeginCard("##apps")) {
        ImGui::PushFont(g_semibold, 17);
        ImGui::TextUnformatted(Tr("Remove built-in apps", "Удаление встроенных приложений"));
        ImGui::PopFont();
        MutedText(Tr("Removes every UWP app except Terminal, Settings and the system pieces Windows needs. You can restore any of them later.",
                     "Удаляет все UWP-приложения, кроме Терминала, Параметров и системных компонентов. Любое можно вернуть."));
        ImGui::Dummy(ImVec2(0, S(6)));
        if (Button(Tr("Review & remove…", "Выбрать и удалить…"), BtnStyle::Secondary)) g_page = Page::Apps;
        EndCard();
    }
    if (BeginCard("##hk")) {
        ImGui::PushFont(g_semibold, 17);
        ImGui::TextUnformatted(Tr("Window gestures & hotkeys", "Жесты окон и горячие клавиши"));
        ImGui::PopFont();
        MutedText(Tr("Win + drag moves windows, Win + right-drag resizes them, plus your own shortcuts. They keep working with this window closed.",
                     "Win + перетаскивание двигает окна, Win + ПКМ меняет размер, плюс ваши сочетания. Работают и при закрытом окне."));
        ImGui::Dummy(ImVec2(0, S(6)));
        if (Button(Tr("Configure…", "Настроить…"), BtnStyle::Secondary)) g_page = Page::Hotkeys;
        EndCard();
    }
}

// ---- apps -------------------------------------------------------------------

std::vector<appx::Pkg> g_pkgs;
int g_pkgVer = -1;
char g_filter[96] = "";
bool g_unlockProtected = false;
bool g_deprovisionTmp = false;
bool g_showRestore = false;
bool g_appsRequested = false;

void SyncPkgs() {
    int v = appx::Version();
    if (v == g_pkgVer) return;
    g_pkgVer = v;
    auto fresh = appx::List();
    const auto& keep = Cfg().keepApps;
    for (auto& p : fresh) {
        if (!p.reason.empty()) { p.selected = false; continue; }
        p.selected = std::find(keep.begin(), keep.end(), p.name) == keep.end();
    }
    g_pkgs = std::move(fresh);
}

void SaveKeepList() {
    auto& keep = Cfg().keepApps;
    keep.clear();
    for (auto& p : g_pkgs)
        if (p.reason.empty() && !p.selected) keep.push_back(p.name);
    SaveConfig();
}

bool ContainsNoCase(const std::string& hay, const char* needle) {
    if (!needle[0]) return true;
    std::string h = hay, n = needle;
    for (auto& c : h) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    for (auto& c : n) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

void DrawRestoreModal() {
    if (g_showRestore) {
        ImGui::OpenPopup("##restore");
        g_showRestore = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(560), S(460)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(14));
    if (ImGui::BeginPopupModal("##restore", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) {
        bool busy = tasks::Busy();
        ImGui::PushFont(g_semibold, 20);
        ImGui::TextUnformatted(Tr("Restore apps", "Восстановление приложений"));
        ImGui::PopFont();
        MutedText(Tr("Apps you removed with PowerTools. Restore re-registers the package from the system store; if its files are gone, reinstall it from Microsoft Store.",
                     "Приложения, удалённые через PowerTools. Восстановление регистрирует пакет из хранилища системы; если файлов нет — установите из Microsoft Store."));
        ImGui::Dummy(ImVec2(0, S(8)));
        auto removed = appx::LoadRemoved();
        if (Button(Tr("Restore all listed", "Вернуть все из списка"), BtnStyle::Primary, ImVec2(0, 0), !busy && !removed.empty())) {
            appx::RestoreAsync(removed);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (Button(Tr("Re-register all system apps", "Перерегистрировать все системные"), BtnStyle::Secondary, ImVec2(0, 0), !busy)) {
            appx::RestoreAllSystemAsync();
            ImGui::CloseCurrentPopup();
        }
        ImGui::Dummy(ImVec2(0, S(8)));
        float footer = S(46);
        ImGui::BeginChild("##rlist", ImVec2(0, -footer));
        if (removed.empty()) MutedText(Tr("Nothing removed yet.", "Пока ничего не удалено."));
        for (size_t i = 0; i < removed.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(removed[i].name.c_str());
            float bw = S(96);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - bw + ImGui::GetCursorPosX());
            if (Button(Tr("Restore", "Вернуть"), BtnStyle::Secondary, ImVec2(bw, 0), !busy)) {
                appx::RestoreAsync({removed[i]});
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (Button(Tr("Close", "Закрыть"), BtnStyle::Secondary)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void DrawApps() {
    if (!g_appsRequested) {
        g_appsRequested = true;
        appx::RefreshAsync();
    }
    SyncPkgs();
    bool busy = tasks::Busy();
    PageHeader(Tr("UWP apps", "UWP-приложения"),
               Tr("Everything installed from the Store / bundled with Windows. Windows Terminal and Settings are always kept.",
                  "Всё, что установлено из Store или идёт с Windows. Windows Terminal и Параметры всегда остаются."));

    int sel = 0;
    for (auto& p : g_pkgs)
        if (p.selected) ++sel;

    ImGui::SetNextItemWidth(S(220));
    ImGui::InputTextWithHint("##filter", Tr("Search…", "Поиск…"), g_filter, sizeof(g_filter));
    ImGui::SameLine();
    if (Button(Tr("Refresh", "Обновить"), BtnStyle::Secondary, ImVec2(0, 0), !busy && !appx::Loading())) appx::RefreshAsync();
    ImGui::SameLine();
    if (Button(Tr("Select all", "Выбрать все"), BtnStyle::Ghost)) {
        for (auto& p : g_pkgs) p.selected = p.reason.empty() || g_unlockProtected;
        SaveKeepList();
    }
    ImGui::SameLine();
    if (Button(Tr("None", "Снять"), BtnStyle::Ghost)) {
        for (auto& p : g_pkgs) p.selected = false;
        SaveKeepList();
    }

    ImGui::Dummy(ImVec2(0, S(2)));
    {
        bool unlock = g_unlockProtected;
        if (Checkbox("unlock", &unlock)) {
            g_unlockProtected = unlock;
            if (!unlock)
                for (auto& p : g_pkgs)
                    if (!p.reason.empty()) p.selected = false;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Tr("Allow removing protected apps (Store, Security, drivers…)",
                                  "Разрешить удалять защищённые (Store, Безопасность, драйверы…)"));
        ImGui::SameLine(0, S(24));
        bool dep = Cfg().deprovision;
        if (Checkbox("deprov", &dep)) {
            Cfg().deprovision = dep;
            SaveConfig();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(Tr("Also remove from system image", "Также убрать из образа системы"));
    }
    ImGui::Dummy(ImVec2(0, S(6)));

    char rm[96];
    snprintf(rm, sizeof(rm), "%s (%d)", Tr("Remove selected", "Удалить выбранные"), sel);
    if (Button(rm, BtnStyle::Danger, ImVec2(0, 0), !busy && sel > 0)) {
        std::vector<appx::Pkg> chosen;
        for (auto& p : g_pkgs)
            if (p.selected) chosen.push_back(p);
        bool dp = Cfg().deprovision;
        AskConfirm(Tr("Remove selected apps?", "Удалить выбранные приложения?"),
                   dp ? Tr("The apps will be removed for all users and from the system image. Restoring them will then require the Microsoft Store.",
                           "Приложения будут удалены для всех пользователей и из образа системы. Вернуть их можно будет только через Microsoft Store.")
                      : Tr("The apps will be removed for all users. Files stay staged in the system, so you can restore them from PowerTools later.",
                           "Приложения будут удалены для всех пользователей. Файлы останутся в системе, поэтому их можно вернуть из PowerTools."),
                   Tr("Remove", "Удалить"), [chosen, dp] { appx::RemoveAsync(chosen, dp); });
    }
    ImGui::SameLine();
    if (Button(Tr("Restore…", "Восстановить…"), BtnStyle::Secondary)) g_showRestore = true;
    ImGui::Dummy(ImVec2(0, S(8)));

    if (appx::Loading() && g_pkgs.empty()) {
        Spinner(10, col::Accent);
        ImGui::SameLine();
        MutedText(Tr("Reading installed apps…", "Читаю список приложений…"));
        return;
    }

    ImGui::BeginChild("##pkgs", ImVec2(0, 0));
    for (size_t i = 0; i < g_pkgs.size(); ++i) {
        auto& p = g_pkgs[i];
        if (!ContainsNoCase(p.name, g_filter)) continue;
        bool locked = !p.reason.empty() && !g_unlockProtected;
        ImGui::PushID(static_cast<int>(i));
        float h = S(44);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x - S(6);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h - S(4)), col::Card, S(9));
        ImGui::SetCursorScreenPos(ImVec2(pos.x + S(12), pos.y + (h - S(4) - S(20)) * 0.5f));
        bool v = p.selected;
        if (Checkbox("##sel", &v, !locked)) {
            p.selected = v;
            SaveKeepList();
        }
        ImGui::SetCursorScreenPos(ImVec2(pos.x + S(44), pos.y + (h - S(4) - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, locked ? col::Muted : col::Text);
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopStyleColor();
        if (!p.reason.empty()) {
            ImGui::SameLine(0, S(10));
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, pos.y + S(7)));
            Pill(p.reason.c_str(), col::Warn);
        }
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---- hotkeys ----------------------------------------------------------------

int g_captureIdx = -1;

bool Chip(const char* label, bool on) { return Button(label, on ? BtnStyle::Primary : BtnStyle::Secondary, ImVec2(S(64), 0)); }

bool ModChips(uint32_t* mods, const char* id) {
    ImGui::PushID(id);
    bool changed = false;
    struct M { const char* n; uint32_t bit; } ms[] = {{"Win", Mod::Win}, {"Ctrl", Mod::Ctrl}, {"Alt", Mod::Alt}, {"Shift", Mod::Shift}};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0, S(6));
        if (Chip(ms[i].n, (*mods & ms[i].bit) != 0)) {
            uint32_t m = *mods ^ ms[i].bit;
            if (m != 0) {  // a gesture needs at least one modifier
                *mods = m;
                changed = true;
            }
        }
    }
    ImGui::PopID();
    return changed;
}

bool GestureCard(const char* id, const char* title, const char* desc, bool* enabled, uint32_t* mods, const char* suffix) {
    bool changed = false;
    if (BeginCard(id)) {
        float x0 = ImGui::GetCursorPosX();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(g_semibold, 16);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::SetCursorPosX(x0 + w - S(40));  // right-aligned toggle
        if (Toggle(id, *enabled)) {
            *enabled = !*enabled;
            changed = true;
        }
        MutedText(desc);
        ImGui::Dummy(ImVec2(0, S(4)));
        if (ModChips(mods, "mods")) changed = true;
        ImGui::SameLine(0, S(10));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(suffix);
        EndCard();
    }
    return changed;
}

bool BrowseFile(std::wstring& out) {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = buf;
    ofn.nMaxFile = ARRAYSIZE(buf);
    ofn.lpstrFilter = L"Programs\0*.exe;*.bat;*.cmd;*.lnk\0All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT;
    if (!GetOpenFileNameW(&ofn)) return false;
    out = std::wstring(L"\"") + buf + L"\"";
    return true;
}

void DrawHotkeys() {
    Config& c = Cfg();
    bool changed = false;
    PageHeader(Tr("Hotkeys & gestures", "Горячие клавиши и жесты"),
               Tr("Work in the background even when this window is closed (PowerTools stays in the tray).",
                  "Работают в фоне даже при закрытом окне (PowerTools остаётся в трее)."));

    // capture result
    uint32_t cm, cv;
    if (engine::PollCapture(cm, cv) && g_captureIdx >= 0 && g_captureIdx < static_cast<int>(c.bindings.size())) {
        c.bindings[g_captureIdx].mods = cm;
        c.bindings[g_captureIdx].vk = cv;
        g_captureIdx = -1;
        changed = true;
    }
    if (g_captureIdx >= 0 && !engine::IsCapturing()) g_captureIdx = -1;  // cancelled with Esc
    if (g_captureIdx >= 0) ui::RequestFrames(2);

    {
        bool on = c.hotkeysEnabled;
        RowInfo r{Tr("Enable hotkeys & gestures", "Включить хоткеи и жесты"),
                  Tr("Master switch for everything on this page.", "Главный выключатель всего на этой странице."), on, true,
                  nullptr, 0, nullptr, 0};
        if (ToggleRow("master", r)) { c.hotkeysEnabled = !on; changed = true; }
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    changed |= GestureCard("drag", Tr("Move window", "Перемещение окна"),
                           Tr("Hold the modifier and drag anywhere on a window with the left mouse button.",
                              "Удерживайте модификатор и тяните за любое место окна левой кнопкой мыши."),
                           &c.dragEnabled, &c.dragMods, "+  LMB");
    changed |= GestureCard("resize", Tr("Resize window", "Изменение размера окна"),
                           Tr("Hold the modifier and drag with the right mouse button. The grabbed corner or edge follows the cursor.",
                              "Удерживайте модификатор и тяните правой кнопкой. Захваченный угол или край следует за курсором."),
                           &c.resizeEnabled, &c.resizeMods, "+  RMB");

    ImGui::Dummy(ImVec2(0, S(6)));
    SectionLabel(Tr("KEYBOARD SHORTCUTS", "СОЧЕТАНИЯ КЛАВИШ"));
    ImGui::Dummy(ImVec2(0, S(4)));

    int del = -1;
    for (size_t i = 0; i < c.bindings.size(); ++i) {
        Binding& b = c.bindings[i];
        ImGui::PushID(static_cast<int>(i));
        float rowH = S(56);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), col::Card, S(10));
        dl->AddRect(p, ImVec2(p.x + w, p.y + rowH), col::Border, S(10));

        float cy = p.y + (rowH - S(22)) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(14), cy));
        if (Toggle("en", b.enabled)) { b.enabled = !b.enabled; changed = true; }

        ImGui::SetCursorScreenPos(ImVec2(p.x + S(68), p.y + (rowH - S(36)) * 0.5f));
        bool capt = g_captureIdx == static_cast<int>(i);
        std::string combo = capt ? Tr("Press keys…  (Esc – cancel)", "Нажмите сочетание… (Esc – отмена)") : engine::ComboName(b.mods, b.vk);
        if (Button((combo + "###combo").c_str(), capt ? BtnStyle::Primary : BtnStyle::Secondary, ImVec2(S(210), S(36)))) {
            if (capt) { engine::CancelCapture(); g_captureIdx = -1; }
            else { g_captureIdx = static_cast<int>(i); engine::BeginCapture(); }
        }

        ImGui::SetCursorScreenPos(ImVec2(p.x + S(292), p.y + (rowH - S(36)) * 0.5f));
        ImGui::SetNextItemWidth(S(220));
        if (ImGui::BeginCombo("##act", ActionName(b.action))) {
            for (int a = 0; a < static_cast<int>(Action::Count); ++a) {
                bool s = static_cast<int>(b.action) == a;
                if (ImGui::Selectable(ActionName(static_cast<Action>(a)), s)) {
                    b.action = static_cast<Action>(a);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        float xRight = p.x + w - S(14) - S(36);
        if (ActionNeedsParam(b.action)) {
            float bx = p.x + S(524);
            float iw = xRight - bx - S(48) - S(8);
            ImGui::SetCursorScreenPos(ImVec2(bx, p.y + (rowH - S(36)) * 0.5f));
            ImGui::SetNextItemWidth(std::max(S(80), iw));
            char buf[512];
            strncpy_s(buf, Narrow(b.param).c_str(), _TRUNCATE);
            if (ImGui::InputTextWithHint("##param", Tr("C:\\path\\app.exe args  or  https://…", "C:\\путь\\app.exe аргументы  или  https://…"), buf, sizeof(buf))) {
                b.param = Widen(buf);
                changed = true;
            }
            ImGui::SameLine(0, S(8));
            if (Button("...", BtnStyle::Secondary, ImVec2(S(48), S(36)))) {
                std::wstring f;
                if (BrowseFile(f)) { b.param = f; changed = true; }
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(xRight, p.y + (rowH - S(36)) * 0.5f));
        if (Button("\xC3\x97", BtnStyle::Ghost, ImVec2(S(36), S(36)))) del = static_cast<int>(i);  // ×

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + S(6)));
        ImGui::PopID();
    }
    if (del >= 0) {
        c.bindings.erase(c.bindings.begin() + del);
        if (g_captureIdx >= 0) { engine::CancelCapture(); g_captureIdx = -1; }
        changed = true;
    }
    if (Button(Tr("+ Add shortcut", "+ Добавить сочетание"), BtnStyle::Secondary)) {
        Binding b;
        b.enabled = false;
        b.mods = Mod::Win;
        b.vk = 0;
        c.bindings.push_back(b);
        g_captureIdx = static_cast<int>(c.bindings.size()) - 1;
        engine::BeginCapture();
        changed = true;
    }
    ImGui::Dummy(ImVec2(0, S(10)));
    MutedText(Tr("Win+L, Ctrl+Alt+Del and other secure-desktop shortcuts cannot be overridden by any program. Programs started from a shortcut run at normal (non-admin) rights.",
                 "Win+L, Ctrl+Alt+Del и прочие защищённые сочетания перехватить нельзя. Программы из сочетаний запускаются с обычными (не админ) правами."));

    if (changed) {
        // A freshly added/edited binding becomes active once it has a key.
        for (auto& b : c.bindings)
            if (b.vk == 0) b.enabled = false;
        SaveConfig();
        PushEngineSettings();
    }
}

// ---- settings -----------------------------------------------------------------

int g_autostart = -1;
size_t g_memBytes = 0;
double g_memAt = 0;

void DrawSettings() {
    Config& c = Cfg();
    PageHeader(Tr("Settings", "Настройки"), "");
    if (g_autostart < 0) g_autostart = AutostartEnabled() ? 1 : 0;

    if (BeginCard("##lang")) {
        ImGui::PushFont(g_semibold, 16);
        ImGui::TextUnformatted(Tr("Language", "Язык"));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(4)));
        if (Button("English", c.lang == Lang::En ? BtnStyle::Primary : BtnStyle::Secondary, ImVec2(S(120), 0))) {
            c.lang = Lang::En; g_lang = Lang::En; SaveConfig();
        }
        ImGui::SameLine();
        if (Button("Русский", c.lang == Lang::Ru ? BtnStyle::Primary : BtnStyle::Secondary, ImVec2(S(120), 0))) {
            c.lang = Lang::Ru; g_lang = Lang::Ru; SaveConfig();
        }
        EndCard();
    }
    {
        RowInfo r{Tr("Start with Windows", "Запускать вместе с Windows"),
                  Tr("Starts minimized to the tray at logon, without a UAC prompt, so hotkeys are always available.",
                     "Запуск свёрнутым в трей при входе, без запроса UAC — хоткеи всегда доступны."),
                  g_autostart == 1, !tasks::Busy(), nullptr, 0, nullptr, 0};
        if (ToggleRow("autostart", r)) {
            bool want = g_autostart != 1;
            if (SetAutostart(want)) g_autostart = want ? 1 : 0;
            else tasks::SetResult(false, Tr("Could not change the startup task.", "Не удалось изменить задачу автозапуска."));
        }
    }
    ImGui::Dummy(ImVec2(0, S(4)));

    double now = ImGui::GetTime();
    if (now - g_memAt > 0.5) { g_memBytes = WorkingSetBytes(); g_memAt = now; }
    if (BeginCard("##about")) {
        ImGui::PushFont(g_semibold, 16);
        ImGui::TextUnformatted("PowerTools " PT_VERSION);
        ImGui::PopFont();
        char mem[96];
        snprintf(mem, sizeof(mem), "%s: %.1f MB", Tr("Memory in use right now", "Сейчас занято памяти"), g_memBytes / 1048576.0);
        MutedText(mem);
        MutedText(Tr("BSD 3-Clause License  •  (c) 2026 ohixx", "Лицензия BSD 3-Clause  •  (c) 2026 ohixx"));
        ImGui::Dummy(ImVec2(0, S(6)));
        if (Button("GitHub", BtnStyle::Secondary))
            ShellExecuteW(nullptr, L"open", L"https://github.com/ohixx/powertools", nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (Button(Tr("Open config folder", "Открыть папку настроек"), BtnStyle::Secondary))
            ShellExecuteW(nullptr, L"open", DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (Button(Tr("Quit PowerTools", "Выйти из PowerTools"), BtnStyle::Danger)) PostQuitMessage(0);
        EndCard();
    }
    ui::RequestFrames(2);  // keep the memory readout fresh
}

// ---- chrome -------------------------------------------------------------------

struct NavDef {
    Page page;
    const char* icon;
    const char* (*label)();
};

const NavDef kNav[] = {
    {Page::Overview, kIcOverview, [] { return Tr("Overview", "Обзор"); }},
    {Page::Apps, kIcApps, [] { return Tr("UWP apps", "UWP-приложения"); }},
    {Page::Ads, kIcAds, [] { return CatName(Cat::Ads); }},
    {Page::Shell, kIcShell, [] { return CatName(Cat::Shell); }},
    {Page::Background, kIcBackground, [] { return CatName(Cat::Background); }},
    {Page::Memory, kIcMem, [] { return CatName(Cat::Memory); }},
    {Page::Performance, kIcPerf, [] { return CatName(Cat::Performance); }},
    {Page::Customize, kIcCustom, [] { return CatName(Cat::Customize); }},
    {Page::Hotkeys, kIcKeys, [] { return Tr("Hotkeys & gestures", "Хоткеи и жесты"); }},
    {Page::Settings, kIcSettings, [] { return Tr("Settings", "Настройки"); }},
};

void DrawSidebar(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Sidebar);
    ImGui::BeginChild("##side", ImVec2(width, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // brand
    ImVec2 p = ImGui::GetCursorScreenPos();
    float pad = S(14);
    ImVec2 lp(p.x + pad + S(6), p.y + S(22));
    float ls = S(34);
    dl->AddRectFilled(lp, ImVec2(lp.x + ls, lp.y + ls), col::Accent, S(10));
    if (g_icons) {
        ImGui::PushFont(g_icons, 20);
        const char* bolt = "\ue945";
        ImVec2 bs = ImGui::CalcTextSize(bolt);
        dl->AddText(ImVec2(lp.x + (ls - bs.x) * 0.5f, lp.y + (ls - bs.y) * 0.5f), IM_COL32_WHITE, bolt);
        ImGui::PopFont();
    }
    ImGui::PushFont(g_semibold, 19);
    dl->AddText(ImVec2(lp.x + ls + S(12), lp.y + S(1)), col::Text, "PowerTools");
    ImGui::PopFont();
    ImGui::PushFont(g_font, 12);
    dl->AddText(ImVec2(lp.x + ls + S(12), lp.y + S(20)), col::Muted, "v" PT_VERSION);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + S(82)));
    ImGui::BeginGroup();
    for (const auto& n : kNav)
        if (NavItem(n.icon, n.label(), g_page == n.page, width - pad * 2)) g_page = n.page;
    ImGui::EndGroup();

    // admin badge at the bottom
    if (!IsElevated()) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad + S(6), p.y + height - S(40)));
        Pill(Tr("not elevated", "нет прав админа"), col::Danger);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DrawStatusBar(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::Sidebar);
    ImGui::BeginChild("##status", ImVec2(width, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), col::Border);
    ImGui::SetCursorScreenPos(ImVec2(p.x + S(32), p.y + (height - ImGui::GetTextLineHeight()) * 0.5f));
    if (tasks::Busy()) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(32), p.y + (height - S(20)) * 0.5f));
        Spinner(10, col::Accent);
        ImGui::SameLine(0, S(10));
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + (height - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(tasks::StatusText().c_str());
        float prog = tasks::ProgressValue();
        if (prog >= 0) {
            ImGui::SameLine(0, S(14));
            ImVec2 b = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(b.x, b.y + S(7)), ImVec2(b.x + S(160), b.y + S(11)), col::Border, S(2));
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(b.x, b.y + S(7)), ImVec2(b.x + S(160) * prog, b.y + S(11)), col::Accent, S(2));
        }
    } else {
        bool ok = true;
        std::string r = tasks::LastResult(&ok);
        if (!r.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ok ? col::Good : col::Danger);
            ImGui::TextUnformatted(ok ? "\xE2\x9C\x93" : "!");  // ✓
            ImGui::PopStyleColor();
            ImGui::SameLine(0, S(8));
            ImGui::TextUnformatted(r.c_str());
        }
    }
    // right side actions
    float bx = p.x + width - S(32);
    if (g_needExplorer || g_needReboot) {
        float bw = S(190);
        if (g_needExplorer) {
            bx -= bw;
            ImGui::SetCursorScreenPos(ImVec2(bx, p.y + (height - S(34)) * 0.5f));
            if (Button(Tr("Restart Explorer", "Перезапустить Проводник"), BtnStyle::Primary, ImVec2(bw, S(34)), !tasks::Busy())) {
                RestartExplorer();
                g_needExplorer = false;
            }
        }
        if (g_needReboot) {
            bx -= S(150) + S(8);
            ImGui::SetCursorScreenPos(ImVec2(bx + (g_needExplorer ? 0 : bw), p.y + (height - S(24)) * 0.5f));
            Pill(Tr("reboot recommended", "нужна перезагрузка"), col::Warn);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace

void OnOpen() {
    g_gen = -1;
    SyncState();
    g_autostart = -1;
    g_pkgVer = -1;
    if (!appx::Loading() && appx::List().empty()) appx::RefreshAsync();
    g_appsRequested = true;
}

void Draw(ImVec2 size) {
    SyncState();
    float sideW = S(250), statusH = S(52);

    DrawSidebar(sideW, size.y);
    ImGui::SameLine(0, 0);
    ImGui::BeginGroup();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(32), S(28)));
    ImGui::BeginChild("##main", ImVec2(size.x - sideW, size.y - statusH), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    bool scrollPage = g_page != Page::Apps;
    if (scrollPage) {
        // whole page scrolls
        ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    }
    switch (g_page) {
        case Page::Overview: DrawOverview(); break;
        case Page::Apps: DrawApps(); break;
        case Page::Ads:
            DrawTweakPage(Cat::Ads, CatName(Cat::Ads),
                          Tr("Stop Windows from pushing apps, tips and ads at you.", "Хватит подсовывать приложения, советы и рекламу."));
            break;
        case Page::Shell:
            DrawTweakPage(Cat::Shell, CatName(Cat::Shell),
                          Tr("Clean up Start, search and the taskbar.", "Наведите порядок в Пуске, поиске и панели задач."));
            break;
        case Page::Background:
            DrawTweakPage(Cat::Background, CatName(Cat::Background),
                          Tr("Telemetry, background apps and services that eat RAM and CPU for nothing.",
                             "Телеметрия, фоновые приложения и службы, которые зря едят ОЗУ и процессор."));
            break;
        case Page::Memory:
            DrawTweakPage(Cat::Memory, CatName(Cat::Memory),
                          Tr("Stop services and apps that sit in RAM for no reason.", "Отключите службы и программы, которые зря занимают ОЗУ."));
            break;
        case Page::Performance:
            DrawTweakPage(Cat::Performance, CatName(Cat::Performance),
                          Tr("Power plan and visual effects tuned for maximum speed.", "Схема питания и эффекты, настроенные на скорость."));
            break;
        case Page::Customize:
            DrawTweakPage(Cat::Customize, CatName(Cat::Customize), Tr("Small things that make Windows nicer to use.", "Мелочи, которые делают Windows приятнее."));
            break;
        case Page::Hotkeys: DrawHotkeys(); break;
        case Page::Settings: DrawSettings(); break;
    }
    if (scrollPage) ImGui::EndChild();
    ImGui::EndChild();

    DrawStatusBar(size.x - sideW, statusH);
    ImGui::EndGroup();

    DrawConfirm();
    DrawRestoreModal();
}

}  // namespace pt::pages
