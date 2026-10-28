// mixer output through XAudio2
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"

namespace audio {

namespace {

using Clock = std::chrono::steady_clock;

// a lost device (unplugged, driver reset) stops the engine for good, it has to be made again
struct Errors final : IXAudio2EngineCallback {
    std::atomic<bool> lost{false};
    void STDMETHODCALLTYPE OnProcessingPassStart() noexcept override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) noexcept override { lost = true; }
};

struct Output {
    IXAudio2* xa = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* voice = nullptr;
    Errors errors;

    HRESULT Open() {
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wf.nChannels = 2;
        wf.nSamplesPerSec = kRate;
        wf.wBitsPerSample = 32;
        wf.nBlockAlign = 8;
        wf.nAvgBytesPerSec = kRate * 8;
        HRESULT hr = XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (SUCCEEDED(hr)) hr = xa->RegisterForCallbacks(&errors);
        if (SUCCEEDED(hr)) hr = xa->CreateMasteringVoice(&master);
        if (SUCCEEDED(hr)) hr = xa->CreateSourceVoice(&voice, &wf);
        if (SUCCEEDED(hr)) hr = voice->Start();
        if (FAILED(hr)) Close();
        return hr;
    }
    void Close() {
        if (voice) voice->DestroyVoice();
        if (master) master->DestroyVoice();
        if (xa) {
            xa->UnregisterForCallbacks(&errors);
            xa->Release();
        }
        voice = nullptr;
        master = nullptr;
        xa = nullptr;
        errors.lost = false;
    }
};

}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    std::thread([render] {
        using namespace std::chrono_literals;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Output out;
        constexpr uint32_t kFrames = kRate / 100;
        constexpr uint32_t kMinQueued = 3;
        constexpr uint32_t kMaxQueued = 10;
        // one slot more than can be queued, so the one rendered is never one XAudio2 still reads
        constexpr uint32_t kSlots = kMaxQueued + 1;
        std::vector<float> bufs(kSlots * kFrames * 2);
        uint32_t depth = kMinQueued;
        bool muted = false;
        uint32_t n = 0;
        // with no device the mixer runs silent on the clock, so streams complete and buffers play on
        auto next = Clock::now(), retry = next + 3s, fed = next;
        bool live = false;
        if (HRESULT hr = out.Open(); FAILED(hr)) LOGI("audio: no output device (%08X), sound off until one appears", unsigned(hr));
        else live = true;
        for (;;) {
            if (!live) {
                next += 10ms;
                std::this_thread::sleep_until(next);
                render(bufs.data(), kFrames);
                auto now = Clock::now();
                if (now - next > 100ms) next = now;  // no burst of guest sound after the host was suspended
                if (now < retry) continue;
                retry = now + 3s;
                if (FAILED(out.Open())) continue;
                LOGI("audio: output device found, sound on");
                live = true;
                muted = false;
                n = 0;
                fed = now;
                continue;
            }
            if (out.errors.lost) {
                out.Close();
                live = false;
                LOGI("audio: output device lost, sound off until one appears");
                next = Clock::now();
                retry = next + 3s;
                continue;
            }
            // the mixer keeps running muted, so the game's sound stays in step
            if (bool mute = app::MuteNow(); mute != muted) {
                out.master->SetVolume(mute ? 0.0f : 1.0f);
                muted = mute;
            }
            XAUDIO2_VOICE_STATE st;
            out.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            // a starved voice crackles: queue one buffer more each time, up to kMaxQueued
            if (n && !st.BuffersQueued && depth < kMaxQueued) {
                depth++;
                LOGI("audio: output starved, queueing %u ms", depth * 10);
            }
            if (st.BuffersQueued >= depth) {
                // a voice that stops taking buffers with no error is treated as a lost device too
                if (Clock::now() - fed > 1s) out.errors.lost = true;
                Sleep(2);
                continue;
            }
            float* b = &bufs[(n % kSlots) * kFrames * 2];
            render(b, kFrames);
            XAUDIO2_BUFFER xb{};
            xb.AudioBytes = kFrames * 8;
            xb.pAudioData = reinterpret_cast<const BYTE*>(b);
            if (FAILED(out.voice->SubmitSourceBuffer(&xb))) out.errors.lost = true;
            fed = Clock::now();
            n++;  // only a submitted buffer takes a slot, XAudio2 still reads the queued ones
        }
    }).detach();
}

}  // namespace audio
