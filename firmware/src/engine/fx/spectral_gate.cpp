#include "fx/spectral_gate.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

SpectralGate::SpectralGate(std::uint32_t sampleRate)
    : sampleRate_(sampleRate) {
    if (sampleRate_ > 0) {
        const float tauSamples =
            kTauSeconds * static_cast<float>(sampleRate_);
        alpha_ = 1.0f - std::exp(-1.0f / tauSamples);
        // Borne de securite : le contrat "pas de clic" tient a toute
        // frequence d'echantillonnage, meme les rates degeneres des tests.
        alpha_ = std::min(alpha_, 0.5f);
    }
    for (std::size_t split = 0; split < kSplitCount; ++split) {
        const double frequency =
            200.0 * std::ldexp(1.0, static_cast<int>(split));
        lpL_[split] = makeLowpass(frequency, sampleRate_);
        lpR_[split] = lpL_[split];
    }
    for (std::size_t band = 0; band < kBandCount; ++band) {
        patterns_[band] = defaultMask(band);
        gains_[band] = 1.0f;
    }
    stepFrames_ = computeStepFrames();
}

void SpectralGate::setActive(bool active) {
    // Le lissage des gains tourne en permanence : l'activation ne provoque
    // aucune discontinuite, les gains sont deja sur les cibles du pas courant.
    requestedActive_ = active;
}

void SpectralGate::setBpm(float bpm) {
    if (!std::isfinite(bpm)) return;
    const float clamped = std::clamp(bpm, kMinBpm, kMaxBpm);
    if (clamped == bpm_) return;
    bpm_ = clamped;
    stepFrames_ = computeStepFrames();
    // Conserve la position de phase en restant sur le pas courant.
    if (stepPosition_ >= stepFrames_) {
        stepPosition_ = stepFrames_ - 1U;
    }
}

void SpectralGate::setPattern(std::uint8_t band, std::uint16_t stepMask) {
    patterns_[std::min<std::size_t>(band, kBandCount - 1U)] = stepMask;
}

std::uint16_t SpectralGate::pattern(std::uint8_t band) const {
    return patterns_[std::min<std::size_t>(band, kBandCount - 1U)];
}

float SpectralGate::currentGain(std::uint8_t band) const {
    return gains_[std::min<std::size_t>(band, kBandCount - 1U)];
}

std::uint32_t SpectralGate::computeStepFrames() const {
    if (sampleRate_ == 0) return 1;
    const double frames = static_cast<double>(sampleRate_) * 60.0 /
                          static_cast<double>(bpm_) / 4.0;
    const long rounded = std::lround(frames);
    return static_cast<std::uint32_t>(std::max<long>(1, rounded));
}

SpectralGate::Biquad SpectralGate::makeLowpass(double frequency,
                                               std::uint32_t sampleRate) {
    const double w0 = 2.0 * kPi * frequency /
                      static_cast<double>(sampleRate == 0 ? 1 : sampleRate);
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double q = 0.5;  // Linkwitz-Riley 2e ordre
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad biquad;
    biquad.b0 = static_cast<float>((1.0 - cw) / 2.0 / a0);
    biquad.b1 = static_cast<float>((1.0 - cw) / a0);
    biquad.b2 = static_cast<float>((1.0 - cw) / 2.0 / a0);
    biquad.a1 = static_cast<float>((-2.0 * cw) / a0);
    biquad.a2 = static_cast<float>((1.0 - alpha) / a0);
    return biquad;
}

std::uint16_t SpectralGate::defaultMask(std::size_t band) {
    // Motif par defaut deterministe : la bande b est ouverte tous les b+1
    // pas (b = 0 : toujours ; b = 1 : un pas sur deux ; ... ; b = 7 : un
    // pas sur huit). Aucun editeur de motif en V0.
    const std::size_t interval = band + 1U;
    std::uint16_t mask = 0;
    for (std::size_t step = 0; step < kStepCount; ++step) {
        if (step % interval == 0) {
            mask = static_cast<std::uint16_t>(
                mask | (std::uint16_t{1} << step));
        }
    }
    return mask;
}

void SpectralGate::process(float* left, float* right, int numFrames) {
    if (left == nullptr || right == nullptr || numFrames <= 0) return;

    for (int frame = 0; frame < numFrames; ++frame) {
        std::array<float, kBandCount> bandL{};
        std::array<float, kBandCount> bandR{};
        if (requestedActive_) {
            const float xL = left[frame];
            const float xR = right[frame];
            float prevL = 0.0f;
            float prevR = 0.0f;
            for (std::size_t split = 0; split < kSplitCount; ++split) {
                const float lpL = lpL_[split].process(xL);
                const float lpR = lpR_[split].process(xR);
                bandL[split] = lpL - prevL;
                bandR[split] = lpR - prevR;
                prevL = lpL;
                prevR = lpR;
            }
            bandL[kBandCount - 1U] = xL - prevL;
            bandR[kBandCount - 1U] = xR - prevR;
        }

        float accL = 0.0f;
        float accR = 0.0f;
        for (std::size_t band = 0; band < kBandCount; ++band) {
            const bool open =
                (patterns_[band] & static_cast<std::uint16_t>(
                                       std::uint16_t{1} << stepIndex_)) != 0;
            const float target = open ? 1.0f : 0.0f;
            gains_[band] += (target - gains_[band]) * alpha_;
            if (requestedActive_) {
                accL += gains_[band] * bandL[band];
                accR += gains_[band] * bandR[band];
            }
        }
        if (requestedActive_) {
            left[frame] = std::clamp(accL, -1.0f, 1.0f);
            right[frame] = std::clamp(accR, -1.0f, 1.0f);
        }
        ++stepPosition_;
        if (stepPosition_ >= stepFrames_) {
            stepPosition_ = 0;
            stepIndex_ = static_cast<std::uint8_t>(
                (stepIndex_ + 1U) % static_cast<unsigned int>(kStepCount));
        }
    }
}
