#include "tasks.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "ui.h"

namespace pt::tasks {
namespace {
std::mutex g_m;
std::string g_text;
std::string g_result;
bool g_resultOk = true;
float g_progress = -1.f;
std::atomic<bool> g_busy{false};
std::atomic<int> g_gen{0};
}  // namespace

int Generation() { return g_gen.load(); }

void Reporter::Text(const std::string& s) {
    {
        std::lock_guard<std::mutex> l(g_m);
        g_text = s;
    }
    ui::Wake();
}

void Reporter::Progress(float p) {
    {
        std::lock_guard<std::mutex> l(g_m);
        g_progress = p;
    }
    ui::Wake();
}

bool Busy() { return g_busy.load(); }

std::string StatusText() {
    std::lock_guard<std::mutex> l(g_m);
    return g_text;
}

float ProgressValue() {
    std::lock_guard<std::mutex> l(g_m);
    return g_progress;
}

void SetResult(bool ok, const std::string& msg) {
    {
        std::lock_guard<std::mutex> l(g_m);
        g_result = msg;
        g_resultOk = ok;
    }
    ui::Wake();
}

std::string LastResult(bool* ok) {
    std::lock_guard<std::mutex> l(g_m);
    if (ok) *ok = g_resultOk;
    return g_result;
}

void ClearResult() {
    std::lock_guard<std::mutex> l(g_m);
    g_result.clear();
}

void Run(const std::string& label, std::function<void(Reporter&)> fn) {
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return;
    {
        std::lock_guard<std::mutex> l(g_m);
        g_text = label;
        g_progress = -1.f;
        g_result.clear();
    }
    ui::Wake();
    std::thread([fn = std::move(fn)] {
        Reporter r;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        fn(r);
        CoUninitialize();
        ++g_gen;
        g_busy = false;
        ui::Wake();
    }).detach();
}

}  // namespace pt::tasks
