#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Spectral gate 8 bandes (plan 7.2, V0).
//
// Le mix stereo global est decoupe en 8 bandes par une echelle de passe-bas
// Linkwitz-Riley d'ordre 2 (Q = 0.5) aux coupures 200/400/800/1600/3200/
// 6400/12800 Hz. Chaque bande b est la difference LP(b) - LP(b-1) des
// passe-bas appliques a l'entree brute : les differences se telescopent et la
// somme des huit bandes reconstruit le signal d'entree exactement (somme
// plate par construction, a l'arrondi flottant pres) - c'est la forme
// algebrique d'un arbre de crossover LR2 qui garantit la platitude.
//
// Chaque bande est ponderee par un motif rythmique de 16 pas (1/16 de note,
// synchronise au BPM global). Les ouvertures/fermetures sont lissees par un
// filtre une-pole de ~5 ms par echantillon : aucun clic, meme en plein
// signal. Pas d'allocation, pas d'attente : tout l'etat vit dans l'objet.
// Un seul effet global est actif a la fois en V0 (le pad FX tenu gagne).
//
// Non reentrant : les appels concurrents doivent etre serialises par
// l'appelant (le harness passe par des atomics lus dans le callback audio).
class SpectralGate {
public:
    static constexpr std::size_t kBandCount = 8;
    static constexpr std::size_t kStepCount = 16;
    static constexpr float kMinBpm = 20.0f;
    static constexpr float kMaxBpm = 300.0f;

    explicit SpectralGate(std::uint32_t sampleRate);

    void setActive(bool active);
    void setBpm(float bpm);
    void setPattern(std::uint8_t band, std::uint16_t stepMask);
    void process(float* left, float* right, int numFrames);

    // Observables pour les tests et le diagnostic.
    float currentGain(std::uint8_t band) const;
    std::uint8_t currentStep() const { return stepIndex_; }
    std::uint16_t pattern(std::uint8_t band) const;
    bool active() const { return requestedActive_; }
    float bpm() const { return bpm_; }
    std::uint32_t stepFrames() const { return stepFrames_; }

private:
    // Biquad Direct Form I transposed (RBJ), coefficients normalises par a0.
    struct Biquad {
        float b0 = 0.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        float process(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static constexpr float kTauSeconds = 0.005f;
    static constexpr std::size_t kSplitCount = 7;  // 8 bandes

    static Biquad makeLowpass(double frequency, std::uint32_t sampleRate);
    static std::uint16_t defaultMask(std::size_t band);
    std::uint32_t computeStepFrames() const;

    std::uint32_t sampleRate_;
    std::array<Biquad, kSplitCount> lpL_{};
    std::array<Biquad, kSplitCount> lpR_{};
    std::array<float, kBandCount> gains_{};
    std::array<std::uint16_t, kBandCount> patterns_{};
    float alpha_ = 1.0f;
    float bpm_ = 120.0f;
    std::uint32_t stepFrames_ = 1;
    std::uint32_t stepPosition_ = 0;
    std::uint8_t stepIndex_ = 0;
    bool requestedActive_ = false;
};
