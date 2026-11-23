#include "game/phase9.cpp"
#include <set>
#include <iostream>
namespace {std::set<std::string> enabled;}
namespace kernel::mods {bool Enabled(const std::string& id){return enabled.contains(id);}}
namespace expansion {bool SchoolActive(uint32_t id){return id>0&&id<=310&&id!=102;}}
int main(int argc,char** argv){
    std::string mode=argc==2?argv[1]:"off";bool annual=mode=="annual",modern=mode=="modern";
    if(annual)enabled.insert("mvp27-annual-schedules");if(modern)enabled.insert("mvp27-modern-dynasty");
    if(phase9::ScheduleSlots()!=(annual?72:37)||phase9::ScheduleManagerBytes()!=(annual?300:160)||phase9::ScheduleImporterExtra()!=(annual?140:0)||phase9::FirstYear()!=(annual?2027:2006)||phase9::RegularSlot(15)!=(annual?37:15)||phase9::Enabled(phase9::Feature::ModernDynasty)!=modern)throw std::runtime_error("optional capability routing");
    if(phase9::ScheduleSchool(102)||!phase9::ScheduleSchool(310)||phase9::CompactTokenBytes(0x1000+20+309*12,0x1000)!=310*12)throw std::runtime_error("active token compaction");
    std::cout<<"PASS "<<mode<<": optional capabilities, disabled baseline, table slots/year and retired-school token compaction\n";
}
