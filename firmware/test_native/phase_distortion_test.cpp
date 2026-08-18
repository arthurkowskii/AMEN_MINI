// Tests natifs du FX PHASE DIST (allpass a coefficient variable).
// Compilation stricte + ASan/UBSan, cf live_repeat_test.cpp.
#include "fx/phase_distortion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kTolerance = 0.0001f;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < kTolerance;
}

// 1. Inactif : passthrough bit-exact, quelle que soit la configuration.
void testInactiveIsBitExactPassthrough() {
    PhaseDistortion pd{48000};
    pd.setAmount(1.0f);
    pd.setMode(PhaseDistMode::Self);
    pd.setKeepBass(1.0f);

    std::array<float, 64> left{};
    std::array<float, 64> right{};
    for (int i = 0; i < 64; ++i) {
        // Valeurs sans zero exact : le passage -0.0f + 0.0f = +0.0f (IEEE)
        // changerait les bits sans changer le signal, et le memcmp doit
        // rester bit-exact pour les vraies valeurs.
        left[i] = static_cast<float>(i) / 200.0f - 0.3f;
        right[i] = 0.5f - left[i];
    }
    const auto copyL = left;
    const auto copyR = right;
    pd.process(left.data(), right.data(), 64);
    require(std::memcmp(left.data(), copyL.data(), 64 * sizeof(float)) == 0,
            "inactive phase dist must pass through exactly (L)");
    require(std::memcmp(right.data(), copyR.data(), 64 * sizeof(float)) == 0,
            "inactive phase dist must pass through exactly (R)");
}

// 2. Silence : aucune sortie, aucun NaN, dans tous les modes.
void testSilenceStaysSilent() {
    for (PhaseDistMode mode :
         {PhaseDistMode::Sine, PhaseDistMode::Saw, PhaseDistMode::Square,
          PhaseDistMode::Self}) {
        PhaseDistortion pd{48000};
        pd.setSlewFrames(1U);
        pd.setAmount(1.0f);
        pd.setMode(mode);
        pd.setActive(true);
        std::array<float, 512> left{};
        std::array<float, 512> right{};
        pd.process(left.data(), right.data(), 512);
        require(std::all_of(left.begin(), left.end(),
                            [](float v) { return v == 0.0f; }),
                "silence must produce exact silence in every mode");
    }
}

// 3. Periodicite : entree periodique (alternance) + LFO periodique =>
// la sortie est periodique apres le transitoire (contraction |c| < 1).
void testSteadyStateIsPeriodic() {
    constexpr std::uint32_t kSr = 100;
    PhaseDistortion pd{kSr};
    pd.setSlewFrames(1U);
    pd.setAmount(1.0f);
    pd.setRateHz(1.0f);  // periode LFO = 100 frames
    pd.setActive(true);

    std::array<float, 10000> left{};
    std::array<float, 10000> right{};
    for (std::size_t i = 0; i < left.size(); ++i) {
        left[i] = (i % 2U == 0) ? 0.5f : -0.5f;  // periode 2
    }
    pd.process(left.data(), right.data(), static_cast<int>(left.size()));
    // Periode combinee LFO (100) et entree (2) : 200 frames.
    for (std::size_t i = 5000; i + 200 < left.size(); ++i) {
        require(std::fabs(left[i] - left[i + 200]) < 0.001f,
                "steady state must be periodic with the driving period");
    }
}

// 4. Stabilite : entree brutale (pleine echelle alternante), tous les
// modes : sortie finie et bornee (clamp d'etat de l'allpass).
void testStabilityUnderBrutalInput() {
    for (PhaseDistMode mode :
         {PhaseDistMode::Sine, PhaseDistMode::Saw, PhaseDistMode::Square,
          PhaseDistMode::Self}) {
        PhaseDistortion pd{100};
        pd.setSlewFrames(1U);
        pd.setAmount(1.0f);
        pd.setMode(mode);
        pd.setActive(true);
        std::array<float, 8000> left{};
        std::array<float, 8000> right{};
        for (std::size_t i = 0; i < left.size(); ++i) {
            left[i] = (i % 2U == 0) ? 1.0f : -1.0f;
            right[i] = left[i];
        }
        pd.process(left.data(), right.data(), static_cast<int>(left.size()));
        require(std::all_of(left.begin(), left.end(), [](float v) {
                    return std::isfinite(v) && v >= -1.0f && v <= 1.0f;
                }),
                "brutal input must stay finite and bounded");
    }
}

// 5. Slew : la rampe d'activation glisse. Entree DC 0.5 : l'allpass la
// preserve exactement, donc seule la rampe de mix s'observe.
void testActivationSlewShape() {
    PhaseDistortion pd{100};
    pd.setAmount(1.0f);
    pd.setMode(PhaseDistMode::Sine);
    pd.setRateHz(1.0f);
    pd.setActive(true);

    std::array<float, 128> left{};
    std::array<float, 128> right{};
    for (int i = 0; i < 128; ++i) {
        left[i] = 0.5f;
        right[i] = 0.5f;
    }
    pd.process(left.data(), right.data(), 128);
    // Frame 0 : wet = 0 (memoire vide), mix = 1/128.
    require(near(left[0], 0.5f - 0.5f / 128.0f),
            "activation must begin with a short slew ramp");
    // Frame 127 : mix installe, DC preserve exactement par l'allpass.
    require(near(left[127], 0.5f),
            "settled DC input must pass through the allpass exactly");
}

// 6. keepBass : a 60 Hz (sous le LP ~120 Hz), re-injecter le dry filtre
// augmente l'energie de la sortie (wet + copie basse correlee au dry).
void testKeepBassReinjectsLowEnd() {
    constexpr std::uint32_t kSr = 1000;
    const auto run = [](float keepBass) {
        PhaseDistortion pd{kSr};
        pd.setSlewFrames(1U);
        pd.setAmount(1.0f);
        pd.setMode(PhaseDistMode::Sine);
        pd.setRateHz(1.0f);
        pd.setKeepBass(keepBass);
        pd.setActive(true);
        std::array<float, 4000> left{};
        std::array<float, 4000> right{};
        for (std::size_t i = 0; i < left.size(); ++i) {
            left[i] = static_cast<float>(0.8 * std::sin(
                            2.0 * kPi * 60.0 * static_cast<double>(i) /
                            static_cast<double>(kSr)));
        }
        pd.process(left.data(), right.data(), static_cast<int>(left.size()));
        double energy = 0.0;
        for (std::size_t i = 2000; i < left.size(); ++i) {
            energy += static_cast<double>(left[i]) * left[i];
        }
        return energy;
    };
    require(run(1.0f) > run(0.0f),
            "keepBass must reinforce the low end of the processed signal");
}

// 7. Defensif : clamps des parametres, round-trip de mode.
void testDefensive() {
    PhaseDistortion pd{48000};
    pd.setAmount(5.0f);
    require(near(pd.amount(), 1.0f), "amount must clamp to 1");
    pd.setAmount(-1.0f);
    require(near(pd.amount(), 0.0f), "amount must clamp to 0");
    pd.setRateHz(1000.0f);
    require(near(pd.rateHz(), PhaseDistortion::kMaxRateHz),
            "rate must clamp to kMaxRateHz");
    pd.setRateHz(-1.0f);
    require(near(pd.rateHz(), PhaseDistortion::kMinRateHz),
            "rate must clamp to kMinRateHz");
    pd.setKeepBass(5.0f);
    require(near(pd.keepBass(), 1.0f), "keepBass must clamp to 1");
    pd.setKeepBass(-1.0f);
    require(near(pd.keepBass(), 0.0f), "keepBass must clamp to 0");
    pd.setSlewFrames(0U);
    require(pd.slewFrames() >= 1U, "slew frames must clamp to at least 1");

    pd.setMode(PhaseDistMode::Self);
    require(pd.mode() == PhaseDistMode::Self, "setMode must select SELF");
    pd.setMode(PhaseDistMode::Saw);
    require(pd.mode() == PhaseDistMode::Saw, "setMode must select SAW");

    // Buffers nuls : aucun crash, aucune sortie.
    pd.setActive(true);
    pd.process(nullptr, nullptr, 64);
}
}  // namespace

int main() {
    testInactiveIsBitExactPassthrough();
    testSilenceStaysSilent();
    testSteadyStateIsPeriodic();
    testStabilityUnderBrutalInput();
    testActivationSlewShape();
    testKeepBassReinjectsLowEnd();
    testDefensive();
    std::cout << "All Phase Distortion tests passed\n";
    return 0;
}
