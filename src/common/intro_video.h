#pragma once
#include <cstdint>
#include <filesystem>
namespace game {
void IntroStart(const std::filesystem::path &path);
uint64_t IntroTexture();
bool IntroDone();
} // namespace game
