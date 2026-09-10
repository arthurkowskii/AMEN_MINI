#include "stream_mixer.h"
#include <algorithm>

namespace amen {
bool StreamMixer::idle(size_t v) const { return !voices_[v].active.load(std::memory_order_acquire); }
void StreamMixer::prepare(size_t v, uint32_t frames) {
    auto& voice = voices_[v];
    voice.written.store(0, std::memory_order_relaxed);
    voice.read.store(0, std::memory_order_relaxed);
    voice.stopping.store(false, std::memory_order_relaxed);
    voice.total = frames;
    voice.consumed = voice.fade = 0;
    voice.last = {};
}
uint32_t StreamMixer::writable(size_t v) const {
    const auto& voice = voices_[v];
    return kCapacity - (voice.written.load(std::memory_order_relaxed) - voice.read.load(std::memory_order_acquire));
}
size_t StreamMixer::write(size_t v, const StereoFrame* frames, size_t count) {
    auto& voice = voices_[v];
    count = std::min(count, size_t(writable(v)));
    const uint32_t written = voice.written.load(std::memory_order_relaxed);
    for (size_t i = 0; i < count; ++i) voice.ring[(written + i) % kCapacity] = frames[i];
    voice.written.store(written + static_cast<uint32_t>(count), std::memory_order_release);
    return count;
}
void StreamMixer::start(size_t v) { voices_[v].active.store(true, std::memory_order_release); }
void StreamMixer::stop(size_t v) { voices_[v].stopping.store(true, std::memory_order_release); }
void StreamMixer::render(int16_t* left, int16_t* right, size_t frames) {
    std::fill_n(left, frames, int16_t(0));
    std::fill_n(right, frames, int16_t(0));
    for (auto& voice : voices_) {
        if (!voice.active.load(std::memory_order_acquire)) continue;
        uint32_t read = voice.read.load(std::memory_order_relaxed);
        const uint32_t written = voice.written.load(std::memory_order_acquire);
        bool finished = false, starved = false;
        for (size_t i = 0; i < frames; ++i) {
            StereoFrame sample{};
            uint32_t gain = kFade;
            if (voice.stopping.load(std::memory_order_acquire)) {
                sample = voice.last;
                gain = kFade - ++voice.fade;
                finished = voice.fade == kFade;
            } else if (read != written) {
                sample = voice.ring[read++ % kCapacity];
                ++voice.consumed;
                gain = std::min(kFade, std::min(voice.consumed, voice.total - voice.consumed));
                voice.last.left = static_cast<int16_t>(int32_t(sample.left) * int32_t(gain) / int32_t(kFade));
                voice.last.right = static_cast<int16_t>(int32_t(sample.right) * int32_t(gain) / int32_t(kFade));
                finished = voice.consumed == voice.total;
            } else {
                starved = true;
                voice.last = {};
            }
            left[i] = static_cast<int16_t>(left[i] + int32_t(sample.left) * int32_t(gain) / int32_t(kFade * 4));
            right[i] = static_cast<int16_t>(right[i] + int32_t(sample.right) * int32_t(gain) / int32_t(kFade * 4));
            if (finished) break;
        }
        voice.read.store(read, std::memory_order_release);
        if (starved) underruns_.fetch_add(1, std::memory_order_relaxed);
        if (finished) voice.active.store(false, std::memory_order_release);
    }
}
}
