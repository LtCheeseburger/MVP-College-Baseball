#pragma once
#include "competition-bridge.h"
namespace mvp::specific_standings_candidate {
void Publish(X86Context&,uint8_t*,uint32_t cache);
void Invalidate(X86Context&,uint8_t*,uint32_t owner);
bool Defer(X86Context&,uint8_t*);
void Retry(X86Context&,uint8_t*,uint32_t world);
}
