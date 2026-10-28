#pragma once
#include <cstdint>

#include <wrl/client.h>

// Screenshots (screenshot_key, F12 by default): the backend copies the presented frame, before the UI overlay, into a
// read-back buffer, and once the GPU is done a worker thread writes it as UserDir()\screenshots\<name>_<date>_<time>.png.
// One at a time: a press while the last one is still being written is dropped.
namespace gpu::screenshot {

// window thread: the next frame is taken
void Request();
// render thread, at the copy: true once per request, and then busy until the worker is done (or Cancel)
bool Take();
void Cancel();
// render thread, the copy finished: px stays readable while keep lives, B8G8R8A8 rows pitch bytes apart
void Save(Microsoft::WRL::ComPtr<IUnknown> keep, const uint8_t* px, uint32_t w, uint32_t h, uint32_t pitch);

}  // namespace gpu::screenshot
