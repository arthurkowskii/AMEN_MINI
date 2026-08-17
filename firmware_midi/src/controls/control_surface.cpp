#include "controls/control_surface.hpp"
#include <algorithm>
#include <cstdint>
namespace amen {
int applyRelative(int value, int delta, int low, int high) noexcept {
  const auto widened = static_cast<int64_t>(value) + static_cast<int64_t>(delta);
  return static_cast<int>(std::clamp(widened, static_cast<int64_t>(low), static_cast<int64_t>(high)));
}
}
