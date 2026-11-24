#include "uniforms.h"
#include "kernel/modvfs.h"

namespace {
const bool g_registered = [] {
    kernel::modvfs::AddPatch("data/frontend/frontend.big", "fes_teamselect.fel", game::uniforms::PatchLayout);
    return true;
}();
}
