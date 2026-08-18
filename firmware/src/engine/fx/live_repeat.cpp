#include "live_repeat.h"

#include <algorithm>
#include <cmath>

namespace {
float divisionScale(RepeatDivision division) {
    switch (division) {
        case RepeatDivision::Quarter:
            return 1.0f;
        case RepeatDivision::Eighth:
            return 0.5f;
        case RepeatDivision::EighthTriplet:
            return 1.0f / 3.0f;
        case RepeatDivision::Sixteenth:
            return 0.25f;
        case RepeatDivision::SixteenthTriplet:
            return 1.0f / 6.0f;
        case RepeatDivision::ThirtySecond:
            return 0.125f;
    }
    return 1.0f;
}
}  // namespace

LiveRepeat::LiveRepeat(std::uint32_t sampleRate, float* historyLeft,
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

void LiveRepeat::setActive(bool active) {
    if (active == requestedActive_) return;
    requestedActive_ = active;
    if (active) {
        captureEnd_ = framesWritten_;
        loopFrames_ = availableLoopFrames();
        loopPosition_ = 0;
        loopPositionF_ = 0.0f;
        shepardPhase_ = 0.0f;
        oldLoopFrames_ = 0;
        loopCrossfadeRemaining_ = 0;
    }
    mixRamp_.setTarget(active ? amount_ : 0.0f);
}

void LiveRepeat::setAmount(float amount) {
    if (!std::isfinite(amount)) return;
    amount_ = std::clamp(amount, 0.0f, 1.0f);
    mixRamp_.setTarget(requestedActive_ ? amount_ : 0.0f);
}

void LiveRepeat::setMode(RepeatMode mode) {
    mode_ = mode;
}

void LiveRepeat::setShepardDepth(float depth) {
    if (!std::isfinite(depth)) return;
    shepardDepth_ = std::clamp(depth, 0.0f, kMaxShepardDepth);
}

void LiveRepeat::setBpm(float bpm) {
    if (!std::isfinite(bpm)) return;
    const float clamped =
        std::clamp(bpm, static_cast<float>(kMinBpm), kMaxBpm);
    if (clamped == bpm_) return;
    bpm_ = clamped;
    if (requestedActive_) beginLoopChange();
}

void LiveRepeat::setDivision(RepeatDivision division) {
    if (division == division_) return;
    division_ = division;
    if (requestedActive_) beginLoopChange();
}

std::size_t LiveRepeat::requestedLoopFrames() const {
    if (sampleRate_ == 0) return 0;
    const float frames = static_cast<float>(sampleRate_) * 60.0f / bpm_ *
                         divisionScale(division_);
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(frames)));
}

std::size_t LiveRepeat::availableLoopFrames() const {
    const std::size_t available = static_cast<std::size_t>(
        std::min<std::uint64_t>(captureEnd_, bufferCapacity_));
    return std::min(requestedLoopFrames(), available);
}

void LiveRepeat::beginLoopChange() {
    const std::size_t nextFrames = availableLoopFrames();
    if (nextFrames == loopFrames_) return;
    if (mode_ == RepeatMode::Shepard) {
        // Pas de crossfade en SHEPARD : la rampe de vitesse tolere le
        // changement de longueur (le lissage de couture masque le wrap).
        loopFrames_ = nextFrames;
        loopPositionF_ = 0.0f;
        shepardPhase_ = 0.0f;
        return;
    }
    oldLoopFrames_ = loopFrames_;
    oldLoopPosition_ = loopPosition_;
    loopFrames_ = nextFrames;
    loopPosition_ = 0;
    loopCrossfadeRemaining_ = oldLoopFrames_ > 0 && loopFrames_ > 0
                                  ? kRampFrames
                                  : 0;
}

float LiveRepeat::historySample(const float* history, const float* frozen,
                                 std::size_t loopFrames, std::size_t position) const {
    if (loopFrames == 0 || bufferCapacity_ == 0) return 0.0f;
    const std::uint64_t absoluteFrame = captureEnd_ - loopFrames + position;
    const std::uint64_t oldestHistoryFrame =
        framesWritten_ > bufferCapacity_ ? framesWritten_ - bufferCapacity_ : 0;
    if (absoluteFrame < oldestHistoryFrame) {
        return frozen[static_cast<std::size_t>(absoluteFrame % bufferCapacity_)];
    }
    return history[static_cast<std::size_t>(absoluteFrame % bufferCapacity_)];
}

float LiveRepeat::smoothedLoopSample(const float* history, const float* frozen,
                                     std::size_t loopFrames,
                                     std::size_t position) const {
    const float sample = historySample(history, frozen, loopFrames, position);
    if (loopFrames < 2) return sample;

    const std::size_t fadeFrames =
        std::min<std::size_t>(kSeamFrames, loopFrames / 4U);
    if (fadeFrames == 0) return sample;

    const std::size_t distanceToSeam =
        std::min(position, loopFrames - 1U - position);
    if (distanceToSeam >= fadeFrames) return sample;

    const float first = historySample(history, frozen, loopFrames, 0);
    const float last = historySample(history, frozen, loopFrames, loopFrames - 1U);
    const float seam = (first + last) * 0.5f;
    const float fade = static_cast<float>(distanceToSeam) /
                       static_cast<float>(fadeFrames);
    return seam + (sample - seam) * fade;
}

float LiveRepeat::smoothedLoopSampleF(const float* history, const float* frozen,
                                      std::size_t loopFrames,
                                      float position) const {
    if (loopFrames == 0 || bufferCapacity_ == 0) return 0.0f;
    // Lecture interpolee pour la position fractionnaire SHEPARD.
    const float clamped =
        std::clamp(position, 0.0f, static_cast<float>(loopFrames - 1U));
    const std::size_t i0 = static_cast<std::size_t>(clamped);
    const std::size_t i1 = i0 + 1U < loopFrames ? i0 + 1U : i0;
    const float frac = clamped - static_cast<float>(i0);
    const float s0 = historySample(history, frozen, loopFrames, i0);
    const float s1 = historySample(history, frozen, loopFrames, i1);
    float sample = s0 + (s1 - s0) * frac;

    // Meme lissage de couture que la lecture entiere, en distance flottante.
    const std::size_t fadeFrames =
        std::min<std::size_t>(kSeamFrames, loopFrames / 4U);
    if (fadeFrames == 0) return sample;

    const float distanceToSeam =
        std::min(position, static_cast<float>(loopFrames - 1U) - position);
    if (distanceToSeam >= static_cast<float>(fadeFrames)) return sample;

    const float first = historySample(history, frozen, loopFrames, 0);
    const float last = historySample(history, frozen, loopFrames, loopFrames - 1U);
    const float seam = (first + last) * 0.5f;
    const float fade = distanceToSeam / static_cast<float>(fadeFrames);
    return seam + (sample - seam) * fade;
}

void LiveRepeat::process(float* left, float* right, int numFrames) {
    if (left == nullptr || right == nullptr || numFrames <= 0 ||
        bufferCapacity_ == 0) return;

    for (int frame = 0; frame < numFrames; ++frame) {
        const float dryL = left[frame];
        const float dryR = right[frame];
        const std::size_t writeIndex =
            static_cast<std::size_t>(framesWritten_ % bufferCapacity_);
        const float mix = mixRamp_.get();
        if ((requestedActive_ || mix > 0.0f) && framesWritten_ >= bufferCapacity_) {
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
            if (mode_ == RepeatMode::Shepard) {
                // Montee infinie : taux de lecture 1 -> 1+depth sur
                // kShepardCyclesPerRise boucles, puis retombee au wrap.
                const float rateMul = 1.0f + shepardDepth_ * shepardPhase_;
                wetL = smoothedLoopSampleF(historyL_, frozenL_, loopFrames_,
                                           loopPositionF_);
                wetR = smoothedLoopSampleF(historyR_, frozenR_, loopFrames_,
                                           loopPositionF_);
                loopPositionF_ += rateMul;
                if (loopPositionF_ >= static_cast<float>(loopFrames_)) {
                    loopPositionF_ -= static_cast<float>(loopFrames_);
                }
                shepardPhase_ += 1.0f / (static_cast<float>(loopFrames_) *
                                         static_cast<float>(kShepardCyclesPerRise));
                if (shepardPhase_ >= 1.0f) shepardPhase_ -= 1.0f;
            } else {
                wetL = smoothedLoopSample(historyL_, frozenL_, loopFrames_, loopPosition_);
                wetR = smoothedLoopSample(historyR_, frozenR_, loopFrames_, loopPosition_);

                if (loopCrossfadeRemaining_ > 0 && oldLoopFrames_ > 0) {
                    const float fade = 1.0f - static_cast<float>(loopCrossfadeRemaining_) /
                                                    static_cast<float>(kRampFrames);
                    const float oldL =
                        smoothedLoopSample(historyL_, frozenL_, oldLoopFrames_, oldLoopPosition_);
                    const float oldR =
                        smoothedLoopSample(historyR_, frozenR_, oldLoopFrames_, oldLoopPosition_);
                    wetL = oldL + (wetL - oldL) * fade;
                    wetR = oldR + (wetR - oldR) * fade;
                    oldLoopPosition_ = (oldLoopPosition_ + 1U) % oldLoopFrames_;
                    --loopCrossfadeRemaining_;
                }
                loopPosition_ = (loopPosition_ + 1U) % loopFrames_;
            }
        }

        // Ordre historique : on avance la rampe PUIS on lit la valeur.
        const float appliedMix = mixRamp_.tick();
        left[frame] = std::clamp(dryL + (wetL - dryL) * appliedMix, -1.0f, 1.0f);
        right[frame] = std::clamp(dryR + (wetR - dryR) * appliedMix, -1.0f, 1.0f);
    }
}
