#include "voice_manager.h"

#include <algorithm>
#include <cmath>

bool VoiceManager::trigger(PadId padId, PcmView pcm, std::size_t startFrame,
                           std::size_t endFrame, float userSpeed) {
    if (!pcm.valid() || startFrame >= endFrame || endFrame > pcm.frameCount() ||
        outputSampleRate_ == 0 || !std::isfinite(userSpeed) ||
        userSpeed < kMinUserSpeed || userSpeed > kMaxUserSpeed) {
        return false;
    }

    const float sourceRateRatio = static_cast<float>(pcm.sampleRate) /
                                  static_cast<float>(outputSampleRate_);
    const float sourceStep = userSpeed * sourceRateRatio;
    if (!std::isfinite(sourceStep) || sourceStep <= 0.0f) return false;

    Voice* selected = nullptr;
    for (Voice& voice : voices_) {
        if (voice.player.isPlaying() && voice.padId == padId) {
            selected = &voice;
            break;
        }
    }

    if (selected == nullptr) {
        for (Voice& voice : voices_) {
            if (!voice.player.isPlaying()) {
                selected = &voice;
                break;
            }
        }
    }

    if (selected == nullptr) {
        selected = &*std::min_element(
            voices_.begin(), voices_.end(),
            [](const Voice& a, const Voice& b) { return a.age < b.age; });
    }

    selected->player.setSample(pcm, startFrame, endFrame);
    selected->player.setSpeed(sourceStep);
    selected->player.trigger();
    selected->padId = padId;
    selected->age = nextAge_++;
    selected->sourceRateRatio = sourceRateRatio;
    return true;
}

bool VoiceManager::setPadSpeed(PadId padId, float userSpeed) {
    if (!std::isfinite(userSpeed) || userSpeed < kMinUserSpeed ||
        userSpeed > kMaxUserSpeed) {
        return false;
    }

    for (Voice& voice : voices_) {
        if (!voice.player.isPlaying() || voice.padId != padId) continue;

        const float sourceStep = userSpeed * voice.sourceRateRatio;
        if (!std::isfinite(sourceStep) || sourceStep <= 0.0f) return false;
        voice.player.setSpeed(sourceStep);
        return true;
    }
    return false;
}

void VoiceManager::render(float* outL, float* outR, int numFrames) {
    if (numFrames <= 0) return;

    std::fill_n(outL, numFrames, 0.0f);
    std::fill_n(outR, numFrames, 0.0f);

    for (Voice& voice : voices_) {
        if (!voice.player.isPlaying()) continue;
        voice.player.renderAdditive(outL, outR, numFrames);
    }

    for (int frame = 0; frame < numFrames; ++frame) {
        outL[frame] = std::clamp(outL[frame], -1.0f, 1.0f);
        outR[frame] = std::clamp(outR[frame], -1.0f, 1.0f);
    }
}

void VoiceManager::stopAll() {
    for (Voice& voice : voices_) voice.player.stop();
}
