#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <Wire.h>
#include "src/engine/pad_browser.h"
#include "src/teensy/stream_io.h"
#include "src/teensy/panel.h"
#include "src/teensy/oled.h"

amen::AudioStreams audio;
AudioOutputI2S output;
AudioConnection leftConnection(audio, 0, output, 0);
AudioConnection rightConnection(audio, 1, output, 1);
AudioControlSGTL5000 codec;
amen::SdStreams streams(audio.mixer);
amen::PadBrowser browser;
amen::Panel panel;
amen::Oled oled;
File directory;
amen::Panel::Snapshot previous{};
bool inputsReady = false, sdReady = false, codecReady = false, oledReady = false;
char message[33] = "SHIFT + PAD TO ASSIGN";
uint32_t lastDisplay = 0, lastDiagnostics = 0, selectionChanged = 0, lastCodecRetry = 0;
int headphoneVolume = 0;
uint8_t codecAck = 255, alternateCodecAck = 255;
uint16_t codecId = 0xffff;

void serviceStreams() { streams.service(); }
void scanInputs() { panel.scan(); }
void showMessage(const char* text) { snprintf(message, sizeof(message), "%s", text); Serial.println(message); }

uint8_t probeAddress(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission();
}

uint32_t busSpeed = 400000;

void scanBus() {
    Serial.printf("I2C scan ACK at %lu Hz:", static_cast<unsigned long>(busSpeed));
    for (uint8_t pass = 0; pass < 3; ++pass) {
        Serial.print(" [");
        for (uint8_t address = 8; address < 120; ++address) {
            Wire.beginTransmission(address);
            if (Wire.endTransmission() == 0) Serial.printf(" %02X", address);
        }
        Serial.print(" ]");
    }
    Serial.println();
    Wire.beginTransmission(0x76);
    Wire.write(uint8_t(0xd0));
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x76, 1) == 1) {
        Serial.printf("i2c_76_id=%02X (58=BMP280 60=BME280)\n", Wire.read());
    } else {
        Serial.println("i2c_76_id unreadable");
    }
}

bool initCodec() {
    static const uint32_t speeds[] = {100000, 50000, 10000};
    for (uint8_t s = 0; s < 3 && !codecReady; ++s) {
        Wire.setClock(speeds[s]);
        codecAck = probeAddress(0x0a);
        alternateCodecAck = probeAddress(0x2a);
        codecId = 0xffff;
        if (codecAck != 0) continue;
        Wire.beginTransmission(0x0a);
        Wire.write(uint8_t(0)); Wire.write(uint8_t(0));
        if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x0a, 2) == 2) {
            codecId = uint16_t(Wire.read()) << 8;
            codecId |= uint16_t(Wire.read());
        }
        codecReady = codec.enable();
        if (codecReady) {
            codecReady = codec.volume(0.0f);
            codec.lineOutLevel(29);
            busSpeed = speeds[s];
        }
    }
    Wire.setClock(codecReady ? busSpeed : 400000);
    if (!codecReady) busSpeed = 400000;
    return codecReady;
}

void turnVolume(int32_t delta) {
    if (!delta) return;
    if (!codecReady) { showMessage("CODEC UNAVAILABLE"); return; }
    const int next = int(std::clamp(int64_t(headphoneVolume) + delta, int64_t(0), int64_t(100)));
    if (next == headphoneVolume) return;
    if (!codec.volume(float(next) / 100.0f)) {
        codecReady = false;
        for (size_t voice = 0; voice < amen::StreamMixer::kVoices; ++voice) audio.mixer.stop(voice);
        showMessage("CODEC VOLUME WRITE ERROR");
        return;
    }
    headphoneVolume = next;
}

void beginScan() {
    directory.close();
    browser.resetEntries();
    selectionChanged = millis();
    directory = SD.open(browser.directory);
    browser.scanning = directory && directory.isDirectory();
    if (!browser.scanning) showMessage("DIRECTORY READ ERROR");
    else message[0] = 0;
}

void scanDirectory() {
    if (!browser.active || !browser.scanning) return;
    File entry = directory.openNextFile();
    if (!entry) { browser.scanning = false; directory.close(); return; }
    const char* name = entry.name();
    const char* slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    const bool more = !strcmp(name, ".") || !strcmp(name, "..") || browser.add(name, entry.isDirectory());
    entry.close();
    if (!more) {
        browser.scanning = false;
        directory.close();
    }
}

void clickBrowser() {
    if (!browser.active || browser.scanning || !browser.count) return;
    if (browser.entries[browser.selected].kind != amen::PadBrowser::Kind::File) {
        if (browser.enter()) beginScan(); else showMessage("PATH TOO LONG");
        return;
    }
    char path[amen::PadBrowser::kPath];
    if (!browser.selectedPath(path)) { showMessage("PATH TOO LONG"); return; }
    File file = SD.open(path, FILE_READ);
    amen::SdReader reader(file, serviceStreams);
    amen::PcmWav wav;
    const bool valid = file && amen::parseWav(reader, wav);
    file.close();
    if (!valid) { showMessage("INVALID WAV PCM16 44100"); return; }
    if (browser.assign(path, wav)) showMessage("ASSIGNED");
}

void handleInputs() {
    const auto current = panel.snapshot();
    if (!inputsReady) {
        previous = current;
        inputsReady = current.scans >= 20;
        return;
    }
    const uint32_t pressed = current.keys & ~previous.keys;
    turnVolume(static_cast<int32_t>(current.volumeEncoder - previous.volumeEncoder));
    const bool canceled = (pressed & (1UL << 20)) && browser.active;
    if (canceled) { browser.cancel(); directory.close(); showMessage("CANCELED"); }
    for (uint8_t pad = 0; pad < 20; ++pad) {
        if (!(pressed & (1UL << pad)) || canceled) continue;
        if (!sdReady) { showMessage("SD UNAVAILABLE"); continue; }
        if (current.keys & (1UL << 20)) { browser.open(pad); beginScan(); }
        else if (!browser.active) {
            const auto& assigned = browser.pads[pad];
            if (!assigned.path[0]) showMessage("UNASSIGNED PAD");
            else if (!codecReady) showMessage("CODEC UNAVAILABLE");
            else if (!streams.trigger(pad, assigned.path, assigned.wav)) showMessage("TRIGGER ERROR");
            else {
                const char* name = strrchr(assigned.path, '/');
                snprintf(message, sizeof(message), "PAD %u %.24s", pad + 1, name ? name + 1 : assigned.path);
            }
        }
    }
    const int32_t delta = static_cast<int32_t>(current.encoder - previous.encoder);
    browser.turn(delta);
    if (browser.active && delta) { message[0] = 0; selectionChanged = millis(); }
    if (current.click && !previous.click) clickBrowser();
    previous = current;
}

void updateDisplay() {
    if (!oledReady) return;
    if (!oled.busy() && millis() - lastDisplay >= 50) {
        lastDisplay = millis();
        oled.clear();
        char line[33];
        if (browser.active) {
            snprintf(line, sizeof(line), "PAD %u %s VOL %d", browser.target + 1, browser.scanning ? "SCAN" : "BROWSE", headphoneVolume);
            oled.text(0, line);
            const size_t first = browser.selected > 1 ? browser.selected - 1 : 0;
            for (size_t row = 0; row < 3 && first + row < browser.count; ++row) {
                const auto& entry = browser.entries[first + row];
                const bool selected = first + row == browser.selected;
                size_t offset = 0;
                const size_t length = strlen(entry.name);
                const uint32_t elapsed = millis() - selectionChanged;
                if (selected && length > 28 && elapsed > 1000) offset = ((elapsed - 1000) / 200) % (length - 28 + 6);
                if (offset > length - std::min(length, size_t(28))) offset = length - 28;
                snprintf(line, sizeof(line), "%c%s%.28s", selected ? '>' : ' ', entry.kind == amen::PadBrowser::Kind::Directory ? "/" : "", entry.name + offset);
                oled.text(row + 1, line);
            }
            if (message[0]) { oled.clear(); oled.text(0, "ASSIGN ERROR - E1 TO RETRY"); oled.text(1, message); }
        } else {
            oled.text(0, "AMEN MINI - 20 PADS");
            oled.text(1, message);
            oled.text(2, "SHIFT + PAD TO ASSIGN");
            snprintf(line, sizeof(line), "E7 VOLUME %d%s", headphoneVolume, headphoneVolume == 0 ? " MUTE" : "");
            oled.text(3, line);
        }
        oled.queue();
    }
    if (!oled.service()) { oledReady = false; Serial.println("OLED WRITE ERROR"); }
}

void setup() {
    Serial.begin(115200);
    AudioMemory(12);
    Wire.begin();
    Wire.setClock(100000);
    for (uint8_t attempt = 0; attempt < 6 && !codecReady; ++attempt) {
        if (attempt) delay(250);
        initCodec();
        Serial.printf("codec attempt %u: ready=%u ack0A=%u ack2A=%u id=%04X\n",
            unsigned(attempt), unsigned(codecReady),
            unsigned(codecAck), unsigned(alternateCodecAck), unsigned(codecId));
    }
    if (!codecReady) scanBus();
    Wire.setClock(400000);
    oledReady = oled.begin();
    sdReady = SD.begin(BUILTIN_SDCARD);
    if (!sdReady) showMessage("SD UNAVAILABLE");
    else if (!codecReady) showMessage("CODEC UNAVAILABLE");
    panel.begin(scanInputs);
    Serial.println("AMEN MINI: 20 pads, 4 SD streams, PCM16 mono/stereo 44100, no PSRAM");
}

void loop() {
    serviceStreams();
    handleInputs();
    serviceStreams();
    scanDirectory();
    serviceStreams();
    if (streams.takeError()) showMessage("SD STREAM READ ERROR");
    if (!codecReady && millis() - lastCodecRetry >= 3000) {
        lastCodecRetry = millis();
        initCodec();
        Serial.printf("codec retry: ready=%u ack0A=%u ack2A=%u id=%04X\n",
            unsigned(codecReady), unsigned(codecAck), unsigned(alternateCodecAck), unsigned(codecId));
        if (!codecReady) scanBus();
        else showMessage("CODEC READY - E7 VOLUME");
    }
    updateDisplay();
    if (millis() - lastDiagnostics >= 1000) {
        lastDiagnostics = millis();
        Serial.printf("sd=%u codec=%u oled=%u volume=%d underruns=%lu audio_cpu=%.2f audio_memory=%u\n",
            unsigned(sdReady), unsigned(codecReady), unsigned(oledReady),
            headphoneVolume,
            static_cast<unsigned long>(audio.mixer.underruns()), AudioProcessorUsageMax(), AudioMemoryUsageMax());
        if (!codecReady) Serial.printf("codec_i2c_0A=%u codec_i2c_2A=%u chip_id=%04X\n",
            unsigned(codecAck), unsigned(alternateCodecAck), unsigned(codecId));
    }
}
