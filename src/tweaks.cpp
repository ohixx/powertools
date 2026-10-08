#include "tweaks.h"

#include <powrprof.h>

#include <cctype>

#include "config.h"
#include "util.h"

namespace pt {
namespace {

const HKEY kCU = HKEY_CURRENT_USER;
const HKEY kLM = HKEY_LOCAL_MACHINE;

RegOp D(HKEY r, const wchar_t* k, const wchar_t* n, DWORD on, DWORD off) {
    RegOp o; o.kind = RegOp::Dword; o.root = r; o.key = k; o.name = n; o.on = on; o.off = off; return o;
}
// Policy-style value: present (== on) when applied, removed when reverted.
RegOp P(HKEY r, const wchar_t* k, const wchar_t* n, DWORD on) {
    RegOp o; o.kind = RegOp::DwordDeleteOff; o.root = r; o.key = k; o.name = n; o.on = on; return o;
}
RegOp S(HKEY r, const wchar_t* k, const wchar_t* n, const wchar_t* on, const wchar_t* off) {
    RegOp o; o.kind = RegOp::String; o.root = r; o.key = k; o.name = n; o.sOn = on; o.sOff = off; return o;
}
RegOp K(HKEY r, const wchar_t* k) {
    RegOp o; o.kind = RegOp::KeyDefault; o.root = r; o.key = k; return o;
}

#define CDM L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager"
#define ADV L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"
#define SRCH L"Software\\Microsoft\\Windows\\CurrentVersion\\Search"
#define POLEXP L"Software\\Policies\\Microsoft\\Windows\\Explorer"

// ---- power plan ------------------------------------------------------------

const wchar_t* kUltimateTemplate = L"e9a42b02-d5df-448d-aa00-03f14749eb61";
const char* kBalanced = "381b4222-f694-41f0-9685-ff5bb260df2e";

std::string GuidStr(const GUID& g) {
    char b[48];
    snprintf(b, sizeof(b), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g.Data1, g.Data2, g.Data3, g.Data4[0],
             g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return b;
}

std::string ActiveScheme(GUID* out = nullptr) {
    GUID* g = nullptr;
    if (PowerGetActiveScheme(nullptr, &g) != ERROR_SUCCESS || !g) return {};
    std::string s = GuidStr(*g);
    if (out) *out = *g;
    LocalFree(g);
    return s;
}

std::string FindGuid(const std::string& text) {
    auto hex = [](char c) { return isxdigit(static_cast<unsigned char>(c)) != 0; };
    for (size_t i = 0; i + 36 <= text.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < 36 && ok; ++j) {
            char c = text[i + j];
            ok = (j == 8 || j == 13 || j == 18 || j == 23) ? c == '-' : hex(c);
        }
        if (ok) {
            std::string g = text.substr(i, 36);
            for (auto& c : g) c = static_cast<char>(tolower(c));
            return g;
        }
    }
    return {};
}

bool PowerState() {
    GUID g{};
    std::string active = ActiveScheme(&g);
    if (active.empty()) return false;
    if (!Cfg().ultimateGuid.empty() && active == Cfg().ultimateGuid) return true;
    wchar_t name[256] = {};
    DWORD sz = sizeof(name);
    if (PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, reinterpret_cast<PUCHAR>(name), &sz) == ERROR_SUCCESS) {
        std::wstring n = name;
        return n.find(L"Ultimate") != std::wstring::npos || n.find(L"Максимальная") != std::wstring::npos;
    }
    return false;
}

bool PowerApply(bool on, std::string& err) {
    Config& c = Cfg();
    if (on) {
        std::string active = ActiveScheme();
        std::string ult = c.ultimateGuid;
        ProcResult list = RunHidden(L"powercfg.exe /list");
        if (ult.empty() || list.output.find(ult) == std::string::npos) {
            ProcResult r = RunHidden(std::wstring(L"powercfg.exe -duplicatescheme ") + kUltimateTemplate);
            ult = FindGuid(r.output);
            if (ult.empty()) {
                err = "powercfg: " + Trim(r.output);
                return false;
            }
            c.ultimateGuid = ult;
        }
        if (active != ult && !active.empty()) c.previousPowerGuid = active;
        ProcResult r = RunHidden(L"powercfg.exe /setactive " + Widen(ult));
        SaveConfig();
        if (r.exitCode != 0) { err = "powercfg: " + Trim(r.output); return false; }
        return true;
    }
    std::string target = c.previousPowerGuid;
    ProcResult list = RunHidden(L"powercfg.exe /list");
    if (target.empty() || target == c.ultimateGuid || list.output.find(target) == std::string::npos) target = kBalanced;
    ProcResult r = RunHidden(L"powercfg.exe /setactive " + Widen(target));
    if (r.exitCode != 0) { err = "powercfg: " + Trim(r.output); return false; }
    return true;
}

// ---- visual effects --------------------------------------------------------

void SpiBool(UINT action, BOOL v) {
    SystemParametersInfoW(action, 0, reinterpret_cast<PVOID>(static_cast<INT_PTR>(v)),
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

bool VisualApply(bool on, std::string&) {
    const BOOL eff = on ? FALSE : TRUE;  // "on" = best performance = effects off
    ANIMATIONINFO ai{sizeof(ai), eff ? 1 : 0};
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    SpiBool(SPI_SETCLIENTAREAANIMATION, eff);
    SpiBool(SPI_SETCOMBOBOXANIMATION, eff);
    SpiBool(SPI_SETLISTBOXSMOOTHSCROLLING, eff);
    SpiBool(SPI_SETMENUANIMATION, eff);
    SpiBool(SPI_SETTOOLTIPANIMATION, eff);
    SpiBool(SPI_SETSELECTIONFADE, eff);
    SpiBool(SPI_SETCURSORSHADOW, eff);
    SpiBool(SPI_SETDROPSHADOW, eff);
    // The three effects we always keep:
    SpiBool(SPI_SETDRAGFULLWINDOWS, TRUE);
    SpiBool(SPI_SETFONTSMOOTHING, TRUE);
    SystemParametersInfoW(SPI_SETFONTSMOOTHINGTYPE, 0, reinterpret_cast<PVOID>(static_cast<INT_PTR>(FE_FONTSMOOTHINGCLEARTYPE)),
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    return true;
}

bool VisualState() {
    BOOL v = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &v, 0);
    BOOL m = TRUE;
    SystemParametersInfoW(SPI_GETMENUANIMATION, 0, &m, 0);
    return !v && !m;
}

std::vector<Tweak> Build() {
    std::vector<Tweak> v;
    auto add = [&](Tweak t) { v.push_back(std::move(t)); };

    // ======================= Ads & suggestions ==============================
    {
        Tweak t; t.id = "ads.cdm"; t.cat = Cat::Ads; t.recommended = true;
        t.name = {"Stop auto-installed \"suggested\" apps", "Не ставить «рекомендуемые» приложения"};
        t.desc = {"Disables Content Delivery Manager, OEM/pre-installed app pushes and consumer features.",
                  "Отключает Content Delivery Manager, подсовывание приложений от OEM/Microsoft и «потребительские функции»."};
        for (auto n : {L"ContentDeliveryAllowed", L"OemPreInstalledAppsEnabled", L"PreInstalledAppsEnabled",
                       L"PreInstalledAppsEverEnabled", L"SilentInstalledAppsEnabled", L"FeatureManagementEnabled",
                       L"SoftLandingEnabled"})
            t.regs.push_back(D(kCU, CDM, n, 0, 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\CloudContent", L"DisableWindowsConsumerFeatures", 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\CloudContent", L"DisableCloudOptimizedContent", 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "ads.suggest"; t.cat = Cat::Ads; t.recommended = true;
        t.name = {"Hide tips & suggestions everywhere", "Убрать советы и рекомендации везде"};
        t.desc = {"Suggested content in Settings, Start, notifications and the share sheet.",
                  "Рекомендуемый контент в Параметрах, Пуске, уведомлениях и меню «Поделиться»."};
        for (auto n : {L"SubscribedContent-338387Enabled", L"SubscribedContent-338388Enabled",
                       L"SubscribedContent-338389Enabled", L"SubscribedContent-338393Enabled",
                       L"SubscribedContent-353694Enabled", L"SubscribedContent-353696Enabled",
                       L"SubscribedContent-310093Enabled", L"SubscribedContent-338381Enabled",
                       L"SubscribedContent-88000326Enabled", L"SystemPaneSuggestionsEnabled"})
            t.regs.push_back(D(kCU, CDM, n, 0, 1));
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings\\Windows.SystemToast.Suggested",
                           L"Enabled", 0, 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "ads.lock"; t.cat = Cat::Ads; t.recommended = true;
        t.name = {"Lock screen tips & ads", "Реклама и «интересные факты» на экране блокировки"};
        t.desc = {"Turns off Spotlight overlay tips on the lock screen.",
                  "Отключает подсказки и рекламные надписи поверх обоев Spotlight."};
        t.regs.push_back(D(kCU, CDM, L"RotatingLockScreenEnabled", 0, 1));
        t.regs.push_back(D(kCU, CDM, L"RotatingLockScreenOverlayEnabled", 0, 1));
        t.regs.push_back(P(kCU, L"Software\\Policies\\Microsoft\\Windows\\CloudContent", L"DisableWindowsSpotlightFeatures", 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "ads.welcome"; t.cat = Cat::Ads; t.recommended = true; t.restartExplorer = true;
        t.name = {"Welcome screens & Explorer ads", "Экраны «Давайте завершим настройку» и реклама в Проводнике"};
        t.desc = {"Disables \"Get even more out of Windows\" and OneDrive / sync-provider banners in File Explorer.",
                  "Отключает «Получите больше от Windows» и баннеры OneDrive / облаков в Проводнике."};
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\UserProfileEngagement",
                           L"ScoobeSystemSettingEnabled", 0, 1));
        t.regs.push_back(D(kCU, ADV, L"ShowSyncProviderNotifications", 0, 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "ads.adid"; t.cat = Cat::Ads; t.recommended = true;
        t.name = {"Advertising ID & tailored experiences", "Рекламный идентификатор и персональные предложения"};
        t.desc = {"Stops per-user ad profiling and diagnostic-data based \"tailored experiences\".",
                  "Выключает рекламный профиль пользователя и «персонализацию» на основе диагностики."};
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0, 1));
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy",
                           L"TailoredExperiencesWithDiagnosticDataEnabled", 0, 1));
        t.regs.push_back(D(kCU, L"Control Panel\\International\\User Profile", L"HttpAcceptLanguageOptOut", 1, 0));
        add(std::move(t));
    }

    // ======================= Start & taskbar ================================
    {
        Tweak t; t.id = "shell.start"; t.cat = Cat::Shell; t.recommended = true; t.restartExplorer = true;
        t.name = {"Start: hide \"Recommended\" & promos", "Пуск: убрать «Рекомендуем» и промо"};
        t.desc = {"Removes the recommended section, account nags and recently-added apps from Start.",
                  "Убирает блок «Рекомендуем», напоминания об аккаунте и «недавно добавленные» из Пуска."};
        t.regs.push_back(D(kCU, ADV, L"Start_IrisRecommendations", 0, 1));
        t.regs.push_back(D(kCU, ADV, L"Start_AccountNotifications", 0, 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Microsoft\\PolicyManager\\current\\device\\Start", L"HideRecommendedSection", 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer", L"HideRecentlyAddedApps", 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "shell.recent"; t.cat = Cat::Shell; t.restartExplorer = true;
        t.name = {"Don't track recent files & apps", "Не запоминать недавние файлы и приложения"};
        t.desc = {"Start and Jump Lists stop showing recently opened items.",
                  "Пуск и списки перехода больше не показывают недавно открытое."};
        t.regs.push_back(D(kCU, ADV, L"Start_TrackDocs", 0, 1));
        t.regs.push_back(D(kCU, ADV, L"Start_TrackProgs", 0, 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "shell.pins"; t.cat = Cat::Shell; t.restartExplorer = true;
        t.name = {"Start: more pins, fewer recommendations", "Пуск: больше закреплённых"};
        t.desc = {"Uses the \"more pins\" layout.", "Включает раскладку Пуска «больше закреплений»."};
        t.regs.push_back(D(kCU, ADV, L"Start_Layout", 1, 0));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "shell.search"; t.cat = Cat::Shell; t.recommended = true; t.restartExplorer = true;
        t.name = {"Taskbar search: no box, no web results", "Поиск: без поля на панели и без Bing"};
        t.desc = {"Hides the taskbar search box, search highlights and Bing web suggestions.",
                  "Скрывает поле поиска на панели задач, «подсвеченное» и веб-результаты Bing."};
        t.regs.push_back(D(kCU, SRCH, L"SearchboxTaskbarMode", 0, 1));
        t.regs.push_back(D(kCU, SRCH, L"BingSearchEnabled", 0, 1));
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\SearchSettings", L"IsDynamicSearchBoxEnabled", 0, 1));
        t.regs.push_back(P(kCU, POLEXP, L"DisableSearchBoxSuggestions", 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "shell.widgets"; t.cat = Cat::Shell; t.recommended = true; t.restartExplorer = true;
        t.name = {"Widgets & News feed", "Виджеты и лента новостей"};
        t.desc = {"Disables the Widgets board (and its background web process).",
                  "Отключает панель виджетов и её фоновые веб-процессы."};
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Dsh", L"AllowNewsAndInterests", 0));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "shell.taskview"; t.cat = Cat::Shell; t.recommended = true; t.restartExplorer = true;
        t.name = {"Hide Task View button", "Скрыть кнопку «Представление задач»"};
        t.desc = {"Win+Tab keeps working.", "Win+Tab продолжает работать."};
        t.regs.push_back(D(kCU, ADV, L"ShowTaskViewButton", 0, 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "shell.copilot"; t.cat = Cat::Shell; t.recommended = true; t.restartExplorer = true;
        t.name = {"Copilot, Recall & AI data analysis", "Copilot, Recall и «анализ данных ИИ»"};
        t.desc = {"Hides the Copilot button and disables Windows Copilot / AI snapshot policies.",
                  "Убирает кнопку Copilot и отключает Windows Copilot / снимки Recall политиками."};
        t.regs.push_back(D(kCU, ADV, L"ShowCopilotButton", 0, 1));
        t.regs.push_back(P(kCU, L"Software\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1));
        t.regs.push_back(P(kCU, L"Software\\Policies\\Microsoft\\Windows\\WindowsAI", L"DisableAIDataAnalysis", 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsAI", L"DisableAIDataAnalysis", 1));
        add(std::move(t));
    }

    // ======================= Background junk ================================
    {
        Tweak t; t.id = "bg.telemetry"; t.cat = Cat::Background; t.recommended = true; t.reboot = true;
        t.name = {"Telemetry & diagnostics services", "Телеметрия и диагностические службы"};
        t.desc = {"Disables DiagTrack, the compatibility appraiser (CompatTelRunner) and CEIP tasks, sets telemetry to the minimum.",
                  "Выключает DiagTrack, CompatTelRunner и задачи CEIP, ставит телеметрию на минимум."};
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", 0));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"DoNotShowFeedbackNotifications", 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\SQMClient\\Windows", L"CEIPEnable", 0));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppCompat", L"AITEnable", 0));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppCompat", L"DisableInventory", 1));
        t.svcs = {{L"DiagTrack", SERVICE_DISABLED, SERVICE_AUTO_START}, {L"dmwappushservice", SERVICE_DISABLED, SERVICE_DEMAND_START}};
        t.tasks = {L"\\Microsoft\\Windows\\Application Experience\\Microsoft Compatibility Appraiser",
                   L"\\Microsoft\\Windows\\Application Experience\\ProgramDataUpdater",
                   L"\\Microsoft\\Windows\\Application Experience\\StartupAppTask",
                   L"\\Microsoft\\Windows\\Customer Experience Improvement Program\\Consolidator",
                   L"\\Microsoft\\Windows\\Customer Experience Improvement Program\\UsbCeip",
                   L"\\Microsoft\\Windows\\Feedback\\Siuf\\DmClient",
                   L"\\Microsoft\\Windows\\Feedback\\Siuf\\DmClientOnScenarioDownload"};
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.activity"; t.cat = Cat::Background; t.recommended = true;
        t.name = {"Activity history", "История действий"};
        t.desc = {"Stops collecting and uploading activity history.", "Прекращает сбор и отправку истории действий."};
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableActivityFeed", 0));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"PublishUserActivities", 0));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"UploadUserActivities", 0));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.apps"; t.cat = Cat::Background; t.recommended = true;
        t.name = {"Background apps", "Фоновая работа приложений"};
        t.desc = {"Store apps can no longer run in the background.", "Приложения из Store больше не работают в фоне."};
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications", L"GlobalUserDisabled", 1, 0));
        t.regs.push_back(D(kCU, SRCH, L"BackgroundAppGlobalToggle", 0, 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", L"LetAppsRunInBackground", 2));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.gamebar"; t.cat = Cat::Background; t.recommended = true;
        t.name = {"Xbox Game Bar & game recording", "Xbox Game Bar и запись игр"};
        t.desc = {"Disables Game Bar, DVR capture and the ms-gamebar popups.",
                  "Отключает Game Bar, фоновую запись (DVR) и всплывающие окна ms-gamebar."};
        t.regs.push_back(D(kCU, L"System\\GameConfigStore", L"GameDVR_Enabled", 0, 1));
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0, 1));
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\GameBar", L"UseNuxEnabled", 0, 1));
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", L"AllowGameDVR", 0));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.edge"; t.cat = Cat::Background; t.recommended = true;
        t.name = {"Edge: no background process, boost, sidebar", "Edge: без фона, предзагрузки и боковой панели"};
        t.desc = {"Stops msedge.exe from running after you close it and removes shopping/rewards/sidebar clutter.",
                  "Edge не висит в памяти после закрытия; убирает шопинг, Rewards и боковую панель."};
        const wchar_t* k = L"SOFTWARE\\Policies\\Microsoft\\Edge";
        t.regs.push_back(P(kLM, k, L"StartupBoostEnabled", 0));
        t.regs.push_back(P(kLM, k, L"BackgroundModeEnabled", 0));
        t.regs.push_back(P(kLM, k, L"HubsSidebarEnabled", 0));
        t.regs.push_back(P(kLM, k, L"EdgeShoppingAssistantEnabled", 0));
        t.regs.push_back(P(kLM, k, L"ShowMicrosoftRewards", 0));
        t.regs.push_back(P(kLM, k, L"EdgeEntraCopilotPageContext", 0));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.wer"; t.cat = Cat::Background; t.recommended = true;
        t.name = {"Windows Error Reporting", "Отчёты об ошибках Windows"};
        t.desc = {"Disables WerSvc and crash-report uploads.", "Отключает WerSvc и отправку отчётов о сбоях."};
        t.regs.push_back(D(kLM, L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting", L"Disabled", 1, 0));
        t.svcs = {{L"WerSvc", SERVICE_DISABLED, SERVICE_DEMAND_START}};
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.xbox"; t.cat = Cat::Memory; t.reboot = true;
        t.name = {"Xbox services", "Службы Xbox"};
        t.desc = {"Disables Xbox auth, game-save and networking services. Breaks Xbox app / Game Pass sign-in.",
                  "Отключает службы Xbox (вход, облачные сохранения, сеть). Ломает вход в Xbox / Game Pass."};
        t.svcs = {{L"XblAuthManager", SERVICE_DISABLED, SERVICE_DEMAND_START},
                  {L"XblGameSave", SERVICE_DISABLED, SERVICE_DEMAND_START},
                  {L"XboxGipSvc", SERVICE_DISABLED, SERVICE_DEMAND_START},
                  {L"XboxNetApiSvc", SERVICE_DISABLED, SERVICE_DEMAND_START}};
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.sysmain"; t.cat = Cat::Memory; t.reboot = true;
        t.name = {"SysMain (Superfetch)", "SysMain (Superfetch)"};
        t.desc = {"Pointless on SSD systems: pre-loads apps into RAM and causes disk churn.",
                  "На SSD бесполезна: предзагружает приложения в ОЗУ и лишний раз грузит диск."};
        t.svcs = {{L"SysMain", SERVICE_DISABLED, SERVICE_AUTO_START}};
        add(std::move(t));
    }
    {
        Tweak t; t.id = "bg.wsearch"; t.cat = Cat::Memory; t.reboot = true;
        t.name = {"Windows Search indexer", "Индексатор Windows Search"};
        t.desc = {"Stops SearchIndexer.exe. Start/Explorer search becomes slower but nothing runs in the background.",
                  "Останавливает SearchIndexer.exe. Поиск станет медленнее, зато ничего не индексируется в фоне."};
        t.svcs = {{L"WSearch", SERVICE_DISABLED, SERVICE_AUTO_START}};
        add(std::move(t));
    }

    {
        Tweak t; t.id = "mem.onedrive"; t.cat = Cat::Memory; t.reboot = true;
        t.name = {"OneDrive sync client", "Клиент OneDrive"};
        t.desc = {"Stops OneDrive.exe from starting (often 60-100 MB of RAM). Files already synced stay on disk.",
                  "OneDrive.exe не запускается (часто 60–100 МБ ОЗУ). Уже синхронизированные файлы остаются на диске."};
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\OneDrive", L"DisableFileSyncNGSC", 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "mem.delivery"; t.cat = Cat::Memory; t.recommended = true; t.reboot = true;
        t.name = {"Delivery Optimization", "Оптимизация доставки"};
        t.desc = {"Stops Windows from sharing updates with other PCs and keeping a background service alive.",
                  "Windows не раздаёт обновления другим ПК и не держит фоновую службу."};
        t.regs.push_back(P(kLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization", L"DODownloadMode", 0));
        t.svcs = {{L"DoSvc", SERVICE_DISABLED, SERVICE_AUTO_START}};
        add(std::move(t));
    }
    {
        Tweak t; t.id = "mem.location"; t.cat = Cat::Memory; t.recommended = true; t.reboot = true;
        t.name = {"Location & Maps services", "Службы геолокации и карт"};
        t.desc = {"Disables lfsvc and MapsBroker. Apps lose location access.", "Отключает lfsvc и MapsBroker. Приложения теряют доступ к геолокации."};
        t.svcs = {{L"lfsvc", SERVICE_DISABLED, SERVICE_DEMAND_START}, {L"MapsBroker", SERVICE_DISABLED, SERVICE_AUTO_START}};
        add(std::move(t));
    }
    {
        Tweak t; t.id = "mem.rare"; t.cat = Cat::Memory; t.recommended = true; t.reboot = true;
        t.name = {"Rarely needed services", "Редко нужные службы"};
        t.desc = {"Retail demo, Fax, Wallet, Phone and Smart Card services.", "Демо-режим магазина, факс, кошелёк, телефон и смарт-карты."};
        t.svcs = {{L"RetailDemo", SERVICE_DISABLED, SERVICE_DEMAND_START}, {L"Fax", SERVICE_DISABLED, SERVICE_DEMAND_START},
                  {L"WalletService", SERVICE_DISABLED, SERVICE_DEMAND_START}, {L"PhoneSvc", SERVICE_DISABLED, SERVICE_DEMAND_START},
                  {L"SCardSvr", SERVICE_DISABLED, SERVICE_DEMAND_START}};
        add(std::move(t));
    }
    {
        Tweak t; t.id = "mem.spooler"; t.cat = Cat::Memory; t.reboot = true;
        t.name = {"Print Spooler", "Диспетчер печати"};
        t.desc = {"Disable only if you never print.", "Отключайте, только если вы не печатаете."};
        t.svcs = {{L"Spooler", SERVICE_DISABLED, SERVICE_AUTO_START}};
        add(std::move(t));
    }

    // ======================= Performance ====================================
    {
        Tweak t; t.id = "perf.power"; t.cat = Cat::Performance; t.recommended = true;
        t.name = {"Ultimate Performance power plan", "Схема питания «Максимальная производительность»"};
        t.desc = {"Creates (if needed) and activates the hidden Ultimate Performance plan. Reverting restores your previous plan.",
                  "Создаёт (при необходимости) и включает скрытую схему Ultimate Performance. Отключение вернёт прежнюю схему."};
        t.custom = PowerApply;
        t.customState = PowerState;
        add(std::move(t));
    }
    {
        Tweak t; t.id = "perf.visual"; t.cat = Cat::Performance; t.recommended = true; t.restartExplorer = true;
        t.name = {"Visual effects: best performance", "Визуальные эффекты: максимальная производительность"};
        t.desc = {"Turns off animations, shadows, transparency and thumbnails. Keeps: window contents while dragging, font smoothing, translucent selection rectangle.",
                  "Выключает анимации, тени, прозрачность и эскизы. Оставляет: содержимое окна при перетаскивании, сглаживание шрифтов, прозрачный прямоугольник выделения."};
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects", L"VisualFXSetting", 3, 0));
        t.regs.push_back(D(kCU, ADV, L"TaskbarAnimations", 0, 1));
        t.regs.push_back(D(kCU, ADV, L"ListviewShadow", 0, 1));
        t.regs.push_back(D(kCU, ADV, L"ListviewAlphaSelect", 1, 1));
        t.regs.push_back(D(kCU, ADV, L"IconsOnly", 1, 0));
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\DWM", L"EnableAeroPeek", 0, 1));
        t.regs.push_back(D(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", 0, 1));
        t.regs.push_back(S(kCU, L"Control Panel\\Desktop", L"MenuShowDelay", L"0", L"400"));
        t.regs.push_back(S(kCU, L"Control Panel\\Desktop", L"DragFullWindows", L"1", L"1"));
        t.regs.push_back(S(kCU, L"Control Panel\\Desktop", L"FontSmoothing", L"2", L"2"));
        t.custom = VisualApply;
        t.customState = VisualState;
        add(std::move(t));
    }
    {
        Tweak t; t.id = "perf.startup"; t.cat = Cat::Performance; t.recommended = true;
        t.name = {"No startup-app delay", "Без задержки автозагрузки"};
        t.desc = {"Windows waits ~10 s before launching startup apps; this removes the wait.",
                  "Windows ждёт ~10 с перед запуском автозагрузки; это убирает ожидание."};
        t.regs.push_back(P(kCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", L"StartupDelayInMSec", 0));
        add(std::move(t));
    }

    // ======================= Customize ======================================
    {
        Tweak t; t.id = "cu.ext"; t.cat = Cat::Customize; t.recommended = true; t.restartExplorer = true;
        t.name = {"Show file extensions", "Показывать расширения файлов"};
        t.desc = {"", ""};
        t.regs.push_back(D(kCU, ADV, L"HideFileExt", 0, 1));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "cu.hidden"; t.cat = Cat::Customize; t.restartExplorer = true;
        t.name = {"Show hidden files", "Показывать скрытые файлы"};
        t.desc = {"", ""};
        t.regs.push_back(D(kCU, ADV, L"Hidden", 1, 2));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "cu.menu"; t.cat = Cat::Customize; t.restartExplorer = true;
        t.name = {"Classic context menu", "Классическое контекстное меню"};
        t.desc = {"Brings back the full right-click menu without \"Show more options\".",
                  "Возвращает полное меню по ПКМ без «Показать дополнительные параметры»."};
        t.regs.push_back(K(kCU, L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32"));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "cu.thispc"; t.cat = Cat::Customize; t.restartExplorer = true;
        t.name = {"Explorer opens \"This PC\"", "Проводник открывается на «Этот компьютер»"};
        t.desc = {"", ""};
        t.regs.push_back(D(kCU, ADV, L"LaunchTo", 1, 0));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "cu.mouse"; t.cat = Cat::Customize;
        t.name = {"Disable mouse acceleration", "Отключить ускорение мыши"};
        t.desc = {"Turns off \"Enhance pointer precision\" for a 1:1 feel.", "Выключает «Повышенную точность указателя» — движение 1:1."};
        t.regs.push_back(S(kCU, L"Control Panel\\Mouse", L"MouseSpeed", L"0", L"1"));
        t.regs.push_back(S(kCU, L"Control Panel\\Mouse", L"MouseThreshold1", L"0", L"6"));
        t.regs.push_back(S(kCU, L"Control Panel\\Mouse", L"MouseThreshold2", L"0", L"10"));
        add(std::move(t));
    }
    {
        Tweak t; t.id = "cu.sticky"; t.cat = Cat::Customize;
        t.name = {"No Sticky/Filter/Toggle Keys popups", "Без окон залипания клавиш"};
        t.desc = {"Pressing Shift 5 times no longer interrupts games.", "Пятикратное нажатие Shift больше не прерывает игры."};
        t.regs.push_back(S(kCU, L"Control Panel\\Accessibility\\StickyKeys", L"Flags", L"506", L"510"));
        t.regs.push_back(S(kCU, L"Control Panel\\Accessibility\\Keyboard Response", L"Flags", L"122", L"126"));
        t.regs.push_back(S(kCU, L"Control Panel\\Accessibility\\ToggleKeys", L"Flags", L"58", L"62"));
        add(std::move(t));
    }
    return v;
}

bool OpApplied(const RegOp& o) {
    if (o.kind == RegOp::KeyDefault) return reg::KeyExists(o.root, o.key);
    auto v = reg::Read(o.root, o.key, o.name);
    if (!v) return false;
    if (o.kind == RegOp::String) return v->type == REG_SZ && _wcsicmp(v->str.c_str(), o.sOn) == 0;
    return v->type == REG_DWORD && v->dword == o.on;
}

bool OpApply(const RegOp& o, bool on) {
    switch (o.kind) {
        case RegOp::Dword: return reg::WriteDword(o.root, o.key, o.name, on ? o.on : o.off);
        case RegOp::DwordDeleteOff: return on ? reg::WriteDword(o.root, o.key, o.name, o.on) : reg::DeleteValue(o.root, o.key, o.name);
        case RegOp::String: return reg::WriteString(o.root, o.key, o.name, on ? o.sOn : o.sOff);
        case RegOp::KeyDefault:
            if (on) return reg::WriteString(o.root, o.key, L"", L"");
            return reg::DeleteKeyTree(o.root, o.key);
    }
    return false;
}

}  // namespace

const std::vector<Tweak>& AllTweaks() {
    static const std::vector<Tweak> all = Build();
    return all;
}

const char* CatName(Cat c) {
    switch (c) {
        case Cat::Ads: return Tr("Ads & suggestions", "Реклама и рекомендации");
        case Cat::Shell: return Tr("Start & taskbar", "Пуск и панель задач");
        case Cat::Background: return Tr("Background junk", "Фоновый мусор");
        case Cat::Memory: return Tr("Memory (RAM)", "Память (ОЗУ)");
        case Cat::Performance: return Tr("Performance", "Производительность");
        case Cat::Customize: return Tr("Customization", "Кастомизация");
    }
    return "";
}

bool IsApplied(const Tweak& t) {
    for (const auto& o : t.regs)
        if (!OpApplied(o)) return false;
    for (const auto& s : t.svcs) {
        auto st = svc::GetStartType(s.name);
        if (st && *st != s.on) return false;  // missing service == nothing to disable
    }
    if (t.customState && !t.customState()) return false;
    return true;
}

bool Apply(const Tweak& t, bool on, std::string& err) {
    std::string failed;
    for (const auto& o : t.regs)
        if (!OpApply(o, on)) failed += (failed.empty() ? "" : ", ") + Narrow(o.name[0] ? o.name : L"(default)");
    for (const auto& s : t.svcs) {
        if (!svc::SetStartType(s.name, on ? s.on : s.off)) failed += (failed.empty() ? "" : ", ") + Narrow(s.name);
        if (on && s.on == SERVICE_DISABLED) svc::Stop(s.name);
    }
    for (auto task : t.tasks) {
        std::wstring cmd = std::wstring(L"schtasks.exe /Change /TN \"") + task + (on ? L"\" /DISABLE" : L"\" /ENABLE");
        RunHidden(cmd);  // tasks missing on some builds are fine
    }
    if (t.custom) {
        std::string e;
        if (!t.custom(on, e)) failed += (failed.empty() ? "" : ", ") + (e.empty() ? std::string("custom step") : e);
    }
    if (!failed.empty()) {
        err = std::string(Tr("Could not change: ", "Не удалось изменить: ")) + failed;
        return false;
    }
    return true;
}

void BroadcastSettingChange() {
    DWORD_PTR res;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 1500, &res);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Policy"), SMTO_ABORTIFHUNG, 1500, &res);
}

void RestartExplorer() {
    // Winlogon relaunches the shell on its own (de-elevated) after it is killed.
    RunHidden(L"taskkill.exe /F /IM explorer.exe");
}

}  // namespace pt
