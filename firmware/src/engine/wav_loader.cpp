#include "wav_loader.h"

#include <algorithm>
#include <cmath>
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

// Convertit un échantillon brut vers int16 selon le format source.
// Le moteur ne connaît que le int16 ; c'est ici, à la frontière, que
// toute conversion a lieu (une fois, au chargement, hors temps réel).
int16_t to_int16(const uint8_t* p, int bytesPerSample, bool floatFormat) {
    if (floatFormat && bytesPerSample == 4) {  // IEEE 754, -1.0 .. 1.0
        uint32_t u = static_cast<uint32_t>(p[0]) |
                     (static_cast<uint32_t>(p[1]) << 8) |
                     (static_cast<uint32_t>(p[2]) << 16) |
                     (static_cast<uint32_t>(p[3]) << 24);
        float f;
        std::memcpy(&f, &u, 4);
        f = std::clamp(f, -1.0f, 1.0f);
        return static_cast<int16_t>(std::lround(f * 32767.0f));
    }
    if (bytesPerSample == 1) {  // PCM 8-bit non signé, silence = 128
        return static_cast<int16_t>((static_cast<int16_t>(p[0]) - 128) << 8);
    }
    if (bytesPerSample == 2) {  // PCM 16-bit signé
        return static_cast<int16_t>(rd16(reinterpret_cast<const char*>(p)));
    }
    if (bytesPerSample == 3) {  // PCM 24-bit signé
        int32_t v = static_cast<int32_t>(p[0]) |
                    (static_cast<int32_t>(p[1]) << 8) |
                    (static_cast<int32_t>(p[2]) << 16);
        if (v & 0x800000) v |= 0xFF000000;  // extension de signe
        return static_cast<int16_t>(v >> 8);
    }
    if (bytesPerSample == 4) {  // PCM 32-bit signé
        int32_t v = static_cast<int32_t>(rd32(reinterpret_cast<const char*>(p)));
        return static_cast<int16_t>(v >> 16);
    }
    return 0;
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

    uint16_t audioFormat = 0;   // 1 = PCM entier, 3 = IEEE float
    uint16_t bitsPerSample = 0;
    while (f) {
        char id[4];
        char sz[4];
        f.read(id, 4);
        if (f.gcount() != 4) break;
        f.read(sz, 4);
        if (f.gcount() != 4) break;
        uint32_t size = rd32(sz);

        if (std::memcmp(id, "fmt ", 4) == 0) {
            char fmt[40] = {0};
            f.read(fmt, size < 40 ? size : 40);
            audioFormat = rd16(fmt);
            out.channels = rd16(fmt + 2);
            out.sampleRate = rd32(fmt + 4);
            bitsPerSample = rd16(fmt + 14);
            if (size > 40) f.seekg(size - 40, std::ios::cur);
        } else if (std::memcmp(id, "data", 4) == 0) {
            const int bytesPerSample = bitsPerSample / 8;
            const bool isPcm = (audioFormat == 1);
            const bool isFloat = (audioFormat == 3);
            const bool supported = (isPcm && (bitsPerSample == 8 || bitsPerSample == 16 ||
                                              bitsPerSample == 24 || bitsPerSample == 32)) ||
                                   (isFloat && bitsPerSample == 32);
            if (!supported) {
                out = WavData{};  // format inconnu : on refuse
                return out;
            }
            // Lecture par paquets + conversion à la volée : jamais deux
            // copies du fichier en mémoire (brut + converti).
            // Taille de bloc multiple de 12 : divisible par 1, 2, 3 et 4
            // octets/échantillon — sinon on perd des échantillons aux
            // frontières de blocs (piège 24-bit : 8192 % 3 != 0).
            out.samples.reserve(size / bytesPerSample);
            char buf[8184];
            uint32_t remaining = size;
            while (remaining > 0) {
                uint32_t want = std::min<uint32_t>(remaining, sizeof(buf));
                f.read(buf, want);
                if (f.gcount() != static_cast<std::streamsize>(want)) {
                    out = WavData{};
                    return out;
                }
                const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
                for (uint32_t i = 0; i + bytesPerSample <= want; i += bytesPerSample) {
                    out.samples.push_back(to_int16(p + i, bytesPerSample, isFloat));
                }
                remaining -= want;
            }
            break;  // data est le dernier chunk en pratique
        } else {
            f.seekg(size, std::ios::cur);  // chunk inconnu : on saute
        }
    }

    if (!out.valid()) out = WavData{};
    return out;
}
