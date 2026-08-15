#ifndef SAMPLE_PLAYER_H
#define SAMPLE_PLAYER_H

#include "wav_loader.h"

class SamplePlayer {
public:
    void setSample(const WavData& wav);
    void trigger();
    void setSpeed(float speed);
    bool render(float* outL, float* outR, int numFrames);

private:
    const WavData* wav_ = nullptr;
    float pos_ = 0.0f;
    float speed_ = 1.0f;
    bool playing_ = false;
};

#endif
