#include "wav_loader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#ifndef ARDUINO
#include <fstream>
#endif
#include <limits>

namespace {
constexpr size_t kReadBlockSize = 8184;  // Divisible by 1, 2, 3 and 4.

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool readExact(WavReader& reader, void* destination, size_t byteCount) {
    auto* bytes = static_cast<uint8_t*>(destination);
    size_t total = 0;
    while (total < byteCount) {
        const size_t count = reader.read(bytes + total, byteCount - total);
        if (count == 0 || count > byteCount - total) return false;
        total += count;
    }
    return true;
}

bool regionExists(WavReader& reader, uint64_t start, uint64_t byteCount) {
    if (byteCount == 0) return true;
    if (start > std::numeric_limits<uint64_t>::max() - (byteCount - 1)) return false;
    uint8_t byte = 0;
    return reader.seek(start + byteCount - 1) && readExact(reader, &byte, 1);
}

bool validateFormat(const WavMetadata& metadata, uint32_t byteRate) {
    const bool pcm = metadata.audioFormat == 1 &&
                     (metadata.bitsPerSample == 8 || metadata.bitsPerSample == 16 ||
                      metadata.bitsPerSample == 24 || metadata.bitsPerSample == 32);
    const bool float32 = metadata.audioFormat == 3 && metadata.bitsPerSample == 32;
    if ((!pcm && !float32) || metadata.sampleRate == 0 ||
        (metadata.channels != 1 && metadata.channels != 2)) {
        return false;
    }

    const uint32_t bytesPerSample = metadata.bitsPerSample / 8;
    const uint32_t expectedAlign = metadata.channels * bytesPerSample;
    const uint64_t expectedRate = static_cast<uint64_t>(metadata.sampleRate) * expectedAlign;
    return metadata.blockAlign == expectedAlign &&
           expectedRate <= std::numeric_limits<uint32_t>::max() && byteRate == expectedRate;
}

bool metadataIsDecodable(const WavMetadata& metadata) {
    if (!metadata.valid()) return false;
    const uint32_t bytesPerSample = metadata.bitsPerSample / 8;
    const bool pcm = metadata.audioFormat == 1 &&
                     (metadata.bitsPerSample == 8 || metadata.bitsPerSample == 16 ||
                      metadata.bitsPerSample == 24 || metadata.bitsPerSample == 32);
    const bool float32 = metadata.audioFormat == 3 && metadata.bitsPerSample == 32;
    const uint32_t expectedAlign = metadata.channels * bytesPerSample;
    if ((!pcm && !float32) || bytesPerSample == 0 || metadata.blockAlign == 0 ||
        metadata.blockAlign != expectedAlign || metadata.dataSize % metadata.blockAlign != 0 ||
        metadata.dataSize % bytesPerSample != 0) {
        return false;
    }
    return metadata.sampleCount == metadata.dataSize / bytesPerSample &&
           metadata.sampleCount <= std::numeric_limits<size_t>::max() / sizeof(int16_t);
}

int16_t toInt16(const uint8_t* p, uint16_t bytesPerSample, bool floatFormat) {
    if (floatFormat) {
        const uint32_t raw = rd32(p);
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        if (std::isnan(value)) value = 0.0f;
        value = std::clamp(value, -1.0f, 1.0f);
        return static_cast<int16_t>(std::lround(value * 32767.0f));
    }
    if (bytesPerSample == 1) {
        return static_cast<int16_t>((static_cast<int16_t>(p[0]) - 128) * 256);
    }
    if (bytesPerSample == 2) return static_cast<int16_t>(rd16(p));
    if (bytesPerSample == 3) {
        int32_t value = static_cast<int32_t>(p[0]) |
                        (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
        if ((value & 0x800000) != 0) value |= static_cast<int32_t>(0xff000000U);
        return static_cast<int16_t>(value >> 8);
    }
    const int32_t value = static_cast<int32_t>(rd32(p));
    return static_cast<int16_t>(value >> 16);
}

bool nativePcm16IsDirectlyReadable() {
    const uint16_t value = 1;
    return sizeof(int16_t) == 2 && *reinterpret_cast<const uint8_t*>(&value) == 1;
}

#ifndef ARDUINO
class FileReader final : public WavReader {
public:
    explicit FileReader(const std::string& path) : file_(path, std::ios::binary) {}

    bool isOpen() const { return file_.is_open(); }

    bool seek(uint64_t offset) override {
        if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            return false;
        }
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        return static_cast<bool>(file_);
    }

    size_t read(void* destination, size_t byteCount) override {
        const size_t maxRead = static_cast<size_t>(std::numeric_limits<std::streamsize>::max());
        const std::streamsize requested = static_cast<std::streamsize>(std::min(byteCount, maxRead));
        file_.read(static_cast<char*>(destination), requested);
        return static_cast<size_t>(file_.gcount());
    }

private:
    std::ifstream file_;
};
#endif
}  // namespace

bool wav_probe(WavReader& reader, WavMetadata& metadata) {
    metadata = WavMetadata{};
    WavMetadata parsed;
    uint8_t header[12];
    if (!reader.seek(0) || !readExact(reader, header, sizeof(header)) ||
        std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }

    const uint32_t riffSize = rd32(header + 4);
    if (riffSize < 4) return false;
    const uint64_t riffEnd = 8ULL + riffSize;
    uint64_t cursor = 12;
    bool foundFmt = false;
    bool foundData = false;

    while (cursor < riffEnd) {
        if (riffEnd - cursor < 8 || !reader.seek(cursor)) return false;
        uint8_t chunkHeader[8];
        if (!readExact(reader, chunkHeader, sizeof(chunkHeader))) return false;

        const uint32_t chunkSize = rd32(chunkHeader + 4);
        const uint64_t payloadOffset = cursor + 8;
        const uint64_t paddedSize = static_cast<uint64_t>(chunkSize) + (chunkSize & 1U);
        if (paddedSize > riffEnd - payloadOffset ||
            !regionExists(reader, payloadOffset, paddedSize)) {
            return false;
        }

        if (std::memcmp(chunkHeader, "fmt ", 4) == 0) {
            if (foundFmt || foundData || chunkSize < 16 || !reader.seek(payloadOffset)) return false;
            uint8_t fmt[16];
            if (!readExact(reader, fmt, sizeof(fmt))) return false;
            parsed.audioFormat = rd16(fmt);
            parsed.channels = rd16(fmt + 2);
            parsed.sampleRate = rd32(fmt + 4);
            const uint32_t byteRate = rd32(fmt + 8);
            parsed.blockAlign = rd16(fmt + 12);
            parsed.bitsPerSample = rd16(fmt + 14);
            if (!validateFormat(parsed, byteRate)) return false;
            foundFmt = true;
        } else if (std::memcmp(chunkHeader, "data", 4) == 0) {
            if (!foundFmt || foundData || chunkSize == 0) return false;
            const uint32_t bytesPerSample = parsed.bitsPerSample / 8;
            if (chunkSize % parsed.blockAlign != 0 || chunkSize % bytesPerSample != 0) {
                return false;
            }
            const uint64_t sampleCount = chunkSize / bytesPerSample;
            if (sampleCount > std::numeric_limits<size_t>::max() / sizeof(int16_t)) return false;
            parsed.dataOffset = payloadOffset;
            parsed.dataSize = chunkSize;
            parsed.sampleCount = static_cast<size_t>(sampleCount);
            foundData = true;
        }
        cursor = payloadOffset + paddedSize;
    }

    if (cursor != riffEnd || !foundFmt || !foundData || !metadataIsDecodable(parsed)) return false;
    metadata = parsed;
    return true;
}

bool wav_decode(WavReader& reader, const WavMetadata& metadata,
                int16_t* destination, size_t capacity) {
    if (!metadataIsDecodable(metadata) || destination == nullptr ||
        capacity < metadata.sampleCount || !reader.seek(metadata.dataOffset)) {
        return false;
    }

    if (metadata.audioFormat == 1 && metadata.bitsPerSample == 16 &&
        nativePcm16IsDirectlyReadable()) {
        size_t remainingBytes = metadata.dataSize;
        auto* output = reinterpret_cast<uint8_t*>(destination);
        while (remainingBytes != 0) {
            const size_t count = std::min(remainingBytes, kReadBlockSize);
            if (!readExact(reader, output, count)) return false;
            output += count;
            remainingBytes -= count;
        }
        return true;
    }

    const uint16_t bytesPerSample = metadata.bitsPerSample / 8;
    const bool floatFormat = metadata.audioFormat == 3;
    uint8_t buffer[kReadBlockSize];
    uint32_t remainingBytes = metadata.dataSize;
    size_t outputIndex = 0;
    while (remainingBytes != 0) {
        const size_t count = std::min<size_t>(remainingBytes, sizeof(buffer));
        if (!readExact(reader, buffer, count)) return false;
        for (size_t offset = 0; offset < count; offset += bytesPerSample) {
            destination[outputIndex++] = toInt16(buffer + offset, bytesPerSample, floatFormat);
        }
        remainingBytes -= static_cast<uint32_t>(count);
    }
    return outputIndex == metadata.sampleCount;
}

#ifndef ARDUINO
WavData wav_load(const std::string& path) {
    WavData output;
    FileReader reader(path);
    WavMetadata metadata;
    if (!reader.isOpen() || !wav_probe(reader, metadata)) return output;

    output.sampleRate = metadata.sampleRate;
    output.channels = metadata.channels;
    output.samples.resize(metadata.sampleCount);
    if (!wav_decode(reader, metadata, output.samples.data(), output.samples.size())) {
        return WavData{};
    }
    return output;
}
#endif
