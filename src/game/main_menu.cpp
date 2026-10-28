// Quit Game on the main menu. The layout row (src/common/main_menu_layout.cpp) has its text box named QUIT, so the
// MainMenu screen's select handler (0x517F60) opens the game's yes/no popup with the prompt string 15223. On Xbox its
// answer goes to an empty function (0x5012E0, shared by other popups); the PC version's own answer (0x518010) took
// answer 1 as yes. Here a yes to that popup ends the process the normal way: saves flushed, then exit.
#include <cstring>
#include <string>

#include "common/game_hooks.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kNoAnswer = 0x5012E0;        // the empty answer function the quit popup is given
constexpr uint32_t kQuitPrompt = 0x3B77;        // string 15223, the quit question
constexpr uint32_t kYes = 1;

bool g_quit_popup = false;  // the open popup is the main menu's quit question

}  // namespace

namespace game {

uint32_t QuitText() {
    static const uint32_t text = [] {
        const std::u16string s = u"Quit Game";
        uint32_t p = mem::AllocSystem(uint32_t((s.size() + 1) * 2));
        std::memcpy(kernel::HostPtr(p), s.c_str(), (s.size() + 1) * 2);
        return p;
    }();
    return text;
}

}  // namespace game

// MainMenu's select handler, thiscall (1 argument): it routes the selected row by name through feflow.txt, where
// QUIT has no route. The QUIT check with the popup (0x517F60, thiscall (control, 3 more)) is never reached on Xbox,
// so a QUIT row is handed to it here with the row grid (the "CELL" control) as the control.
X86_EXTERN_FUNC(__imp__sub_00517EE0);
X86_FUNC(sub_00517EE0) {
    constexpr uint32_t kCellName = 0x604910, kControlOf = 0xA60A0, kQuitCheck = 0x517F60;
    uint32_t screen = ctx.ecx, keep = ctx.ecx;
    ctx.ecx = screen;
    uint32_t widget = kernel::CallGuest(ctx, kernel::R32(kernel::R32(screen) + 0x7C), {kCellName, 1});
    ctx.ecx = widget;
    uint32_t cell = widget ? kernel::CallGuest(ctx, kControlOf, {}) : 0;
    uint32_t text = 0;
    if (cell) {
        ctx.ecx = cell;
        uint32_t row = kernel::CallGuest(ctx, kernel::R32(kernel::R32(cell) + 0x11C), {});
        ctx.ecx = row;
        text = row ? kernel::CallGuest(ctx, kernel::R32(kernel::R32(row) + 0x78), {5, 1}) : 0;
    }
    ctx.ecx = keep;
    std::string name = text ? kernel::ReadCString(text + 0x10, 16) : std::string();
    LOGR("main menu: selected %s", name.c_str());
    if (name == "QUIT") {
        ctx.ecx = screen;
        kernel::CallGuest(ctx, kQuitCheck, {cell, 0, 0, 0});
        g_quit_popup = true;
        ctx.ecx = keep;
        return kernel::Return(ctx, 1, 1);
    }
    __imp__sub_00517EE0(ctx, base);
}

// popup setup, thiscall (owner, answer function, ?, prompt string, ...)
X86_EXTERN_FUNC(__imp__sub_000E1620);
X86_FUNC(sub_000E1620) {
    // any other popup ends the quit question; the select hook marks it once the popup is up
    if (kernel::R32(ctx.esp + 8) != kNoAnswer || kernel::R32(ctx.esp + 16) != kQuitPrompt) g_quit_popup = false;
    else LOGR("main menu: quit popup opened");
    __imp__sub_000E1620(ctx, base);
}

// the empty popup answer, cdecl (answer, context)
X86_EXTERN_FUNC(__imp__sub_005012E0);
X86_FUNC(sub_005012E0) {
    if (g_quit_popup) {
        g_quit_popup = false;
        uint32_t answer = kernel::R32(ctx.esp + 4);
        LOGR("main menu: quit popup answered %u", answer);
        if (answer == kYes) kernel::Shutdown(0);
    }
    __imp__sub_005012E0(ctx, base);
}
