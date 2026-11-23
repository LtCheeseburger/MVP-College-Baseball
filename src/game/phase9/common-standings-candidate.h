#pragma once
#include "competition-bridge.h"

namespace mvp::common_standings_candidate {
// Called immediately before the original cache publishes its valid flag.
// Changes only the current, non-divided conferences with the shared policy.
void Publish(X86Context&, uint8_t*, uint32_t cache);
}
