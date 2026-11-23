#include "game/phase9.h"
#include "kernel/mods.h"
#include "common/overhaul.h"
#include "game/expansion.h"
#include <stdexcept>
#include <array>

namespace phase9 {
bool Enabled(Feature feature) {
    // Rule/capacity choices stay fixed for the session; changes require restart.
    static const auto enabled=[]{
        static const char* ids[]={"mvp27-annual-schedules","mvp27-competition","mvp27-standings-awards","mvp27-modern-dynasty","mvp27-saves-launcher"};
        const bool combined = kernel::mods::Enabled(overhaul::kCombinedModId);
        std::array<bool, 5> values{};
        for (unsigned i = 0; i < values.size(); ++i)
            values[i] = combined || kernel::mods::Enabled(ids[i]);
        return values;
    }();
    const auto index=static_cast<unsigned>(feature);
    return index<enabled.size() && enabled[index];
}
uint32_t ScheduleSlots() {return Enabled(Feature::AnnualSchedules)||Enabled(Feature::Competition)?72:37;}
uint32_t ScheduleManagerBytes() {return 12+4*ScheduleSlots();}
uint32_t ScheduleImporterExtra() {return 4*(ScheduleSlots()-37);}
uint32_t RegularSlot(uint32_t conference) {return ScheduleSlots()>37&&conference>14&&conference<=30?conference+22:conference;}
uint32_t FirstYear() {return Enabled(Feature::AnnualSchedules)?2027:2006;}
bool ScheduleSchool(uint32_t id){return expansion::SchoolActive(id);}
uint32_t CompactTokenBytes(uint32_t cursor,uint32_t buffer){if(cursor<buffer+20||(cursor-buffer-20)%12)throw std::runtime_error("Annual token cursor is outside its original 12-byte rows");return cursor-buffer-8;}
}
