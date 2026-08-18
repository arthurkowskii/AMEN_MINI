#include "phase_distortion.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
// One-pole du chemin keep-bass : ~120 Hz (alpha = 1 - exp(-2*pi*f/sr)).
float lowpassAlpha(std::uint32_t sampleRate) {
    if (sampleRate == 0) return 0.0f;
    const double w = 2.0 * kPi * 120.0 / static_cast<double>(sampleRate);
    return static_cast<float>(1.0 - std::exp(-w));
}
// Release de l'enveloppe SELF : ~50 ms.
float envelopeRelease(std::uint32_t sampleRate) {
    if (sampleRate == 0) return 0.0f;
    return static_cast<float>(
        1.0 - std::exp(-1.0 / (0.050 * static_cast<double>(sampleRate))));
}
}  // namespace

PhaseDistortion::PhaseDistortion(std::uint32_t sampleRate)
    : sampleRate_(sampleRate) {}

void PhaseDistortion::setActive(bool active) {
    if (active == requestedActive_) return;
    requestedActive_ = active;
    mixRamp_.setTarget(active ? amount_ : 0.0f);
}

void PhaseDistortion::setAmount(float amount) {
    if (!std::isfinite(amount)) return;
    amount_ = std::clamp(amount, 0.0f, 1.0f);
    mixRamp_.setTarget(requestedActive_ ? amount_ : 0.0f);
}

void PhaseDistortion::setRateHz(float rate) {
    if (!std::isfinite(rate)) return;
    rateHz_ = std::clamp(rate, kMinRateHz, kMaxRateHz);
}

void PhaseDistortion::setKeepBass(float keepBass) {
    if (!std::isfinite(keepBass)) return;
    keepBass_ = std::clamp(keepBass, 0.0f, 1.0f);
}

float PhaseDistortion::nextModulator(float inputAbs) {
    if (mode_ == PhaseDistMode::Self) {
        // Enveloppe peak-hold a release one-pole : le modulateur est le
        // signal lui-meme, remis a l'echelle [-1, 1] (2 * env - 1).
        if (inputAbs > envelope_) {
            envelope_ = inputAbs;
        } else {
            envelope_ += envelopeRelease(sampleRate_) * (inputAbs - envelope_);
        }
        return std::clamp(2.0f * envelope_ - 1.0f, -1.0f, 1.0f);
    }

    float mod = 0.0f;
    switch (mode_) {
        case PhaseDistMode::Saw:
            mod = 2.0f * lfoPhase_ - 1.0f;
            break;
        case PhaseDistMode::Square:
            mod = lfoPhase_ < 0.5f ? 1.0f : -1.0f;
            break;
        case PhaseDistMode::Sine:
        default: {
            const double angle = 2.0 * kPi * static_cast<double>(lfoPhase_);
            mod = static_cast<float>(std::sin(angle));
            break;
        }
    }
    lfoPhase_ += rateHz_ / static_cast<float>(sampleRate_ > 0 ? sampleRate_ : 1);
    if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
    return mod;
}

float PhaseDistortion::allpass(float input, float& x1, float& y1,
                               float coefficient) const {
    // y[n] = c * x[n] + x[n-1] - c * y[n-1] ; gain unitaire pour c fixe,
    // mais a coefficient VARIABLE la memoire peut transitoirement depasser
    // : le clamp d'etat borne la recursion (garde anti-emballement, meme
    // prix qu'un std::clamp).
    const float output =
        std::clamp(coefficient * input + x1 - coefficient * y1, -1.0f, 1.0f);
    x1 = input;
    y1 = output;
    return output;
}

void PhaseDistortion::process(float* left, float* right, int numFrames) {
    if (left == nullptr || right == nullptr || numFrames <= 0) return;

    const float lowAlpha = lowpassAlpha(sampleRate_);
    for (int frame = 0; frame < numFrames; ++frame) {
        const float dryL = left[frame];
        const float dryR = right[frame];
        const float mix = mixRamp_.get();

        float wetL = dryL;
        float wetR = dryR;
        if (requestedActive_ || mix > 0.0f) {
            // Modulateur partage entre canaux : la phase reste coherente.
            const float inputAbs =
                std::max(std::fabs(dryL), std::fabs(dryR));
            const float mod = nextModulator(inputAbs);
            const float coefficient =
                kMaxCoefficient * amount_ * std::clamp(mod, -1.0f, 1.0f);
            wetL = allpass(dryL, x1L_, y1L_, coefficient);
            wetR = allpass(dryR, x1R_, y1R_, coefficient);
        }

        const float appliedMix = mixRamp_.tick();
        // keep-bass : le dry filtre passe-bas est re-injecte dans le wet
        // (les fondations survivent a la deformation, sans toucher au dry).
        lowL_ += lowAlpha * (dryL - lowL_);
        lowR_ += lowAlpha * (dryR - lowR_);
        const float outL = dryL + (wetL - dryL) * appliedMix +
                           lowL_ * keepBass_ * appliedMix;
        const float outR = dryR + (wetR - dryR) * appliedMix +
                           lowR_ * keepBass_ * appliedMix;
        left[frame] = std::clamp(outL, -1.0f, 1.0f);
        right[frame] = std::clamp(outR, -1.0f, 1.0f);
    }
}
