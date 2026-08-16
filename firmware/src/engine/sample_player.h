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

    bool renderAdditive(float* outL, float* outR, int numFrames);

    PcmView pcm_;
    std::size_t startFrame_ = 0;
    std::size_t endFrame_ = 0;
    double pos_ = 0.0;
    float speed_ = 1.0f;
    bool playing_ = false;
};
