#pragma once

#include "sample_player.h"

#include <array>
#include <cstddef>
#include <cstdint>

class VoiceManager {
public:
    static constexpr std::size_t kVoiceCount = 4;

    bool trigger(PcmView pcm, std::size_t startFrame, std::size_t endFrame, float speed);
    void render(float* outL, float* outR, int numFrames);
    void stopAll();

private:
    struct Voice {
        SamplePlayer player;
        uint64_t age = 0;
    };

    std::array<Voice, kVoiceCount> voices_{};
    uint64_t nextAge_ = 1;
};
