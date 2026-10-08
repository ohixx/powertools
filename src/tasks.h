#pragma once
#include <functional>
#include <string>

namespace pt::tasks {

// One background job at a time; the UI polls its state every frame.
struct Reporter {
    void Text(const std::string& s);
    void Progress(float p);  // 0..1, or <0 for indeterminate
};

bool Busy();
int Generation();  // bumps whenever a job finishes; UI re-reads system state on change
std::string StatusText();
float ProgressValue();
void Run(const std::string& label, std::function<void(Reporter&)> fn);

// Outcome of the last finished job (shown in the status bar).
void SetResult(bool ok, const std::string& msg);
std::string LastResult(bool* ok = nullptr);
void ClearResult();

}  // namespace pt::tasks
