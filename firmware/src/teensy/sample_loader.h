#pragma once

#include "pcm_view.h"

class PsramArena;
class VoiceManager;

enum class SampleLoadError {
    None,
    PsramUnavailable,
    FileOpenFailed,
    InvalidWav,
    InsufficientCapacity,
    DecodeFailed,
};

// Loads a complete WAV into the one persistent PSRAM arena. Voices are stopped
// only after the file and its required capacity have been validated.
class SampleLoader {
public:
    bool load(const char* path, PsramArena& arena, VoiceManager& voices);

    PcmView pcm() const { return pcm_; }
    SampleLoadError error() const { return error_; }

private:
    PcmView pcm_;
    SampleLoadError error_ = SampleLoadError::None;
};
