#include "wav_loader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
class FakeReader final : public WavReader {
public:
    explicit FakeReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    bool seek(uint64_t offset) override {
        if (offset > bytes_.size()) return false;
        position_ = static_cast<size_t>(offset);
        return true;
    }

    size_t read(void* destination, size_t byteCount) override {
        const size_t available = bytes_.size() - position_;
        const size_t count = std::min(byteCount, available);
        if (count != 0) {
            std::memcpy(destination, bytes_.data() + position_, count);
            position_ += count;
        }
        return count;
    }

private:
    const std::vector<uint8_t>& bytes_;
    size_t position_ = 0;
};

void appendU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void appendId(std::vector<uint8_t>& bytes, const char* id) {
    bytes.insert(bytes.end(), id, id + 4);
}

void appendChunk(std::vector<uint8_t>& bytes, const char* id,
                 const std::vector<uint8_t>& payload) {
    appendId(bytes, id);
    appendU32(bytes, static_cast<uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if ((payload.size() & 1U) != 0) bytes.push_back(0);
}

std::vector<uint8_t> makeWav(uint16_t format, uint16_t bits, uint16_t channels,
                             const std::vector<uint8_t>& samples, bool oddChunk = false) {
    std::vector<uint8_t> bytes;
    appendId(bytes, "RIFF");
    appendU32(bytes, 0);
    appendId(bytes, "WAVE");
    if (oddChunk) appendChunk(bytes, "JUNK", {1, 2, 3});

    std::vector<uint8_t> fmt;
    const uint16_t bytesPerSample = static_cast<uint16_t>(bits / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * bytesPerSample);
    appendU16(fmt, format);
    appendU16(fmt, channels);
    appendU32(fmt, 48000);
    appendU32(fmt, 48000U * blockAlign);
    appendU16(fmt, blockAlign);
    appendU16(fmt, bits);
    appendChunk(bytes, "fmt ", fmt);
    appendChunk(bytes, "data", samples);

    const uint32_t riffSize = static_cast<uint32_t>(bytes.size() - 8);
    bytes[4] = static_cast<uint8_t>(riffSize);
    bytes[5] = static_cast<uint8_t>(riffSize >> 8);
    bytes[6] = static_cast<uint8_t>(riffSize >> 16);
    bytes[7] = static_cast<uint8_t>(riffSize >> 24);
    return bytes;
}

bool checkDecode(const std::vector<uint8_t>& wav, const std::vector<int16_t>& expected) {
    FakeReader reader(wav);
    WavMetadata metadata;
    if (!wav_probe(reader, metadata) || !metadata.valid() ||
        metadata.sampleCount != expected.size()) {
        return false;
    }
    std::vector<int16_t> output(expected.size());
    return wav_decode(reader, metadata, output.data(), output.size()) && output == expected;
}

bool formatsDecode() {
    std::vector<uint8_t> floatBytes;
    for (float value : std::array<float, 3>{-1.0f, 0.5f, 1.0f}) {
        uint32_t raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        appendU32(floatBytes, raw);
    }

    return checkDecode(makeWav(1, 8, 1, {0, 128, 255}), {-32768, 0, 32512}) &&
           checkDecode(makeWav(1, 16, 2, {0x00, 0x80, 0xff, 0x7f}, true),
                       {-32768, 32767}) &&
           checkDecode(makeWav(1, 24, 1, {0x00, 0x00, 0x80, 0xff, 0xff, 0x7f}),
                       {-32768, 32767}) &&
           checkDecode(makeWav(1, 32, 1, {0x00, 0x00, 0x00, 0x80,
                                          0xff, 0xff, 0xff, 0x7f}),
                       {-32768, 32767}) &&
           checkDecode(makeWav(3, 32, 1, floatBytes), {-32767, 16384, 32767});
}

bool insufficientCapacityDoesNotWrite() {
    const auto wav = makeWav(1, 16, 2, {1, 0, 2, 0, 3, 0, 4, 0});
    FakeReader reader(wav);
    WavMetadata metadata;
    if (!wav_probe(reader, metadata)) return false;
    std::array<int16_t, 4> output = {111, 222, 333, 444};
    return !wav_decode(reader, metadata, output.data(), 3) &&
           output == std::array<int16_t, 4>{111, 222, 333, 444};
}

bool truncationIsRejected() {
    auto wav = makeWav(1, 24, 1, {0, 0, 0, 1, 2, 3});
    wav.pop_back();
    FakeReader reader(wav);
    WavMetadata metadata;
    if (wav_probe(reader, metadata) || metadata.valid()) return false;

    auto lateTruncation = makeWav(1, 16, 1, {0, 0});
    appendChunk(lateTruncation, "JUNK", {1, 2, 3});
    const uint32_t riffSize = static_cast<uint32_t>(lateTruncation.size() - 8);
    lateTruncation[4] = static_cast<uint8_t>(riffSize);
    lateTruncation[5] = static_cast<uint8_t>(riffSize >> 8);
    lateTruncation[6] = static_cast<uint8_t>(riffSize >> 16);
    lateTruncation[7] = static_cast<uint8_t>(riffSize >> 24);
    lateTruncation.pop_back();
    FakeReader lateReader(lateTruncation);
    return !wav_probe(lateReader, metadata) && !metadata.valid();
}

bool dataBeforeFmtIsRejected() {
    auto valid = makeWav(1, 16, 1, {0, 0});
    std::vector<uint8_t> wav(valid.begin(), valid.begin() + 12);
    appendChunk(wav, "data", {0, 0});
    wav.insert(wav.end(), valid.begin() + 12, valid.begin() + 36);
    const uint32_t riffSize = static_cast<uint32_t>(wav.size() - 8);
    wav[4] = static_cast<uint8_t>(riffSize);
    wav[5] = static_cast<uint8_t>(riffSize >> 8);
    wav[6] = static_cast<uint8_t>(riffSize >> 16);
    wav[7] = static_cast<uint8_t>(riffSize >> 24);
    FakeReader reader(wav);
    WavMetadata metadata;
    return !wav_probe(reader, metadata) && !metadata.valid();
}
}  // namespace

int main() {
    const bool ok = formatsDecode() && insufficientCapacityDoesNotWrite() &&
                    truncationIsRejected() && dataBeforeFmtIsRejected();
    std::cout << (ok ? "wav reader tests: OK\n" : "wav reader tests: FAILED\n");
    return ok ? 0 : 1;
}
