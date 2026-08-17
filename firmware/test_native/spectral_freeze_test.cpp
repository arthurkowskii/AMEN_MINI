// Tests natifs du spectral freeze (plan 7.1, V0).
// Compilation stricte (-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
// -Werror -fno-exceptions -fno-rtti) + ASan/UBSan.
#include "fx/spectral_freeze.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kSampleRate = 44100;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

float rms(const float* samples, std::size_t count) {
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

// Fenetre Hann periodique de reference (doit correspondre a l'implementation).
double hann(std::size_t n, std::size_t size) {
    return 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(n) /
                                 static_cast<double>(size)));
}

// DFT naive (cote test uniquement : O(N^2) acceptable pour 512 points).
void dftMagnitude(const float* x, std::size_t count, float* mags) {
    for (std::size_t k = 0; k < count; ++k) {
        double re = 0.0;
        double im = 0.0;
        for (std::size_t n = 0; n < count; ++n) {
            const double angle = 2.0 * kPi * static_cast<double>(k) *
                                 static_cast<double>(n) /
                                 static_cast<double>(count);
            re += static_cast<double>(x[n]) * std::cos(angle);
            im -= static_cast<double>(x[n]) * std::sin(angle);
        }
        mags[k] = static_cast<float>(std::sqrt(re * re + im * im));
    }
}

std::size_t dominantBin(const float* mags, std::size_t count) {
    std::size_t best = 0;
    for (std::size_t k = 1; k < count; ++k) {
        if (mags[k] > mags[best]) best = k;
    }
    return best;
}

// Sinusoide continue (phase poursuivie entre les appels).
void fillSine(float* left, float* right, int count, float frequency,
              float amplitude, double& phase) {
    const double step =
        2.0 * kPi * static_cast<double>(frequency) / static_cast<double>(kSampleRate);
    for (int i = 0; i < count; ++i) {
        left[i] = amplitude * static_cast<float>(std::sin(phase));
        right[i] = left[i];
        phase += step;
    }
}

// Bruit pseudo-aleatoire deterministe (xorshift32), borne a [-0.5, 0.5].
void fillNoise(float* left, float* right, int count, std::uint32_t& state) {
    for (int i = 0; i < count; ++i) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        left[i] = static_cast<float>(state) / 4294967296.0f - 0.5f;
        right[i] = -left[i];
    }
}

// 0. Identite numerique du fenetrage : w[n] + w[n+hop] == 1 (Hann periodique
// a 50 % de recouvrement). C'est le contrat de la synthese OLA : la boucle
// gelee restitue la trame sans ondulation d'amplitude.
void testWindowOverlapIdentity() {
    std::array<float, SpectralFreeze::kFftSize> w{};
    for (std::size_t n = 0; n < w.size(); ++n) {
        w[n] = static_cast<float>(hann(n, w.size()));
    }
    float maxDev = 0.0f;
    for (std::size_t n = 0; n < SpectralFreeze::kHopSize; ++n) {
        maxDev = std::max(
            maxDev,
            std::fabs(w[n] + w[n + SpectralFreeze::kHopSize] - 1.0f));
    }
    require(maxDev < 1.0e-6f,
            "Hann 50% overlap sum must equal 1 numerically");
}

// 1. Inactif : passthrough bit-exact pour des echantillons arbitraires.
void testInactivePassthroughBitExact() {
    SpectralFreeze fx(kSampleRate);
    std::vector<float> left(4096);
    std::vector<float> right(4096);
    std::uint32_t state = 0xDEADBEEFU;
    fillNoise(left.data(), right.data(), static_cast<int>(left.size()), state);
    const std::vector<float> copyL = left;
    const std::vector<float> copyR = right;
    fx.process(left.data(), right.data(), 512);
    fx.process(left.data() + 512, right.data() + 512, 2048);
    fx.process(left.data() + 2560, right.data() + 2560, 1536);
    require(std::memcmp(left.data(), copyL.data(),
                        left.size() * sizeof(float)) == 0,
            "inactive left channel must be bit-exact");
    require(std::memcmp(right.data(), copyR.data(),
                        right.size() * sizeof(float)) == 0,
            "inactive right channel must be bit-exact");
    require(fx.wetGain() == 0.0f,
            "wet must stay exactly 0 while never activated");
    require(!fx.frozen(), "nothing must be frozen while inactive");
}

// 2. Determinisme : meme entree + meme frame d'activation, deux objets
// distincts -> sorties byte-identiques (phases LCG + capture deterministe).
void testDeterminism() {
    SpectralFreeze a(kSampleRate);
    SpectralFreeze b(kSampleRate);
    std::vector<float> leftA(8192);
    std::vector<float> rightA(8192);
    std::uint32_t state = 42U;
    fillNoise(leftA.data(), rightA.data(), static_cast<int>(leftA.size()),
              state);
    std::vector<float> leftB = leftA;
    std::vector<float> rightB = rightA;

    const auto run = [](SpectralFreeze& fx, float* l, float* r) {
        fx.process(l, r, 700);
        fx.setActive(true);
        fx.process(l + 700, r + 700, 4096);
        fx.setActive(false);
        fx.process(l + 4796, r + 4796, 3396);
    };
    run(a, leftA.data(), rightA.data());
    run(b, leftB.data(), rightB.data());
    require(std::memcmp(leftA.data(), leftB.data(),
                        leftA.size() * sizeof(float)) == 0,
            "same input + same activation must give byte-identical left");
    require(std::memcmp(rightA.data(), rightB.data(),
                        rightA.size() * sizeof(float)) == 0,
            "same input + same activation must give byte-identical right");
}

// 3. Le gel tient : burst sinus, activation, puis silence total. Une fois le
// wet stabilise, le RMS du drone ne decroit pas sur >= 1 s de sortie gelee
// (blocs alignes sur la periode de 512 frames : egalite numerique).
void testFreezeHoldsNoDecay() {
    SpectralFreeze fx(kSampleRate);
    constexpr int kSettle = 5000;
    constexpr int kBlock = 86 * static_cast<int>(SpectralFreeze::kFftSize);
    std::vector<float> burst(2048);
    std::vector<float> burstR(2048);
    double phase = 0.0;
    fillSine(burst.data(), burstR.data(), static_cast<int>(burst.size()),
             600.0f, 0.5f, phase);
    fx.process(burst.data(), burstR.data(), static_cast<int>(burst.size()));
    fx.setActive(true);

    // Silence total apres l'activation : le drone doit tenir seul.
    std::vector<float> buf(static_cast<std::size_t>(kSettle + 2 * kBlock),
                           0.0f);
    std::vector<float> bufr(static_cast<std::size_t>(kSettle + 2 * kBlock),
                            0.0f);
    fx.process(buf.data(), bufr.data(), kSettle);
    fx.process(buf.data() + kSettle, bufr.data() + kSettle, 2 * kBlock);
    const float rms1 = rms(buf.data() + kSettle,
                           static_cast<std::size_t>(kBlock));
    const float rms2 = rms(buf.data() + kSettle + kBlock,
                           static_cast<std::size_t>(kBlock));
    require(rms1 > 0.05f, "frozen drone must carry the captured energy");
    require(std::fabs(rms1 - rms2) / rms1 < 0.01f,
            "frozen RMS must not decay over 1 s of drone");
    require(std::fabs(rms1 - rms2) / rms1 < 1.0e-4f,
            "period-aligned frozen RMS must be constant to rounding");
}

// 4. Contrat spectral : le bin dominant de la sortie gelee est celui de la
// fenetre capturee (tolerance 2 bins).
void testSpectralPreservation() {
    constexpr float kFreq = 344.53125f;  // bin 4 exact a 44,1 kHz / FFT 512
    SpectralFreeze fx(kSampleRate);
    constexpr int kPre = 600;
    std::vector<float> sine(static_cast<std::size_t>(kPre));
    std::vector<float> sineR(static_cast<std::size_t>(kPre));
    double phase = 0.0;
    fillSine(sine.data(), sineR.data(), kPre, kFreq, 0.4f, phase);
    fx.process(sine.data(), sineR.data(), kPre);

    // Reference : les 512 dernieres frames d'entree, fenetrees Hann.
    std::vector<float> capture(SpectralFreeze::kFftSize);
    std::vector<float> captureMags(SpectralFreeze::kFftSize);
    for (std::size_t n = 0; n < capture.size(); ++n) {
        capture[n] =
            sine[static_cast<std::size_t>(kPre) - capture.size() + n] *
            static_cast<float>(hann(n, capture.size()));
    }
    dftMagnitude(capture.data(), capture.size(), captureMags.data());
    const std::size_t binCapture = dominantBin(captureMags.data(),
                                               captureMags.size());
    require(binCapture == 4, "fixture: captured sine must peak at bin 4");

    fx.setActive(true);
    std::vector<float> out(4096, 0.0f);
    std::vector<float> outR(4096, 0.0f);
    fx.process(out.data(), outR.data(), static_cast<int>(out.size()));
    // Toute fenetre de 512 frames de sortie est une rotation circulaire de la
    // trame gelee : meme spectre de magnitude que la capture.
    std::vector<float> period(SpectralFreeze::kFftSize);
    std::vector<float> periodMags(SpectralFreeze::kFftSize);
    for (std::size_t n = 0; n < period.size(); ++n) {
        period[n] = out[1024 + n];
    }
    dftMagnitude(period.data(), period.size(), periodMags.data());
    const std::size_t binFrozen = dominantBin(periodMags.data(),
                                              periodMags.size());
    require(std::labs(static_cast<long>(binFrozen) -
                      static_cast<long>(binCapture)) <= 2,
            "frozen dominant bin must match the captured window (2-bin tol)");
}

// 5. Clic : sinus continu a travers l'activation ET le relachement, delta
// par echantillon borne (rampe une-pole ~10 ms, pre-roll de la boucle).
void testNoClickAcrossTransitions() {
    SpectralFreeze fx(kSampleRate);
    constexpr int kTotal = 40000;
    std::vector<float> left(static_cast<std::size_t>(kTotal));
    std::vector<float> right(static_cast<std::size_t>(kTotal));
    double phase = 0.0;
    fillSine(left.data(), right.data(), kTotal, 220.0f, 0.25f, phase);
    float maxDelta = 0.0f;
    float prevL = 0.0f;
    float prevR = 0.0f;
    constexpr int kChunk = 64;
    for (int start = 0; start < kTotal; start += kChunk) {
        if (start == 3072) fx.setActive(true);
        if (start == 24064) fx.setActive(false);
        fx.process(left.data() + start, right.data() + start, kChunk);
        for (int i = 0; i < kChunk; ++i) {
            maxDelta = std::max(
                maxDelta, std::fabs(left[static_cast<std::size_t>(start + i)] -
                                    prevL));
            maxDelta = std::max(
                maxDelta, std::fabs(right[static_cast<std::size_t>(start + i)] -
                                    prevR));
            prevL = left[static_cast<std::size_t>(start + i)];
            prevR = right[static_cast<std::size_t>(start + i)];
        }
    }
    require(maxDelta < 0.05f,
            "activation and release must stay click-free (delta bound)");
}

// 6. Relachement : wet -> 0 exactement, etat gele remis a zero, passthrough
// bit-exact restaure apres le temps de stabilisation.
void testReleaseRestoresExactPassthrough() {
    SpectralFreeze fx(kSampleRate);
    std::vector<float> pre(1048);
    std::vector<float> preR(1048);
    std::uint32_t state = 7U;
    fillNoise(pre.data(), preR.data(), static_cast<int>(pre.size()), state);
    fx.process(pre.data(), preR.data(), 700);
    fx.setActive(true);
    fx.process(pre.data() + 700, preR.data() + 700, 348);
    fx.setActive(false);

    std::vector<float> tail(6000);
    std::vector<float> tailR(6000);
    std::uint32_t tailState = 99U;
    fillNoise(tail.data(), tailR.data(), static_cast<int>(tail.size()),
              tailState);
    fx.process(tail.data(), tailR.data(), static_cast<int>(tail.size()));
    require(fx.wetGain() == 0.0f,
            "wet must return to exactly 0 after the release settle");
    require(!fx.frozen(), "frozen state must reset after the release settle");

    std::vector<float> check(1024);
    std::vector<float> checkR(1024);
    std::uint32_t checkState = 2024U;
    fillNoise(check.data(), checkR.data(), static_cast<int>(check.size()),
              checkState);
    const std::vector<float> copyL = check;
    const std::vector<float> copyR = checkR;
    fx.process(check.data(), checkR.data(), static_cast<int>(check.size()));
    require(std::memcmp(check.data(), copyL.data(),
                        check.size() * sizeof(float)) == 0,
            "passthrough must be bit-exact again after release");
    require(std::memcmp(checkR.data(), copyR.data(),
                        checkR.size() * sizeof(float)) == 0,
            "passthrough right must be bit-exact again after release");
}

// 7. Defensif : pointeurs nuls / frames <= 0 = no-op ; setActive idempotent.
void testNullAndIdempotence() {
    SpectralFreeze fx(kSampleRate);
    std::vector<float> left(512, 0.25f);
    std::vector<float> right(512, -0.25f);
    const std::vector<float> copyL = left;
    const std::vector<float> copyR = right;
    fx.process(nullptr, right.data(), 512);
    fx.process(left.data(), nullptr, 512);
    fx.process(left.data(), right.data(), 0);
    fx.process(left.data(), right.data(), -5);
    require(std::memcmp(left.data(), copyL.data(),
                        left.size() * sizeof(float)) == 0 &&
                std::memcmp(right.data(), copyR.data(),
                            right.size() * sizeof(float)) == 0,
            "null pointers and non-positive frames must be a no-op");
    require(fx.wetGain() == 0.0f && !fx.frozen(),
            "no-op calls must not change the freeze state");

    // setActive(true) repete pendant le gel == appele une seule fois.
    const auto run = [](bool spam) {
        SpectralFreeze g(kSampleRate);
        std::vector<float> a(4096);
        std::vector<float> b(4096);
        std::uint32_t st = 1234U;
        fillNoise(a.data(), b.data(), static_cast<int>(a.size()), st);
        g.process(a.data(), b.data(), 600);
        g.setActive(true);
        for (int start = 600; start < 4096; start += 8) {
            if (spam) g.setActive(true);
            g.process(a.data() + start, b.data() + start, 8);
        }
        return a;
    };
    const std::vector<float> once = run(false);
    const std::vector<float> spam = run(true);
    require(std::memcmp(once.data(), spam.data(),
                        once.size() * sizeof(float)) == 0,
            "repeated setActive(true) must be idempotent (byte-identical)");
}

// 8. Budget CPU : 1 s de drone gele (dont la capture : 2 x FFT 512 + 2 x
// IFFT 512 une seule fois) largement sous le temps reel.
void testCpuBudget() {
    SpectralFreeze fx(kSampleRate);
    std::vector<float> pre(2048);
    std::vector<float> preR(2048);
    std::uint32_t state = 555U;
    fillNoise(pre.data(), preR.data(), static_cast<int>(pre.size()), state);
    fx.process(pre.data(), preR.data(), static_cast<int>(pre.size()));
    fx.setActive(true);
    std::vector<float> out(44100);
    std::vector<float> outR(44100);
    fillNoise(out.data(), outR.data(), static_cast<int>(out.size()), state);
    const auto start = std::chrono::steady_clock::now();
    fx.process(out.data(), outR.data(), static_cast<int>(out.size()));
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      start)
            .count();
    std::cout << "SpectralFreeze: 44100 frames (1 s, capture FFT 512 x2 "
                 "incluse) en "
              << elapsed * 1000.0 << " ms (" << elapsed * 100.0
              << " % du temps reel)\n";
    require(elapsed < 0.5,
            "1 s of frozen audio must process far below real time");
}
}  // namespace

int main() {
    testWindowOverlapIdentity();
    testInactivePassthroughBitExact();
    testDeterminism();
    testFreezeHoldsNoDecay();
    testSpectralPreservation();
    testNoClickAcrossTransitions();
    testReleaseRestoresExactPassthrough();
    testNullAndIdempotence();
    testCpuBudget();
    std::cout << "All Spectral Freeze tests passed\n";
    return 0;
}
