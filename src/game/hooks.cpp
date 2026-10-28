// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include "app/app.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "common/game_hooks.h"
#include "xdk/hle.h"
#include <cstring>


// XGetVideoFlags: the user's video flags, without the HDTV modes unless the boot AV pack is component. With
// xbox_720p on a widescreen output it reports 720p (and 480p), and the game picks its own 1280x720 mode: the display
// setup at 0x2DC0A0 makes a 1280x720 device, the heap setup at 0x317590 leaves room for its bigger frame buffers.
X86_EXTERN_FUNC(__imp__sub_0031D913);
X86_FUNC(sub_0031D913) {
    constexpr uint32_t kWidescreen = 0x01, kHdtv720p = 0x02, kHdtv480p = 0x08;
    __imp__sub_0031D913(ctx, base);
    if (game::g_xbox_720p && app::Widescreen()) ctx.eax |= kWidescreen | kHdtv720p | kHdtv480p;
}

// D3DDevice_SetBackBufferScale(x, y), which xbe_symbols does not name. The library scales the arguments by the
// multisample factors, stores them at CDevice_BackBufferScale and rebuilds its viewport from the frame buffers the
// host device never fills in (0x57ADA0 reads through a null surface). The host renderer keeps its own frame buffers,
// so only the stored scale is kept.
X86_FUNC(sub_00586810) {
    (void)base;
    uint32_t device = kernel::R32(SYM(D3D_pDevice));
    if (device) {
        kernel::W32(device + 0x96C, kernel::R32(ctx.esp + 4));
        kernel::W32(device + 0x970, kernel::R32(ctx.esp + 8));
    }
    kernel::Return(ctx, 0, 2);
}

// Game heap setup, called once at boot: one contiguous block for the game's own allocator (0x41AA3D), then the file
// cache size. The Xbox sizes the heap at 40 MB, 0.8 MB less in its 720p mode for the bigger frame buffers. Quitting a
// match back to the menus ran that heap out (a 60 KB allocation failed in 0x325E60 and the game's out-of-memory
// assert ended the process). The host renderer keeps its own frame buffers and the XBE image and virtual allocations
// are not in the contiguous range here, so the heap gets 8 MB more when the contiguous range has room for it.
X86_FUNC(sub_00317590) {
    constexpr uint32_t kHdtv720p = 0x02, kXboxHeap = 0x2800000, kExtra = 0x800000, kKeepFree = 0x1000000;
    constexpr uint32_t kGetVideoFlags = 0x31D913, kAllocContiguous = 0x31EAF8, kHeapInit = 0x41AA3D,
                       kSetFileCache = 0x31ED24;
    bool hd = kernel::CallGuest(ctx, kGetVideoFlags, {}) & kHdtv720p;
    mem::Stats s = mem::Query();
    uint32_t free = (s.total_pages - s.contiguous_used) * mem::kPage - mem::kKernelDataSize;
    uint32_t size = free >= kXboxHeap + kExtra + kKeepFree ? kXboxHeap + kExtra : kXboxHeap;
    uint32_t heap = kernel::CallGuest(ctx, kAllocContiguous, {size, 0xFFFFFFFF, 0, 4});
    std::memset(kernel::HostPtr(heap), 0, size);
    kernel::CallGuest(ctx, kHeapInit, {heap, size});
    LOGR("game heap: %u KB at %08X, %u KB contiguous free before it", size >> 10, heap, free >> 10);
    kernel::Return(ctx, kernel::CallGuest(ctx, kSetFileCache, {hd ? 0x10000u : 0x80000u}), 0);
}