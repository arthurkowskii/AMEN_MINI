#include <Arduino.h>
#include <SD.h>

#include "src/engine/voice_manager.h"
#include "src/teensy/psram_arena.h"
#include "src/teensy/sample_loader.h"

extern "C" uint8_t external_psram_size;

namespace {
constexpr char kDiagnosticWavPath[] = "/test.wav";
constexpr uint32_t kBytesPerMib = 1024U * 1024U;

PsramArena psram;
SampleLoader loader;
VoiceManager voices;

const char* psramError(PsramArena::Error error) {
    switch (error) {
        case PsramArena::Error::None: return "none";
        case PsramArena::Error::AlreadyInitialized: return "already initialized";
        case PsramArena::Error::NoPsram: return "not detected";
        case PsramArena::Error::InsufficientPsram: return "capacity too small";
        case PsramArena::Error::AllocationFailed: return "allocation failed";
    }
    return "unknown";
}

const char* loadError(SampleLoadError error) {
    switch (error) {
        case SampleLoadError::None: return "none";
        case SampleLoadError::PsramUnavailable: return "PSRAM unavailable";
        case SampleLoadError::FileOpenFailed: return "SD file open failed";
        case SampleLoadError::InvalidWav: return "invalid WAV";
        case SampleLoadError::InsufficientCapacity: return "WAV exceeds PSRAM arena";
        case SampleLoadError::DecodeFailed: return "WAV decode failed";
    }
    return "unknown";
}
}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t serialDeadline = millis() + 2000;
    while (!Serial && millis() < serialDeadline) {}

    const uint32_t psramBytes = static_cast<uint32_t>(external_psram_size) * kBytesPerMib;
    Serial.printf("PSRAM detected: %lu bytes\n", static_cast<unsigned long>(psramBytes));
    if (!psram.begin()) {
        Serial.printf("PSRAM setup failed: %s\n", psramError(psram.error()));
        return;
    }
    Serial.printf("PSRAM arena: %lu bytes\n", static_cast<unsigned long>(psram.capacityBytes()));

    if (!SD.begin(BUILTIN_SDCARD)) {
        Serial.println("SD setup failed");
        return;
    }

    if (!loader.load(kDiagnosticWavPath, psram, voices)) {
        Serial.printf("Load %s failed: %s\n", kDiagnosticWavPath, loadError(loader.error()));
        return;
    }

    const PcmView pcm = loader.pcm();
    Serial.printf("Loaded %s: %lu Hz, %u channels, %lu samples\n", kDiagnosticWavPath,
                  static_cast<unsigned long>(pcm.sampleRate), pcm.channels,
                  static_cast<unsigned long>(pcm.sampleCount));
}

void loop() {}
