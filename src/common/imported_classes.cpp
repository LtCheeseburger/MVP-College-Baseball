#include "imported_classes.h"
#include "game/phase9.h"
#include "kernel/mods.h"
#include "kernel/modvfs.h"
#include "core/log.h"
namespace {
bool Apply(std::vector<uint8_t>& bytes) {
    if(!phase9::Enabled(phase9::Feature::ModernDynasty))return true;
    std::vector<uint8_t> ledger;
    if(!kernel::mods::Load(kernel::mods::GameDir()/"data/league/graduate-classes.csv",ledger,1024*1024))return true;
    const bool restored=game::RestoreGraduateClasses(bytes,std::string_view(reinterpret_cast<const char*>(ledger.data()),ledger.size()));
    if(restored)LOGR("modern dynasty: source graduate classes restored in the mounted player table");
    return restored;
}
const bool registered=[] {kernel::modvfs::AddPatch("data/database/database.big","attrib.dat",Apply);return true;}();
}
