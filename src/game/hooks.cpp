// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include "app/app.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "kernel/mods.h"
#include "kernel/xsh.h"
#include "common/game_hooks.h"
#include "common/overhaul.h"
#include "game/expansion.h"
#include "xdk/hle.h"
#include <cstring>
#include <atomic>
#include <algorithm>
#include <stdexcept>

namespace {
// The startup splash is authored for 16:9. Other frontend screens still use
// the original 4:3 safe area until their layouts are converted individually.
std::atomic<bool> g_startup_splash{false};
thread_local bool g_full_width_quad = false;
struct FullWidthQuad {
    bool previous = g_full_width_quad;
    explicit FullWidthQuad(bool enabled) { g_full_width_quad = enabled; }
    ~FullWidthQuad() { g_full_width_quad = previous; }
};
}

namespace game {
void EndStartupLoading() {g_startup_splash=false;}
bool StartupSplashActive() { return g_startup_splash.load(); }
}

// The frontend logo cache reserves twenty 60,000-byte buffers. Resized native
// XSH members otherwise overwrite the next slot, including its texture handles.
// Keep the game's allocator and cache logic; adjust only this allocation site's
// requested capacity to the largest rebuilt logo in the active loose/mod view.
static uint32_t NativeLogoCapacity();
X86_EXTERN_FUNC(__imp__sub_00325F20);
X86_FUNC(sub_00325F20) {
    uint32_t stack = ctx.esp;
    bool logo = kernel::R32(stack) == 0x00259F7F && kernel::R32(stack + 4) == 60000;
    if (!logo) return __imp__sub_00325F20(ctx, base);
    uint32_t capacity = NativeLogoCapacity();
    kernel::W32(stack + 4, capacity);
    __imp__sub_00325F20(ctx, base);
    kernel::W32(stack + 4, 60000);
    if (!ctx.eax) throw std::runtime_error("Native logo cache allocation failed");
}

static uint32_t NativeLogoCapacity() {
    static const uint32_t capacity = [] {
        uint64_t largest = 60000;
        auto folder = kernel::mods::GameDir() / "data/frontend/logos_big";
        for (const auto& child : kernel::mods::List(folder)) {
            auto path = folder / child.name;
            uint64_t size = 0;
            if (child.dir && kernel::xsh::IsFolder(path) && kernel::xsh::Size(path, size)) largest = std::max(largest, size);
        }
        if (largest > UINT32_MAX - 127) throw std::runtime_error("Native logo exceeds cache address range");
        uint32_t bytes = largest > 60000 ? uint32_t((largest + 127) & ~uint64_t(127)) : 60000;
        if (bytes > 60000) LOGR("native logos: cache capacity %u bytes x 20 slots (%llu bytes)", bytes, (unsigned long long)bytes * 20);
        return bytes;
    }();
    return capacity;
}

X86_EXTERN_FUNC(__imp__sub_002D1530);
X86_FUNC(sub_002D1530) {
    if (!overhaul::Active()) return __imp__sub_002D1530(ctx, base);
    g_startup_splash = true;
    FullWidthQuad scope(app::Widescreen());
    __imp__sub_002D1530(ctx, base);
}

// A subsequent, context-specific loading background ends the startup layout.
X86_EXTERN_FUNC(__imp__sub_002D16E0);
X86_FUNC(sub_002D16E0) {
    if (!overhaul::Active()) return __imp__sub_002D16E0(ctx, base);
    g_startup_splash = false;
    __imp__sub_002D16E0(ctx, base);
}

// Progress-bar draws must use the same coordinate space as their background.
X86_EXTERN_FUNC(__imp__sub_002D1880);
X86_FUNC(sub_002D1880) {
    if (!overhaul::Active()) return __imp__sub_002D1880(ctx, base);
    FullWidthQuad scope(g_startup_splash && app::Widescreen());
    // Lazy progress-texture load normally formats misc/textures/grad.%s.
    // Give the startup bar its own native asset without recolouring shared UI.
    constexpr uint32_t kProgressPath = 0x0062F48C;
    char original[21];
    if (g_startup_splash) {
        std::memcpy(original, kernel::HostPtr(kProgressPath), sizeof(original));
        std::memcpy(kernel::HostPtr(kProgressPath), "misc/textures/lbar.%s", sizeof(original));
    }
    __imp__sub_002D1880(ctx, base);
    if (g_startup_splash) std::memcpy(kernel::HostPtr(kProgressPath), original, sizeof(original));
}

// This helper transforms normalized x coordinates into a centered 4:3 region
// (x * .75 + .125 at 16:9). Skip only inside the converted startup draws.
X86_EXTERN_FUNC(__imp__sub_002BFE70);
X86_FUNC(sub_002BFE70) {
    if (!overhaul::Active()) return __imp__sub_002BFE70(ctx, base);
    // Splash batches can be flushed after the setup/draw call has returned.
    if (g_full_width_quad || (g_startup_splash && app::Widescreen())) {
        kernel::Return(ctx, 0, 0);  // cdecl: the caller removes the four arguments
        return;
    }
    __imp__sub_002BFE70(ctx, base);
}

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
    constexpr uint32_t kHdtv720p = 0x02, kXboxHeap = 0x2800000, kKeepFree = 0x1000000;
    const uint32_t kExtra = 0x800000 + (expansion::Active() ? expansion::kExtraHeap : 0);
    constexpr uint32_t kGetVideoFlags = 0x31D913, kAllocContiguous = 0x31EAF8, kHeapInit = 0x41AA3D,
                       kSetFileCache = 0x31ED24;
    bool hd = kernel::CallGuest(ctx, kGetVideoFlags, {}) & kHdtv720p;
    mem::Stats s = mem::Query();
    uint32_t free = (s.total_pages - s.contiguous_used) * mem::kPage - mem::kKernelDataSize;
    // HD logos need more game-heap room, while match resources also need the
    // original contiguous reserve outside it. Use available 2 MB increments,
    // up to the existing 8 MB extension. Stock keeps its existing heap policy.
    uint32_t extra = kExtra;
    if (NativeLogoCapacity() > 60000) {
        uint32_t room = free > kXboxHeap + kKeepFree ? free - kXboxHeap - kKeepFree : 0;
        extra = std::min(kExtra, room & ~uint32_t(0x1FFFFF));
    }
    uint32_t size = free >= kXboxHeap + extra + kKeepFree ? kXboxHeap + extra : kXboxHeap;
    uint32_t heap = kernel::CallGuest(ctx, kAllocContiguous, {size, 0xFFFFFFFF, 0, 4});
    std::memset(kernel::HostPtr(heap), 0, size);
    kernel::CallGuest(ctx, kHeapInit, {heap, size});
    LOGR("game heap: %u KB at %08X, %u KB contiguous free before it", size >> 10, heap, free >> 10);
    kernel::Return(ctx, kernel::CallGuest(ctx, kSetFileCache, {hd ? 0x10000u : 0x80000u}), 0);
}
