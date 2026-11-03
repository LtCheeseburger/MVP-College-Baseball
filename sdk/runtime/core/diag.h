#pragma once
#include "core/dev_tools.h"
#include <cstdint>
#include <cstdlib>
#include <filesystem>

// diagnostics switched on by environment variables, read once
namespace diag {

// Optional one-shot capture without keyboard input, using the same path as F3.
// Frame 0 disables it; useful for inspecting startup/loading UI.
inline uint64_t CaptureFrame() {
    static const uint64_t frame = [] {
        const char* value = dev::Env("XBE_CAPTURE_FRAME");
        return value ? std::strtoull(value, nullptr, 10) : 0ull;
    }();
    return frame;
}

// XBE_PERF set: perf log lines once a second and the read-backs named
inline bool Perf() {
    static const bool on = dev::Env("XBE_PERF") != nullptr;
    return on;
}

// XBE_CAPTURE_DIR: where F3 saves a frame, empty: F3 does not capture
inline const std::filesystem::path& CaptureDir() {
    static const std::filesystem::path dir = [] {
#if MVP06_DEV_TOOLS && defined(_WIN32)
        const wchar_t* v = _wgetenv(L"XBE_CAPTURE_DIR");
#else
        const char* v = dev::Env("XBE_CAPTURE_DIR");
#endif
        return v ? std::filesystem::path(v) : std::filesystem::path();
    }();
    return dir;
}

}  // namespace diag
