#pragma once

#include <cstdint>

#include "ramp_gain.h"

enum class PhaseDistMode : std::uint8_t {
    Sine,    // LFO sinusoidal classique (vibe CZ)
    Saw,     // LFO en dents de scie montantes
    Square,  // LFO carre (bascule franche de la phase)
    Self,    // enveloppe du signal : le break plie sa propre phase
};

// PHASE DIST (pad FX) : distorsion de phase auto-modulee.
//
// Un allpass du premier ordre a coefficient variable deforme la phase du
// signal : y[n] = c[n] * x[n] + x[n-1] - c[n] * y[n-1], avec c = 0.95 *
// amount * modulateur. Le modulateur est soit un LFO (SINE / SAW / SQUARE,
// rate 0.1..20 Hz), soit l'enveloppe du signal lui-meme (SELF) : le break
// plie sa propre phase — le squelchy Casio-CZ / piripiri.
//
// keepBass re-injecte une copie filtree passe-bas du dry (one-pole ~120 Hz)
// : les fondations du break survivent a la deformation. La rampe de slew
// glisse l'activation/desactivation (aucun clic, nuance de filtre).
//
// Sans allocation, sans blocage : utilisable dans le callback audio.
class PhaseDistortion {
public:
    static constexpr float kMaxCoefficient = 0.95f;
    static constexpr float kMinRateHz = 0.1f;
    static constexpr float kMaxRateHz = 20.0f;

    explicit PhaseDistortion(std::uint32_t sampleRate);

    void setActive(bool active);
    void setAmount(float amount);
    void setMode(PhaseDistMode mode) { mode_ = mode; }
    void setRateHz(float rate);
    void setKeepBass(float keepBass);
    void setSlewFrames(std::uint32_t frames) { mixRamp_.setSlewFrames(frames); }
    void process(float* left, float* right, int numFrames);

    bool isActive() const { return requestedActive_; }
    float amount() const { return amount_; }
    PhaseDistMode mode() const { return mode_; }
    float rateHz() const { return rateHz_; }
    float keepBass() const { return keepBass_; }
    std::uint32_t slewFrames() const { return mixRamp_.slewFrames(); }

private:
    // Fait avancer l'etat du modulateur et retourne mod dans [-1, 1].
    float nextModulator(float inputAbs);
    float allpass(float input, float& x1, float& y1, float coefficient) const;

    std::uint32_t sampleRate_;
    bool requestedActive_ = false;
    float amount_ = 1.0f;
    PhaseDistMode mode_ = PhaseDistMode::Sine;
    float rateHz_ = 1.0f;
    float keepBass_ = 0.0f;
    RampGain mixRamp_;

    // Memoire d'allpass (une paire par canal) et d'enveloppe SELF.
    float x1L_ = 0.0f;
    float y1L_ = 0.0f;
    float x1R_ = 0.0f;
    float y1R_ = 0.0f;
    float envelope_ = 0.0f;

    // Phase du LFO dans [0, 1).
    float lfoPhase_ = 0.0f;

    // Filtre passe-bas du chemin keep-bass (one-pole).
    float lowL_ = 0.0f;
    float lowR_ = 0.0f;
};
