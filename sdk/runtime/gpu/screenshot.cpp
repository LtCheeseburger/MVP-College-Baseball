#include "gpu/screenshot.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace gpu::screenshot {

namespace {

std::atomic<bool> g_requested{false};
std::atomic<bool> g_busy{false};

std::string Utf8(const fs::path& p) {
    std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// screenshots\MVP06_20261027_153012.png, _2, _3 after it for more than one in a second
fs::path NewPath() {
    fs::path dir = app::UserDir() / "screenshots";
    std::error_code ec;
    fs::create_directories(dir, ec);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stem[96];
    swprintf(stem, 96, L"%ls_%04u%02u%02u_%02u%02u%02u", app::kGame.name, t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    fs::path path = dir / (std::wstring(stem) + L".png");
    for (int n = 2; fs::exists(path, ec); n++) path = dir / (std::wstring(stem) + L"_" + std::to_wstring(n) + L".png");
    return path;
}

// 24 bit: the back buffer's alpha is not the picture's
HRESULT WritePng(const fs::path& path, const std::vector<uint8_t>& bgr, uint32_t w, uint32_t h) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> file;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&file);
    if (SUCCEEDED(hr)) hr = file->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(file.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat24bppBGR) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 3, UINT(bgr.size()), const_cast<BYTE*>(bgr.data()));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return hr;
}

}  // namespace

void Request() { g_requested = true; }

bool Take() {
    if (g_busy || !g_requested.exchange(false)) return false;
    g_busy = true;
    return true;
}

void Cancel() { g_busy = false; }

void Save(ComPtr<IUnknown> keep, const uint8_t* px, uint32_t w, uint32_t h, uint32_t pitch) {
    std::thread([keep = std::move(keep), px, w, h, pitch] {
        HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::vector<uint8_t> bgr(size_t(w) * h * 3);
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* s = px + size_t(y) * pitch;
            uint8_t* d = &bgr[size_t(y) * w * 3];
            for (uint32_t x = 0; x < w; x++, s += 4, d += 3) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
            }
        }
        fs::path path = NewPath();
        HRESULT hr = WritePng(path, bgr, w, h);
        if (SUCCEEDED(hr)) LOGR("screenshot: %s (%ux%u)", Utf8(path).c_str(), w, h);
        else LOGE("screenshot: %s not written (0x%08lx)", Utf8(path).c_str(), static_cast<unsigned long>(hr));
        if (SUCCEEDED(co)) CoUninitialize();
        g_busy = false;
    }).detach();
}

}  // namespace gpu::screenshot
