#include "teensy_port.h"
#include <Arduino.h>
#include <Wire.h>

namespace amen::teensy {

TeensyPort::TeensyPort() {
  for (auto& line : lastLines_) line.fill('\0');
}

void TeensyPort::begin() {
  // Matrix: rows are outputs driven LOW one at a time; columns are inputs
  // with pull-ups. A pressed key conducts through its diode (anode on the
  // column side) and pulls the column LOW.
  for (int pin : kRows) pinMode(pin, OUTPUT);
  for (int pin : kColumns) pinMode(pin, INPUT_PULLUP);
  for (int pin : kRows) digitalWrite(pin, HIGH);  // all rows idle HIGH

  // Encoders: A/B quadrature + push button, all inputs with pull-ups.
  for (const auto& enc : kEncoders) {
    pinMode(enc[0], INPUT_PULLUP);
    pinMode(enc[1], INPUT_PULLUP);
    pinMode(enc[2], INPUT_PULLUP);
  }

  Wire.begin();
  display_.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display_.clearDisplay();
  display_.setTextSize(1);
  display_.setTextColor(SSD1306_WHITE);
  display_.setCursor(0, 0);
  display_.print("AMEN MIDI boot");
  display_.display();

  // Remember the initial AB state of each encoder so the first detent is
  // not counted as a spurious step.
  for (std::size_t i = 0; i < kEncoders.size(); ++i) {
    encPrev_[i] = (digitalRead(kEncoders[i][0]) ? 2u : 0u) |
                  (digitalRead(kEncoders[i][1]) ? 1u : 0u);
  }
}

void TeensyPort::scanInputs(PerformanceEngine& engine, uint32_t now) {
  if (now - lastScanMs_ < 2) return;  // ~500 Hz scan cadence
  lastScanMs_ = now;
  scanMatrix(engine, now);
  scanEncoders(engine, now);
}

void TeensyPort::scanMatrix(PerformanceEngine& engine, uint32_t now) {
  for (std::size_t r = 0; r < kRows.size(); ++r) {
    digitalWrite(kRows[r], LOW);  // activate row
    for (std::size_t c = 0; c < kColumns.size(); ++c) {
      const bool raw = digitalRead(kColumns[c]) == LOW;
      const auto key = kKeyMap[r][c];
      if (key.role == KeyRole::None) {
        // Keep reading so the shared column stays stable; nothing to emit.
        (void)raw;
        continue;
      }
      debounce_[r][c] = raw ? static_cast<uint8_t>(debounce_[r][c] + 1) : 0;
      if (debounce_[r][c] > 3) debounce_[r][c] = 3;
      const bool pressed = debounce_[r][c] == 3;
      if (pressed != pressed_[r][c]) {
        pressed_[r][c] = pressed;
        handleKey(key.role, key.index, pressed, engine, now);
      }
    }
    digitalWrite(kRows[r], HIGH);  // deactivate row
  }
}

void TeensyPort::handleKey(KeyRole role, uint8_t index, bool pressed,
                           PerformanceEngine& engine, uint32_t now) {
  switch (role) {
    case KeyRole::Pad:
      if (pressed) engine.noteDown(index, 100, now);
      else engine.noteUp(index, now);
      break;
    case KeyRole::Fx:
      if (pressed) engine.fxDown(index, now);
      else engine.fxUp(index, now);
      break;
    case KeyRole::Shift:
      if (pressed) engine.shiftDown(now);
      else engine.shiftUp(now);
      break;
    default:
      break;
  }
}

void TeensyPort::scanEncoders(PerformanceEngine& engine, uint32_t now) {
  // Standard 4-state quadrature decode. If rotation feels inverted on the
  // hardware, swap the A/B pins in teensy_pinmap.h (see HARDWARE_VALIDATION.md).
  static constexpr int8_t kQuad[16] = {
      0, 1, -1, 0,  -1, 0, 0, 1,  1, 0, 0, -1,  0, -1, 1, 0};
  for (std::size_t i = 0; i < kEncoders.size(); ++i) {
    const uint8_t ab = (digitalRead(kEncoders[i][0]) ? 2u : 0u) |
                       (digitalRead(kEncoders[i][1]) ? 1u : 0u);
    const uint8_t idx = static_cast<uint8_t>((encPrev_[i] << 2) | ab);
    encPrev_[i] = ab;
    if (idx < 16) encPos_[i] = static_cast<uint8_t>(encPos_[i] + kQuad[idx]);

    // EC11: one full detent = 4 quadrature transitions. Emit once per detent
    // and keep the remainder, so partial rotation never gets lost.
    if (encPos_[i] >= 4) {
      encPos_[i] = static_cast<uint8_t>(encPos_[i] - 4);
      engine.encoderTurn(static_cast<uint8_t>(i + 1), +1, now);
    } else if (encPos_[i] <= 252) {  // wrapped negative
      encPos_[i] = static_cast<uint8_t>(encPos_[i] + 4);
      engine.encoderTurn(static_cast<uint8_t>(i + 1), -1, now);
    }

    // Push button, same 3-sample debounce as matrix keys.
    const bool raw = digitalRead(kEncoders[i][2]) == LOW;
    encDebounce_[i] = raw ? static_cast<uint8_t>(encDebounce_[i] + 1) : 0;
    if (encDebounce_[i] > 3) encDebounce_[i] = 3;
    const bool pressed = encDebounce_[i] == 3;
    if (pressed && !encPressed_[i]) {
      engine.encoderClick(static_cast<uint8_t>(i + 1), now);
    }
    encPressed_[i] = pressed;
  }
}

void TeensyPort::send(const MidiEvent& event) {
  switch (event.type) {
    case MidiType::NoteOn:
      usbMIDI.sendNoteOn(event.data1, event.data2, event.channel);
      break;
    case MidiType::NoteOff:
      usbMIDI.sendNoteOff(event.data1, event.data2, event.channel);
      break;
    case MidiType::ControlChange:
      usbMIDI.sendControlChange(event.data1, event.data2, event.channel);
      break;
  }
}

void TeensyPort::refreshDisplay(const PerformanceEngine& engine, uint32_t now) {
  if (now - lastDisplayMs_ < 100) return;  // ~10 Hz cap
  lastDisplayMs_ = now;

  const UiTextModel ui = makeUiText(engine);
  const std::array<const char*, 4> lines{{ui.line1.data(), ui.line2.data(),
                                          ui.line3.data(), ui.line4.data()}};

  bool changed = !displayValid_;
  for (std::size_t i = 0; i < 4 && !changed; ++i) {
    for (std::size_t j = 0; j < 32; ++j) {
      if (lines[i][j] != lastLines_[i][j]) { changed = true; break; }
    }
  }
  if (!changed) return;

  for (std::size_t i = 0; i < 4; ++i) {
    std::size_t j = 0;
    while (lines[i][j] != '\0' && j < 32) { lastLines_[i][j] = lines[i][j]; ++j; }
    while (j < 32) lastLines_[i][j++] = '\0';
  }

  display_.clearDisplay();
  display_.setTextSize(1);
  display_.setTextColor(SSD1306_WHITE);
  for (std::size_t i = 0; i < 4; ++i) {
    display_.setCursor(0, static_cast<int16_t>(i * 8));
    // 128 px / 6 px per char = 21 chars at size 1.
    char buf[22];
    std::size_t k = 0;
    while (lastLines_[i][k] != '\0' && k < 21) { buf[k] = lastLines_[i][k]; ++k; }
    buf[k] = '\0';
    display_.print(buf);
  }
  display_.display();
  displayValid_ = true;
}

}  // namespace amen::teensy
