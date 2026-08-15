#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Un WAV chargé en mémoire : échantillons 16-bit entrelacés (L R L R... en stéréo).
struct WavData {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    std::vector<int16_t> samples;

    bool valid() const { return sampleRate > 0 && channels > 0 && !samples.empty(); }
};

// Charge un WAV (PCM 8/16/24/32-bit ou float 32) et renvoie les échantillons
// convertis en int16. Renvoie un WavData invalide si le fichier est illisible
// ou d'un format inconnu.
WavData wav_load(const std::string& path);
