#include "algorithms/algorithms.hpp"
#include <cstdint>

namespace amen {
void AlgorithmScheduler::start(FxType type, uint32_t now) noexcept {
  if (static_cast<uint8_t>(type) > static_cast<uint8_t>(FxType::Velocity)) {
    type_ = FxType::Blank;
    cancel();
    return;
  }
  type_ = type; active_ = type != FxType::Blank; next_ = now; step_ = 0;
}
void AlgorithmScheduler::cancel() noexcept { active_ = false; step_ = 0; }
AlgorithmFrame AlgorithmScheduler::tick(uint32_t now, uint16_t interval, uint8_t count) noexcept {
  AlgorithmFrame frame{};
  if (!active_ || now - next_ >= 0x80000000U) return frame;
  next_ = now + (interval ? interval : 1U); frame.emit = true; const uint8_t notes = count ? count : 1;
  switch (type_) {
    case FxType::Strum: frame.noteIndex = static_cast<int8_t>(step_ % notes); if (++step_ >= notes) active_ = false; break;
    case FxType::Arp: frame.noteIndex = static_cast<int8_t>((step_++) % notes); break;
    case FxType::RunUp: frame.noteIndex = static_cast<int8_t>(step_ % notes); if (++step_ >= notes) active_ = false; break;
    case FxType::RunDown: frame.noteIndex = static_cast<int8_t>(notes - 1U - (step_ % notes)); if (++step_ >= notes) active_ = false; break;
    case FxType::TranceGate: frame.gateOpen = (step_++ % 2U) == 0; break;
    case FxType::NoteRepeat: ++step_; break; // engine rearticulates the complete source voicing.
    case FxType::Random: random_ = random_ * 1664525U + 1013904223U; frame.noteIndex = static_cast<int8_t>((random_ >> 16U) % notes); break;
    case FxType::Velocity: frame.velocity = static_cast<uint8_t>(32U + (step_++ % 4U) * 31U); break;
    case FxType::Blank: frame.emit = false; break;
    default: cancel(); frame.emit = false; break;
  }
  return frame;
}
}
