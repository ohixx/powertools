#include "appx.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>

#include "i18n.h"
#include "tasks.h"
#include "ui.h"
#include "util.h"

namespace pt::appx {
namespace {

std::mutex g_m;
std::vector<Pkg> g_list;
std::atomic<bool> g_loading{false};
std::atomic<int> g_version{0};

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// Single-quoted PowerShell literal.
std::wstring Q(const std::wstring& s) {
    std::wstring o = L"'";
    for (wchar_t c : s) {
        if (c == L'\'') o += L'\'';
        o += c;
    }
    return o + L"'";
}

std::wstring RemovedFile() { return DataDir() + L"\\removed_apps.txt"; }

std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> f;
    size_t s = 0;
    for (;;) {
        size_t e = line.find('\t', s);
        f.push_back(line.substr(s, e == std::string::npos ? e : e - s));
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return f;
}

struct Rule {
    const char* prefixOrPart;
    const char* en;
    const char* ru;
};

const Rule kRules[] = {
    // Shell / platform pieces the desktop needs to function.
    {"microsoft.windowsterminal", "Windows Terminal (kept)", "Windows Terminal (оставлен)"},
    {"windows.immersivecontrolpanel", "Settings (kept)", "Параметры (оставлены)"},
    {"microsoftwindows.", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.shellexperiencehost", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.startmenuexperiencehost", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.search", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.cloudexperiencehost", "Sign-in / OOBE component", "Компонент входа / OOBE"},
    {"microsoft.windows.oobe", "Sign-in / OOBE component", "Компонент входа / OOBE"},
    {"microsoft.aad.brokerplugin", "Sign-in component", "Компонент входа"},
    {"microsoft.accountscontrol", "Sign-in component", "Компонент входа"},
    {"microsoft.lockapp", "Lock screen", "Экран блокировки"},
    {"microsoft.credDialoghost", "Credential dialog", "Диалог учётных данных"},
    {"microsoft.win32webviewhost", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.contentdeliverymanager", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.peopleexperiencehost", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.pinningconfirmationdialog", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.secureassessmentbrowser", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.xgpuejectdialog", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.parentalcontrols", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.capturepicker", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.printdialog", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.modalsharepickerhost", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.windows.apprep", "SmartScreen component", "Компонент SmartScreen"},
    {"microsoft.windows.assignedaccess", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.bioenrollment", "Windows Hello", "Windows Hello"},
    {"microsoft.ecapp", "Windows shell component", "Компонент оболочки Windows"},
    {"microsoft.asynctextservice", "Text input component", "Компонент ввода текста"},
    {"microsoft.microsoftedge", "Edge component", "Компонент Edge"},
    {"microsoft.ui.xaml", "Runtime", "Среда выполнения"},
    {"microsoft.vclibs", "Runtime", "Среда выполнения"},
    {"microsoft.net.", "Runtime", "Среда выполнения"},
    {"microsoft.windowsappruntime", "Runtime", "Среда выполнения"},
    // Security + the means to reinstall things.
    {"microsoft.sechealthui", "Windows Security", "Безопасность Windows"},
    {"microsoft.windows.sechealthui", "Windows Security", "Безопасность Windows"},
    {"microsoft.windowsstore", "Microsoft Store (needed to reinstall apps)", "Microsoft Store (нужен для возврата приложений)"},
    {"microsoft.storepurchaseapp", "Microsoft Store component", "Компонент Microsoft Store"},
    {"microsoft.desktopappinstaller", "App Installer / winget", "App Installer / winget"},
    {"microsoft.services.store", "Microsoft Store component", "Компонент Microsoft Store"},
    // Hardware vendor control panels and codecs.
    {"nvidiacorp.", "GPU driver app", "Приложение драйвера GPU"},
    {"realtek", "Audio driver app", "Приложение аудиодрайвера"},
    {"advancedmicrodevices", "GPU driver app", "Приложение драйвера GPU"},
    {"appup.intel", "Intel driver app", "Приложение драйвера Intel"},
    {"intelgraphics", "Intel driver app", "Приложение драйвера Intel"},
    {"dolbylaboratories", "Audio driver app", "Приложение аудиодрайвера"},
    {"waves", "Audio driver app", "Приложение аудиодрайвера"},
    {"videoextension", "Media codec", "Медиакодек"},
    {"imageextension", "Image codec", "Кодек изображений"},
    {"mediaextension", "Media codec", "Медиакодек"},
    {"webmediaextensions", "Media codec", "Медиакодек"},
};

}  // namespace

std::string ProtectReason(const std::string& name) {
    std::string n = Lower(name);
    for (const auto& r : kRules) {
        std::string p = Lower(r.prefixOrPart);
        bool part = (p == "videoextension" || p == "imageextension" || p == "mediaextension" || p == "webmediaextensions" ||
                     p == "realtek" || p == "waves");
        if (part ? n.find(p) != std::string::npos : n.rfind(p, 0) == 0) return Tr(r.en, r.ru);
    }
    return {};
}

bool Loading() { return g_loading.load(); }
int Version() { return g_version.load(); }

std::vector<Pkg> List() {
    std::lock_guard<std::mutex> l(g_m);
    return g_list;
}

void RefreshAsync() {
    bool expected = false;
    if (!g_loading.compare_exchange_strong(expected, true)) return;
    std::thread([] {
        std::vector<Pkg> out;
        RunPowerShell(
            LR"PS(Get-AppxPackage -AllUsers | Where-Object { -not $_.IsFramework -and -not $_.IsResourcePackage -and -not $_.NonRemovable -and $_.SignatureKind -ne 'System' } | Sort-Object Name -Unique | ForEach-Object { "$($_.Name)`t$($_.PackageFamilyName)" })PS",
            [&](const std::string& line) {
                auto f = Fields(line);
                if (f.size() < 2 || f[0].empty()) return;
                Pkg p;
                p.name = f[0];
                p.family = f[1];
                p.reason = ProtectReason(p.name);
                p.selected = p.reason.empty();
                out.push_back(std::move(p));
            });
        std::sort(out.begin(), out.end(), [](const Pkg& a, const Pkg& b) {
            if (a.reason.empty() != b.reason.empty()) return a.reason.empty();
            return Lower(a.name) < Lower(b.name);
        });
        {
            std::lock_guard<std::mutex> l(g_m);
            g_list = std::move(out);
        }
        ++g_version;
        g_loading = false;
        ui::Wake();
    }).detach();
}

static void AppendRemoved(const Removed& r) {
    for (auto& e : LoadRemoved())
        if (e.name == r.name) return;
    std::ofstream f(RemovedFile(), std::ios::binary | std::ios::app);
    f << r.name << '\t' << r.family << '\n';
}

static void DropRemoved(const std::string& name) {
    auto all = LoadRemoved();
    std::ofstream f(RemovedFile(), std::ios::binary | std::ios::trunc);
    for (auto& e : all)
        if (e.name != name) f << e.name << '\t' << e.family << '\n';
}

std::vector<Removed> LoadRemoved() {
    std::vector<Removed> out;
    std::ifstream f(RemovedFile(), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto p = Fields(line);
        if (p.size() >= 2 && !p[0].empty()) out.push_back({p[0], p[1]});
    }
    return out;
}

void RemoveAsync(const std::vector<Pkg>& pkgs, bool deprovision) {
    if (pkgs.empty()) return;
    tasks::Run(Tr("Removing apps…", "Удаление приложений…"), [pkgs, deprovision](tasks::Reporter& rep) {
        std::wstring listFile = DataDir() + L"\\remove_list.txt";
        {
            std::ofstream f(listFile, std::ios::binary | std::ios::trunc);
            for (auto& p : pkgs) f << p.name << '\n';
        }
        std::string script = std::string("$dep=") + (deprovision ? "$true" : "$false");
        std::wstring ps = Widen(script) + L";$names=Get-Content -LiteralPath " + Q(listFile) +
                          LR"PS( -Encoding UTF8;
foreach($n in $names){
  "START`t$n"
  try {
    Get-AppxPackage -AllUsers -Name $n | ForEach-Object { Remove-AppxPackage -Package $_.PackageFullName -AllUsers -ErrorAction Stop }
    if($dep){ Get-AppxProvisionedPackage -Online | Where-Object { $_.DisplayName -eq $n } | ForEach-Object { Remove-AppxProvisionedPackage -Online -PackageName $_.PackageName | Out-Null } }
    "OK`t$n"
  } catch { "FAIL`t$n`t$($_.Exception.Message -replace '\s+',' ')" }
})PS";
        int done = 0, ok = 0;
        std::string lastErr;
        const int total = static_cast<int>(pkgs.size());
        RunPowerShell(ps, [&](const std::string& line) {
            auto f = Fields(line);
            if (f.size() < 2) return;
            if (f[0] == "START") {
                rep.Text(std::string(Tr("Removing ", "Удаляю ")) + f[1]);
            } else if (f[0] == "OK" || f[0] == "FAIL") {
                ++done;
                if (f[0] == "OK") {
                    ++ok;
                    for (auto& p : pkgs)
                        if (p.name == f[1]) AppendRemoved({p.name, p.family});
                } else if (f.size() >= 3) {
                    lastErr = f[1] + ": " + f[2];
                }
                rep.Progress(static_cast<float>(done) / total);
            }
        });
        DeleteFileW(listFile.c_str());
        char buf[64];
        snprintf(buf, sizeof(buf), " %d/%d", ok, total);
        std::string msg = std::string(Tr("Removed apps:", "Удалено приложений:")) + buf;
        if (ok < total && !lastErr.empty()) msg += " — " + lastErr.substr(0, 140);
        tasks::SetResult(ok == total, msg);
        // Refresh the visible list after we are done.
        g_loading = false;
        RefreshAsync();
    });
}

void RestoreAsync(const std::vector<Removed>& items) {
    if (items.empty()) return;
    tasks::Run(Tr("Restoring apps…", "Восстановление приложений…"), [items](tasks::Reporter& rep) {
        std::wstring listFile = DataDir() + L"\\restore_list.txt";
        {
            std::ofstream f(listFile, std::ios::binary | std::ios::trunc);
            for (auto& r : items) f << r.name << '\t' << r.family << '\n';
        }
        std::wstring ps = L"$lines=Get-Content -LiteralPath " + Q(listFile) +
                          LR"PS( -Encoding UTF8;
foreach($line in $lines){
  $p=$line -split "`t"; $n=$p[0]; $f=$p[1]
  "START`t$n"
  $done=$false
  try { Add-AppxPackage -RegisterByFamilyName -MainPackage $f -ErrorAction Stop; $done=$true } catch { $e1=$_.Exception.Message }
  if(-not $done){
    $m = Get-ChildItem "$env:ProgramFiles\WindowsApps" -Directory -Filter "${n}_*" -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if($m -and (Test-Path "$($m.FullName)\AppxManifest.xml")){
      try { Add-AppxPackage -DisableDevelopmentMode -Register "$($m.FullName)\AppxManifest.xml" -ErrorAction Stop; $done=$true } catch { $e1=$_.Exception.Message }
    }
  }
  if($done){ "OK`t$n" } else { "FAIL`t$n`tPackage files are gone - reinstall it from Microsoft Store" }
})PS";
        int done = 0, ok = 0;
        std::string lastErr;
        const int total = static_cast<int>(items.size());
        RunPowerShell(ps, [&](const std::string& line) {
            auto f = Fields(line);
            if (f.size() < 2) return;
            if (f[0] == "START") rep.Text(std::string(Tr("Restoring ", "Восстанавливаю ")) + f[1]);
            else if (f[0] == "OK" || f[0] == "FAIL") {
                ++done;
                if (f[0] == "OK") { ++ok; DropRemoved(f[1]); }
                else if (f.size() >= 3) lastErr = f[1] + ": " + f[2];
                rep.Progress(static_cast<float>(done) / total);
            }
        });
        DeleteFileW(listFile.c_str());
        char buf[64];
        snprintf(buf, sizeof(buf), " %d/%d", ok, total);
        std::string msg = std::string(Tr("Restored apps:", "Восстановлено приложений:")) + buf;
        if (ok < total && !lastErr.empty()) msg += " — " + lastErr.substr(0, 140);
        tasks::SetResult(ok == total, msg);
        g_loading = false;
        RefreshAsync();
    });
}

void RestoreAllSystemAsync() {
    tasks::Run(Tr("Re-registering system apps…", "Перерегистрация системных приложений…"), [](tasks::Reporter&) {
        int n = 0;
        RunPowerShell(
            LR"PS($c=0; Get-AppxPackage -AllUsers | Where-Object { $_.InstallLocation -and (Test-Path "$($_.InstallLocation)\AppxManifest.xml") } | ForEach-Object { try { Add-AppxPackage -DisableDevelopmentMode -Register "$($_.InstallLocation)\AppxManifest.xml" -ErrorAction Stop; $c++ } catch {} }; "DONE`t$c")PS",
            [&](const std::string& line) {
                auto f = Fields(line);
                if (f.size() == 2 && f[0] == "DONE") n = atoi(f[1].c_str());
            });
        char buf[96];
        snprintf(buf, sizeof(buf), "%s %d", Tr("Re-registered packages:", "Перерегистрировано пакетов:"), n);
        tasks::SetResult(true, buf);
        g_loading = false;
        RefreshAsync();
    });
}

}  // namespace pt::appx
