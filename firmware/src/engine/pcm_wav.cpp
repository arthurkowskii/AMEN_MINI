#include "pcm_wav.h"
#include <cstring>

namespace amen {
static uint16_t u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8));
}
static uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool parseWav(WavReader& reader, PcmWav& result) {
    uint8_t header[16];
    if (reader.size() < 12 || !reader.readAt(0, header, 12)) return false;
    if (std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
    const uint64_t end = uint64_t(u32(header + 4)) + 8;
    if (end < 12 || end > reader.size() || end > UINT32_MAX) return false;
    bool format = false, data = false;
    uint32_t bytes = 0;
    PcmWav candidate;
    for (uint64_t pos = 12; pos < end;) {
        if (end - pos < 8 || !reader.readAt(static_cast<uint32_t>(pos), header, 8)) return false;
        const uint32_t length = u32(header + 4);
        const uint64_t next = pos + 8 + length + (length & 1U);
        if (next > end) return false;
        if (!std::memcmp(header, "fmt ", 4)) {
            if (format || length < 16 || !reader.readAt(static_cast<uint32_t>(pos + 8), header, 16)) return false;
            candidate.channels = u16(header + 2);
            const uint16_t align = candidate.channels * 2;
            if (u16(header) != 1 || (candidate.channels != 1 && candidate.channels != 2) ||
                u32(header + 4) != 44100 || u16(header + 14) != 16 ||
                u16(header + 12) != align || u32(header + 8) != 44100U * align) return false;
            format = true;
        } else if (!std::memcmp(header, "data", 4)) {
            if (data) return false;
            candidate.offset = static_cast<uint32_t>(pos + 8);
            bytes = length;
            data = true;
        }
        pos = next;
    }
    if (!format || !data || bytes == 0 || bytes % (candidate.channels * 2)) return false;
    candidate.frames = bytes / (candidate.channels * 2);
    result = candidate;
    return true;
}
}
