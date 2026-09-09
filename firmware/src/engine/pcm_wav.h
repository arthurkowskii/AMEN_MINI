#pragma once
#include <cstddef>
#include <cstdint>

namespace amen {
class WavReader {
public:
    virtual ~WavReader() = default;
    virtual uint64_t size() const = 0;
    virtual bool readAt(uint32_t offset, void* destination, size_t count) = 0;
};
struct PcmWav {
    uint32_t offset = 0;
    uint32_t frames = 0;
    uint16_t channels = 0;
};
bool parseWav(WavReader& reader, PcmWav& result);
}
