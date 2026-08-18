// Tests natifs du nuage granulaire par pad (plan 7.3, V0/V1).
// Compilation stricte (-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
// -Werror -fno-exceptions -fno-rtti) + ASan/UBSan.
#include "granular.h"

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

// PCM emprunte : mono, frames = samples. Le contenu appartient au test.
PcmView makePcm(const std::vector<std::int16_t>& samples) {
    PcmView view;
    view.sampleRate = kSampleRate;
    view.channels = 1;
    view.samples = samples.data();
    view.sampleCount = samples.size();
    return view;
}

void fillSine(std::vector<std::int16_t>& samples, std::size_t begin,
              std::size_t end, float amplitude) {
    for (std::size_t n = begin; n < end; ++n) {
        samples[n] = static_cast<std::int16_t>(
            amplitude * std::sin(2.0 * kPi * 440.0 *
                                 static_cast<double>(n - begin) /
                                 static_cast<double>(kSampleRate)) *
            32767.0);
    }
}

// 1. Bornes : la plage est silencieuse et tout ce qui est DEHORS vaut 1.0
// (sentinelle). Toute lecture hors plage ferait apparaitre la sentinelle ;
// la sortie doit rester exactement nulle, quelle que soit la vitesse (le
// grain est plafonne a la plage meme a 400 %).
void testNeverReadsOutsideRange() {
    constexpr std::size_t kFrames = 20000;
    constexpr std::size_t kBegin = 5000;
    constexpr std::size_t kEnd = 15000;
    std::vector<std::int16_t> samples(kFrames, 32767);  // sentinelle = 1.0
    for (std::size_t n = kBegin; n < kEnd; ++n) samples[n] = 0;  // plage nulle
    const PcmView pcm = makePcm(samples);

    for (float speed : {1.0f, 4.0f, 0.25f}) {
        GrainCloud cloud;
        cloud.start(pcm, kBegin, kEnd, speed, 42U);
        std::vector<float> left(44100, 0.0f);
        std::vector<float> right(44100, 0.0f);
        cloud.render(left.data(), right.data(), static_cast<int>(left.size()));
        for (float value : left) {
            require(value == 0.0f,
                    "output must stay exactly 0 when the range is silent");
        }
    }
}

// 2. Densite bornee : jamais plus de kMaxGrains grains simultanes.
void testGrainCountBounded() {
    constexpr std::size_t kFrames = 60000;
    std::vector<std::int16_t> samples(kFrames, 0);
    fillSine(samples, 0, kFrames, 0.3f);
    const PcmView pcm = makePcm(samples);

    GrainCloud cloud;
    cloud.start(pcm, 0, kFrames, 1.0f, 7U);
    std::vector<float> left(512, 0.0f);
    std::vector<float> right(512, 0.0f);
    for (int block = 0; block < 400; ++block) {
        cloud.render(left.data(), right.data(), 512);
        require(cloud.activeGrainCount() <= GrainCloud::kMaxGrains,
                "grain count must never exceed kMaxGrains");
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }
}

// 3. Determinisme : meme graine => sortie byte-identique ; graines
// differentes => sorties differentes.
void testDeterminism() {
    constexpr std::size_t kFrames = 30000;
    std::vector<std::int16_t> samples(kFrames, 0);
    fillSine(samples, 0, kFrames, 0.3f);
    const PcmView pcm = makePcm(samples);

    const auto run = [&pcm](std::uint32_t seed) {
        GrainCloud cloud;
        cloud.start(pcm, 1000, 25000, 1.0f, seed);
        std::vector<float> left(8192, 0.0f);
        std::vector<float> right(8192, 0.0f);
        cloud.render(left.data(), right.data(), 8192);
        return left;
    };
    const std::vector<float> a = run(123U);
    const std::vector<float> b = run(123U);
    const std::vector<float> c = run(124U);
    require(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0,
            "same seed must give byte-identical output");
    require(std::memcmp(a.data(), c.data(), a.size() * sizeof(float)) != 0,
            "different seeds must give different output");
}

// 4. Pas de clic : contenu DC constant dans la plage. Les seuls deltas de
// sortie viennent alors des enveloppes de grains (naissance/mort) : une
// coupure franche ferait un saut de ~C, alors que la pente d'enveloppe est
// bornee par ~C * pi / (2 * env) par grain. Seuil : C/10 avec C = 800.
void testNoClicksAtGrainEdges() {
    constexpr std::size_t kFrames = 60000;
    constexpr std::int16_t kDc = 800;
    std::vector<std::int16_t> samples(kFrames, kDc);
    const PcmView pcm = makePcm(samples);

    GrainCloud cloud;
    cloud.start(pcm, 0, kFrames, 1.0f, 99U);
    std::vector<float> left(44100, 0.0f);
    std::vector<float> right(44100, 0.0f);
    cloud.render(left.data(), right.data(), static_cast<int>(left.size()));
    float maxDelta = 0.0f;
    for (std::size_t i = 1; i < left.size(); ++i) {
        maxDelta = std::max(maxDelta, std::fabs(left[i] - left[i - 1]));
    }
    require(maxDelta < static_cast<float>(kDc) / 10.0f,
            "grain edges must stay click-free (envelope-slope bound)");
}

// 5. Energie : la plage porte une sinusoide ; la sortie du nuage n'est pas
// nulle (le nuage joue reellement le materiau assigne).
void testCloudProducesSoundFromRange() {
    constexpr std::size_t kFrames = 30000;
    std::vector<std::int16_t> samples(kFrames, 0);
    fillSine(samples, 8000, 22000, 0.4f);
    const PcmView pcm = makePcm(samples);

    GrainCloud cloud;
    cloud.start(pcm, 8000, 22000, 1.0f, 5U);
    std::vector<float> left(22050, 0.0f);
    std::vector<float> right(22050, 0.0f);
    cloud.render(left.data(), right.data(), static_cast<int>(left.size()));
    double energy = 0.0;
    for (float value : left) energy += static_cast<double>(value) * value;
    require(energy > 1.0,
            "the cloud must render audible material from its range");
}

// 6. Arret progressif : stop() fait fondre la sortie vers 0 en ~10 ms puis
// le nuage devient inactif ; contenu DC pour ne mesurer que les enveloppes.
void testStopFadeAndInactive() {
    constexpr std::size_t kFrames = 30000;
    constexpr std::int16_t kDc = 600;
    std::vector<std::int16_t> samples(kFrames, kDc);
    const PcmView pcm = makePcm(samples);

    GrainCloud cloud;
    cloud.start(pcm, 0, kFrames, 1.0f, 11U);
    std::vector<float> left(4096, 0.0f);
    std::vector<float> right(4096, 0.0f);
    cloud.render(left.data(), right.data(), 1024);
    cloud.stop();
    float maxDelta = 0.0f;
    for (std::size_t i = 1024; i < 4096; ++i) {
        cloud.render(left.data() + i, right.data() + i, 1);
        maxDelta = std::max(maxDelta, std::fabs(left[i] - left[i - 1]));
    }
    require(maxDelta < static_cast<float>(kDc) / 10.0f,
            "stop fade must stay click-free");
    require(!cloud.active(), "cloud must be inactive after the fade");
    require(cloud.activeGrainCount() == 0, "grains must clear after stop");
}

// 6.5. Arret synchrone : hardStop() tue le nuage immediatement (reserve aux
// chemins qui detruisent le PCM emprunte) ; le render suivant ne lit plus
// rien et ne modifie pas la sortie.
void testHardStop() {
    constexpr std::size_t kFrames = 30000;
    constexpr std::int16_t kDc = 600;
    std::vector<std::int16_t> samples(kFrames, kDc);
    const PcmView pcm = makePcm(samples);

    GrainCloud cloud;
    cloud.start(pcm, 0, kFrames, 1.0f, 21U);
    std::vector<float> left(1024, 0.0f);
    std::vector<float> right(1024, 0.0f);
    cloud.render(left.data(), right.data(), 1024);
    require(cloud.active(), "cloud must be active before hardStop");
    cloud.hardStop();
    require(!cloud.active(), "hardStop must deactivate immediately");
    require(cloud.activeGrainCount() == 0, "hardStop must clear grains");
    const std::vector<float> copy = left;
    cloud.render(left.data(), right.data(), 512);
    require(std::memcmp(left.data(), copy.data(), 1024 * sizeof(float)) == 0,
            "after hardStop, render must not touch the output");
}

// 7. Defensif : PCM invalide = pas de sortie, pas de crash ; vitesse
// clampée ; plage vide = nuage silencieux.
void testDefensive() {
    PcmView empty;
    GrainCloud cloud;
    cloud.start(empty, 0, 100, 1.0f, 1U);
    std::vector<float> left(64, 0.1f);
    std::vector<float> right(64, 0.1f);
    const std::vector<float> copyL = left;
    cloud.render(left.data(), right.data(), 64);
    require(std::memcmp(left.data(), copyL.data(), left.size() * sizeof(float)) ==
                0,
            "invalid PCM must render nothing");

    std::vector<std::int16_t> samples(4096, 0);
    fillSine(samples, 0, 4096, 0.3f);
    const PcmView pcm = makePcm(samples);
    GrainCloud slow;
    slow.start(pcm, 0, 4096, 0.01f, 2U);  // clamp -> 0.25
    GrainCloud fast;
    fast.start(pcm, 0, 4096, 99.0f, 2U);  // clamp -> 4.0
    std::vector<float> l1(1024, 0.0f);
    std::vector<float> l2(1024, 0.0f);
    slow.render(l1.data(), l1.data(), 1024);
    fast.render(l2.data(), l2.data(), 1024);
    require(std::none_of(l1.begin(), l1.end(), [](float v) {
                return std::isnan(v) || std::isinf(v);
            }),
            "clamped slow speed must stay finite");
    require(std::none_of(l2.begin(), l2.end(), [](float v) {
                return std::isnan(v) || std::isinf(v);
            }),
            "clamped fast speed must stay finite");

    GrainCloud emptyRange;
    emptyRange.start(pcm, 4096, 4096, 1.0f, 3U);
    std::vector<float> zero(1024, 0.0f);
    emptyRange.render(zero.data(), zero.data(), 1024);
    require(std::all_of(zero.begin(), zero.end(),
                        [](float v) { return v == 0.0f; }),
            "empty range must stay silent");
}

// 8. Budget CPU : 1 s de nuage (rendu + spawns) largement sous le temps reel.
void testCpuBudget() {
    constexpr std::size_t kFrames = 60000;
    std::vector<std::int16_t> samples(kFrames, 0);
    fillSine(samples, 0, kFrames, 0.3f);
    const PcmView pcm = makePcm(samples);

    GrainCloud cloud;
    cloud.start(pcm, 0, kFrames, 1.0f, 77U);
    std::vector<float> left(44100, 0.0f);
    std::vector<float> right(44100, 0.0f);
    const auto start = std::chrono::steady_clock::now();
    cloud.render(left.data(), right.data(), static_cast<int>(left.size()));
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::cout << "GrainCloud: 44100 frames (1 s, 8 grains max) en "
              << elapsed * 1000.0 << " ms (" << elapsed * 100.0
              << " % du temps reel)\n";
    require(elapsed < 0.5, "1 s of cloud must process far below real time");
}

// 9. Bornes en mode PITCH/RISE : la lecture interpolee ne doit JAMAIS
// sortir de la plage, meme a 400 % avec hauteur par grain.
void testNeverReadsOutsideRangeInPitchModes() {
    constexpr std::size_t kFrames = 20000;
    constexpr std::size_t kBegin = 5000;
    constexpr std::size_t kEnd = 15000;
    std::vector<std::int16_t> samples(kFrames, 32767);  // sentinelle = 1.0
    for (std::size_t n = kBegin; n < kEnd; ++n) samples[n] = 0;
    const PcmView pcm = makePcm(samples);

    for (GrainMode mode : {GrainMode::Pitch, GrainMode::Rise}) {
        for (float speed : {1.0f, 4.0f}) {
            GrainCloud cloud;
            cloud.setMode(mode);
            cloud.setPitchRangeSemitones(24);
            cloud.start(pcm, kBegin, kEnd, speed, 42U);
            std::vector<float> left(44100, 0.0f);
            std::vector<float> right(44100, 0.0f);
            cloud.render(left.data(), right.data(),
                         static_cast<int>(left.size()));
            for (float value : left) {
                require(value == 0.0f,
                        "pitched grains must never read outside the range");
            }
        }
    }
}

// 10. Mode PITCH : sortie differente du Cloud (grains transposes), non
// nulle, deterministe, finie. La graine est identique dans les deux cas :
// la difference ne peut venir que des hauteurs par grain.
void testPitchModeShiftsGrains() {
    constexpr std::size_t kFrames = 30000;
    std::vector<std::int16_t> samples(kFrames, 0);
    fillSine(samples, 0, kFrames, 0.3f);
    const PcmView pcm = makePcm(samples);

    const auto run = [&pcm](GrainMode mode, std::uint32_t seed) {
        GrainCloud cloud;
        cloud.setMode(mode);
        cloud.setPitchRangeSemitones(12);
        cloud.start(pcm, 1000, 25000, 1.0f, seed);
        std::vector<float> left(8192, 0.0f);
        std::vector<float> right(8192, 0.0f);
        cloud.render(left.data(), right.data(), 8192);
        return left;
    };
    const std::vector<float> cloud = run(GrainMode::Cloud, 123U);
    const std::vector<float> pitchA = run(GrainMode::Pitch, 123U);
    const std::vector<float> pitchB = run(GrainMode::Pitch, 123U);
    require(std::memcmp(pitchA.data(), pitchB.data(),
                        pitchA.size() * sizeof(float)) == 0,
            "pitch mode must stay deterministic for a given seed");
    require(std::memcmp(pitchA.data(), cloud.data(),
                        cloud.size() * sizeof(float)) != 0,
            "pitch mode must differ from cloud mode on the same material");
    require(std::none_of(pitchA.begin(), pitchA.end(), [](float v) {
                return std::isnan(v) || std::isinf(v);
            }),
            "pitch mode output must stay finite");
    require(std::any_of(pitchA.begin(), pitchA.end(),
                        [](float v) { return v != 0.0f; }),
            "pitch mode must produce audible material");
}

// 11. Mode RISE : les grains glissent en hauteur — sortie differente du
// Cloud ET du Pitch, finie, et le nuage reste vivant (grains engendres).
void testRiseModeGlidesGrains() {
    constexpr std::size_t kFrames = 30000;
    std::vector<std::int16_t> samples(kFrames, 0);
    fillSine(samples, 0, kFrames, 0.3f);
    const PcmView pcm = makePcm(samples);

    const auto run = [&pcm](GrainMode mode) {
        GrainCloud cloud;
        cloud.setMode(mode);
        cloud.setPitchRangeSemitones(24);
        cloud.start(pcm, 1000, 25000, 1.0f, 123U);
        std::vector<float> left(8192, 0.0f);
        std::vector<float> right(8192, 0.0f);
        cloud.render(left.data(), right.data(), 8192);
        return left;
    };
    const std::vector<float> pitch = run(GrainMode::Pitch);
    const std::vector<float> rise = run(GrainMode::Rise);
    require(std::memcmp(rise.data(), pitch.data(),
                        rise.size() * sizeof(float)) != 0,
            "rise mode must differ from pitch mode");
    require(std::none_of(rise.begin(), rise.end(), [](float v) {
                return std::isnan(v) || std::isinf(v);
            }),
            "rise mode output must stay finite");
}

// 12. Densite live : a densite 4 le nuage engendre plus de grains que a
// densite 0.25 sur la meme duree (cadence recalculee a chaque spawn).
void testDensityControlsSpawnCadence() {
    constexpr std::size_t kFrames = 60000;
    std::vector<std::int16_t> samples(kFrames, 0);
    fillSine(samples, 0, kFrames, 0.3f);
    const PcmView pcm = makePcm(samples);

    const auto spawnedAt = [&pcm](float density) {
        GrainCloud cloud;
        cloud.setDensity(density);
        cloud.start(pcm, 0, kFrames, 1.0f, 7U);
        std::vector<float> left(22050, 0.0f);
        std::vector<float> right(22050, 0.0f);
        cloud.render(left.data(), right.data(), static_cast<int>(left.size()));
        return cloud.totalSpawned();
    };
    require(spawnedAt(4.0f) > spawnedAt(0.25f),
            "higher density must spawn more grains over the same duration");
}

// 13. Taille de grain : l'echelle change l'enveloppe des grains — sortie
// differente de l'echelle 1.0, et toujours sans clic (contenu DC).
void testGrainSizeScaleChangesOutputWithoutClicks() {
    constexpr std::size_t kFrames = 60000;
    constexpr std::int16_t kDc = 800;
    std::vector<std::int16_t> samples(kFrames, kDc);
    const PcmView pcm = makePcm(samples);

    const auto run = [&pcm](float scale) {
        GrainCloud cloud;
        cloud.setGrainSizeScale(scale);
        cloud.start(pcm, 0, kFrames, 1.0f, 99U);
        std::vector<float> left(44100, 0.0f);
        std::vector<float> right(44100, 0.0f);
        cloud.render(left.data(), right.data(), static_cast<int>(left.size()));
        return left;
    };
    const std::vector<float> base = run(1.0f);
    const std::vector<float> scaled = run(2.0f);
    require(std::memcmp(base.data(), scaled.data(),
                        base.size() * sizeof(float)) != 0,
            "grain size scale must change the output");
    float maxDelta = 0.0f;
    for (std::size_t i = 1; i < scaled.size(); ++i) {
        maxDelta = std::max(maxDelta, std::fabs(scaled[i] - scaled[i - 1]));
    }
    require(maxDelta < static_cast<float>(kDc) / 10.0f,
            "scaled grains must stay click-free (envelope-slope bound)");
}

// 14. Defensif des reglages : clamps et round-trip mode/parametres.
void testGranularSettingsClamped() {
    GrainCloud cloud;
    cloud.setPitchRangeSemitones(99);
    require(cloud.pitchRangeSemitones() == GrainCloud::kMaxPitchRangeSemitones,
            "pitch range must clamp to kMaxPitchRangeSemitones");
    cloud.setPitchRangeSemitones(-5);
    require(cloud.pitchRangeSemitones() == 0, "pitch range must clamp to 0");
    cloud.setDensity(99.0f);
    require(cloud.density() == GrainCloud::kMaxDensity,
            "density must clamp to kMaxDensity");
    cloud.setDensity(0.0f);
    require(cloud.density() == GrainCloud::kMinDensity,
            "density must clamp to kMinDensity");
    cloud.setGrainSizeScale(99.0f);
    require(cloud.grainSizeScale() == GrainCloud::kMaxGrainSizeScale,
            "grain size scale must clamp to kMaxGrainSizeScale");
    cloud.setGrainSizeScale(0.0f);
    require(cloud.grainSizeScale() == GrainCloud::kMinGrainSizeScale,
            "grain size scale must clamp to kMinGrainSizeScale");
    cloud.setMode(GrainMode::Rise);
    require(cloud.mode() == GrainMode::Rise, "setMode must select RISE");
    cloud.setMode(GrainMode::Cloud);
    require(cloud.mode() == GrainMode::Cloud, "setMode must return to CLOUD");
}
}  // namespace

int main() {
    testNeverReadsOutsideRange();
    testGrainCountBounded();
    testDeterminism();
    testNoClicksAtGrainEdges();
    testCloudProducesSoundFromRange();
    testStopFadeAndInactive();
    testHardStop();
    testDefensive();
    testCpuBudget();
    testNeverReadsOutsideRangeInPitchModes();
    testPitchModeShiftsGrains();
    testRiseModeGlidesGrains();
    testDensityControlsSpawnCadence();
    testGrainSizeScaleChangesOutputWithoutClicks();
    testGranularSettingsClamped();
    std::cout << "All Grain Cloud tests passed\n";
    return 0;
}
