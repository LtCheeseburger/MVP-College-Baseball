// Execute the production hooks against a small guest-memory/API fixture. This checks ABI, side isolation,
// descriptor preservation, atomic component fallback and lifetime; it is not a substitute for a native game run.
#include "common/uniforms.h"
#include "kernel/kernel.h"
#include "kernel/mods.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

#define CHECK(c) do { if (!(c)) { std::cerr << __LINE__ << ": " #c "\n"; std::exit(1); } } while (0)
using kernel::R32;
using kernel::W32;
using namespace game::uniforms;
X86_EXTERN_FUNC(sub_00515C70);
X86_EXTERN_FUNC(sub_00538830);
X86_EXTERN_FUNC(sub_00516290);
X86_EXTERN_FUNC(sub_00538970);
X86_EXTERN_FUNC(sub_005389D0);
X86_EXTERN_FUNC(sub_005389A0);
X86_EXTERN_FUNC(sub_00538A00);
X86_EXTERN_FUNC(sub_00566BD0);
X86_EXTERN_FUNC(sub_00566C40);
X86_EXTERN_FUNC(sub_00516050);
X86_EXTERN_FUNC(sub_00515C60);
X86_EXTERN_FUNC(sub_00272FE0);
X86_EXTERN_FUNC(sub_0027AAD0);

namespace {
std::vector<uint8_t> memory(32 << 20);
uint32_t allocation = 0x800000, focus = 0x20000, next_resource = 0x1000000;
constexpr uint32_t kDesc[] = {0x110AC, 0x120AC};
constexpr uint32_t kControl[] = {0x20000, 0x21000};
constexpr uint32_t kUniform[] = {0x60000, 0x61000};
std::set<uint32_t> live_buffers, live_resources;
std::map<uint32_t, std::string> resource_names, displayed;
std::map<std::string, unsigned> loads;
std::vector<Piece> additions;
bool missing_helmet = false;
bool missing_letter = false;
unsigned fallback_calls = 0;
unsigned derived_releases = 0, derived_rebuilds = 0;
uint32_t active_team[2] = {44, 44};
const std::map<std::string, unsigned> rows = {{"JERSEYNAME", 0}, {"UNIFORMHAT", 1},
    {"UNIFORMJERSEY", 2}, {"UNIFORMPANTS", 3}, {"UNIFORMHELP", 4}};
unsigned Tag(const std::string& tag) { unsigned hash = 0; for (unsigned char c : tag) hash = hash * 31 + c; return hash % 512; }
void Run(X86Func* fn, uint32_t self, std::initializer_list<uint32_t> args = {}) {
    X86Context ctx{};
    ctx.ecx = self; ctx.esp = 0x700000; ctx.ebx = 11; ctx.esi = 12; ctx.edi = 13; ctx.ebp = 14;
    W32(ctx.esp, 0xDEADBEEF);
    unsigned i = 1; for (auto arg : args) W32(ctx.esp + 4 * i++, arg);
    fn(ctx, memory.data());
    CHECK(ctx.esp == 0x700004 + args.size() * 4);
    CHECK(ctx.ebx == 11 && ctx.esi == 12 && ctx.edi == 13 && ctx.ebp == 14);
}
void Init(unsigned side) {
    uint32_t self = kControl[side];
    Run(sub_00515C70, self, {0, side == 0 ? 1u : 0u, 0});
    Run(sub_00538830, self);
}
}  // namespace

namespace logx { void Write(Level, const char*, ...) {} }
namespace mem {
uint32_t AllocSystem(uint32_t size, uint32_t) { uint32_t p = allocation; allocation += (size + 15) & ~15u; CHECK(allocation < 0xF00000); live_buffers.insert(p); return p; }
void FreeSystem(uint32_t p) { CHECK(live_buffers.erase(p) == 1); }
}
namespace game::uniforms { std::vector<Piece> LoadPieces(uint32_t) { return additions; } }
namespace kernel::mods { std::filesystem::path Find(const std::filesystem::path&, const std::string&) { return {}; } }
namespace kernel {
uint8_t* g_base = memory.data();
std::string ReadCString(uint32_t p, size_t max) { size_t n = 0; while (n < max && g_base[p + n]) ++n; return std::string(reinterpret_cast<char*>(g_base + p), n); }
std::string ResolvePath(const std::string&, uint32_t) { return "/game/data/models_big"; }
uint32_t CallGuest(X86Context& ctx, uint32_t fn, const uint32_t* args, size_t count) {
    switch (fn) {
        case 0x8C510: CHECK(count == 4); W32(ctx.ecx, args[0]); W32(ctx.ecx + 4, args[1]);
            W32(ctx.ecx + 8, args[0] + (args[1] % 2 ? 200 : 100)); return ctx.ecx;
        case 0xA60A0: return ctx.ecx;
        case 0x516430: CHECK(count == 1); return 0;
        case 0x25F140: ++derived_releases; return 0;
        case 0x25F230: ++derived_rebuilds; CHECK(derived_rebuilds == derived_releases); return 0;
        case 0x1068:  // group existence / text assignment
            if (ctx.ecx == 0x30000 || ctx.ecx == 0x31000) return rows.contains(ReadCString(args[0]));
            displayed[ctx.ecx] = ReadCString(args[0]); return 0;
        case 0x107C: return ctx.ecx + 0x100 + rows.at(ReadCString(args[0])) * 0x100;
        case 0x103C: return 0;
        case 0x1160: return 1;
        case 0x1150: return focus;
        case 0x2673B0: {
            std::string name = ReadCString(args[0]); ++loads[name];
            uint32_t handle = next_resource; next_resource += 0x1000;
            if (name == "letters.xsh") {
                W32(handle, 0x58504853); W32(handle + 4, 512); W32(handle + 8, missing_letter ? 35 : 36);
                for (unsigned i = 0; i < 36; ++i) {
                    const auto tag = i < 26 ? std::string(1, char('A' + i)) + "___" : "zig" + std::to_string(i - 26);
                    std::memcpy(HostPtr(handle + 16 + i * 8), tag.data(), 4);
                }
            }
            resource_names[handle] = name; live_resources.insert(handle); return handle;
        }
        case 0x423B10: {
            CHECK(live_resources.contains(args[0])); std::string tag = ReadCString(args[1]);
            if (missing_helmet && resource_names.at(args[0]) == "navy.xsh" && tag == "hlm2") return 0;
            return args[0] + Tag(tag);
        }
        case 0x2670E0: CHECK(args[1] == 0x10); CHECK(live_resources.erase(args[0]) == 1); return 0;
        case 0x326180: live_resources.erase(args[0]); return 0;
        default: std::cerr << "unexpected guest call " << std::hex << fn << '\n'; std::exit(1);
    }
}
}  // namespace kernel

X86_FUNC(__imp__sub_00515C70) {
    unsigned side = ctx.ecx == kControl[0] ? 0 : 1;
    W32(ctx.ecx + 0x23C, side); W32(ctx.ecx + 0x218, 0x30000 + side * 0x1000);
    W32(ctx.ecx + 0x238, active_team[side]); W32(kDesc[side], active_team[side]); W32(kDesc[side] + 4, side);
    kernel::Return(ctx, 0, 3);
}
X86_FUNC(__imp__sub_00538830) { W32(ctx.ecx + 0x238, active_team[ctx.ecx == kControl[0] ? 0 : 1]); kernel::Return(ctx, 0, 0); }
X86_FUNC(__imp__sub_00516290) { kernel::Return(ctx, 0, 0); }
X86_FUNC(__imp__sub_00516370) { kernel::Return(ctx, 0, 2); }
X86_FUNC(__imp__sub_00516050) { kernel::Return(ctx, 0, 0); }
X86_FUNC(__imp__sub_00515C60) { kernel::Return(ctx, 0, 0); }
#define FALLBACK(name) X86_FUNC(__imp__sub_##name) { ++fallback_calls; kernel::Return(ctx, 0, 1); }
FALLBACK(00538970) FALLBACK(005389D0) FALLBACK(005389A0) FALLBACK(00538A00) FALLBACK(00566BD0) FALLBACK(00566C40)
X86_FUNC(__imp__sub_00272FE0) {
    for (unsigned i = 0; i < 39; ++i) W32(ctx.ecx + 0x20 + 4 * i, 0);
    for (unsigned offset : {0xBCu, 0xC0u}) { live_resources.erase(R32(ctx.ecx + offset)); W32(ctx.ecx + offset, 0); }
    kernel::Return(ctx, 0, 0);
}
X86_FUNC(__imp__sub_0027AAD0) {
    uint32_t self = ctx.ecx, desc = R32(ctx.esp + 4);
    Run(sub_00272FE0, self);
    W32(self + 0xCC, R32(desc + 4)); W32(self + 0xD0, R32(desc));
    W32(self + 0xBC, 0x55000);  // sentinel donor lettering
    for (unsigned i = 0; i < 39; ++i) W32(self + 0x20 + 4 * i, 0x400000 + R32(desc + 4) * 0x1000 + i);
    kernel::Return(ctx, 0, 2);
}

int main() {
    // The guest's match/team chain and virtual controls. Asset ids intentionally differ from roster ids.
    W32(0x6C655C, 0x10000); W32(0x10008, 0x10100); W32(0x10114, 0x10200);
    W32(0x10204, kDesc[0] - 0xAC); W32(0x10208, kDesc[1] - 0xAC);
    for (unsigned offset : {0x68u, 0x7Cu, 0x3Cu, 0x160u, 0x150u}) W32(0x8000 + offset, 0x1000 + offset);
    W32(0x50000, 0x8000);
    for (unsigned side = 0; side < 2; ++side) for (unsigned i = 0; i < 6; ++i) W32(0x30000 + side * 0x1000 + i * 0x100, 0x8000);
    Init(0); Init(1);
    CHECK(displayed[0x30100].find("Home") != std::string::npos && displayed[0x31100].find("Away") != std::string::npos);
    Run(sub_00566C40, 0x50000, {0}); Run(sub_00566C40, 0x50000, {0}); // Jersey row
    Run(sub_005389D0, focus, {0}); Run(sub_00538A00, focus, {0}); // press + release = one change
    CHECK(displayed[0x30300].find("Away") != std::string::npos && displayed[0x30200].find("Home") != std::string::npos);
    Run(sub_00516050, focus); Run(sub_00516050, kControl[1]);
    Run(sub_0027AAD0, kUniform[0], {kDesc[0], 0});
    CHECK(R32(kDesc[0] + 4) == 0 && R32(kUniform[0] + 0xCC) == 1); // native preset preserved, jersey donor loaded
    CHECK(loads["u144a.xsh"] == 1 && live_resources.size() == 1); // hat and pants share one owned pack
    CHECK(derived_releases == 1 && derived_rebuilds == 1);
    uint32_t extra = *live_resources.begin();
    for (Part part : {Part::Hat, Part::Pants}) for (const auto& slot : Slots(part))
        CHECK(R32(kUniform[0] + 0x20 + slot.slot * 4) == extra + Tag(slot.tag));
    CHECK(R32(kUniform[0] + 0x74) == 0x401000 + 21); // unrelated allocated/lettering slot untouched
    Run(sub_0027AAD0, kUniform[1], {kDesc[1], 1}); CHECK(live_resources.size() == 1); // same team away remains stock
    Run(sub_0027AAD0, kUniform[0], {kDesc[0], 0}); CHECK(live_resources.size() == 1); // reload frees old handle
    Run(sub_00272FE0, kUniform[0]); CHECK(live_resources.empty());
    Init(0); // a new matchup discards the committed combination even with identical team and reused addresses
    Run(sub_0027AAD0, kUniform[0], {kDesc[0], 0}); CHECK(live_resources.empty() && R32(kUniform[0] + 0xCC) == 0);
    additions = {{"navy", "Navy", Part::Hat, 44, 0, "navy.xsh", {}}};
    Init(0); Run(sub_00566C40, 0x50000, {0}); Run(sub_00538970, focus, {0}); Run(sub_005389A0, focus, {0});
    CHECK(displayed[0x30200].find("Navy") != std::string::npos);
    Run(sub_00516050, focus); missing_helmet = true;
    Run(sub_0027AAD0, kUniform[0], {kDesc[0], 0});
    uint32_t donor = 0; for (auto p : live_resources) if (resource_names[p] == "u144a.xsh") donor = p;
    CHECK(donor);
    for (const auto& slot : Slots(Part::Hat)) CHECK(R32(kUniform[0] + 0x20 + slot.slot * 4) == donor + Tag(slot.tag));
    active_team[0] = 45; W32(kDesc[0], 45); Run(sub_00538830, focus);
    CHECK(displayed[0x30200].find("Home") != std::string::npos);
    Run(sub_0027AAD0, kUniform[0], {kDesc[0], 0}); CHECK(live_resources.empty());
    active_team[0] = 44;
    additions = {{"silver", "Silver", Part::Jersey, 44, 0, "silver.xsh", "letters.xsh"}};
    Init(0); Run(sub_00566C40, 0x50000, {0}); Run(sub_00566C40, 0x50000, {0});
    Run(sub_00538970, focus, {0}); Run(sub_00516050, focus);
    Run(sub_0027AAD0, kUniform[0], {kDesc[0], 0});
    CHECK(resource_names.at(R32(kUniform[0] + 0xBC)) == "letters.xsh");
    CHECK(live_resources.size() == 2);
    Run(sub_00272FE0, kUniform[0]); CHECK(live_resources.empty());
    missing_letter = true;
    Run(sub_0027AAD0, kUniform[0], {kDesc[0], 0});
    CHECK(R32(kUniform[0] + 0xBC) == 0x55000 && live_resources.size() == 1); // incomplete font discarded
    Run(sub_00272FE0, kUniform[0]); CHECK(live_resources.empty());
    Run(sub_005389D0, 0x25000, {0}); CHECK(fallback_calls == 1); // other screens retain original behavior
    Run(sub_00515C60, kControl[0]); Run(sub_00515C60, kControl[1]);
    CHECK(live_buffers.empty());
    std::cout << "production uniform hooks: ABI, selection, side isolation, fallback and cleanup passed\n";
}
