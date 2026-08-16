#pragma once
#include "pcm_view.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class WavReader {
public:
    virtual ~WavReader() = default;
    virtual bool seek(uint64_t offset) = 0;
    virtual size_t read(void* destination, size_t byteCount) = 0;
};

struct WavMetadata {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t audioFormat = 0;
    uint16_t bitsPerSample = 0;
    uint16_t blockAlign = 0;
    uint64_t dataOffset = 0;
    uint32_t dataSize = 0;
    size_t sampleCount = 0;

    bool valid() const {
        return sampleRate > 0 && (channels == 1 || channels == 2) &&
               dataOffset > 0 && dataSize > 0 && sampleCount > 0;
    }
};

// Un WAV chargé en mémoire : échantillons 16-bit entrelacés (L R L R... en stéréo).
struct WavData {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    std::vector<int16_t> samples;

    bool valid() const { return sampleRate > 0 && channels > 0 && !samples.empty(); }
    PcmView view() const {
        return {sampleRate, channels, samples.data(), samples.size()};
    }
};

// Valide la structure RIFF/WAVE et décrit exactement la sortie PCM16.
// En cas d'échec, metadata est remis à son état invalide par défaut.
bool wav_probe(WavReader& reader, WavMetadata& metadata);

// Décode directement dans un stockage fourni par l'appelant, sans allocation.
// La capacité est exprimée en échantillons int16 entrelacés.
bool wav_decode(WavReader& reader, const WavMetadata& metadata,
                int16_t* destination, size_t capacity);

// Charge un WAV (PCM 8/16/24/32-bit ou float 32) et renvoie les échantillons
// convertis en int16. Renvoie un WavData invalide si le fichier est illisible
// ou d'un format inconnu.
#ifndef ARDUINO
WavData wav_load(const std::string& path);
#endif
