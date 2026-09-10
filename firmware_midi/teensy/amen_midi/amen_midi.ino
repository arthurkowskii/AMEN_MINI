#include <Arduino.h>
#include <IntervalTimer.h>
#include <Wire.h>

#include "oled_ui.h"
#include "simple_midi_controller.h"

constexpr uint8_t ROWS[] = {5, 6, 9, 14, 15};
constexpr uint8_t COLS[] = {0, 1, 2, 3, 4};
constexpr uint8_t ENCODER_A[] = {16, 22, 25, 27, 29, 31, 33};
constexpr uint8_t ENCODER_B[] = {17, 24, 26, 28, 30, 32, 34};
constexpr uint8_t PUSH[] = {35, 36, 37, 38, 39, 40, 41};
constexpr int8_t QUADRATURE[] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
constexpr uint32_t SCAN_US = 500;
constexpr uint32_t DEBOUNCE_US = 5000;
constexpr uint8_t SCANNED_KEYS = 21;

volatile bool contacts[21] = {};
bool raw[21] = {};
uint32_t changedAt[21] = {};
volatile int32_t encoderPositions[7] = {};
uint8_t encoderAb[7] = {};
int8_t encoderPartial[7] = {};
volatile bool e1Push = false;
bool e1PushRaw = false;
uint32_t e1PushChangedAt = 0;
volatile bool e2Push = false;
bool e2PushRaw = false;
uint32_t e2PushChangedAt = 0;
volatile bool e3Push = false;
bool e3PushRaw = false;
uint32_t e3PushChangedAt = 0;
volatile bool e5Push = false;
bool e5PushRaw = false;
uint32_t e5PushChangedAt = 0;
volatile uint32_t scanCount = 0;
bool firstScan = true;
IntervalTimer scanTimer;

amen::SimpleMidiController controller;
bool previousContacts[21] = {};
int32_t previousEncoderPositions[7] = {};
bool previousE1Push = false;
bool previousE2Push = false;
bool previousE3Push = false;
bool previousE5Push = false;
bool inputReady = false;
amen::OledUi oledUi;
amen::E1Page e1Page = amen::E1Page::Octave;
amen::E2Page e2Page = amen::E2Page::Root;
std::array<uint8_t, amen::MonoFramebuffer::kSize> displayedFrame{};
std::array<uint8_t, amen::MonoFramebuffer::kSize> pendingFrame{};
size_t displayOffset = 0;
bool displayPending = false;
uint8_t oledAddress = 0;
uint32_t lastDisplayAt = 0;
bool oledReady = false;

bool oledWrite(uint8_t control, const uint8_t* data, size_t count) {
    Wire.beginTransmission(oledAddress);
    Wire.write(control);
    const bool complete = Wire.write(data, count) == count;
    return complete && Wire.endTransmission() == 0;
}

bool beginOled() {
    Wire.begin();
    Wire.setClock(400000);
    for (uint8_t address = 0x3C; address <= 0x3D; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            oledAddress = address;
            break;
        }
    }
    if (oledAddress == 0) return false;
    const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
                            0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x02,
                            0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    return oledWrite(0x00, init, sizeof(init));
}

void queueDisplay(const amen::MonoFramebuffer& framebuffer) {
    const auto& pixels = framebuffer.pixels();
    if ((!displayPending && pixels == displayedFrame) || (displayPending && pixels == pendingFrame)) return;
    pendingFrame = pixels;
    displayOffset = 0;
    displayPending = true;
}

bool flushDisplay() {
    if (!displayPending) return true;
    if (displayOffset == 0) {
        const uint8_t window[] = {0x21, 0, 127, 0x22, 0, 3};
        if (!oledWrite(0x00, window, sizeof(window))) return false;
    }
    if (!oledWrite(0x40, pendingFrame.data() + displayOffset, 16)) return false;
    displayOffset += 16;
    if (displayOffset == pendingFrame.size()) {
        displayedFrame = pendingFrame;
        displayPending = false;
    }
    return true;
}

void scanInputs() {
    const uint32_t now = micros();
    bool sample[21];
    const bool e1PushSample = !digitalRead(PUSH[0]);
    const bool e2PushSample = !digitalRead(PUSH[1]);
    const bool e3PushSample = !digitalRead(PUSH[2]);
    const bool e5PushSample = !digitalRead(PUSH[4]);

    for (uint8_t row = 0; row < 5; ++row) {
        digitalWrite(ROWS[row], LOW);
        pinMode(ROWS[row], OUTPUT);
        delayMicroseconds(3);
        for (uint8_t col = 0; col < 4; ++col) sample[(4 - row) * 4 + col] = !digitalRead(COLS[col]);
        if (row == 4) sample[20] = !digitalRead(COLS[4]);
        pinMode(ROWS[row], INPUT);
    }

    for (uint8_t i = 0; i < 7; ++i) {
        const uint8_t ab = (digitalRead(ENCODER_A[i]) << 1) | digitalRead(ENCODER_B[i]);
        const uint8_t before = encoderAb[i];
        if (!firstScan && ab != before) {
            if ((ab ^ before) == 3) {
                encoderPartial[i] = 0;
            } else {
                encoderPartial[i] += QUADRATURE[before * 4 + ab];
                if (encoderPartial[i] == 4 || encoderPartial[i] == -4) {
                    encoderPositions[i] += encoderPartial[i] > 0 ? 1 : -1;
                    encoderPartial[i] = 0;
                }
            }
        }
        encoderAb[i] = ab;
    }

    for (uint8_t i = 0; i < 21; ++i) {
        if (firstScan) {
            raw[i] = contacts[i] = sample[i];
            changedAt[i] = now;
        } else if (sample[i] != raw[i]) {
            raw[i] = sample[i];
            changedAt[i] = now;
        }
        if (contacts[i] != raw[i] && now - changedAt[i] >= DEBOUNCE_US) contacts[i] = raw[i];
    }

    if (firstScan) {
        e1PushRaw = e1Push = e1PushSample;
        e1PushChangedAt = now;
    } else if (e1PushSample != e1PushRaw) {
        e1PushRaw = e1PushSample;
        e1PushChangedAt = now;
    }
    if (e1Push != e1PushRaw && now - e1PushChangedAt >= DEBOUNCE_US) e1Push = e1PushRaw;

    if (firstScan) {
        e2PushRaw = e2Push = e2PushSample;
        e2PushChangedAt = now;
    } else if (e2PushSample != e2PushRaw) {
        e2PushRaw = e2PushSample;
        e2PushChangedAt = now;
    }
    if (e2Push != e2PushRaw && now - e2PushChangedAt >= DEBOUNCE_US) e2Push = e2PushRaw;

    if (firstScan) {
        e3PushRaw = e3Push = e3PushSample;
        e3PushChangedAt = now;
    } else if (e3PushSample != e3PushRaw) {
        e3PushRaw = e3PushSample;
        e3PushChangedAt = now;
    }
    if (e3Push != e3PushRaw && now - e3PushChangedAt >= DEBOUNCE_US) e3Push = e3PushRaw;

    if (firstScan) {
        e5PushRaw = e5Push = e5PushSample;
        e5PushChangedAt = now;
    } else if (e5PushSample != e5PushRaw) {
        e5PushRaw = e5PushSample;
        e5PushChangedAt = now;
    }
    if (e5Push != e5PushRaw && now - e5PushChangedAt >= DEBOUNCE_US) e5Push = e5PushRaw;

    firstScan = false;
    ++scanCount;
}

void sendMidi(const amen::MidiCommand& command) {
    const uint8_t channel = controller.midiChannel();
    if (command.type == amen::MidiCommandType::NoteOn) {
        usbMIDI.sendNoteOn(command.note, command.velocity, channel);
    } else if (command.type == amen::MidiCommandType::NoteOff) {
        usbMIDI.sendNoteOff(command.note, command.velocity, channel);
    } else if (command.type == amen::MidiCommandType::ControlChange) {
        usbMIDI.sendControlChange(command.note, command.velocity, channel);
    }
}

void setup() {
    Serial.begin(115200);
    for (uint8_t pin : ROWS) {
        pinMode(pin, INPUT);
        digitalWrite(pin, LOW);
    }
    for (uint8_t pin : COLS) pinMode(pin, INPUT_PULLUP);
    for (uint8_t i = 0; i < 7; ++i) {
        pinMode(ENCODER_A[i], INPUT_PULLUP);
        pinMode(ENCODER_B[i], INPUT_PULLUP);
        pinMode(PUSH[i], INPUT_PULLUP);
        encoderAb[i] = (digitalRead(ENCODER_A[i]) << 1) | digitalRead(ENCODER_B[i]);
    }
    delayMicroseconds(20);
    oledReady = beginOled();
    scanTimer.begin(scanInputs, SCAN_US);
    scanTimer.priority(64);
    Serial.println("AMEN MIDI E1 OCTAVE, E2 RATE, E3 ROOT, E4 SCALE, E5 MODE/BANK, E6 ASSIGN");
    if (!oledReady) Serial.println("OLED unavailable");
}

void loop() {
    bool contactSnapshot[21];
    int32_t encoderSnapshot[7];
    bool e1PushSnapshot;
    bool e2PushSnapshot;
    bool e3PushSnapshot;
    bool e5PushSnapshot;
    uint32_t scans;

    noInterrupts();
    for (uint8_t i = 0; i < 21; ++i) contactSnapshot[i] = contacts[i];
    for (uint8_t i = 0; i < 7; ++i) encoderSnapshot[i] = encoderPositions[i];
    e1PushSnapshot = e1Push;
    e2PushSnapshot = e2Push;
    e3PushSnapshot = e3Push;
    e5PushSnapshot = e5Push;
    scans = scanCount;
    interrupts();

    if (!inputReady) {
        if (scans < 20) return;
        for (uint8_t i = 0; i < 21; ++i) previousContacts[i] = contactSnapshot[i];
        for (uint8_t i = 0; i < 7; ++i) previousEncoderPositions[i] = encoderSnapshot[i];
        previousE1Push = e1PushSnapshot;
        previousE2Push = e2PushSnapshot;
        previousE3Push = e3PushSnapshot;
        previousE5Push = e5PushSnapshot;
        inputReady = true;
        return;
    }

    bool sent = false;
    amen::MidiCommand commands[amen::SimpleMidiController::kMaxEventsPerAction];
    const uint32_t inputNow = millis();
    const uint32_t clockNow = micros();
    const uint8_t tickCount = controller.tick(clockNow, commands, amen::SimpleMidiController::kMaxEventsPerAction);
    for (uint8_t i = 0; i < tickCount; ++i) sendMidi(commands[i]);
    sent = tickCount > 0;
    if (e3PushSnapshot != previousE3Push) previousE3Push = e3PushSnapshot;
    if (contactSnapshot[amen::SimpleMidiController::kShiftKey] !=
        previousContacts[amen::SimpleMidiController::kShiftKey]) {
        const bool down = contactSnapshot[amen::SimpleMidiController::kShiftKey];
        const uint8_t count = down
            ? controller.press(amen::SimpleMidiController::kShiftKey, clockNow, commands,
                               amen::SimpleMidiController::kMaxEventsPerAction)
            : controller.release(amen::SimpleMidiController::kShiftKey, clockNow, commands,
                                 amen::SimpleMidiController::kMaxEventsPerAction);
        for (uint8_t i = 0; i < count; ++i) sendMidi(commands[i]);
        sent = sent || count > 0;
        if (down) oledUi.showShift(inputNow);
        previousContacts[amen::SimpleMidiController::kShiftKey] = down;
    }
    for (uint8_t index = 0; index < SCANNED_KEYS - 1; ++index) {
        const uint8_t key = index < 8 ? index + 12 : (index < 20 ? index - 8 : 20);
        if (contactSnapshot[key] == previousContacts[key]) continue;
        const uint8_t count = contactSnapshot[key]
            ? controller.press(key, clockNow, commands, amen::SimpleMidiController::kMaxEventsPerAction)
            : controller.release(key, clockNow, commands, amen::SimpleMidiController::kMaxEventsPerAction);
        for (uint8_t i = 0; i < count; ++i) sendMidi(commands[i]);
        sent = sent || count > 0;
        if (contactSnapshot[key] && key >= amen::SimpleMidiController::kHarmonyStartKey &&
            key < amen::SimpleMidiController::kShiftKey) {
            if (controller.page() == amen::PerformancePage::Pattern) oledUi.showPattern(inputNow);
            else oledUi.showHarmony(inputNow);
        }
        previousContacts[key] = contactSnapshot[key];
    }

    if (e1PushSnapshot != previousE1Push) previousE1Push = e1PushSnapshot;

    const int32_t e1Delta = encoderSnapshot[0] - previousEncoderPositions[0];
    if (e1Delta != 0) {
        const bool changed = controller.turnOctave(e1Delta);
        if (changed) {
            oledUi.showE1(amen::E1Page::Octave, millis());
            Serial.printf("Octave O%u, SW1=%u, SW12=%u\n", controller.octaveNumber(), controller.rootNote(), controller.highestNote());
        }
        previousEncoderPositions[0] = encoderSnapshot[0];
    }

    if (e2PushSnapshot != previousE2Push) {
        if (e2PushSnapshot) {
            controller.toggleClockMode(clockNow);
            e1Page = controller.clockMode() == amen::ClockMode::Tempo ? amen::E1Page::Tempo : amen::E1Page::Frequency;
            oledUi.showE1(e1Page, millis());
        }
        previousE2Push = e2PushSnapshot;
    }

    const int32_t e2Delta = encoderSnapshot[1] - previousEncoderPositions[1];
    if (e2Delta != 0) {
        const bool changed = controller.clockMode() == amen::ClockMode::Tempo
            ? controller.turnTempo(e2Delta, clockNow) : controller.turnFrequency(e2Delta, clockNow);
        if (changed) {
            e1Page = controller.clockMode() == amen::ClockMode::Tempo ? amen::E1Page::Tempo : amen::E1Page::Frequency;
            oledUi.showE1(e1Page, millis());
        }
        previousEncoderPositions[1] = encoderSnapshot[1];
    }

    const int32_t e3Delta = encoderSnapshot[2] - previousEncoderPositions[2];
    if (e3Delta != 0) {
        if (controller.turnRoot(e3Delta)) oledUi.showRoot(millis());
        previousEncoderPositions[2] = encoderSnapshot[2];
    }

    const int32_t e4Delta = encoderSnapshot[3] - previousEncoderPositions[3];
    if (e4Delta != 0) {
        if (controller.shiftHeld()) {
            const uint8_t count = controller.turnShiftMode(
                e4Delta, clockNow, commands, amen::SimpleMidiController::kMaxEventsPerAction);
            for (uint8_t i = 0; i < count; ++i) sendMidi(commands[i]);
            sent = sent || count > 0;
            oledUi.showShift(millis());
        } else if (controller.turnPreset(e4Delta)) oledUi.showPreset(millis());
        previousEncoderPositions[3] = encoderSnapshot[3];
    }

    const int32_t e5Delta = encoderSnapshot[4] - previousEncoderPositions[4];
    if (e5Delta != 0) {
        const uint8_t count = controller.turnPage(e5Delta, commands, amen::SimpleMidiController::kMaxEventsPerAction);
        for (uint8_t i = 0; i < count; ++i) sendMidi(commands[i]);
        sent = sent || count > 0;
        oledUi.showMode(millis());
        previousEncoderPositions[4] = encoderSnapshot[4];
    }

    if (e5PushSnapshot != previousE5Push) {
        if (e5PushSnapshot && controller.nextPatternBank()) oledUi.showBank(millis());
        previousE5Push = e5PushSnapshot;
    }

    const int32_t e6Delta = encoderSnapshot[5] - previousEncoderPositions[5];
    if (e6Delta != 0) {
        if (controller.turnPattern(e6Delta)) oledUi.showPatternEdit(millis());
        previousEncoderPositions[5] = encoderSnapshot[5];
    }

    const int32_t e7Delta = encoderSnapshot[6] - previousEncoderPositions[6];
    if (e7Delta != 0) previousEncoderPositions[6] = encoderSnapshot[6];

    if (sent) usbMIDI.send_now();
    while (usbMIDI.read()) {}

    const uint32_t now = millis();
    if (oledReady && now - lastDisplayAt >= 33U) {
        lastDisplayAt = now;
        const auto& framebuffer = oledUi.render(controller, e2Page, now);
        queueDisplay(framebuffer);
    }
    if (oledReady && !flushDisplay()) {
        oledReady = false;
        Serial.println("OLED write failed");
    }
}
