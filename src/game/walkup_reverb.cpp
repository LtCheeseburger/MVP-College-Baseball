// Walk-up songs: makes a clip sound like it is coming over a ballpark PA instead of studio-clean.
// PA tone (low and high roll-off), slapback echoes off the far stands, and a Freeverb-style hall tail, applied once
// when a clip is loaded. The tail is appended so it rings out after the closing fade.
#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/audio.h"
#include "game/walkup.h"

namespace walkup {
namespace {

constexpr float kLowCutHz = 140.0f, kHighCutHz = 6500.0f;   // PA horns: little deep bass, no air
constexpr float kPreDelay = 0.045f;                         // seconds before the hall answers
constexpr float kSlap1 = 0.23f, kSlap1Gain = 0.28f;         // first bounce off the opposite stands
constexpr float kSlap2 = 0.47f, kSlap2Gain = 0.13f;         // second, fainter bounce
constexpr float kRoomSize = 1.7f;                           // Freeverb delay scale (1 = a large room)
constexpr float kFeedback = 0.86f, kDamping = 0.45f;        // decay length, how fast the highs die
constexpr float kDry = 0.80f, kWet = 0.42f;
constexpr float kTail = 2.5f;                               // seconds appended for the reverb to ring out
constexpr int kSpread = 23;                                 // right channel's extra delay (44.1 kHz samples)
const int kCombs[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};  // Freeverb tunings at 44.1 kHz
const int kAllpasses[] = {556, 441, 341, 225};

struct OnePole {
    float a = 0, z = 0;
    OnePole(float hz, float rate) : a(std::exp(-2.0f * 3.14159265f * hz / rate)) {}
    float Low(float x) { return z = x * (1 - a) + z * a; }
    float High(float x) { return x - Low(x); }
};

struct Comb {
    std::vector<float> buf;
    size_t i = 0;
    float store = 0;
    explicit Comb(size_t n) : buf(std::max<size_t>(n, 1)) {}
    float Run(float x) {
        float y = buf[i];
        store = y * (1 - kDamping) + store * kDamping;
        buf[i] = x + store * kFeedback;
        i = (i + 1) % buf.size();
        return y;
    }
};

struct Allpass {
    std::vector<float> buf;
    size_t i = 0;
    explicit Allpass(size_t n) : buf(std::max<size_t>(n, 1)) {}
    float Run(float x) {
        float b = buf[i];
        buf[i] = x + b * 0.5f;
        i = (i + 1) % buf.size();
        return b - x;
    }
};

// one channel of the hall: parallel combs into series allpasses
struct Hall {
    std::vector<Comb> combs;
    std::vector<Allpass> allpasses;
    Hall(float scale, int spread) {
        for (int n : kCombs) combs.emplace_back(size_t((n + spread) * scale));
        for (int n : kAllpasses) allpasses.emplace_back(size_t((n + spread) * scale));
    }
    float Run(float x) {
        float y = 0;
        for (auto& c : combs) y += c.Run(x);
        for (auto& a : allpasses) y = a.Run(y);
        return y;
    }
};

float Peak(const std::vector<float>& pcm) {
    float peak = 0;
    for (float s : pcm) peak = std::max(peak, std::fabs(s));
    return peak;
}

}  // namespace

void Stadium(Clip& clip) {
    const float rate = float(audio::kRate);
    const size_t frames = clip.pcm.size() / 2, total = frames + size_t(kTail * rate);
    const float in_peak = Peak(clip.pcm);
    if (!frames || in_peak <= 0) return;

    // PA tone on the dry signal
    std::vector<float> dry(total * 2, 0.0f);
    OnePole low_l(kLowCutHz, rate), low_r(kLowCutHz, rate), high_l(kHighCutHz, rate), high_r(kHighCutHz, rate);
    for (size_t f = 0; f < frames; f++) {
        dry[f * 2] = high_l.Low(low_l.High(clip.pcm[f * 2]));
        dry[f * 2 + 1] = high_r.Low(low_r.High(clip.pcm[f * 2 + 1]));
    }

    const float scale = kRoomSize * rate / 44100.0f;
    Hall hall_l(scale, 0), hall_r(scale, kSpread);
    const size_t pre = size_t(kPreDelay * rate), slap1 = size_t(kSlap1 * rate), slap2 = size_t(kSlap2 * rate);
    std::vector<float> out(total * 2);
    for (size_t f = 0; f < total; f++) {
        // mono feed into the hall, plus the stand bounces (left/right swapped: they come from across the park)
        float feed = f >= pre ? 0.5f * (dry[(f - pre) * 2] + dry[(f - pre) * 2 + 1]) * 0.015f : 0.0f;
        float wl = hall_l.Run(feed), wr = hall_r.Run(feed);
        if (f >= slap1) wl += dry[(f - slap1) * 2 + 1] * kSlap1Gain, wr += dry[(f - slap1) * 2] * kSlap1Gain;
        if (f >= slap2) wl += dry[(f - slap2) * 2] * kSlap2Gain, wr += dry[(f - slap2) * 2 + 1] * kSlap2Gain;
        out[f * 2] = dry[f * 2] * kDry + wl * kWet;
        out[f * 2 + 1] = dry[f * 2 + 1] * kDry + wr * kWet;
    }

    // keep the loudness the file was mastered to (and never clip)
    const float out_peak = Peak(out);
    if (out_peak > 0) {
        const float gain = std::min(in_peak, 0.98f) / out_peak;
        for (float& s : out) s *= gain;
    }
    clip.pcm = std::move(out);
}

}  // namespace walkup
