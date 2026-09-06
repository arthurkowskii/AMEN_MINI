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

volatile bool contacts[21] = {};
bool raw[21] = {};
uint32_t changedAt[21] = {};
volatile int32_t encoderPositions[7] = {};
uint8_t encoderAb[7] = {};
int8_t encoderPartial[7] = {};
volatile uint32_t scanCount = 0;
bool firstScan = true;
IntervalTimer scanTimer;

amen::SimpleMidiController controller;
bool previousContacts[21] = {};
int32_t previousEncoderPositions[7] = {};
bool inputReady = false;
amen::OledUi oledUi;
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
    Serial.println("AMEN MIDI V0");
    if (!oledReady) Serial.println("OLED unavailable");
}

void loop() {
    bool contactSnapshot[21];
    int32_t encoderSnapshot[7];
    uint32_t scans;

    noInterrupts();
    for (uint8_t i = 0; i < 21; ++i) contactSnapshot[i] = contacts[i];
    for (uint8_t i = 0; i < 7; ++i) encoderSnapshot[i] = encoderPositions[i];
    scans = scanCount;
    interrupts();

    if (!inputReady) {
        if (scans < 20) return;
        for (uint8_t i = 0; i < 21; ++i) previousContacts[i] = contactSnapshot[i];
        for (uint8_t i = 0; i < 7; ++i) previousEncoderPositions[i] = encoderSnapshot[i];
        inputReady = true;
        return;
    }

    bool sent = false;
    for (uint8_t key = 0; key < amen::SimpleMidiController::kKeyCount; ++key) {
        if (contactSnapshot[key] == previousContacts[key]) continue;
        const auto command = contactSnapshot[key] ? controller.press(key) : controller.release(key);
        sendMidi(command);
        sent = sent || command.type != amen::MidiCommandType::None;
        previousContacts[key] = contactSnapshot[key];
    }

    const int32_t octaveDelta = encoderSnapshot[0] - previousEncoderPositions[0];
    if (octaveDelta != 0) {
        if (controller.turnOctave(octaveDelta)) {
            Serial.printf("Octave %+d, SW1=%u, SW20=%u\n", controller.octave(), controller.baseNote(), controller.baseNote() + 19);
            oledUi.showOctave(millis());
        }
        previousEncoderPositions[0] = encoderSnapshot[0];
    }

    if (sent) usbMIDI.send_now();
    while (usbMIDI.read()) {}

    const uint32_t now = millis();
    if (oledReady && now - lastDisplayAt >= 33U) {
        lastDisplayAt = now;
        const auto& framebuffer = oledUi.render(controller, now);
        if (framebuffer.pixels() != displayedFrame && !display(framebuffer)) {
            oledReady = false;
            Serial.println("OLED write failed");
        }
    }
}
