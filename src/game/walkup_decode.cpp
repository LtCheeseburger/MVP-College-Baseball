// Walk-up songs: song decoders for walkup.cpp. Media Foundation (Windows' own codecs: WAV, MP3, M4A/AAC,
// WMA, FLAC) and stb_vorbis for OGG, each to interleaved stereo float, then a linear resample to the mixer's rate.
// Runs on walkup.cpp's worker thread, never the game's.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>

#include "audio/audio.h"
#include "game/walkup.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PUSHDATA_API
#include "game/third_party/stb_vorbis.c"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace walkup {
namespace {

constexpr double kHundredNs = 1e7;  // Media Foundation time units per second

struct Pcm {
    std::vector<float> stereo;  // interleaved L R at rate
    uint32_t rate = 0;
};

// appends frames of any channel count as stereo: mono doubled, past two channels the first two kept
void AppendStereo(Pcm& out, const float* in, size_t frames, uint32_t channels) {
    out.stereo.reserve(out.stereo.size() + frames * 2);
    for (size_t i = 0; i < frames; i++) {
        float l = in[i * channels];
        float r = channels > 1 ? in[i * channels + 1] : l;
        out.stereo.push_back(l);
        out.stereo.push_back(r);
    }
}

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

bool StartMediaFoundation(std::string& error) {
    static const HRESULT hr = [] {
        HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // this worker thread, once
        if (FAILED(co) && co != RPC_E_CHANGED_MODE) return co;
        return MFStartup(MF_VERSION, MFSTARTUP_LITE);
    }();
    if (FAILED(hr)) error = "Media Foundation did not start (" + std::to_string(uint32_t(hr)) + ")";
    return SUCCEEDED(hr);
}

bool DecodeMediaFoundation(const std::filesystem::path& file, double start, double length, Pcm& out,
                           std::string& error) {
    if (!StartMediaFoundation(error)) return false;
    ComPtr<IMFSourceReader> reader;
    constexpr DWORD kAudio = DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    if (FAILED(MFCreateSourceReaderFromURL(file.wstring().c_str(), nullptr, &reader))) {
        error = "Media Foundation cannot open it (unsupported format?)";
        return false;
    }
    reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(kAudio, TRUE);
    ComPtr<IMFMediaType> want;
    MFCreateMediaType(&want);
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(reader->SetCurrentMediaType(kAudio, nullptr, want.p))) {
        error = "no decoder gives float PCM for it";
        return false;
    }
    ComPtr<IMFMediaType> got;
    reader->GetCurrentMediaType(kAudio, &got);
    out.rate = MFGetAttributeUINT32(got.p, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    uint32_t channels = MFGetAttributeUINT32(got.p, MF_MT_AUDIO_NUM_CHANNELS, 0);
    if (!out.rate || !channels) {
        error = "no audio stream";
        return false;
    }
    if (start > 0) {
        PROPVARIANT at;
        InitPropVariantFromInt64(int64_t(start * kHundredNs), &at);
        reader->SetCurrentPosition(GUID_NULL, at);  // may land before start; the timestamps below trim the rest
    }
    const size_t want_frames = length > 0 ? size_t(length * out.rate) : SIZE_MAX;
    size_t frames = 0;
    for (;;) {
        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(kAudio, 0, nullptr, &flags, &ts, &sample))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample.p) continue;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) break;
        BYTE* data = nullptr;
        DWORD bytes = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &bytes))) break;
        size_t n = bytes / (sizeof(float) * channels);
        const float* f = reinterpret_cast<const float*>(data);
        double t = double(ts) / kHundredNs;
        if (start > 0 && t < start) {  // the seek's leftover before start
            size_t skip = std::min(n, size_t((start - t) * out.rate));
            f += skip * channels;
            n -= skip;
        }
        n = std::min(n, want_frames - frames);
        AppendStereo(out, f, n, channels);
        frames += n;
        buffer->Unlock();
        if (frames >= want_frames) break;
    }
    if (!frames) error = "decoded no audio (start past the end?)";
    return frames > 0;
}

bool DecodeVorbis(const std::filesystem::path& file, double start, double length, Pcm& out, std::string& error) {
    FILE* fp = _wfopen(file.wstring().c_str(), L"rb");
    if (!fp) {
        error = "cannot open it";
        return false;
    }
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_file(fp, 1, &err, nullptr);  // closes fp
    if (!v) {
        error = "not an Ogg Vorbis file (stb_vorbis error " + std::to_string(err) + ")";
        return false;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    out.rate = info.sample_rate;
    int channels = std::clamp(info.channels, 1, 2);
    if (start > 0) stb_vorbis_seek(v, unsigned(start * out.rate));
    const size_t want_frames = length > 0 ? size_t(length * out.rate) : SIZE_MAX;
    std::vector<float> chunk(4096 * 2);
    size_t frames = 0;
    while (frames < want_frames) {
        int n = stb_vorbis_get_samples_float_interleaved(v, channels, chunk.data(), int(chunk.size()));
        if (n <= 0) break;
        size_t take = std::min(size_t(n), want_frames - frames);
        AppendStereo(out, chunk.data(), take, uint32_t(channels));
        frames += take;
    }
    stb_vorbis_close(v);
    if (!frames) error = "decoded no audio (start past the end?)";
    return frames > 0;
}

std::vector<float> Resample(const Pcm& in) {
    if (in.rate == audio::kRate) return in.stereo;
    size_t frames = in.stereo.size() / 2;
    if (frames < 2) return in.stereo;
    double step = double(in.rate) / audio::kRate;
    size_t out_frames = size_t(double(frames - 1) / step);
    std::vector<float> out(out_frames * 2);
    for (size_t i = 0; i < out_frames; i++) {
        double x = i * step;
        size_t j = size_t(x);
        float t = float(x - double(j));
        for (int c = 0; c < 2; c++) {
            float a = in.stereo[j * 2 + c], b = in.stereo[(j + 1) * 2 + c];
            out[i * 2 + c] = a + (b - a) * t;
        }
    }
    return out;
}

std::wstring LowerExtension(const std::filesystem::path& file) {
    std::wstring ext = file.extension().wstring();
    for (auto& c : ext) c = wchar_t(std::towlower(c));
    return ext;
}

}  // namespace

Clip Decode(const std::filesystem::path& file, double start, double length, std::string& error) {
    Pcm pcm;
    bool ok = LowerExtension(file) == L".ogg" ? DecodeVorbis(file, start, length, pcm, error)
                                               : DecodeMediaFoundation(file, start, length, pcm, error);
    Clip clip;
    if (ok) clip.pcm = Resample(pcm);
    return clip;
}

}  // namespace walkup
