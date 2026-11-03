#include "intro_video.h"
#include "overhaul.h"
#include "core/log.h"
#include "gpu/backend.h"
#include <atomic>
#include <chrono>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace game {
namespace {
struct Video {
  std::mutex mutex;
  std::vector<uint8_t> pixels;
  uint32_t width = 0, height = 0;
  std::atomic<bool> done{false};
  std::jthread worker;
  void Run(std::stop_token stop, std::filesystem::path path) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
      LOGE("menu intro: MFStartup failed %08X", unsigned(hr));
      done = true;
      if (SUCCEEDED(co))
        CoUninitialize();
      return;
    }
    {
      ComPtr<IMFAttributes> attrs;
      ComPtr<IMFSourceReader> reader;
      ComPtr<IMFMediaType> type;
      MFCreateAttributes(&attrs, 2);
      attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
      hr = MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &reader);
      if (SUCCEEDED(hr))
        hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
      if (SUCCEEDED(hr))
        hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                        TRUE);
      if (SUCCEEDED(hr))
        hr = MFCreateMediaType(&type);
      if (SUCCEEDED(hr)) {
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                         nullptr, type.Get());
      }
      if (SUCCEEDED(hr))
        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                         &type);
      UINT32 w = 0, h = 0;
      if (SUCCEEDED(hr))
        hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
      auto start = std::chrono::steady_clock::now();
      bool first = true;
      LONGLONG origin = 0, last = 0;
      uint32_t frames = 0;
      while (SUCCEEDED(hr) && !stop.stop_requested()) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
                                &flags, &timestamp, &sample);
        if (FAILED(hr) || flags & MF_SOURCE_READERF_ENDOFSTREAM)
          break;
        if (!sample)
          continue;
        if (first) {
          start = std::chrono::steady_clock::now();
          origin = timestamp;
          first = false;
          LOGR("menu intro: playback started %ux%u", w, h);
        }
        auto due = start + std::chrono::nanoseconds((timestamp - origin) * 100);
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < due)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (stop.stop_requested())
          break;
        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr))
          break;
        BYTE *bytes = nullptr;
        DWORD length = 0;
        hr = buffer->Lock(&bytes, nullptr, &length);
        if (FAILED(hr))
          break;
        if (length >= w * h * 4) {
          std::lock_guard lock(mutex);
          width = w;
          height = h;
          pixels.resize(size_t(w) * h * 4);
          // RGB32 source reader output uses the media type's signed row stride.
          LONG stride = LONG(
              MFGetAttributeUINT32(type.Get(), MF_MT_DEFAULT_STRIDE, w * 4));
          for (UINT32 y = 0; y < h; y++) {
            const BYTE *row = stride < 0
                                  ? bytes + size_t(h - 1 - y) * size_t(-stride)
                                  : bytes + size_t(y) * stride;
            memcpy(pixels.data() + size_t(y) * w * 4, row, size_t(w) * 4);
          }
          for (size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        }
        buffer->Unlock();
        last = timestamp - origin;
        frames++;
      }
      auto end = start + std::chrono::nanoseconds((last + 166667) * 100);
      while (!stop.stop_requested() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (FAILED(hr))
        LOGE("menu intro: decode failed %08X", unsigned(hr));
      LOGR("menu intro: playback complete, %u frames", frames);
    }
    MFShutdown();
    if (SUCCEEDED(co))
      CoUninitialize();
    done = true;
  }
};
Video video;
} // namespace
void IntroStart(const std::filesystem::path &p) {
  if (video.worker.joinable()) {
    overhaul::ReleaseDiskFile(p);
    return;
  }
  if (p.empty()) { video.done = true; return; }
  video.worker =
      std::jthread([p](std::stop_token stop) {
        video.Run(stop, p);
        overhaul::ReleaseDiskFile(p);
      });
}
uint64_t IntroTexture() {
  std::lock_guard lock(video.mutex);
  if (video.pixels.empty() || !gpu::g_ui_video)
    return 0;
  return gpu::g_ui_video(video.pixels.data(), video.width, video.height,
                         video.width * 4);
}
bool IntroDone() { return video.done.load(); }
} // namespace game
