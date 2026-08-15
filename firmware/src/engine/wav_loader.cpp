#include "wav_loader.h"

#include <cstring>
#include <fstream>

namespace {
// Lecture little-endian (octet de poids faible d'abord) — le format WAV.
uint16_t rd16(const char* p) {
    return static_cast<uint16_t>(static_cast<uint8_t>(p[0]) |
                                 (static_cast<uint8_t>(p[1]) << 8));
}
uint32_t rd32(const char* p) {
    return static_cast<uint32_t>(static_cast<uint8_t>(p[0])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24);
}
}  // namespace

WavData wav_load(const std::string& path) {
    WavData out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;

    // En-tête RIFF : "RIFF" + taille + "WAVE".
    char hdr[12];
    f.read(hdr, 12);
    if (f.gcount() != 12 || std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        return out;
    }

    // Marche des chunks : chaque chunk = nom (4) + taille LE (4) + contenu.
    while (f) {
        char id[4];
        char sz[4];
        f.read(id, 4);
        if (f.gcount() != 4) break;
        f.read(sz, 4);
        if (f.gcount() != 4) break;
        uint32_t size = rd32(sz);

        if (std::memcmp(id, "fmt ", 4) == 0) {
            char fmt[16] = {0};
            f.read(fmt, size < 16 ? size : 16);
            uint16_t audioFormat = rd16(fmt);
            out.channels = rd16(fmt + 2);
            out.sampleRate = rd32(fmt + 4);
            uint16_t bitsPerSample = rd16(fmt + 14);
            if (audioFormat != 1 || bitsPerSample != 16) {
                out = WavData{};  // pas du PCM 16-bit : on refuse
                return out;
            }
            if (size > 16) f.seekg(size - 16, std::ios::cur);
        } else if (std::memcmp(id, "data", 4) == 0) {
            out.samples.resize(size / 2);
            f.read(reinterpret_cast<char*>(out.samples.data()), size);
            if (f.gcount() != static_cast<std::streamsize>(size)) {
                out = WavData{};
                return out;
            }
            break;  // data est le dernier chunk en pratique
        } else {
            f.seekg(size, std::ios::cur);  // chunk inconnu : on saute
        }
    }

    if (!out.valid()) out = WavData{};
    return out;
}
