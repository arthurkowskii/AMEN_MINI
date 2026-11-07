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
constexpr const char* FIRMWARE_BANNER =
    "AMEN MIDI PANEL V2.5: SHIFT-E2 ORIENTATION V/H, BOOT VERTICAL, NOTES/HARMONY/PATTERN, E1 MUSIC/SHIFT/ASSIGN, E2 MODE/BANK, E3 VELOCITY/MOD, E4 CC23 + SHIFT-SMART, E5-E7 CC20-22, CC CLICK X1/2/4/8, DEFAULT X8";

volatile bool contacts[21] = {};
bool raw[21] = {};
uint32_t changedAt[21] = {};
volatile int32_t encoderPositions[7] = {};
uint8_t encoderAb[7] = {};
int8_t encoderPartial[7] = {};
volatile bool encoderPush[7] = {};
bool encoderPushRaw[7] = {};
uint32_t encoderPushChangedAt[7] = {};
volatile uint32_t scanCount = 0;
bool firstScan = true;
IntervalTimer scanTimer;

amen::SimpleMidiController controller;
bool previousContacts[21] = {};
int32_t previousEncoderPositions[7] = {};
bool previousEncoderPush[7] = {};
bool inputReady = false;
amen::OledUi oledUi;
amen::FrontPanel frontPanel;
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
    bool pushSample[7];
    for (uint8_t i = 0; i < 7; ++i) pushSample[i] = !digitalRead(PUSH[i]);

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

    for (uint8_t i = 0; i < 7; ++i) {
        if (firstScan) {
            encoderPushRaw[i] = encoderPush[i] = pushSample[i];
            encoderPushChangedAt[i] = now;
        } else if (pushSample[i] != encoderPushRaw[i]) {
            encoderPushRaw[i] = pushSample[i];
            encoderPushChangedAt[i] = now;
        }
        if (encoderPush[i] != encoderPushRaw[i] && now - encoderPushChangedAt[i] >= DEBOUNCE_US)
            encoderPush[i] = encoderPushRaw[i];
    }

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
    Serial.println(FIRMWARE_BANNER);
    if (!oledReady) Serial.println("OLED unavailable");
}

void loop() {
    bool contactSnapshot[21];
    int32_t encoderSnapshot[7];
    bool pushSnapshot[7];
    uint32_t scans;

    noInterrupts();
    for (uint8_t i = 0; i < 21; ++i) contactSnapshot[i] = contacts[i];
    for (uint8_t i = 0; i < 7; ++i) encoderSnapshot[i] = encoderPositions[i];
    for (uint8_t i = 0; i < 7; ++i) pushSnapshot[i] = encoderPush[i];
    scans = scanCount;
    interrupts();

    if (!inputReady) {
        if (scans < 20) return;
        for (uint8_t i = 0; i < 21; ++i) previousContacts[i] = contactSnapshot[i];
        for (uint8_t i = 0; i < 7; ++i) previousEncoderPositions[i] = encoderSnapshot[i];
        for (uint8_t i = 0; i < 7; ++i) previousEncoderPush[i] = pushSnapshot[i];
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
        const uint8_t key = controller.keyInScanOrder(index);
        const uint8_t physical = controller.physicalForKey(key);
        if (contactSnapshot[physical] == previousContacts[physical]) continue;
        const uint8_t count = contactSnapshot[physical]
            ? controller.press(key, clockNow, commands, amen::SimpleMidiController::kMaxEventsPerAction)
            : controller.release(key, clockNow, commands, amen::SimpleMidiController::kMaxEventsPerAction);
        for (uint8_t i = 0; i < count; ++i) sendMidi(commands[i]);
        sent = sent || count > 0;
        if (contactSnapshot[physical] && key >= controller.degreeKeyCount() &&
            key < amen::SimpleMidiController::kShiftKey) {
            if (controller.page() == amen::PerformancePage::Pattern) oledUi.showPattern(inputNow);
            else if (controller.page() == amen::PerformancePage::Harmony) oledUi.showHarmony(inputNow);
        }
        previousContacts[physical] = contactSnapshot[physical];
    }

    for (uint8_t encoder = 0; encoder < 7; ++encoder) {
        if (pushSnapshot[encoder] != previousEncoderPush[encoder]) {
            if (pushSnapshot[encoder]) {
                const auto previousOrientation = controller.orientation();
                const amen::PanelResult result = frontPanel.click(
                    controller, encoder, commands, amen::SimpleMidiController::kMaxEventsPerAction);
                for (uint8_t i = 0; i < result.count; ++i) sendMidi(commands[i]);
                sent = sent || result.count > 0;
                if (controller.orientation() != previousOrientation)
                    Serial.println(controller.orientation() == amen::Orientation::Horizontal
                        ? "ORIENTATION HORIZONTAL" : "ORIENTATION VERTICAL");
                oledUi.showPanel(result.feedback, frontPanel, encoder, inputNow);
            }
            previousEncoderPush[encoder] = pushSnapshot[encoder];
        }
        const int32_t delta = encoderSnapshot[encoder] - previousEncoderPositions[encoder];
        if (delta != 0) {
            const amen::PanelResult result = frontPanel.turn(
                controller, encoder, delta, clockNow, commands, amen::SimpleMidiController::kMaxEventsPerAction);
            for (uint8_t i = 0; i < result.count; ++i) sendMidi(commands[i]);
            sent = sent || result.count > 0;
            oledUi.showPanel(result.feedback, frontPanel, encoder, inputNow);
            previousEncoderPositions[encoder] = encoderSnapshot[encoder];
        }
    }

    if (sent) usbMIDI.send_now();
    while (usbMIDI.read()) {}
    // Read-only firmware identification, available even if the boot banner was missed.
    if (Serial.available() > 0 && Serial.read() == '?') Serial.println(FIRMWARE_BANNER);

    const uint32_t now = millis();
    if (oledReady && now - lastDisplayAt >= 33U) {
        lastDisplayAt = now;
        const auto& framebuffer = oledUi.render(controller, frontPanel, now);
        queueDisplay(framebuffer);
    }
    if (oledReady && !flushDisplay()) {
        oledReady = false;
        Serial.println("OLED write failed");
    }
}
