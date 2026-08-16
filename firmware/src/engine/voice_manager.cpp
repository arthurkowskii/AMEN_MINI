#include "voice_manager.h"

#include <algorithm>
#include <cmath>

bool VoiceManager::trigger(PcmView pcm, std::size_t startFrame, std::size_t endFrame,
                           float speed) {
    if (!pcm.valid() || startFrame >= endFrame || endFrame > pcm.frameCount() ||
        !std::isfinite(speed) || speed <= 0.0f) {
        return false;
    }

    Voice* selected = nullptr;
    for (Voice& voice : voices_) {
        if (!voice.player.isPlaying()) {
            selected = &voice;
            break;
        }
    }

    if (selected == nullptr) {
        selected = &*std::min_element(
            voices_.begin(), voices_.end(),
            [](const Voice& a, const Voice& b) { return a.age < b.age; });
    }

    selected->player.setSample(pcm, startFrame, endFrame);
    selected->player.setSpeed(speed);
    selected->player.trigger();
    selected->age = nextAge_++;
    return true;
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
