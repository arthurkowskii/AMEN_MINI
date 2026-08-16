#pragma once

#include "sample_player.h"

#include <array>
#include <cstddef>
#include <cstdint>

class VoiceManager {
public:
    using PadId = uint32_t;

    static constexpr std::size_t kVoiceCount = 4;
    static constexpr uint32_t kDefaultOutputSampleRate = 44100;
    static constexpr float kMinUserSpeed = 0.25f;
    static constexpr float kMaxUserSpeed = 4.0f;

    explicit VoiceManager(uint32_t outputSampleRate = kDefaultOutputSampleRate)
        : outputSampleRate_(outputSampleRate) {}

    bool trigger(PadId padId, PcmView pcm, std::size_t startFrame,
                 std::size_t endFrame, float userSpeed);
    bool setPadSpeed(PadId padId, float userSpeed);
    void render(float* outL, float* outR, int numFrames);
    void stopAll();

private:
    struct Voice {
        SamplePlayer player;
        PadId padId = 0;
        uint64_t age = 0;
        float sourceRateRatio = 1.0f;
    };

    std::array<Voice, kVoiceCount> voices_{};
    uint64_t nextAge_ = 1;
    uint32_t outputSampleRate_;
};
