#pragma once
#include <filesystem>
#include <string>

namespace translated_patches {
// Stable content digest of sorted manifests. Missing/malformed files throw.
std::string Revision(const std::filesystem::path& manifests);
std::string Sha256(const std::string& text);
// Validate every edit before changing any source. Empty manifests are a no-op.
void Apply(const std::filesystem::path& generated, const std::filesystem::path& manifests);
}
