#pragma once
#include "game/expansion.h"
#include <cstring>

// Only authored algorithms live here. Every original callee is dispatched to
// the player's local game DLL, with the native call/return stack contract.
namespace mvp::game_record_candidate {
inline uint32_t Read(const uint8_t* base,uint32_t at){uint32_t value;std::memcpy(&value,base+at,4);return value;}
}
namespace phase9 {
unsigned SeasonYear(const uint8_t* base);
uint32_t TournamentStart(const uint8_t* base,unsigned conference);
int32_t QualificationCutoff(const uint8_t* base,unsigned conference);
}
