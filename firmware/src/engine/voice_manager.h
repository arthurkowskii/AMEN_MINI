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
                 std::size_t endFrame, float userSpeed,
                 PlaybackMode mode = PlaybackMode::OneShot);
    bool setPadSpeed(PadId padId, float userSpeed);
    void stopPad(PadId padId);
    bool isPadPlaying(PadId padId) const;
    void render(float* outL, float* outR, int numFrames);
    void stopAll();

private:
    struct Voice {
        SamplePlayer player;
        PadId padId = 0;
        uint64_t age = 0;
        float sourceRateRatio = 1.0f;
        std::size_t crossfadeFrame = kCrossfadeFrames;
    };

    struct Retirement {
        SamplePlayer player;
        PadId outgoingPadId = 0;
        PadId incomingPadId = 0;
        std::size_t crossfadeFrame = kCrossfadeFrames;
        uint64_t serial = 0;
        bool active = false;
    };

    static constexpr std::size_t kCrossfadeFrames = 64;
    static constexpr std::size_t kRetirementCount = kVoiceCount;

    void cancelRetirementsForPad(PadId padId);
    void retire(const SamplePlayer& player, PadId outgoingPadId, PadId incomingPadId);
    void renderVoice(Voice& voice, float* outL, float* outR, int numFrames);
    void renderRetirement(Retirement& retirement, float* outL, float* outR,
                          int numFrames);

    std::array<Voice, kVoiceCount> voices_{};
    std::array<Retirement, kRetirementCount> retirements_{};
    uint64_t nextAge_ = 1;
    uint64_t nextRetirementSerial_ = 1;
    uint32_t outputSampleRate_;
};
