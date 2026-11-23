#pragma once
#include <string>
namespace save_compatibility {
// Cosmetic mods and order do not affect save identity. Malformed records fail closed.
bool Compatible(const std::string& saved, const std::string& active, std::string& reason);
bool HasGameData(const std::string& record);
}
