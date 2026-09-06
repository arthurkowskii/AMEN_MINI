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
volatile bool e2Push = false;
bool e2PushRaw = false;
uint32_t e2PushChangedAt = 0;
volatile uint32_t scanCount = 0;
bool firstScan = true;
IntervalTimer scanTimer;

amen::SimpleMidiController controller;
bool previousContacts[21] = {};
int32_t previousEncoderPositions[7] = {};
bool previousE2Push = false;
bool inputReady = false;
amen::OledUi oledUi;
amen::E2Page e2Page = amen::E2Page::Root;
std::array<uint8_t, amen::MonoFramebuffer::kSize> displayedFrame{};
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

bool display(const amen::MonoFramebuffer& framebuffer) {
    const uint8_t window[] = {0x21, 0, 127, 0x22, 0, 3};
    if (!oledWrite(0x00, window, sizeof(window))) return false;
    const auto& pixels = framebuffer.pixels();
    for (size_t offset = 0; offset < pixels.size(); offset += 16)
        if (!oledWrite(0x40, pixels.data() + offset, 16)) return false;
    displayedFrame = pixels;
    return true;
}

void scanInputs() {
    const uint32_t now = micros();
    bool sample[21];
    const bool e2PushSample = !digitalRead(PUSH[1]);

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
        e2PushRaw = e2Push = e2PushSample;
        e2PushChangedAt = now;
    } else if (e2PushSample != e2PushRaw) {
        e2PushRaw = e2PushSample;
        e2PushChangedAt = now;
    }
    if (e2Push != e2PushRaw && now - e2PushChangedAt >= DEBOUNCE_US) e2Push = e2PushRaw;

    firstScan = false;
    ++scanCount;
}

void sendMidi(const amen::MidiCommand& command) {
    if (command.type == amen::MidiCommandType::NoteOn) {
        usbMIDI.sendNoteOn(command.note, command.velocity, amen::SimpleMidiController::kChannel);
    } else if (command.type == amen::MidiCommandType::NoteOff) {
        usbMIDI.sendNoteOff(command.note, command.velocity, amen::SimpleMidiController::kChannel);
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
    Serial.println("AMEN MIDI HARMONIC");
    if (!oledReady) Serial.println("OLED unavailable");
}

void loop() {
    bool contactSnapshot[21];
    int32_t encoderSnapshot[7];
    bool e2PushSnapshot;
    uint32_t scans;

    noInterrupts();
    for (uint8_t i = 0; i < 21; ++i) contactSnapshot[i] = contacts[i];
    for (uint8_t i = 0; i < 7; ++i) encoderSnapshot[i] = encoderPositions[i];
    e2PushSnapshot = e2Push;
    scans = scanCount;
    interrupts();

    if (!inputReady) {
        if (scans < 20) return;
        for (uint8_t i = 0; i < 21; ++i) previousContacts[i] = contactSnapshot[i];
        for (uint8_t i = 0; i < 7; ++i) previousEncoderPositions[i] = encoderSnapshot[i];
        previousE2Push = e2PushSnapshot;
        inputReady = true;
        return;
    }

    bool sent = false;
    amen::MidiCommand commands[amen::SimpleMidiController::kMaxEventsPerAction];
    for (uint8_t key = 0; key < SCANNED_KEYS; ++key) {
        if (contactSnapshot[key] == previousContacts[key]) continue;
        const uint8_t count = contactSnapshot[key]
            ? controller.press(key, commands, amen::SimpleMidiController::kMaxEventsPerAction)
            : controller.release(key, commands, amen::SimpleMidiController::kMaxEventsPerAction);
        for (uint8_t i = 0; i < count; ++i) sendMidi(commands[i]);
        sent = sent || count > 0;
        if (contactSnapshot[key] && key >= amen::SimpleMidiController::kHarmonyStartKey &&
            key < amen::SimpleMidiController::kShiftKey) {
            oledUi.showHarmony(millis());
            Serial.printf("PRESET %s, Harmony %s\n", controller.presetName(), controller.harmonyName());
        }
        previousContacts[key] = contactSnapshot[key];
    }

    const int32_t octaveDelta = encoderSnapshot[0] - previousEncoderPositions[0];
    if (octaveDelta != 0) {
        if (controller.turnOctave(octaveDelta)) {
            Serial.printf("Octave O%u, SW1=%u, SW12=%u\n", controller.octaveNumber(), controller.rootNote(), controller.highestNote());
            oledUi.showOctave(millis());
        }
        previousEncoderPositions[0] = encoderSnapshot[0];
    }

    if (e2PushSnapshot != previousE2Push) {
        if (e2PushSnapshot) {
            e2Page = e2Page == amen::E2Page::Root ? amen::E2Page::Preset : amen::E2Page::Root;
            oledUi.showE2(e2Page, millis());
            Serial.printf("E2 %s\n", e2Page == amen::E2Page::Root ? "ROOT" : "PRESET");
        }
        previousE2Push = e2PushSnapshot;
    }

    const int32_t e2Delta = encoderSnapshot[1] - previousEncoderPositions[1];
    if (e2Delta != 0) {
        const bool changed = e2Page == amen::E2Page::Root
            ? controller.turnRoot(e2Delta)
            : controller.turnPreset(e2Delta);
        if (changed) {
            oledUi.showE2(e2Page, millis());
            Serial.printf("Root %s, PRESET %s, Harmony %s, SW1=%u, SW12=%u\n",
                          amen::pitchClassName(controller.rootPitchClass()), controller.presetName(), controller.harmonyName(),
                          controller.rootNote(), controller.highestNote());
        }
        previousEncoderPositions[1] = encoderSnapshot[1];
    }

    if (sent) usbMIDI.send_now();
    while (usbMIDI.read()) {}

    const uint32_t now = millis();
    if (oledReady && now - lastDisplayAt >= 33U) {
        lastDisplayAt = now;
        const auto& framebuffer = oledUi.render(controller, e2Page, now);
        if (framebuffer.pixels() != displayedFrame && !display(framebuffer)) {
            oledReady = false;
            Serial.println("OLED write failed");
        }
    }
}
