#pragma once
#include "common/types.hpp"
namespace amen {
struct AlgorithmFrame { bool emit{}; bool gateOpen{true}; int8_t noteIndex{}; uint8_t velocity{100}; };
class AlgorithmScheduler { public: void start(FxType type,uint32_t now)noexcept; void cancel()noexcept; AlgorithmFrame tick(uint32_t now,uint16_t intervalMs,uint8_t noteCount)noexcept; bool active()const noexcept{return active_;} FxType type()const noexcept{return type_;}
 private: FxType type_{FxType::Blank}; bool active_{}; uint32_t next_{}; uint16_t step_{}; uint32_t random_{0x12345678U};
}; }
