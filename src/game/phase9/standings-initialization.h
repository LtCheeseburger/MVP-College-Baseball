#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
namespace mvp::standings_initialization {
inline uint32_t Word(const uint8_t* b,uint32_t at){uint32_t v;std::memcpy(&v,b+at,4);return v;}
inline bool AwaitingFirstSchedule(const uint8_t* b,uint32_t manager){
    // Original004E7850 constructs every table with base_day=-1 and used=0.
    // School selection publishes lists before00501110 imports the schedule;
    // its original0xAE event invalidates them after import. Only the exact
    // empty constructor state defers modern ordering. Invalid/mixed/populated
    // states still reach the callers' unchanged strict year/identity guards.
    const auto anchor=Word(b,manager+16);
    if(!anchor||Word(b,anchor+8)!=1||Word(b,anchor+20)!=UINT32_MAX)return false;
    for(unsigned slot=1;slot<72;++slot){
        const auto table=Word(b,manager+12+4*slot);if(!table)continue;
        if(Word(b,table+8)!=slot||Word(b,table+20)!=UINT32_MAX||Word(b,table+36))return false;
        const auto capacity=Word(b,table+28),data=Word(b,table+40);
        if(capacity>16384||uint64_t(data)+20ull*capacity>0x100000000ull)
            throw std::runtime_error("Standings: initial table bounds");
    }
    return true;
}
}
