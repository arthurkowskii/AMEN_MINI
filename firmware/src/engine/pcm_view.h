#pragma once

#include <cstddef>
#include <cstdint>

struct PcmView {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    const int16_t* samples = nullptr;
    std::size_t sampleCount = 0;

    bool valid() const {
        return sampleRate > 0 && channels > 0 && samples != nullptr &&
               sampleCount >= channels;
    }

    std::size_t frameCount() const {
        return channels > 0 ? sampleCount / channels : 0;
    }
};
