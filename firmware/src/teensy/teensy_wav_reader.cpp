#include "teensy_wav_reader.h"

#include <limits>

bool TeensyWavReader::seek(uint64_t offset) {
    if (!file_ || offset > std::numeric_limits<uint32_t>::max()) return false;
    return file_.seek(static_cast<uint32_t>(offset));
}

std::size_t TeensyWavReader::read(void* destination, std::size_t byteCount) {
    if (!file_ || destination == nullptr || byteCount == 0) return 0;
    return file_.read(static_cast<uint8_t*>(destination), byteCount);
}
