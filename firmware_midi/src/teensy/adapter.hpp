#pragma once
#include "performance/engine.hpp"

namespace amen::teensy {
// Implement on Teensy with elapsedMillis/usbMIDI. The portable engine never includes Arduino.
class HardwarePort {
 public:
  virtual ~HardwarePort() = default;
  virtual void scanInputs(PerformanceEngine&, uint32_t now) = 0;
  virtual void send(const MidiEvent&) = 0;
};

inline void drain(HardwarePort& port, PerformanceEngine& engine) {
  for (std::size_t i = 0; i < engine.events().size(); ++i) port.send(engine.events()[i]);
  engine.clearEvents();
}

inline void service(HardwarePort& port, PerformanceEngine& engine, uint32_t now) {
  port.scanInputs(engine, now);
  engine.tick(now);
  drain(port, engine);
  // A saturated host queue may have deferred NoteOns. Retry only after the
  // first batch has physically drained, then transmit the recovered batch.
  if (engine.syncPending()) {
    engine.servicePending(now);
    drain(port, engine);
  }
}
}
