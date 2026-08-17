#include "fx/spectral_freeze.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kTauSeconds = 0.010f;  // rampe wet ~10 ms
}  // namespace

SpectralFreeze::SpectralFreeze(std::uint32_t sampleRate)
    : sampleRate_(sampleRate) {
    if (sampleRate_ > 0) {
        const float tauSamples =
            kTauSeconds * static_cast<float>(sampleRate_);
        alpha_ = 1.0f - std::exp(-1.0f / tauSamples);
        alpha_ = std::clamp(alpha_, 0.0f, 1.0f);
    } else {
        alpha_ = 1.0f;
    }
    for (std::size_t n = 0; n < kFftSize; ++n) {
        window_[n] = static_cast<float>(
            0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(n) /
                                  static_cast<double>(kFftSize))));
    }
}

void SpectralFreeze::setActive(bool active) {
    // L'activation ne provoque aucun saut : la capture et la rampe wet se
    // font au prochain process(), et le dry reste melange au wet.
    requestedActive_ = active;
}

void SpectralFreeze::updateRing(const float* left, const float* right,
                                int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
        ringL_[ringPos_] = left[i];
        ringR_[ringPos_] = right[i];
        ++ringPos_;
        if (ringPos_ >= kFftSize) {
            ringPos_ = 0;
            ringFull_ = true;
        }
    }
}

std::uint32_t SpectralFreeze::nextLcg(std::uint32_t& state) {
    // Xorshift32 seme constant : phases deterministes, identiques a chaque
    // capture, quel que soit l'objet ou l'historique. Le contenu capture
    // reste, lui, propre a chaque gel.
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

void SpectralFreeze::fft(std::array<float, kFftSize>& real,
                         std::array<float, kFftSize>& imag, bool inverse) {
    // Radix-2 iteratif in-place (Cooley-Tukey). Twiddles calcules par etage :
    // la FFT ne tourne qu'a la capture (2 avant + 2 inverse par gel), donc le
    // cout des sin/cos est negligeable et aucune table n'est precalculee.
    const int n = static_cast<int>(kFftSize);
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[static_cast<std::size_t>(i)],
                      real[static_cast<std::size_t>(j)]);
            std::swap(imag[static_cast<std::size_t>(i)],
                      imag[static_cast<std::size_t>(j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = 2.0 * kPi / static_cast<double>(len) *
                             (inverse ? 1.0 : -1.0);
        const float wRe = static_cast<float>(std::cos(angle));
        const float wIm = static_cast<float>(std::sin(angle));
        for (int i = 0; i < n; i += len) {
            float curRe = 1.0f;
            float curIm = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                const std::size_t a = static_cast<std::size_t>(i + k);
                const std::size_t b = static_cast<std::size_t>(i + k + len / 2);
                const float uRe = real[a];
                const float uIm = imag[a];
                const float vRe = real[b] * curRe - imag[b] * curIm;
                const float vIm = real[b] * curIm + imag[b] * curRe;
                real[a] = uRe + vRe;
                imag[a] = uIm + vIm;
                real[b] = uRe - vRe;
                imag[b] = uIm - vIm;
                const float nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }
    if (inverse) {
        const float scale = 1.0f / static_cast<float>(n);
        for (float& value : real) value *= scale;
        for (float& value : imag) value *= scale;
    }
}

void SpectralFreeze::captureFrozenFrame() {
    // Reordonne l'anneau en une fenetre continue : n = 0 est la frame la plus
    // ancienne des 512 retenues, n = 511 la plus recente.
    std::array<float, kFftSize> reL{};
    std::array<float, kFftSize> imL{};
    std::array<float, kFftSize> reR{};
    std::array<float, kFftSize> imR{};
    const std::size_t start = ringFull_ ? ringPos_ : 0;
    for (std::size_t n = 0; n < kFftSize; ++n) {
        const std::size_t idx = ringFull_
                                    ? (start + n) % kFftSize
                                    : std::min(n, ringPos_);
        const float frameL = ringFull_ ? ringL_[idx] : (n < ringPos_ ? ringL_[idx] : 0.0f);
        const float frameR = ringFull_ ? ringR_[idx] : (n < ringPos_ ? ringR_[idx] : 0.0f);
        reL[n] = frameL * window_[n];
        reR[n] = frameR * window_[n];
    }
    fft(reL, imL, false);
    fft(reR, imR, false);

    // Spectre d'amplitude conserve, phases LCG deterministes, symetrie
    // hermitienne respectee (le signal re-synthetise est reel).
    std::uint32_t lcg = 0x2545F491U;  // graine fixe : determinisme des tests
    const std::size_t half = kFftSize / 2;
    std::array<float, kFftSize> magL{};
    std::array<float, kFftSize> magR{};
    std::array<float, kFftSize> phase{};
    for (std::size_t k = 0; k <= half; ++k) {
        magL[k] = std::sqrt(reL[k] * reL[k] + imL[k] * imL[k]);
        magR[k] = std::sqrt(reR[k] * reR[k] + imR[k] * imR[k]);
        const bool edge = (k == 0 || k == half);
        phase[k] = edge ? 0.0f
                        : static_cast<float>(nextLcg(lcg)) / 4294967296.0f *
                              static_cast<float>(2.0 * kPi);
    }
    for (std::size_t k = 0; k <= half; ++k) {
        const float ph = phase[k];
        const float c = std::cos(ph);
        const float s = std::sin(ph);
        const std::size_t mirror = (kFftSize - k) % kFftSize;
        if (k == 0 || k == half) {
            reL[k] = magL[k];
            imL[k] = 0.0f;
            reR[k] = magR[k];
            imR[k] = 0.0f;
        } else {
            reL[k] = magL[k] * c;
            imL[k] = magL[k] * s;
            reR[k] = magR[k] * c;
            imR[k] = magR[k] * s;
            reL[mirror] = reL[k];
            imL[mirror] = -imL[k];
            reR[mirror] = reR[k];
            imR[mirror] = -imR[k];
        }
    }
    fft(reL, imL, true);
    fft(reR, imR, true);
    frozenL_ = reL;
    frozenR_ = reR;
    frozenPos_ = 0;
    frozen_ = true;
}

void SpectralFreeze::process(float* left, float* right, int numFrames) {
    if (left == nullptr || right == nullptr || numFrames <= 0) return;

    // Front montant : capture de la fenetre la plus recente, une seule fois.
    // AVANT updateRing : l'anneau contient encore les 512 dernieres frames
    // reellement ecoutees (l'activation fige le passe immediat, pas le bloc
    // courant qui n'a pas encore ete rendu).
    if (requestedActive_ && !activeAtLastProcess_) {
        captureFrozenFrame();
    }

    updateRing(left, right, numFrames);
    activeAtLastProcess_ = requestedActive_;

    if (!frozen_ && wet_ == 0.0f) {
        // Jamais gele : passthrough bit-exact, les buffers ne sont pas
        // touches (l'anneau a deja copie son contenu).
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        const float target = requestedActive_ ? 1.0f : 0.0f;
        wet_ += (target - wet_) * alpha_;
        const float frozenL = frozenL_[frozenPos_];
        const float frozenR = frozenR_[frozenPos_];
        ++frozenPos_;
        if (frozenPos_ >= kFftSize) frozenPos_ = 0;

        if (!requestedActive_ && wet_ < kWetSnap) {
            // Relachement termine : gel remis a zero. Les frames restantes de
            // ce bloc sont deja du pur dry (buffers non modifies) : retour au
            // passthrough exact des le prochain appel.
            wet_ = 0.0f;
            frozen_ = false;
            return;
        }
        const float dryL = left[i];
        const float dryR = right[i];
        left[i] = std::clamp(dryL * (1.0f - wet_) + frozenL * wet_, -1.0f, 1.0f);
        right[i] = std::clamp(dryR * (1.0f - wet_) + frozenR * wet_, -1.0f, 1.0f);
    }
}
