#include "sample_player.h"

void SamplePlayer::setSample(const WavData& wav) {
    wav_ = &wav;
}

void SamplePlayer::trigger() {
    pos_ = 0.0f;
    playing_ = true;
}

void SamplePlayer::setSpeed(float speed) {
    speed_ = speed;
}

bool SamplePlayer::render(float* outL, float* outR, int numFrames) {
    if (!playing_ || !wav_) {
        for (int i = 0; i < numFrames; ++i)
            outL[i] = outR[i] = 0.0f;
        return false;
    }

    const auto& samples = wav_->samples;
    int ch = wav_->channels;
    int totalFrames = (int)samples.size() / ch;

    for (int i = 0; i < numFrames; ++i) {
        if (pos_ >= totalFrames) {
            playing_ = false;
            break;
        }

        int i0 = (int)pos_;
        float t = pos_ - i0;
        int i1 = (i0 + 1 < totalFrames) ? i0 + 1 : i0;

        outL[i] = (samples[i0 * ch] * (1.0f - t) + samples[i1 * ch] * t) / 32768.0f;

        if (ch == 2) {
            outR[i] = (samples[i0 * ch + 1] * (1.0f - t) + samples[i1 * ch + 1] * t) / 32768.0f;
        } else {
            outR[i] = outL[i];
        }

        pos_ += speed_;
    }
    return playing_;
}