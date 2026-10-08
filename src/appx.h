#pragma once
#include <string>
#include <vector>

namespace pt::appx {

struct Pkg {
    std::string name;      // e.g. Microsoft.BingWeather
    std::string family;    // PackageFamilyName, needed for restore
    std::string reason;    // non-empty => protected (why)
    bool selected = false;
};

struct Removed {
    std::string name;
    std::string family;
};

// Lists removable (non-framework, non-system) packages in the background.
void RefreshAsync();
bool Loading();
int Version();  // bumps whenever the list changes
std::vector<Pkg> List();

// Returns a non-empty reason when the package must not be removed by default.
std::string ProtectReason(const std::string& name);

void RemoveAsync(const std::vector<Pkg>& pkgs, bool deprovision);

std::vector<Removed> LoadRemoved();
void RestoreAsync(const std::vector<Removed>& items);
void RestoreAllSystemAsync();

}  // namespace pt::appx
