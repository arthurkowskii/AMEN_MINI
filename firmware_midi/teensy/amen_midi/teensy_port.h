#pragma once
#include "src/performance/engine.hpp"
#include "src/teensy/adapter.hpp"
#include "src/teensy/teensy_pinmap.h"
#include "src/ui/text_model.hpp"
#include <Adafruit_SSD1306.h>
#include <array>
#include <cstdint>

// Teensy 4.1 hardware port for AMEN MIDI.
// This is the ONLY Arduino-dependent layer; the engine core stays portable.
namespace amen::teensy {

class TeensyPort final : public HardwarePort {
 public:
  TeensyPort();
  void begin();
  void scanInputs(PerformanceEngine& engine, uint32_t now) override;
  void send(const MidiEvent& event) override;
  void refreshDisplay(const PerformanceEngine& engine, uint32_t now);

 private:
  enum class KeyRole : uint8_t { None, Pad, Fx, Shift };
  struct KeyDef { KeyRole role; uint8_t index; };

  // Physical layout derived from hardware/AMEN_MINI.net + AMEN_MINI.kicad_sch
  // (switch symbols sorted by schematic coordinates). Rows 0..4 = ROW0..ROW4
  // (top to bottom on the panel), cols 0..3 = COL0..COL3, col 4 = COL_SHIFT.
  // Bottom 3 rows = 12 musical pads, top 2 rows = 8 FX pads, SW21 = Shift.
  static constexpr std::array<std::array<KeyDef, 5>, 5> kKeyMap{{
      {{{KeyRole::Fx, 4}, {KeyRole::Fx, 5}, {KeyRole::Fx, 6}, {KeyRole::Fx, 7}, {KeyRole::None, 0}}},  // ROW0: SW17..20
      {{{KeyRole::Fx, 0}, {KeyRole::Fx, 1}, {KeyRole::Fx, 2}, {KeyRole::Fx, 3}, {KeyRole::None, 0}}},  // ROW1: SW13..16
      {{{KeyRole::Pad, 8}, {KeyRole::Pad, 9}, {KeyRole::Pad, 10}, {KeyRole::Pad, 11}, {KeyRole::None, 0}}},  // ROW2: SW9..12
      {{{KeyRole::Pad, 4}, {KeyRole::Pad, 5}, {KeyRole::Pad, 6}, {KeyRole::Pad, 7}, {KeyRole::None, 0}}},  // ROW3: SW5..8
      {{{KeyRole::Pad, 0}, {KeyRole::Pad, 1}, {KeyRole::Pad, 2}, {KeyRole::Pad, 3}, {KeyRole::Shift, 0}}},  // ROW4: SW1..4 + SW21
  }};

  void scanMatrix(PerformanceEngine& engine, uint32_t now);
  void scanEncoders(PerformanceEngine& engine, uint32_t now);
  void handleKey(KeyRole role, uint8_t index, bool pressed, PerformanceEngine& engine, uint32_t now);

  Adafruit_SSD1306 display_{128, 64, &Wire};
  uint32_t lastScanMs_{0};
  uint32_t lastDisplayMs_{0};
  std::array<std::array<uint8_t, 5>, 5> debounce_{};
  std::array<std::array<bool, 5>, 5> pressed_{};
  std::array<uint8_t, 7> encPos_{};      // quadrature position accumulator
  std::array<uint8_t, 7> encPrev_{};     // last AB state (bits 0,1)
  std::array<uint8_t, 7> encDebounce_{};
  std::array<bool, 7> encPressed_{};
  std::array<std::array<char, 32>, 4> lastLines_{};
  bool displayValid_{false};
};

}  // namespace amen::teensy
