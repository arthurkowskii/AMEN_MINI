#pragma once

#include "../engine/wav_loader.h"

#include <SD.h>

class TeensyWavReader final : public WavReader {
public:
    explicit TeensyWavReader(File file) : file_(file) {}

    bool seek(uint64_t offset) override;
    std::size_t read(void* destination, std::size_t byteCount) override;

private:
    File file_;
};
