#pragma once

// SDK consumers and small standalone checks default to the player feature set.
#ifndef MVP06_DEV_TOOLS
#define MVP06_DEV_TOOLS 0
#endif

#if MVP06_DEV_TOOLS
#include <cstdlib>
#endif

namespace dev {
inline constexpr bool Enabled = MVP06_DEV_TOOLS != 0;
#if MVP06_DEV_TOOLS
inline const char* Env(const char* name) { return std::getenv(name); }
#else
// A constant result removes diagnostic branches (including synthetic input)
// without consulting the player's environment.
constexpr const char* Env(const char*) { return nullptr; }
#endif
}
