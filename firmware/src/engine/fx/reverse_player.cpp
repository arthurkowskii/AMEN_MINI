#include "reverse_player.h"

#include <algorithm>
#include <cmath>

ReversePlayer::ReversePlayer(std::uint32_t sampleRate, float* historyLeft,
                             float* historyRight, float* frozenLeft,
                             float* frozenRight, std::size_t bufferCapacity)
    : sampleRate_(sampleRate),
      historyL_(historyLeft),
      historyR_(historyRight),
      frozenL_(frozenLeft),
      frozenR_(frozenRight),
      bufferCapacity_(historyLeft != nullptr && historyRight != nullptr &&
                              frozenLeft != nullptr && frozenRight != nullptr
                          ? bufferCapacity
                          : 0) {
    if (bufferCapacity_ == 0) return;
    std::fill_n(historyL_, bufferCapacity_, 0.0f);
    std::fill_n(historyR_, bufferCapacity_, 0.0f);
    std::fill_n(frozenL_, bufferCapacity_, 0.0f);
    std::fill_n(frozenR_, bufferCapacity_, 0.0f);
}

void ReversePlayer::setActive(bool active) {
    if (active == requestedActive_) return;
    requestedActive_ = active;
    if (active) {
        captureEnd_ = framesWritten_;
        const std::size_t window =
            static_cast<std::size_t>(static_cast<float>(sampleRate_) *
                                     kWindowSeconds);
        loopFrames_ = std::min(window, static_cast<std::size_t>(captureEnd_));
        loopPosition_ = 0;
    }
    mixRamp_.setTarget(active ? amount_ : 0.0f);
}

void ReversePlayer::setAmount(float amount) {
    if (!std::isfinite(amount)) return;
    amount_ = std::clamp(amount, 0.0f, 1.0f);
    mixRamp_.setTarget(requestedActive_ ? amount_ : 0.0f);
}

float ReversePlayer::historySample(const float* history, const float* frozen,
                                   std::size_t positionFromEnd) const {
    if (loopFrames_ == 0 || bufferCapacity_ == 0) return 0.0f;
    // positionFromEnd = 0 designe la frame la plus recente de la fenetre
    // (captureEnd_ - 1), positionFromEnd = loopFrames_-1 la plus ancienne.
    const std::uint64_t absoluteFrame = captureEnd_ - 1U - positionFromEnd;
    const std::uint64_t oldestHistoryFrame =
        framesWritten_ > bufferCapacity_ ? framesWritten_ - bufferCapacity_ : 0;
    if (absoluteFrame < oldestHistoryFrame) {
        return frozen[static_cast<std::size_t>(absoluteFrame % bufferCapacity_)];
    }
    return history[static_cast<std::size_t>(absoluteFrame % bufferCapacity_)];
}

float ReversePlayer::smoothedReverseSample(const float* history,
                                           const float* frozen,
                                           std::size_t positionFromEnd) const {
    const float sample = historySample(history, frozen, positionFromEnd);
    if (loopFrames_ < 2) return sample;

    const std::size_t fadeFrames =
        std::min<std::size_t>(kSeamFrames, loopFrames_ / 4U);
    if (fadeFrames == 0) return sample;

    // Couture symetrique : meme formule que LiveRepeat, la distance a la
    // couture est identique en lecture avant ou arriere.
    const std::size_t distanceToSeam =
        std::min(positionFromEnd, loopFrames_ - 1U - positionFromEnd);
    if (distanceToSeam >= fadeFrames) return sample;

    const float first = historySample(history, frozen, 0);
    const float last = historySample(history, frozen, loopFrames_ - 1U);
    const float seam = (first + last) * 0.5f;
    const float fade = static_cast<float>(distanceToSeam) /
                       static_cast<float>(fadeFrames);
    return seam + (sample - seam) * fade;
}

void ReversePlayer::process(float* left, float* right, int numFrames) {
    if (left == nullptr || right == nullptr || numFrames <= 0 ||
        bufferCapacity_ == 0) return;

    for (int frame = 0; frame < numFrames; ++frame) {
        const float dryL = left[frame];
        const float dryR = right[frame];
        const std::size_t writeIndex =
            static_cast<std::size_t>(framesWritten_ % bufferCapacity_);
        const float mix = mixRamp_.get();
        if ((requestedActive_ || mix > 0.0f) && framesWritten_ >= bufferCapacity_) {
            // Meme protection d'ecrasement que LiveRepeat : les frames de la
            // fenetre active sur le point d'etre remplacees passent au gel.
            const std::uint64_t overwrittenFrame = framesWritten_ - bufferCapacity_;
            const std::uint64_t captureStart =
                captureEnd_ > bufferCapacity_
                    ? captureEnd_ - bufferCapacity_
                    : 0;
            if (overwrittenFrame >= captureStart && overwrittenFrame < captureEnd_) {
                frozenL_[writeIndex] = historyL_[writeIndex];
                frozenR_[writeIndex] = historyR_[writeIndex];
            }
        }
        historyL_[writeIndex] = dryL;
        historyR_[writeIndex] = dryR;
        ++framesWritten_;

        float wetL = dryL;
        float wetR = dryR;
        if (loopFrames_ > 0 && (requestedActive_ || mix > 0.0f)) {
            wetL = smoothedReverseSample(historyL_, frozenL_, loopPosition_);
            wetR = smoothedReverseSample(historyR_, frozenR_, loopPosition_);
            loopPosition_ = (loopPosition_ + 1U) % loopFrames_;
        }

        const float appliedMix = mixRamp_.tick();
        left[frame] = std::clamp(dryL + (wetL - dryL) * appliedMix, -1.0f, 1.0f);
        right[frame] = std::clamp(dryR + (wetR - dryR) * appliedMix, -1.0f, 1.0f);
    }
}
