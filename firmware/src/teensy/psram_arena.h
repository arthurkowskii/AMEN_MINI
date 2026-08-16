#pragma once

#include "../engine/pcm_view.h"

#include <cstddef>
#include <cstdint>

class PsramArena {
public:
    static constexpr std::size_t kDefaultCapacityBytes = 8U * 1024U * 1024U;

    enum class Error {
        None,
        AlreadyInitialized,
        NoPsram,
        InsufficientPsram,
        AllocationFailed,
    };

    // Must be called once during setup(), before audio starts.
    bool begin(std::size_t capacityBytes = kDefaultCapacityBytes);

    bool ready() const { return samples_ != nullptr; }
    Error error() const { return error_; }
    int16_t* data() { return samples_; }
    const int16_t* data() const { return samples_; }
    std::size_t capacitySamples() const { return capacitySamples_; }
    std::size_t capacityBytes() const { return capacitySamples_ * sizeof(int16_t); }

    PcmView view(uint32_t sampleRate, uint16_t channels, std::size_t sampleCount) const;

private:
    int16_t* samples_ = nullptr;
    std::size_t capacitySamples_ = 0;
    Error error_ = Error::None;
};
