// Test natif du wav_loader (PC, sans Teensy).
// Usage : amen_test <fichier.wav>
// Charge le fichier, affiche ses infos, réécrit une copie out.wav.
#include "wav_loader.h"

#include <cstring>
#include <fstream>
#include <iostream>

static void write_u32(std::ofstream& f, uint32_t v) {
    char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                 static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    f.write(b, 4);
}
static void write_u16(std::ofstream& f, uint16_t v) {
    char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    f.write(b, 2);
}

static bool save_wav(const std::string& path, const WavData& w) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t dataBytes = static_cast<uint32_t>(w.samples.size()) * 2;
    f.write("RIFF", 4);
    write_u32(f, 36 + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    write_u32(f, 16);
    write_u16(f, 1);                        // PCM
    write_u16(f, w.channels);
    write_u32(f, w.sampleRate);
    write_u32(f, w.sampleRate * w.channels * 2);  // byte rate
    write_u16(f, w.channels * 2);                 // block align
    write_u16(f, 16);                             // bits
    f.write("data", 4);
    write_u32(f, dataBytes);
    f.write(reinterpret_cast<const char*>(w.samples.data()), dataBytes);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: amen_test <fichier.wav>\n";
        return 1;
    }
    WavData w = wav_load(argv[1]);
    if (!w.valid()) {
        std::cerr << "ECHEC : fichier invalide ou non WAV PCM 16-bit\n";
        return 1;
    }
    std::cout << "OK  " << argv[1] << "\n"
              << "  sample rate : " << w.sampleRate << " Hz\n"
              << "  canaux      : " << w.channels << "\n"
              << "  echantillons: " << w.samples.size() << " (entrelaces)\n"
              << "  duree       : "
              << static_cast<double>(w.samples.size() / w.channels) / w.sampleRate
              << " s\n";
    if (save_wav("out.wav", w)) std::cout << "Copie ecrite : out.wav\n";
    return 0;
}
