// Pregame mix-and-match uniforms for the supported MVP 06 XBE. The original descriptor stays 12 bytes and
// retains the chosen preset. Components live only in host memory for this matchup; no roster/save/settings fields
// are repurposed. See docs/UNIFORM_COMPONENTS.md for the address/ownership contract and authoring format.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "common/uniforms.h"
#include "kernel/kernel.h"
#include "kernel/mods.h"

namespace {
using namespace game::uniforms;
using kernel::R32;
using kernel::W32;

// Helpers must not leak their this pointer, flags or temporary return values into the original guest caller.
struct ContextGuard {
    X86Context& ctx;
    X86Context saved;
    explicit ContextGuard(X86Context& c) : ctx(c), saved(c) {}
    ~ContextGuard() { ctx = saved; }
};
struct GuestBuffer {
    uint32_t address;
    explicit GuestBuffer(uint32_t size) : address(mem::AllocSystem(size)) {}
    ~GuestBuffer() { if (address) mem::FreeSystem(address); }
    GuestBuffer(const GuestBuffer&) = delete;
    GuestBuffer& operator=(const GuestBuffer&) = delete;
    void Text(const std::string& text, uint32_t offset = 0) const {
        std::memcpy(kernel::HostPtr(address + offset), text.c_str(), text.size() + 1);
    }
};
uint32_t Call(X86Context& ctx, uint32_t self, uint32_t function, std::initializer_list<uint32_t> args = {}) {
    ContextGuard guard(ctx);
    ctx.ecx = self;
    return kernel::CallGuest(ctx, function, args);
}
uint32_t Virtual(X86Context& ctx, uint32_t self, uint32_t offset, std::initializer_list<uint32_t> args = {}) {
    return self ? Call(ctx, self, R32(R32(self) + offset), args) : 0;
}
uint32_t Find(X86Context& ctx, uint32_t group, const char* name) {
    GuestBuffer text(64);
    if (!text.address) return 0;
    text.Text(name);
    if (!Virtual(ctx, group, 0x68, {text.address})) return 0;
    uint32_t widget = Virtual(ctx, group, 0x7C, {text.address, 1});
    return widget ? Call(ctx, widget, 0xA60A0) : 0;
}
uint32_t MatchDescriptor(unsigned side) {
    uint32_t p = R32(0x6C655C);
    if (p) p = R32(p + 8);
    if (p) p = R32(p + 0x14);
    if (p && side < 2) p = R32(p + 4 + side * 4);
    return p ? p + 0xAC : 0;
}
void Descriptor(X86Context& ctx, uint32_t dest, uint32_t team, uint32_t variant) {
    Call(ctx, dest, 0x8C510, {team, variant, 0, 0});  // resolves the roster's home/away asset id
}
std::string Pack(X86Context& ctx, uint32_t team, uint32_t variant) {
    GuestBuffer desc(12);
    if (!desc.address) return {};
    Descriptor(ctx, desc.address, team, variant);
    char name[48];
    std::snprintf(name, sizeof(name), "u%03u%c.xsh", R32(desc.address + 8), kVariants[variant]);
    return name;
}

struct Control {
    unsigned side = 0;
    std::array<uint32_t, 5> labels{};
    std::array<uint32_t, 4> obsolete{};
    GuestBuffer text{5 * 128};  // retained until control destruction, even if a text widget borrows its string
    std::optional<Selection> selection;
};
struct Committed { uint32_t descriptor; Outfit outfit; };
std::unordered_map<uint32_t, std::unique_ptr<Control>> g_controls;
std::array<std::optional<Committed>, 2> g_outfits;
// Each extra XSH remains alive until the original uniform object resets. Never replace its owning +4 handle.
std::unordered_map<uint32_t, std::vector<uint32_t>> g_resources;

Control* Get(uint32_t self) {
    auto it = g_controls.find(self);
    return it == g_controls.end() ? nullptr : it->second.get();
}
void Refresh(X86Context& ctx, uint32_t self) {
    auto* control = Get(self);
    if (!control || !control->selection) return;
    // Native activation can show these again. Hide the single-row frame/arrows/title after each refresh.
    for (uint32_t widget : control->obsolete) Virtual(ctx, widget, 0x1C, {0});
    const auto labels = control->selection->Labels();
    for (unsigned row = 0; row < 5; ++row) {
        std::string value = row == 4 ? "Up/Down: part   Left/Right: change" :
            (row == control->selection->Row() ? "> " : "  ") + labels[row];
        control->text.Text(value, row * 128);
        Virtual(ctx, control->labels[row], 0x68, {control->text.address + row * 128});
        Virtual(ctx, control->labels[row], 0x3C, {row == control->selection->Row() ? 1u : 0u});
    }
}
bool Edit(X86Context& ctx, uint32_t self, int direction, bool row) {
    auto* control = Get(self);
    if (!control || !control->selection) return false;
    if (!*static_cast<uint8_t*>(kernel::HostPtr(self + 0x210))) {
        if (row) control->selection->MoveRow(direction);
        else {
            control->selection->Cycle(direction);
            // The original artwork is a jersey thumbnail. Keep it on the jersey's donor; text names the actual
            // components. Added pieces do not pretend to have a composite/3D preview.
            Call(ctx, self, 0x516430, {control->selection->Current().pieces[Index(Part::Jersey)].variant});
        }
        Refresh(ctx, self);
    }
    return true;
}
uint32_t FocusedUniform(X86Context& ctx, uint32_t screen, uint32_t port) {
    // TeamSelect's own dispatcher resolves controller port -> focused home/away child. Other screens, side
    // assignment, difficulty and team pickers keep their original input handlers.
    if (!(Virtual(ctx, screen, 0x160, {port}) & 0xFF)) return 0;
    uint32_t child = Virtual(ctx, screen, 0x150, {port});
    return Get(child) ? child : 0;
}
bool MoveRow(X86Context& ctx, int direction) {
    return Edit(ctx, FocusedUniform(ctx, ctx.ecx, R32(ctx.esp + 4)), direction, true);
}
bool UniformOwnsDirection(X86Context& ctx) {
    // CNTRLSELECT receives the same press independently of TeamSelect. Its handler must not reassign this port
    // while that port edits a uniform. The control's +208 owner is set by 0xAF7B0; the screen's +4C bit is set on
    // activation (0xAF950) and cleared on exit (0xB2A90), so cached/inactive screens cannot block other menus.
    const uint32_t selector = ctx.ecx, port = R32(ctx.esp + 4);
    for (const auto& [self, control] : g_controls) {
        if (!control->selection) continue;
        const uint32_t screen = R32(self + 0x208);
        if (!screen || !(*static_cast<uint8_t*>(kernel::HostPtr(screen + 0x4C)) & 1) ||
            R32(screen + 0x200) != selector) continue;
        if (FocusedUniform(ctx, screen, port) == self) return true;
    }
    return false;
}

void Release(X86Context& ctx, uint32_t self) {
    auto it = g_resources.find(self);
    if (it == g_resources.end()) return;
    auto handles = std::move(it->second);
    g_resources.erase(it);
    for (uint32_t handle : handles) Call(ctx, 0, 0x2670E0, {handle, 0x10});
}

void Compose(X86Context& ctx, uint32_t self, const Outfit& outfit) {
    const auto jersey_variant = outfit.pieces[Index(Part::Jersey)].variant;
    if (std::all_of(outfit.pieces.begin(), outfit.pieces.end(), [&](const Piece& p) {
            return p.texture.empty() && p.variant == jersey_variant;
        })) return;
    GuestBuffer scratch(128);
    if (!scratch.address) return;
    // The loader makes separate cap/helmet copies (+9C/+A0/+A4), and may own a synthesized front torso (+B4,
    // guarded by +F0). Dispose of those BEFORE replacing source pointers, then rebuild them from the new parts.
    // Otherwise helmets keep the old hat, or teardown can try to free an interior pointer in a custom jersey.
    Call(ctx, self, 0x25F140);
    std::map<std::string, uint32_t> loaded;
    auto load = [&](const std::string& name) -> uint32_t {
        if (name.empty()) return 0;
        auto found = loaded.find(name);
        if (found != loaded.end()) return found->second;
        scratch.Text(name);
        uint32_t handle = Call(ctx, 0, 0x2673B0, {scratch.address, R32(0x6F4EC4) | 0x400});
        loaded.emplace(name, handle);
        if (handle) g_resources[self].push_back(handle);
        return handle;
    };
    auto apply = [&](Part part, uint32_t handle) {
        if (!handle) return false;
        std::vector<uint32_t> textures;
        for (const auto& slot : Slots(part)) {
            scratch.Text(slot.tag);
            uint32_t texture = Call(ctx, 0, 0x423B10, {handle, scratch.address});
            if (!texture) return false;
            textures.push_back(texture);
        }
        // Validate the entire component before changing any pointer (no half hat/helmet or mixed LOD fallback).
        size_t i = 0;
        for (const auto& slot : Slots(part)) W32(self + 0x20 + slot.slot * 4, textures[i++]);
        return true;
    };
    for (const auto& piece : outfit.pieces) {
        if (piece.texture.empty() && piece.variant == jersey_variant) continue;
        std::string name = piece.texture.empty() ? Pack(ctx, outfit.team, piece.variant) : piece.texture;
        if (!apply(piece.part, load(name))) {
            LOGR("uniforms: %s lacks a complete component; using donor %u", name.c_str(), piece.variant);
            apply(piece.part, load(Pack(ctx, outfit.team, piece.variant)));
            continue;
        }
        if (piece.part == Part::Jersey && !piece.font.empty()) {
            scratch.Text(piece.font);
            uint32_t font = Call(ctx, 0, 0x2673B0, {scratch.address, 0x400});
            // Lettering is indexed by its four-byte directory tags; image-name attachments can differ.
            const uint32_t count = font ? R32(font + 8) : 0;
            bool complete = font && R32(font) == 0x58504853 && count >= 36 && count <= 4096 &&
                            R32(font + 4) >= 16 + 8 * count;
            for (unsigned glyph = 0; complete && glyph < 36; ++glyph) {
                const std::string tag = glyph < 26 ? std::string(1, char('A' + glyph)) + "___" :
                    "zig" + std::to_string(glyph - 26);
                complete = false;
                for (uint32_t i = 0; i < count; ++i)
                    if (std::memcmp(kernel::HostPtr(font + 16 + i * 8), tag.data(), 4) == 0) { complete = true; break; }
            }
            if (complete) {
                // Fonts are raw SHPX blocks, unlike the material resource handles. Mirror 0x2732A0/0x272FE0.
                for (uint32_t offset : {0xBCu, 0xC0u}) {
                    if (uint32_t old = R32(self + offset)) Call(ctx, 0, 0x326180, {old});
                    W32(self + offset, 0);
                }
                W32(self + 0xBC, font);
                *static_cast<uint8_t*>(kernel::HostPtr(self + 0xC4)) = 0;
            } else {
                if (font) Call(ctx, 0, 0x326180, {font});
                LOGR("uniforms: incomplete lettering %s; retaining donor font", piece.font.c_str());
            }
        }
    }
    Call(ctx, self, 0x25F230);
}
}  // namespace

// Native pregame uniform control initialization: each new matchup begins with the game's own home/away defaults.
X86_EXTERN_FUNC(__imp__sub_00515C70);
X86_FUNC(sub_00515C70) {
    uint32_t self = ctx.ecx;
    g_controls.erase(self);
    __imp__sub_00515C70(ctx, base);
    ContextGuard guard(ctx);
    unsigned side = R32(self + 0x23C);
    if (side >= 2) return;
    g_outfits[side].reset();
    auto control = std::make_unique<Control>();
    control->side = side;
    uint32_t group = R32(self + 0x218);
    const char* names[] = {"JERSEYNAME", "UNIFORMHAT", "UNIFORMJERSEY", "UNIFORMPANTS", "UNIFORMHELP"};
    for (unsigned i = 0; i < 5; ++i) control->labels[i] = Find(ctx, group, names[i]);
    // HRD, mini-games and incompatible replacement FELs use the original single selector.
    if (!control->text.address || std::ranges::find(control->labels, 0u) != control->labels.end()) return;
    const uint32_t selector = R32(self + 0x220);
    control->obsolete = {Find(ctx, selector, "FC1"), Find(ctx, selector, "NEXT"),
                         Find(ctx, selector, "PREV"), Find(ctx, group, "JERSEYTITLE")};
    g_controls[self] = std::move(control);
}

X86_EXTERN_FUNC(__imp__sub_00538830);
X86_FUNC(sub_00538830) {
    uint32_t self = ctx.ecx;
    __imp__sub_00538830(ctx, base);
    ContextGuard guard(ctx);
    auto* control = Get(self);
    if (!control) return;
    uint32_t team = R32(self + 0x238);
    if (!control->selection || control->selection->Current().team != team) {
        g_outfits[control->side].reset();
        std::vector<Preset> presets = {{0, "Home"}, {1, "Away"}};
        // The stock list only supplies a/b. Added full packs can also expose the engine's other native variants.
        const auto models = kernel::ResolvePath("D:\\data\\models_big", 0);
        for (uint32_t variant = 2; !models.empty() && variant < kVariants.size(); ++variant) {
            const auto texture = Pack(ctx, team, variant);
            const auto font = texture.empty() ? std::string{} : "f" + texture.substr(1);
            if (!kernel::mods::Find(models, texture).empty() && !kernel::mods::Find(models, font).empty())
                presets.push_back({variant, VariantName(variant)});
        }
        auto additions = LoadPieces(team);
        control->selection.emplace(team, MakeWardrobe(team, presets, additions), control->side == 0 ? 0 : 1);
    }
    Call(ctx, self, 0x516430, {control->selection->Current().pieces[Index(Part::Jersey)].variant});
    Refresh(ctx, self);
}

// Native refreshes (focus/animation) must not overwrite the four labels.
X86_EXTERN_FUNC(__imp__sub_00516290);
X86_FUNC(sub_00516290) {
    uint32_t self = ctx.ecx;
    __imp__sub_00516290(ctx, base);
    ContextGuard guard(ctx);
    Refresh(ctx, self);
}

// The existing random-uniform shortcut still selects a complete preset.
X86_EXTERN_FUNC(__imp__sub_00516370);
X86_FUNC(sub_00516370) {
    uint32_t self = ctx.ecx;
    const bool randomize = R32(ctx.esp + 4) == 0;
    __imp__sub_00516370(ctx, base);
    ContextGuard guard(ctx);
    auto* control = Get(self);
    if (!randomize || !control || !control->selection) return;
    uint32_t list = R32(self + 0x224);
    uint32_t selected = Virtual(ctx, list + 0x9C, 0x10);
    uint32_t text = Virtual(ctx, list, 0x7C, {selected});
    const auto name = text ? kernel::ReadCString(text, 16) : std::string{};
    if (name.size() >= 4) {
        const auto variant = kVariants.find(name[3]);
        const auto& presets = control->selection->Choices().presets;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].variant == variant) { control->selection->ApplyPreset(i); break; }
    }
    Refresh(ctx, self);
}

// Left/right press callbacks and their releases. The native dispatcher supplies keyboard and controller input.
X86_EXTERN_FUNC(__imp__sub_00538970);
X86_FUNC(sub_00538970) {
    if (Edit(ctx, ctx.ecx, -1, false)) return kernel::Return(ctx, 1, 1);
    __imp__sub_00538970(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_005389D0);
X86_FUNC(sub_005389D0) {
    if (Edit(ctx, ctx.ecx, 1, false)) return kernel::Return(ctx, 1, 1);
    __imp__sub_005389D0(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_005389A0);
X86_FUNC(sub_005389A0) {
    if (Get(ctx.ecx)) return kernel::Return(ctx, 1, 1);
    __imp__sub_005389A0(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_00538A00);
X86_FUNC(sub_00538A00) {
    if (Get(ctx.ecx)) return kernel::Return(ctx, 1, 1);
    __imp__sub_00538A00(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_00566BD0);
X86_FUNC(sub_00566BD0) {
    if (MoveRow(ctx, -1)) return kernel::Return(ctx, 1, 1);
    __imp__sub_00566BD0(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_00566C40);
X86_FUNC(sub_00566C40) {
    if (MoveRow(ctx, 1)) return kernel::Return(ctx, 1, 1);
    __imp__sub_00566C40(ctx, base);
}
// Keep the controller-assignment widget's unhandled return value: TeamSelect still receives this same press.
// Only TeamSelect moves the row, regardless of which of the two independently dispatched handlers runs first.
X86_EXTERN_FUNC(__imp__sub_000D3310);
X86_FUNC(sub_000D3310) {
    if (UniformOwnsDirection(ctx)) return kernel::Return(ctx, 0, 1);
    __imp__sub_000D3310(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_000D3560);
X86_FUNC(sub_000D3560) {
    if (UniformOwnsDirection(ctx)) return kernel::Return(ctx, 0, 1);
    __imp__sub_000D3560(ctx, base);
}

// Confirm this side. Keep only the preset in the native descriptor; component choices never enter a save.
X86_EXTERN_FUNC(__imp__sub_00516050);
X86_FUNC(sub_00516050) {
    uint32_t self = ctx.ecx;
    __imp__sub_00516050(ctx, base);
    ContextGuard guard(ctx);
    auto* control = Get(self);
    if (!control || !control->selection) return;
    uint32_t desc = MatchDescriptor(control->side);
    const auto& outfit = control->selection->Current();
    if (!desc || R32(desc) != outfit.team) return;
    Descriptor(ctx, desc, outfit.team, outfit.preset);
    g_outfits[control->side] = Committed{desc, outfit};
    LOGR("uniforms: side %u team %u preset %u, hat %s, jersey %s, pants %s", control->side, outfit.team,
         outfit.preset, outfit.pieces[0].id.c_str(), outfit.pieces[1].id.c_str(), outfit.pieces[2].id.c_str());
}

// Embedded controls use the non-deleting destructor too. Release strings only after their original teardown.
X86_EXTERN_FUNC(__imp__sub_00515C60);
X86_FUNC(sub_00515C60) {
    uint32_t self = ctx.ecx;
    __imp__sub_00515C60(ctx, base);
    g_controls.erase(self);
}

X86_EXTERN_FUNC(__imp__sub_00272FE0);
X86_FUNC(sub_00272FE0) {
    Release(ctx, ctx.ecx);
    __imp__sub_00272FE0(ctx, base);
}

// Configure a uniform instance for one match side. Loading the jersey donor through the original pipeline also
// chooses its cut, lettering, sleeve behavior and team branding. Extra resources replace only component slots.
X86_EXTERN_FUNC(__imp__sub_0027AAD0);
X86_FUNC(sub_0027AAD0) {
    uint32_t self = ctx.ecx, arg = ctx.esp + 4, source = R32(arg), side = R32(arg + 4);
    if (side >= g_outfits.size() || !source || !g_outfits[side] ||
        g_outfits[side]->descriptor != MatchDescriptor(side) || g_outfits[side]->outfit.team != R32(source) ||
        g_outfits[side]->outfit.preset != R32(source + 4)) {
        __imp__sub_0027AAD0(ctx, base);
        return;
    }
    const Outfit outfit = g_outfits[side]->outfit;
    GuestBuffer desc(12);
    if (!desc.address) { __imp__sub_0027AAD0(ctx, base); return; }
    Descriptor(ctx, desc.address, outfit.team, outfit.pieces[Index(Part::Jersey)].variant);
    struct ArgumentGuard {
        uint32_t slot, original;
        ~ArgumentGuard() { W32(slot, original); }
    } argument{arg, source};
    W32(arg, desc.address);
    __imp__sub_0027AAD0(ctx, base);
    ContextGuard guard(ctx);
    Compose(ctx, self, outfit);
}
