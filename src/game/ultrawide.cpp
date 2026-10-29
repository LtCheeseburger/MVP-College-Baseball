// Ultrawide (wider than 16:9) frames. The game draws 1280x720 at 720p; on a wider output (frame_aspect, else the
// output's shape) the frame grows to 720 * aspect (2560x720 at 32:9) and the game's views follow it during play. The
// menus stay 16:9 unless ultrawide_menus is set: while no match is loaded the game is told its screen is 1280 wide, so
// the front end lays out as on a 16:9 TV in the frame's left 1280 columns, and the host shows only those, pillarboxed.
#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "common/game_hooks.h"
#include "xdk/hle.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace {

constexpr uint32_t kStockWidth = 1280, kStockHeight = 720;
// read-only constants the game builds its views from: the camera aspect (getter 0x2827D0), the projection aspect
// (0x2C4410), the camera's horizontal field of view factor (getter 0x282790, 4/3 at 16:9) and the 1280.0 the 720p
// glow quads are sized with (0x2C7DC0, 0x2C8080)
constexpr uint32_t kCameraAspect = 0x628FC8, kProjectionAspect = 0x62ECA4, kFovFactor = 0x629CD8, kQuadWidth = 0x62ED84;
constexpr uint32_t kWorldRenderer = 0x6DA288;  // the match's renderer, made by 0x270800 while a match loads, freed by 0x27D510

uint32_t g_wide = 0;               // the widened frame's width, 0 when the frame is the game's own
float g_wide_aspect = 0;
uint32_t g_stock[3] = {};          // the three view constants as the game had them
bool g_loading = false;            // from the match load (0x281080) until its renderer exists
bool g_in_load = false;            // inside the match load
int g_mode = -1;                   // the layout last applied: 1 wide, 0 16:9

// the game lays out for the whole frame: a match is loading or loaded, or ultrawide_menus
bool WideLayout() {
    return game::g_ultrawide_menus || g_loading || kernel::R32(kWorldRenderer) != 0;
}

// the view constants and the host's crop for the layout the game is in now
void ApplyLayout(X86Context& ctx) {
    if (!g_wide) return;
    bool wide = WideLayout();
    if (int(wide) == g_mode) return;
    g_mode = wide;
    constexpr float kFovBaseAspect = 4.0f / 3.0f;
    if (wide) {
        kernel::W32(kCameraAspect, std::bit_cast<uint32_t>(g_wide_aspect));
        kernel::W32(kProjectionAspect, std::bit_cast<uint32_t>(g_wide_aspect));
        kernel::W32(kFovFactor, std::bit_cast<uint32_t>(g_wide_aspect / kFovBaseAspect));
    } else {
        kernel::W32(kCameraAspect, g_stock[0]);
        kernel::W32(kProjectionAspect, g_stock[1]);
        kernel::W32(kFovFactor, g_stock[2]);
    }
    // the game's logical screen size (getters 0xA19F0, 0xA1A10, text 0x2C0750): +0x20 set uses +0x24/+0x28 over the
    // frame's +0x0C/+0x10
    constexpr uint32_t kDisplay = 0x6DA228, kOverride = 0x20, kOverrideW = 0x24, kOverrideH = 0x28;
    if (uint32_t g = kernel::R32(kDisplay)) {
        *static_cast<uint8_t*>(kernel::HostPtr(g + kOverride)) = wide ? 0 : 1;
        kernel::W32(g + kOverrideW, kStockWidth);
        kernel::W32(g + kOverrideH, kStockHeight);
    }
    // the front end's movie player (0x6DA258, laid out once at boot by 0x2C30C0): its root is centred on the screen
    // again, +0x14/+0x18 its size, +0x2C/+0x30 its scale, +0x24/+0x28 the offset, root vfunc 1 (x, y, 0) moves it
    constexpr uint32_t kPlayer = 0x6DA258, kRoot = 0x10, kSizeW = 0x14, kSizeH = 0x18, kOffX = 0x24, kOffY = 0x28,
                       kScaleX = 0x2C, kScaleY = 0x30, kMoveSlot = 4;
    uint32_t player = kernel::R32(kPlayer), root = player ? kernel::R32(player + kRoot) : 0;
    if (root) {
        auto rf = [](uint32_t va) { return std::bit_cast<float>(kernel::R32(va)); };
        float ox = (float(wide ? g_wide : kStockWidth) - rf(player + kSizeW) * rf(player + kScaleX)) * 0.5f;
        float oy = (float(kStockHeight) - rf(player + kSizeH) * rf(player + kScaleY)) * 0.5f;
        kernel::W32(player + kOffX, std::bit_cast<uint32_t>(ox));
        kernel::W32(player + kOffY, std::bit_cast<uint32_t>(oy));
        uint32_t keep_a = ctx.eax, keep_c = ctx.ecx, keep_d = ctx.edx;  // the hooked function's results stay
        ctx.ecx = root;
        kernel::CallGuest(ctx, kernel::R32(kernel::R32(root) + kMoveSlot),
                          {std::bit_cast<uint32_t>(ox), std::bit_cast<uint32_t>(oy), 0});
        ctx.eax = keep_a;
        ctx.ecx = keep_c;
        ctx.edx = keep_d;
    }
    gpu::g_present_width = wide ? 0 : kStockWidth;
    LOGR("ultrawide: %s", wide ? "match, whole frame" : "menus, 16:9");
}

}  // namespace

// Device setup at 0x2D2CB0, called once by the display setup at 0x2DC228 with (device, 720p flag, widescreen flag,
// width, height, ...). An output wider than 16:9 widens the 1280x720 mode to 720 * aspect: the frame buffers grow (the
// 128 MB contiguous range holds them; 64 MB ran out at 2560 wide). The host shows the frame at its own shape
// (FrameViewport), or its left 1280 columns in the menus.
X86_EXTERN_FUNC(__imp__sub_002D2CB0);
X86_FUNC(sub_002D2CB0) {
    constexpr uint32_t kWidthArg = 0x10, kHeightArg = 0x14;
    constexpr uint32_t kMaxWidth = 3840, kWidthStep = 16;
    constexpr float kStockAspect = 16.0f / 9.0f;
    uint32_t w = kernel::R32(ctx.esp + kWidthArg), h = kernel::R32(ctx.esp + kHeightArg);
    float want = game::FrameAspect();
    if (w == kStockWidth && h == kStockHeight && want > kStockAspect + 0.01f) {
        g_wide = std::min(uint32_t(std::lround(h * want / kWidthStep)) * kWidthStep, kMaxWidth);
        g_wide_aspect = float(g_wide) / float(h);
        g_stock[0] = kernel::R32(kCameraAspect);
        g_stock[1] = kernel::R32(kProjectionAspect);
        g_stock[2] = kernel::R32(kFovFactor);
        kernel::W32(ctx.esp + kWidthArg, g_wide);
        LOGR("ultrawide: %ux%u frame, aspect %.3f, menus %s", g_wide, h, g_wide_aspect,
             game::g_ultrawide_menus ? "wide" : "16:9");
    }
    __imp__sub_002D2CB0(ctx, base);
    ApplyLayout(ctx);
}

// The device's screen size getter, thiscall (float* width, float* height). The front end centres and anchors its
// screens on it (APT elements 0x418D54, the movie player 0x2C30C0), the 2D projection (0x3AD5C0) is built from it each
// frame. In the menus it answers 1280, except to the device and display setup (0x2D2D75, 0x2DC245), which size the
// frame and the game's own record of it, and to the view rectangle's clamp (0x3ADD80).
X86_EXTERN_FUNC(__imp__sub_003AAC90);
X86_FUNC(sub_003AAC90) {
    constexpr uint32_t kDeviceSetup = 0x2D2D75, kDisplaySetup = 0x2DC245, kViewClamp = 0x3ADDE4;
    uint32_t ret = kernel::R32(ctx.esp), width = kernel::R32(ctx.esp + 4);
    __imp__sub_003AAC90(ctx, base);
    if (!g_wide || ret == kDeviceSetup) return;
    if (ret == kDisplaySetup) {
        g_mode = -1;  // it records the full width next: the following call applies the layout over it
        return;
    }
    ApplyLayout(ctx);
    if (ret == kViewClamp) return;  // views keep the frame's size; their apply (below) picks the width
    if (!WideLayout() && width) kernel::W32(width, std::bit_cast<uint32_t>(float(kStockWidth)));
}

// The field of view factor's getter (0x282790, 4/3 at 16:9 against 4:3). The camera's projection (0x2D12E0) multiplies
// its field of view in degrees by it straight; at 32:9 (8/3) a wide camera such as the stadium fly-in went past 180
// degrees and its picture turned upside down and mirrored. For that caller the factor becomes the one that widens the
// field of view as a wider screen does, 2 atan(tan(fov / 2) * k), which stays under 180; k is set so the game's
// default 45 degrees widens exactly as before (45 * factor), so play looks as it did.
X86_EXTERN_FUNC(__imp__sub_00282790);
X86_FUNC(sub_00282790) {
    constexpr uint32_t kProjection = 0x2D1312, kCallerFov = 8;  // its field of view at its [esp+4]
    constexpr float kRadians = 0.01745329f, kDefaultFov = 45.0f, kMaxCalibrated = 170.0f;
    uint32_t keep = kernel::R32(kFovFactor);
    float factor = std::bit_cast<float>(keep);
    float fov = std::bit_cast<float>(kernel::R32(ctx.esp + kCallerFov));
    bool fix = g_wide && kernel::R32(ctx.esp) == kProjection && factor > 4.0f / 3.0f + 0.01f && fov > 1.0f && fov < 179.0f;
    if (fix) {
        float half = kDefaultFov * kRadians / 2.0f;
        float k = std::tan(std::min(kDefaultFov * factor, kMaxCalibrated) * kRadians / 2.0f) / std::tan(half);
        float wide = 2.0f * std::atan(std::tan(fov * kRadians / 2.0f) * k) / kRadians;
        kernel::W32(kFovFactor, std::bit_cast<uint32_t>(wide / fov));
    }
    __imp__sub_00282790(ctx, base);
    if (fix) kernel::W32(kFovFactor, keep);
}

// A view's apply (thiscall), which hands its viewport (+0x10: x, y, width, height) to D3DDevice_SetViewport. Views
// made in the menus keep the 1280 wide screen they were sized for, so in a match a full screen view is widened to
// the frame (the match's overlays and glow were cut off at 1280), and back in the menus narrowed again.
X86_EXTERN_FUNC(__imp__sub_003ADAB0);
X86_FUNC(sub_003ADAB0) {
    // the view's rectangle as last set (0x3ADD80, thiscall (x, y, width, height, min z, max z) as floats)
    constexpr uint32_t kRect = 0x118, kX = 0, kY = 4, kWidth = 8, kHeight = 12, kMinZ = 16, kMaxZ = 20;
    constexpr uint32_t kSetRect = 0x3ADD80;
    uint32_t view = ctx.ecx, r = view + kRect;
    if (g_wide && kernel::R32(r + kX) == 0 && kernel::R32(r + kY) == 0 && kernel::R32(r + kHeight) == kStockHeight) {
        uint32_t w = kernel::R32(r + kWidth), want = WideLayout() ? g_wide : kStockWidth;
        if ((w == kStockWidth || w == g_wide) && w != want) {
            auto f = [](float v) { return std::bit_cast<uint32_t>(v); };
            kernel::CallGuest(ctx, kSetRect, {0, 0, f(float(want)), f(float(kStockHeight)), kernel::R32(r + kMinZ),
                                              kernel::R32(r + kMaxZ)});
            ctx.ecx = view;
        }
    }
    __imp__sub_003ADAB0(ctx, base);
}

// Match load, thiscall (2 arguments): the match's screens and overlays are made from here on, so they lay out wide.
X86_EXTERN_FUNC(__imp__sub_00281080);
X86_FUNC(sub_00281080) {
    g_loading = g_in_load = true;
    ApplyLayout(ctx);
    __imp__sub_00281080(ctx, base);
    g_in_load = false;
    ApplyLayout(ctx);
}

// The match's renderer, made while the match loads (0x270800) and freed when it ends (0x27D510, also run first by
// each load): the layout follows it.
X86_EXTERN_FUNC(__imp__sub_00270800);
X86_FUNC(sub_00270800) {
    __imp__sub_00270800(ctx, base);
    g_loading = false;  // the renderer stands for the match from here
    ApplyLayout(ctx);
}

X86_EXTERN_FUNC(__imp__sub_0027D510);
X86_FUNC(sub_0027D510) {
    __imp__sub_0027D510(ctx, base);
    if (!g_in_load) g_loading = false;
    ApplyLayout(ctx);
}

// The glow's two quads at 720p, built once per match: 0x2C7DC0 reads the frame into the 320x240 glow buffer through
// texture coordinates 0..1280, 0x2C8080 adds the buffer back over positions 0..1280. Both take the width from the
// 1280.0 at 0x62ED84, so on a wider frame only its left 1280 columns glowed and the frame showed a seam down the
// middle. The constant is the frame's width while they run.
namespace {
void WithFrameWidth(void (*original)(X86Context&, uint8_t*), X86Context& ctx, uint8_t* base) {
    uint32_t keep = kernel::R32(kQuadWidth);
    if (g_wide) kernel::W32(kQuadWidth, std::bit_cast<uint32_t>(float(g_wide)));
    original(ctx, base);
    kernel::W32(kQuadWidth, keep);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_002C7DC0);
X86_FUNC(sub_002C7DC0) {
    WithFrameWidth(__imp__sub_002C7DC0, ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_002C8080);
X86_FUNC(sub_002C8080) {
    WithFrameWidth(__imp__sub_002C8080, ctx, base);
}
