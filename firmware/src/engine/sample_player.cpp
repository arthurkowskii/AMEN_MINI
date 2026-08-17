#include "sample_player.h"

#include <algorithm>
#include <cmath>

void SamplePlayer::setSample(PcmView pcm, std::size_t startFrame,
                             std::size_t endFrame, PlaybackMode mode) {
    pcm_ = pcm;
    startFrame_ = startFrame;
    endFrame_ = endFrame;
    mode_ = mode;
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
    return renderAdditiveScaled(outL, outR, numFrames, 1.0f, 0.0f);
}

bool SamplePlayer::wrapLoopPosition() {
    if (mode_ != PlaybackMode::Loop || startFrame_ >= endFrame_ ||
        !std::isfinite(pos_)) {
        playing_ = false;
        return false;
    }

    const double start = static_cast<double>(startFrame_);
    const double rangeLength = static_cast<double>(endFrame_ - startFrame_);
    pos_ = start + std::fmod(pos_ - start, rangeLength);
    return true;
}

bool SamplePlayer::renderAdditiveScaled(float* outL, float* outR, int numFrames,
                                        float startGain, float gainIncrement) {
    if (numFrames <= 0) return playing_;

    if (!playing_ || !std::isfinite(speed_) || speed_ <= 0.0f) {
        playing_ = false;
        return false;
    }

    float gain = startGain;
    for (int i = 0; i < numFrames; ++i, gain += gainIncrement) {
        if (pos_ >= static_cast<double>(endFrame_)) {
            if (!wrapLoopPosition()) break;
        }

        const std::size_t i0 = static_cast<std::size_t>(pos_);
        const float t = static_cast<float>(pos_ - static_cast<double>(i0));
        const std::size_t i1 = (i0 + 1 < endFrame_)
                                   ? i0 + 1
                                   : (mode_ == PlaybackMode::Loop ? startFrame_ : i0);
        const std::size_t channels = pcm_.channels;

        const float left = (pcm_.samples[i0 * channels] * (1.0f - t) +
                            pcm_.samples[i1 * channels] * t) /
                           32768.0f;
        outL[i] += left * gain;

        if (channels > 1) {
            outR[i] += ((pcm_.samples[i0 * channels + 1] * (1.0f - t) +
                         pcm_.samples[i1 * channels + 1] * t) /
                        32768.0f) * gain;
        } else {
            outR[i] += left * gain;
        }

        pos_ += speed_;
        if (speedRampRemaining_ > 0) {
            speed_ += speedIncrement_;
            --speedRampRemaining_;
            if (speedRampRemaining_ == 0) speed_ = targetSpeed_;
        }
        if (pos_ >= static_cast<double>(endFrame_)) {
            if (mode_ != PlaybackMode::Loop) {
                playing_ = false;
            } else if (!wrapLoopPosition()) {
                break;
            }
        }
    }
    return playing_;
}
