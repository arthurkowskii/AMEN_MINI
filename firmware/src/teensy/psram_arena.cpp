#include "psram_arena.h"

#include <Arduino.h>

#include <limits>

extern "C" uint8_t external_psram_size;

bool PsramArena::begin(std::size_t capacityBytes) {
    if (samples_ != nullptr) {
        error_ = Error::AlreadyInitialized;
        return false;
    }
    if (capacityBytes == 0 || capacityBytes % sizeof(int16_t) != 0 ||
        capacityBytes > std::numeric_limits<uint32_t>::max()) {
        error_ = Error::AllocationFailed;
        return false;
    }
    if (external_psram_size == 0) {
        error_ = Error::NoPsram;
        return false;
    }
    constexpr std::size_t kBytesPerMib = 1024U * 1024U;
    const std::size_t psramBytes = static_cast<std::size_t>(external_psram_size) * kBytesPerMib;
    if (psramBytes < capacityBytes) {
        error_ = Error::InsufficientPsram;
        return false;
    }

    samples_ = static_cast<int16_t*>(extmem_malloc(capacityBytes));
    if (samples_ == nullptr) {
        error_ = Error::AllocationFailed;
        return false;
    }

    constexpr std::uintptr_t kExtmemBase = 0x70000000U;
    const std::uintptr_t psramEnd =
        kExtmemBase + static_cast<std::uintptr_t>(external_psram_size) * kBytesPerMib;
    const std::uintptr_t allocationStart = reinterpret_cast<std::uintptr_t>(samples_);
    if (allocationStart < kExtmemBase || allocationStart >= psramEnd ||
        capacityBytes > psramEnd - allocationStart) {
        extmem_free(samples_);
        samples_ = nullptr;
        error_ = Error::AllocationFailed;
        return false;
    }

    capacitySamples_ = capacityBytes / sizeof(int16_t);
    error_ = Error::None;
    return true;
}

PcmView PsramArena::view(uint32_t sampleRate, uint16_t channels,
                         std::size_t sampleCount) const {
    if (!ready() || sampleCount > capacitySamples_) return {};
    return {sampleRate, channels, samples_, sampleCount};
}
