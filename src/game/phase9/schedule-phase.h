#pragma once
#include <cstdint>
struct X86Context;
namespace phase9 {
void RefreshConferenceResets(X86Context&,uint8_t*,uint32_t manager);
void ResumePostseason(X86Context& ctx,uint8_t* base);
// Native phase classes remain unchanged: regular=0, nonconference=1,
// conference tournament=2, regionals=3, super regionals=4, CWS=5.
// This maps only the added owners for classification, never for table lookup.
constexpr uint32_t LegacyPhaseSlot(uint32_t slot){
 return slot>=37 && slot<=52?1u:slot>=53 && slot<=71?16u:slot;
}
}
