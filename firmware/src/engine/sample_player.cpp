#include "sample_player.h"

#include <algorithm>
#include <cmath>

void SamplePlayer::setSample(PcmView pcm, std::size_t startFrame, std::size_t endFrame) {
    pcm_ = pcm;
    startFrame_ = startFrame;
    endFrame_ = endFrame;
    playing_ = false;

    if (!pcm_.valid() || startFrame_ >= endFrame_ || endFrame_ > pcm_.frameCount()) {
        startFrame_ = 0;
        endFrame_ = 0;
    }
}

void SamplePlayer::trigger() {
    pos_ = static_cast<double>(startFrame_);
    playing_ = endFrame_ > startFrame_ && std::isfinite(speed_) && speed_ > 0.0f;
}

void SamplePlayer::stop() {
    playing_ = false;
}

void SamplePlayer::setSpeed(float speed) {
    targetSpeed_ = speed;
    if (!playing_ || !std::isfinite(speed) || speed <= 0.0f || speed == speed_) {
        speed_ = speed;
        speedIncrement_ = 0.0f;
        speedRampRemaining_ = 0;
        return;
    }

    speedRampRemaining_ = kSpeedRampFrames;
    speedIncrement_ = (targetSpeed_ - speed_) /
                      static_cast<float>(speedRampRemaining_);
}

bool SamplePlayer::render(float* outL, float* outR, int numFrames) {
    if (numFrames <= 0) return playing_;

    std::fill_n(outL, numFrames, 0.0f);
    std::fill_n(outR, numFrames, 0.0f);

    return renderAdditive(outL, outR, numFrames);
}

bool SamplePlayer::renderAdditive(float* outL, float* outR, int numFrames) {
    if (numFrames <= 0) return playing_;

    if (!playing_ || !std::isfinite(speed_) || speed_ <= 0.0f) {
        playing_ = false;
        return false;
    }

    for (int i = 0; i < numFrames; ++i) {
        if (pos_ >= static_cast<double>(endFrame_)) {
            playing_ = false;
            break;
        }

        const std::size_t i0 = static_cast<std::size_t>(pos_);
        const float t = static_cast<float>(pos_ - static_cast<double>(i0));
        const std::size_t i1 = (i0 + 1 < endFrame_) ? i0 + 1 : i0;
        const std::size_t channels = pcm_.channels;

        const float left = (pcm_.samples[i0 * channels] * (1.0f - t) +
                            pcm_.samples[i1 * channels] * t) /
                           32768.0f;
        outL[i] += left;

        if (channels > 1) {
            outR[i] += (pcm_.samples[i0 * channels + 1] * (1.0f - t) +
                        pcm_.samples[i1 * channels + 1] * t) /
                       32768.0f;
        } else {
            outR[i] += left;
        }

        pos_ += speed_;
        if (speedRampRemaining_ > 0) {
            speed_ += speedIncrement_;
            --speedRampRemaining_;
            if (speedRampRemaining_ == 0) speed_ = targetSpeed_;
        }
        if (pos_ >= static_cast<double>(endFrame_)) playing_ = false;
    }
    return playing_;
}
