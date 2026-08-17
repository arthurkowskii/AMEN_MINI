#pragma once
#include "common/types.hpp"
namespace amen { struct PerformanceScene { HarmonySnapshot harmony{}; std::array<FxType,kFxPadCount> fx{}; std::array<FxMode,kFxPadCount> modes{}; uint16_t bpm{120}; }; }
