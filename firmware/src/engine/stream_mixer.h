#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace amen {
struct StereoFrame { int16_t left = 0; int16_t right = 0; };
class StreamMixer {
public:
    static constexpr size_t kVoices = 4;
    static constexpr uint32_t kCapacity = 8192;
    static constexpr uint32_t kFade = 64;
    bool idle(size_t voice) const;
    void prepare(size_t voice, uint32_t frames);
    size_t write(size_t voice, const StereoFrame* frames, size_t count);
    uint32_t writable(size_t voice) const;
    void start(size_t voice);
    void stop(size_t voice);
    void render(int16_t* left, int16_t* right, size_t frames);
    uint32_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
private:
    struct Voice {
        std::array<StereoFrame, kCapacity> ring{};
        std::atomic<uint32_t> written{0}, read{0};
        std::atomic<bool> active{false}, stopping{false};
        uint32_t total = 0, consumed = 0, fade = 0;
        StereoFrame last{};
    };
    std::array<Voice, kVoices> voices_{};
    std::atomic<uint32_t> underruns_{0};
};
static_assert(std::atomic<uint32_t>::is_always_lock_free, "Audio counters must be lock free");
static_assert(std::atomic<bool>::is_always_lock_free, "Audio state must be lock free");
}
