#include "pad_recorder.h"

#include <algorithm>

PadRecorder::PadRecorder(std::size_t padCount, std::int16_t* storage,
                         std::size_t capacityFrames)
    : padCount_(storage == nullptr ? 0 : std::min(padCount, kMaxPads)),
      storage_(storage),
      capacityFrames_(capacityFrames) {}

void PadRecorder::arm(std::size_t pad, std::uint32_t sampleRate) {
    if (pad >= padCount_ || capacityFrames_ == 0) return;
    recording_ = true;
    activePad_ = static_cast<int>(pad);
    frames_[pad] = 0;
    sampleRates_[pad] = sampleRate;
}

void PadRecorder::stop() {
    recording_ = false;
    activePad_ = -1;
}

void PadRecorder::record(const float* left, const float* right, int numFrames) {
    if (!recording_ || left == nullptr || right == nullptr || numFrames <= 0) {
        return;
    }
    const std::size_t pad = static_cast<std::size_t>(activePad_);
    std::int16_t* block = storage_ + pad * capacityFrames_;
    std::size_t written = frames_[pad];

    for (int frame = 0; frame < numFrames; ++frame) {
        if (written >= capacityFrames_) {
            // Capacite atteinte : l'enregistrement s'arrete de lui-meme
            // (aucun debordement dans le bloc du pad suivant). Le compteur
            // est persiste AVANT le stop pour ne pas perdre la derniere
            // frame ecrite dans ce meme bloc.
            frames_[pad] = written;
            stop();
            return;
        }
        // Mono = (L+R)/2, clamp [-1, 1], puis int16 arrondi au plus proche
        // (symetrique, deterministe) : +0.5 pour les positifs, -0.5 pour
        // les negatifs, puis troncature.
        const float mono =
            std::clamp(0.5f * (left[frame] + right[frame]), -1.0f, 1.0f);
        const float scaled = mono * 32767.0f;
        const std::int32_t rounded = static_cast<std::int32_t>(
            scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
        block[written] =
            static_cast<std::int16_t>(std::clamp(rounded, -32768, 32767));
        ++written;
    }
    frames_[pad] = written;
}

PcmView PadRecorder::pcm(std::size_t pad) const {
    PcmView view{};
    if (pad >= padCount_ || storage_ == nullptr) return view;
    view.sampleRate = sampleRates_[pad];
    view.channels = 1;
    view.samples = storage_ + pad * capacityFrames_;
    view.sampleCount = frames_[pad];
    return view;
}
