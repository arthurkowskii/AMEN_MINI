#include "voice_manager.h"

#include <algorithm>
#include <cmath>

bool VoiceManager::trigger(PadId padId, PcmView pcm, std::size_t startFrame,
                           std::size_t endFrame, float userSpeed,
                           PlaybackMode mode) {
    if (!pcm.valid() || startFrame >= endFrame || endFrame > pcm.frameCount() ||
        outputSampleRate_ == 0 || !std::isfinite(userSpeed) ||
        userSpeed < kMinUserSpeed || userSpeed > kMaxUserSpeed) {
        return false;
    }

    const float sourceRateRatio = static_cast<float>(pcm.sampleRate) /
                                  static_cast<float>(outputSampleRate_);
    const float sourceStep = userSpeed * sourceRateRatio;
    if (!std::isfinite(sourceStep) || sourceStep <= 0.0f) return false;

    cancelRetirementsForPad(padId);

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

    bool stealing = false;
    if (selected == nullptr) {
        selected = &*std::min_element(
            voices_.begin(), voices_.end(),
            [](const Voice& a, const Voice& b) { return a.age < b.age; });
        stealing = true;
    }

    if (stealing) retire(selected->player, selected->padId, padId);
    selected->player.setSample(pcm, startFrame, endFrame, mode);
    selected->player.setSpeed(sourceStep);
    selected->player.trigger();
    selected->padId = padId;
    selected->age = nextAge_++;
    selected->sourceRateRatio = sourceRateRatio;
    selected->crossfadeFrame = stealing ? 0 : kCrossfadeFrames;
    return true;
}

void VoiceManager::cancelRetirementsForPad(PadId padId) {
    for (Retirement& retirement : retirements_) {
        if (!retirement.active ||
            (retirement.outgoingPadId != padId && retirement.incomingPadId != padId)) {
            continue;
        }
        retirement.player.stop();
        retirement.crossfadeFrame = kCrossfadeFrames;
        retirement.active = false;
    }
}

void VoiceManager::retire(const SamplePlayer& player, PadId outgoingPadId,
                          PadId incomingPadId) {
    Retirement* selected = nullptr;
    for (Retirement& retirement : retirements_) {
        if (!retirement.active) {
            selected = &retirement;
            break;
        }
    }

    if (selected == nullptr) {
        // Dropping the oldest tail makes overload bounded and repeatable without
        // allowing trigger bursts to allocate or grow work in the audio thread.
        selected = &*std::min_element(
            retirements_.begin(), retirements_.end(),
            [](const Retirement& a, const Retirement& b) { return a.serial < b.serial; });
    }

    selected->player = player;
    selected->outgoingPadId = outgoingPadId;
    selected->incomingPadId = incomingPadId;
    selected->crossfadeFrame = 0;
    selected->serial = nextRetirementSerial_++;
    selected->active = true;
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

void VoiceManager::stopPad(PadId padId) {
    for (Voice& voice : voices_) {
        if (!voice.player.isPlaying() || voice.padId != padId) continue;
        voice.player.stop();
        voice.crossfadeFrame = kCrossfadeFrames;
    }
    cancelRetirementsForPad(padId);
}

bool VoiceManager::isPadPlaying(PadId padId) const {
    for (const Voice& voice : voices_) {
        if (voice.padId == padId && voice.player.isPlaying()) return true;
    }
    return false;
}

void VoiceManager::render(float* outL, float* outR, int numFrames) {
    if (numFrames <= 0) return;

    std::fill_n(outL, numFrames, 0.0f);
    std::fill_n(outR, numFrames, 0.0f);

    for (Voice& voice : voices_) {
        if (!voice.player.isPlaying()) continue;
        renderVoice(voice, outL, outR, numFrames);
    }

    for (Retirement& retirement : retirements_) {
        if (!retirement.active) continue;
        renderRetirement(retirement, outL, outR, numFrames);
    }

    for (int frame = 0; frame < numFrames; ++frame) {
        outL[frame] = std::clamp(outL[frame], -1.0f, 1.0f);
        outR[frame] = std::clamp(outR[frame], -1.0f, 1.0f);
    }
}

void VoiceManager::renderVoice(Voice& voice, float* outL, float* outR, int numFrames) {
    if (voice.crossfadeFrame >= kCrossfadeFrames) {
        voice.player.renderAdditive(outL, outR, numFrames);
        return;
    }

    const std::size_t remaining = kCrossfadeFrames - voice.crossfadeFrame;
    const int fadedFrames = static_cast<int>(
        std::min(remaining, static_cast<std::size_t>(numFrames)));
    // Both endpoints belong to the 64-frame transition: frame 0 is 0/63 and
    // frame 63 is 63/63, so frame 64 is already steady-state incoming audio.
    constexpr float kGainStep = 1.0f / static_cast<float>(kCrossfadeFrames - 1);
    const float startGain = static_cast<float>(voice.crossfadeFrame) * kGainStep;
    voice.player.renderAdditiveScaled(outL, outR, fadedFrames, startGain, kGainStep);
    voice.crossfadeFrame += static_cast<std::size_t>(fadedFrames);

    if (fadedFrames < numFrames && voice.player.isPlaying()) {
        voice.player.renderAdditive(outL + fadedFrames, outR + fadedFrames,
                                    numFrames - fadedFrames);
    }
}

void VoiceManager::renderRetirement(Retirement& retirement, float* outL, float* outR,
                                    int numFrames) {
    const std::size_t remaining = kCrossfadeFrames - retirement.crossfadeFrame;
    const int fadedFrames = static_cast<int>(
        std::min(remaining, static_cast<std::size_t>(numFrames)));
    constexpr float kGainStep = 1.0f / static_cast<float>(kCrossfadeFrames - 1);
    const float startGain =
        1.0f - static_cast<float>(retirement.crossfadeFrame) * kGainStep;
    const bool stillPlaying = retirement.player.renderAdditiveScaled(
        outL, outR, fadedFrames, startGain, -kGainStep);
    retirement.crossfadeFrame += static_cast<std::size_t>(fadedFrames);
    if (!stillPlaying || retirement.crossfadeFrame == kCrossfadeFrames) {
        retirement.active = false;
    }
}

void VoiceManager::stopAll() {
    for (Voice& voice : voices_) {
        voice.player.stop();
        voice.crossfadeFrame = kCrossfadeFrames;
    }
    for (Retirement& retirement : retirements_) {
        retirement.player.stop();
        retirement.crossfadeFrame = kCrossfadeFrames;
        retirement.active = false;
    }
}
