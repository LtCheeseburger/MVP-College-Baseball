#pragma once
#include "competition-bridge.h"
#include <utility>

namespace mvp::bracket_browser {
// A bracket may display a placeholder before its schedule owner exists.
// Validate both descriptor words before the native renderer reads a game row.
template<class Committed>
inline uint32_t ResolveRow(const uint8_t* base,uint32_t manager,uint32_t descriptor,unsigned slots,Committed committed){
 const auto read=[&](uint32_t p){return game_record_candidate::Read(base,p);};
 if(!committed(descriptor,8)||!committed(manager,12+4*slots))return 0;
 const auto slot=read(descriptor),index=read(descriptor+4);if(slot>=slots)return 0;
 const auto table=read(manager+12+4*slot);if(!committed(table,44))return 0;
 const auto count=read(table+36),rows=read(table+40);
 if(count>40000||index>=count||!committed(rows,count*20))return 0;
 return rows+20*index;
}
inline bool EmptyVisualOrdinal(uint32_t caller,unsigned ordinal,unsigned count){return (caller==0x00520403u||caller==0x00520414u||caller==0x00520442u||caller==0x00520453u)&&ordinal>=count;}
// Return the native (table, row) descriptor, rather than assuming that every
// bracket fits the original twelve-descriptor search window.
inline std::pair<uint32_t,uint32_t> Pending(const uint8_t* base,uint32_t child) {
    const auto read=[&](uint32_t at){return game_record_candidate::Read(base,at);};
    const auto root=read(child+0x64),games=read(child+0x60),descriptors=read(child+0x6C);
    if(!root || !descriptors || games>128)return {0xFFFFFFFFu,0xFFFFFFFFu};
    const auto count=read(root+0x24),rows=read(root+0x28);
    if(!rows)return {0xFFFFFFFFu,0xFFFFFFFFu};
    for(unsigned i=0;i<games;++i){
        const auto descriptor=descriptors+12*i,index=read(descriptor+4);
        if(index>=count)continue;
        // Status 2 is a cancelled if-needed game; status 3 is final.
        if(((read(rows+20*index)>>20)&15)<2)
            return {read(descriptor),index};
    }
    return {0xFFFFFFFFu,0xFFFFFFFFu};
}
}


