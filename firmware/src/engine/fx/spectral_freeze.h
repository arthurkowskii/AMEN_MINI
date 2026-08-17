#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Spectral freeze global (plan 7.1, V0).
//
// Capture le spectre d'amplitude de la fenetre la plus recente du mix
// (FFT reelle 512, fenetre de Hann periodique), abandonne les phases, puis
// re-synthetise une boucle 512-periodique a partir des amplitudes avec des
// phases deterministes (LCG seme constant). La boucle est rejouee en
// circulaire : aucun recouvrement, aucune couture, aucune ondulation
// d'amplitude, et le spectre de magnitude de la sortie est exactement celui
// de la capture.
//
// L'activation et le relachement passent par une rampe une-pole (~10 ms)
// sur le gain wet : aucun clic par construction. Tant que l'effet n'a
// jamais ete actif, process() est un passthrough bit-exact (les buffers
// d'entree ne sont pas touches). L'anneau de capture suit l'entree en
// permanence pour qu'une reactivation gele le mix recent sans latence.
//
// Aucune allocation, aucune attente dans process() : tout l'etat vit dans
// l'objet. Non reentrant : serialiser les appels (le harness passe par des
// atomics lus dans le callback audio).
class SpectralFreeze {
public:
    static constexpr std::size_t kFftSize = 512;
    static constexpr std::size_t kHopSize = 256;
    static constexpr float kWetSnap = 1.0e-4f;

    explicit SpectralFreeze(std::uint32_t sampleRate);

    void setActive(bool active);
    void process(float* left, float* right, int numFrames);

    bool active() const { return requestedActive_; }
    float wetGain() const { return wet_; }
    bool frozen() const { return frozen_; }

private:
    void updateRing(const float* left, const float* right, int numFrames);
    void captureFrozenFrame();
    static void fft(std::array<float, kFftSize>& real,
                    std::array<float, kFftSize>& imag, bool inverse);
    static std::uint32_t nextLcg(std::uint32_t& state);

    std::uint32_t sampleRate_;
    float alpha_;  // une-pole ~10 ms pour les rampes wet
    float wet_ = 0.0f;
    bool requestedActive_ = false;
    bool activeAtLastProcess_ = false;
    bool frozen_ = false;

    // Anneau des kFftSize dernieres frames d'entree (par canal), mis a jour
    // en permanence : la capture fige les frames les plus recentes.
    std::array<float, kFftSize> ringL_{};
    std::array<float, kFftSize> ringR_{};
    std::size_t ringPos_ = 0;
    bool ringFull_ = false;

    // Fenetre de Hann periodique (analyse uniquement).
    std::array<float, kFftSize> window_{};

    // Boucle gelee : une periode de 512 frames par canal, rejouee en
    // circulaire depuis frozenPos_.
    std::array<float, kFftSize> frozenL_{};
    std::array<float, kFftSize> frozenR_{};
    std::size_t frozenPos_ = 0;
};
