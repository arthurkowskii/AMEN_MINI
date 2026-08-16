#pragma once

#include "pcm_view.h"

#include <cstddef>

class SamplePlayer {
public:
    void setSample(PcmView pcm, std::size_t startFrame, std::size_t endFrame);
    void trigger();
    void stop();
    void setSpeed(float speed);
    bool render(float* outL, float* outR, int numFrames);
    bool isPlaying() const { return playing_; }

private:
    friend class VoiceManager;

    static constexpr std::size_t kSpeedRampFrames = 128;

    bool renderAdditive(float* outL, float* outR, int numFrames);

    PcmView pcm_;
    std::size_t startFrame_ = 0;
    std::size_t endFrame_ = 0;
    double pos_ = 0.0;
    float speed_ = 1.0f;
    float targetSpeed_ = 1.0f;
    float speedIncrement_ = 0.0f;
    std::size_t speedRampRemaining_ = 0;
    bool playing_ = false;
};
