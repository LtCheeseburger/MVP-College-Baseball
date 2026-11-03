#pragma once
#include <atomic>
#include <cstdint>
#include <functional>

namespace audio {

constexpr uint32_t kRate = 48000;

// the platform output pulls interleaved stereo float frames from render
void StartOutput(std::function<void(float* out, uint32_t frames)> render);

// a host source added to the DirectSound mix (interleaved stereo at kRate) before it is
// clipped, on the output thread. Set by src/game/walkup.cpp; null mixes nothing.
using ExtraMix = void (*)(float* out, uint32_t frames);
inline std::atomic<ExtraMix> g_extra_mix{nullptr};

}  // namespace audio
