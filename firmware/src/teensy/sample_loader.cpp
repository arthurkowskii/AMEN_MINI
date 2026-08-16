#include "sample_loader.h"

#include "psram_arena.h"
#include "teensy_wav_reader.h"
#include "../engine/voice_manager.h"
#include "../engine/wav_loader.h"

#include <SD.h>

bool SampleLoader::load(const char* path, PsramArena& arena, VoiceManager& voices) {
    error_ = SampleLoadError::None;
    if (path == nullptr || !arena.ready()) {
        error_ = SampleLoadError::PsramUnavailable;
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        error_ = SampleLoadError::FileOpenFailed;
        return false;
    }

    TeensyWavReader reader(file);
    WavMetadata metadata;
    if (!wav_probe(reader, metadata)) {
        error_ = SampleLoadError::InvalidWav;
        return false;
    }
    if (metadata.sampleCount > arena.capacitySamples()) {
        error_ = SampleLoadError::InsufficientCapacity;
        return false;
    }

    // Decoding overwrites the shared arena, so no voice may retain its old view.
    voices.stopAll();
    if (!wav_decode(reader, metadata, arena.data(), arena.capacitySamples())) {
        pcm_ = {};
        error_ = SampleLoadError::DecodeFailed;
        return false;
    }

    pcm_ = arena.view(metadata.sampleRate, metadata.channels, metadata.sampleCount);
    if (!pcm_.valid()) {
        pcm_ = {};
        error_ = SampleLoadError::DecodeFailed;
        return false;
    }
    return true;
}
