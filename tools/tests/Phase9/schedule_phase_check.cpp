#include "game/phase9/schedule-phase.h"
#include <cassert>
#include <iostream>
int main(){
 for(unsigned slot=0;slot<37;++slot)assert(phase9::LegacyPhaseSlot(slot)==slot);
 for(unsigned slot=37;slot<=52;++slot)assert(phase9::LegacyPhaseSlot(slot)==1);
 for(unsigned slot=53;slot<=71;++slot)assert(phase9::LegacyPhaseSlot(slot)==16);
 for(unsigned slot:{72u,99u,0xffffffffu})assert(phase9::LegacyPhaseSlot(slot)==slot);
 std::cout<<"PASS all legacy/NCAA slots preserved, 16 added regular and 19 added conference slots classified, invalid slots preserved\n";
}
