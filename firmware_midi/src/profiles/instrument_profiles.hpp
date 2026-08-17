#pragma once
#include "common/types.hpp"
namespace amen {
struct InstrumentProfile { const char* name; std::array<const char*,7> labels; std::array<uint8_t,7> cc; };
const std::array<InstrumentProfile,4>& instrumentProfiles() noexcept;
}
