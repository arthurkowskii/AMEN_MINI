// Tests natifs de la spectral gate 8 bandes (plan 7.2, V0).
// Compilation stricte (-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
// -Werror -fno-exceptions -fno-rtti) + ASan/UBSan.
#include "fx/spectral_gate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.0001f;
}

float rms(const float* samples, std::size_t count) {
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

// Remplit une sinusoide (plus un offset DC optionnel) en poursuivant la phase.
void fillSine(float* left, float* right, int count, float frequency,
              float amplitude, double& phase, double sampleRate,
              float dcOffset = 0.0f) {
    const double step = 2.0 * kPi * static_cast<double>(frequency) / sampleRate;
    for (int i = 0; i < count; ++i) {
        left[i] = dcOffset + amplitude * static_cast<float>(std::sin(phase));
        right[i] = left[i];
        phase += step;
    }
}

// 1. Periode : 16 pas de 1/16 note, enroulement 0..15, repetition a la barre.
void testPeriodAndStepWrap() {
    constexpr std::uint32_t kSampleRate = 44100;
    SpectralGate gate(kSampleRate);
    gate.setBpm(120.0f);
    const long ideal = std::lround(44100.0 * 60.0 / 120.0 / 4.0);
    const long frames = static_cast<long>(gate.stepFrames());
    require(std::labs(frames - ideal) <= 1,
            "1/16 note must round to the BPM step length within one frame");
    require(gate.currentStep() == 0, "phase must start on step 0");

    // Un pas = exactement stepFrames() frames : le pas avance d'un cran par
    // bloc et la grille boucle sur 16 pas (une barre de 4 temps).
    std::vector<float> left(static_cast<std::size_t>(gate.stepFrames()));
    std::vector<float> right(static_cast<std::size_t>(gate.stepFrames()));
    double phase = 0.0;
    for (int bar = 0; bar < 2; ++bar) {
        for (int step = 1; step <= 16; ++step) {
            fillSine(left.data(), right.data(), static_cast<int>(left.size()),
                     60.0f, 0.25f, phase, kSampleRate, 0.1f);
            gate.process(left.data(), right.data(), static_cast<int>(left.size()));
            const int expected = (step % 16);
            require(gate.currentStep() == static_cast<std::uint8_t>(expected),
                    "step index must advance one sixteenth per step");
        }
        require(gate.currentStep() == 0, "pattern must repeat every bar");
    }
    // 16 pas valent une barre entiere : 4 temps = 4 * round(sr*60/bpm) frames,
    // a +/- 1 frame de tolerance par pas.
    const long barFrames = frames * 16;
    const long idealBar = 4 * std::lround(44100.0 * 60.0 / 120.0);
    require(std::labs(barFrames - idealBar) <= 16,
            "16 steps must complete one bar within rounding tolerance");
}

// 2. Gain : bande fermee (masque 0), les autres ouvertes, sinus DANS la bande.
// L'energie de la bande dans la sortie (gain observable) chute de >= 30 dB,
// puis la reouverture la restaure sans verrou residuel.
void testBandClosureDropsBandEnergyAndRestores() {
    constexpr std::uint32_t kSampleRate = 44100;
    SpectralGate gate(kSampleRate);
    for (std::size_t b = 0; b < SpectralGate::kBandCount; ++b) {
        gate.setPattern(static_cast<std::uint8_t>(b), 0xFFFF);
    }
    gate.setActive(true);

    const int settle = static_cast<int>(kSampleRate) / 4;  // 250 ms
    std::vector<float> left(static_cast<std::size_t>(settle));
    std::vector<float> right(static_cast<std::size_t>(settle));
    double phase = 0.0;
    fillSine(left.data(), right.data(), settle, 900.0f, 0.5f, phase, kSampleRate);
    gate.process(left.data(), right.data(), settle);
    const float openRms = rms(left.data(), left.size());

    gate.setPattern(3, 0);
    std::vector<float> closed(static_cast<std::size_t>(settle));
    std::vector<float> closedR(static_cast<std::size_t>(settle));
    fillSine(closed.data(), closedR.data(), settle, 900.0f, 0.5f, phase,
             kSampleRate);
    gate.process(closed.data(), closedR.data(), settle);
    require(gate.currentGain(3) < 0.03f,
            "a closed band gain must settle below -30 dB");
    const float closedRms = rms(closed.data(), closed.size());
    require(closedRms < openRms * 0.708f,
            "closing the band must cut the output level by at least 3 dB");

    gate.setPattern(3, 0xFFFF);
    std::vector<float> reopened(static_cast<std::size_t>(settle));
    std::vector<float> reopenedR(static_cast<std::size_t>(settle));
    fillSine(reopened.data(), reopenedR.data(), settle, 900.0f, 0.5f, phase,
             kSampleRate);
    gate.process(reopened.data(), reopenedR.data(), settle);
    require(gate.currentGain(3) > 0.999f,
            "reopening must restore the band gain with no residual latch");
    const float reopenedRms = rms(reopened.data(), reopened.size());
    require(std::fabs(reopenedRms - openRms) / openRms < 0.01f,
            "reopening must restore the output level");
}

// 3. Clic : bascule 0xFFFF <-> 0 aux frontieres de pas, sinus continu. Le
// lissage une-pole borne le delta par echantillon du gain ET de la sortie.
void testPatternToggleHasNoClick() {
    constexpr std::uint32_t kSampleRate = 44100;
    SpectralGate gate(kSampleRate);
    for (std::size_t b = 0; b < SpectralGate::kBandCount; ++b) {
        gate.setPattern(static_cast<std::uint8_t>(b), 0xFFFF);
    }
    gate.setActive(true);

    const int bar = static_cast<int>(gate.stepFrames()) * 16;
    double phase = 0.0;
    float maxOutputDelta = 0.0f;
    float maxGainDelta = 0.0f;
    float prevOut = 0.0f;
    const auto runBar = [&](std::uint16_t mask) {
        for (std::size_t b = 0; b < SpectralGate::kBandCount; ++b) {
            gate.setPattern(static_cast<std::uint8_t>(b), mask);
        }
        for (int i = 0; i < bar; ++i) {
            const float gainBefore = gate.currentGain(0);
            float l = 0.5f * static_cast<float>(std::sin(phase));
            float r = l;
            phase += 2.0 * kPi * 220.0 / kSampleRate;
            gate.process(&l, &r, 1);
            maxGainDelta = std::max(maxGainDelta,
                                    std::fabs(gate.currentGain(0) - gainBefore));
            maxOutputDelta = std::max(maxOutputDelta, std::fabs(l - prevOut));
            prevOut = l;
        }
    };
    runBar(0xFFFF);
    runBar(0x0000);
    runBar(0xFFFF);

    require(maxGainDelta < 0.05f,
            "pattern toggle must keep per-sample gain delta below the bound");
    require(maxOutputDelta < 0.05f,
            "pattern toggle must not produce a click above the smoothing bound");
}

// 4. Passe-bas : inactif = bit-exact ; actif toutes bandes ouvertes = entree
// reconstructe (somme des bandes plate par construction).
void testPassthroughAndAllOpenIdentity() {
    constexpr std::uint32_t kSampleRate = 44100;
    SpectralGate gate(kSampleRate);

    std::vector<float> left(1024);
    std::vector<float> right(1024);
    for (std::size_t i = 0; i < left.size(); ++i) {
        left[i] = 0.5f - static_cast<float>(i) / 1024.0f;
        right[i] = -left[i];
    }
    const std::vector<float> copyL = left;
    const std::vector<float> copyR = right;
    gate.process(left.data(), right.data(), 1024);
    require(left == copyL && right == copyR,
            "inactive process must leave samples bit-exact");

    for (std::size_t b = 0; b < SpectralGate::kBandCount; ++b) {
        gate.setPattern(static_cast<std::uint8_t>(b), 0xFFFF);
    }
    gate.setActive(true);
    const int count = 4096;
    std::vector<float> sineL(static_cast<std::size_t>(count));
    std::vector<float> sineR(static_cast<std::size_t>(count));
    std::vector<float> expected(static_cast<std::size_t>(count));
    double phase = 0.0;
    fillSine(sineL.data(), sineR.data(), count, 1000.0f, 0.5f, phase,
             kSampleRate);
    expected = sineL;
    gate.process(sineL.data(), sineR.data(), count);
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < sineL.size(); ++i) {
        maxDiff = std::max(maxDiff, std::fabs(sineL[i] - expected[i]));
    }
    require(maxDiff < 0.01f,
            "all-open active gate must reconstruct the input within tolerance");
}

// 5. Changement de BPM en plein flux : longueur recalculee, pas conserve,
// sortie finie et bornee, la nouvelle grille est respectee.
void testBpmChangeMidStream() {
    constexpr std::uint32_t kSampleRate = 44100;
    SpectralGate gate(kSampleRate);
    gate.setBpm(120.0f);
    gate.setActive(true);
    std::vector<float> left(10000);
    std::vector<float> right(10000);
    double phase = 0.0;
    fillSine(left.data(), right.data(), 10000, 440.0f, 0.5f, phase, kSampleRate);
    gate.process(left.data(), right.data(), 10000);
    const std::uint8_t stepBefore = gate.currentStep();

    gate.setBpm(60.0f);
    require(near(gate.bpm(), 60.0f), "BPM change must be applied");
    const long expected = std::lround(44100.0 * 60.0 / 60.0 / 4.0);
    require(std::labs(static_cast<long>(gate.stepFrames()) - expected) <= 1,
            "step length must be recalculated for the new BPM");
    require(gate.currentStep() == stepBefore,
            "BPM change must snap to the current step index");

    std::vector<float> cont(30000);
    std::vector<float> contR(30000);
    fillSine(cont.data(), contR.data(), 30000, 440.0f, 0.5f, phase, kSampleRate);
    gate.process(cont.data(), contR.data(), 30000);
    for (std::size_t i = 0; i < cont.size(); ++i) {
        require(std::isfinite(cont[i]) && std::isfinite(contR[i]),
                "output must stay finite after a live BPM change");
        require(std::fabs(cont[i]) <= 1.0f && std::fabs(contR[i]) <= 1.0f,
                "output must stay bounded after a live BPM change");
    }

    const std::uint8_t stepAtStart = gate.currentStep();
    std::vector<float> oneStep(static_cast<std::size_t>(gate.stepFrames()), 0.0f);
    std::vector<float> oneStepR(static_cast<std::size_t>(gate.stepFrames()), 0.0f);
    gate.process(oneStep.data(), oneStepR.data(), static_cast<int>(oneStep.size()));
    require(gate.currentStep() == static_cast<std::uint8_t>((stepAtStart + 1) % 16),
            "step grid must follow the new BPM after the change");
}

// 6. Defensif : BPM borne 20..300, NaN ignore, indices de bande bornes 0..7.
void testDefensiveClamping() {
    SpectralGate gate(44100);
    gate.setBpm(0.0f);
    require(near(gate.bpm(), 20.0f), "BPM below range must clamp to 20");
    gate.setBpm(10000.0f);
    require(near(gate.bpm(), 300.0f), "BPM above range must clamp to 300");
    gate.setBpm(120.0f);
    gate.setBpm(std::numeric_limits<float>::quiet_NaN());
    require(near(gate.bpm(), 120.0f), "NaN BPM must be ignored");

    gate.setPattern(3, 0xABCD);
    require(gate.pattern(3) == 0xABCD, "valid band pattern must be stored");
    gate.setPattern(static_cast<std::uint8_t>(8), 0x1234);
    require(gate.pattern(7) == 0x1234, "band index must clamp to 7");
    gate.setPattern(static_cast<std::uint8_t>(255), 0x9999);
    require(gate.pattern(7) == 0x9999, "oversized band index must clamp to 7");
    require(near(gate.currentGain(static_cast<std::uint8_t>(9)), gate.currentGain(7)),
            "gain getter must clamp the band index");
}

// 7. Motifs par defaut deterministes : bande b ouverte tous les b+1 pas.
void testDefaultPatternsDeterministic() {
    SpectralGate gate(44100);
    require(gate.pattern(0) == 0xFFFF, "band 0 default must open every step");
    require(gate.pattern(1) == 0x5555, "band 1 default must open every other step");
    require(gate.pattern(3) == 0x1111, "band 3 default must open every fourth step");
    require(gate.pattern(7) == 0x0101, "band 7 default must open every eighth step");
}

// 8. Budget CPU : 1000 frames largement sous 2 s (garde-fou, pas un bench).
void testCpuBudget() {
    constexpr std::uint32_t kSampleRate = 44100;
    SpectralGate gate(kSampleRate);
    gate.setActive(true);
    std::vector<float> left(1000);
    std::vector<float> right(1000);
    double phase = 0.0;
    fillSine(left.data(), right.data(), 1000, 440.0f, 0.5f, phase, kSampleRate);
    const auto start = std::chrono::steady_clock::now();
    gate.process(left.data(), right.data(), 1000);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::cout << "SpectralGate: 1000 frames traitees en " << elapsed * 1000.0
              << " ms\n";
    require(elapsed < 2.0,
            "1000 frames must process well inside the CPU budget");
}
}  // namespace

int main() {
    testPeriodAndStepWrap();
    testBandClosureDropsBandEnergyAndRestores();
    testPatternToggleHasNoClick();
    testPassthroughAndAllOpenIdentity();
    testBpmChangeMidStream();
    testDefensiveClamping();
    testDefaultPatternsDeterministic();
    testCpuBudget();
    std::cout << "All Spectral Gate tests passed\n";
    return 0;
}
