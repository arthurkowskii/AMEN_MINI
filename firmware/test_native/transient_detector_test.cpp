#include "pad_assignment.h"
#include "transient_detector.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr std::size_t kToleranceFrames = 480;  // 10 ms at 48 kHz.

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

PcmView makeView(const std::vector<int16_t>& samples,
                 uint16_t channels = 1,
                 uint32_t sampleRate = kSampleRate) {
    return {sampleRate, channels, samples.data(), samples.size()};
}

void addBurst(std::vector<int16_t>& samples,
              std::size_t frame,
              uint16_t channels,
              bool antiPhase = false,
              int16_t amplitude = 30000) {
    constexpr std::size_t kBurstFrames = 96;
    for (std::size_t offset = 0; offset < kBurstFrames; ++offset) {
        if (frame + offset >= samples.size() / channels) break;
        const int16_t value = (offset % 2U == 0U)
                                  ? amplitude
                                  : static_cast<int16_t>(-amplitude);
        const std::size_t base = (frame + offset) * channels;
        samples[base] = value;
        if (channels == 2U) {
            samples[base + 1U] =
                antiPhase ? static_cast<int16_t>(-value) : value;
        }
    }
}

void requireValidBoundaries(const TransientBoundaries& boundaries,
                            std::size_t frameCount) {
    require(boundaries.front() == 0U, "first boundary must be zero");
    require(boundaries.back() == frameCount, "last boundary must be frameCount");
    for (std::size_t i = 1; i < boundaries.size(); ++i) {
        require(boundaries[i - 1U] < boundaries[i],
                "all twelve ranges must be non-empty and sorted");
    }
}

void requireAttacksNear(const TransientBoundaries& boundaries,
                        const std::array<std::size_t, 11>& attacks) {
    for (const std::size_t attack : attacks) {
        const auto nearest = std::min_element(
            boundaries.begin() + 1, boundaries.end() - 1,
            [attack](std::size_t lhs, std::size_t rhs) {
                const std::size_t lhsDistance = lhs > attack ? lhs - attack : attack - lhs;
                const std::size_t rhsDistance = rhs > attack ? rhs - attack : attack - rhs;
                return lhsDistance < rhsDistance;
            });
        const std::size_t distance = *nearest > attack ? *nearest - attack : attack - *nearest;
        require(distance <= kToleranceFrames,
                "each known attack must have a detected boundary within 10 ms");
    }
}

void testRegularMonoImpulsesAndInputUnchanged() {
    constexpr std::size_t kFrames = 48000;
    std::vector<int16_t> samples(kFrames, int16_t{0});
    std::array<std::size_t, 11> attacks{};
    for (std::size_t i = 0; i < attacks.size(); ++i) {
        attacks[i] = 3000U + i * 4000U;
        addBurst(samples, attacks[i], 1U);
    }
    const std::vector<int16_t> snapshot = samples;

    const auto result = detectTransientBoundaries(makeView(samples));
    require(result.has_value(), "valid mono PCM must be analyzed");
    requireValidBoundaries(*result, kFrames);
    requireAttacksNear(*result, attacks);
    require(samples == snapshot, "detector must not modify borrowed PCM");
}

void testIrregularStereoAntiPhaseAndSilence() {
    constexpr std::size_t kFrames = 60000;
    std::vector<int16_t> samples(kFrames * 2U, int16_t{0});
    const std::array<std::size_t, 11> attacks{{
        2400U, 6500U, 11100U, 15900U, 21800U, 27100U,
        33700U, 38900U, 45100U, 50800U, 55700U,
    }};
    for (std::size_t i = 0; i < attacks.size(); ++i) {
        addBurst(samples, attacks[i], 2U, (i % 2U) == 0U);
    }

    const auto first = detectTransientBoundaries(makeView(samples, 2U));
    const auto second = detectTransientBoundaries(makeView(samples, 2U));
    require(first.has_value() && second.has_value(), "stereo PCM must be analyzed");
    require(*first == *second, "repeated analysis must be deterministic");
    requireValidBoundaries(*first, kFrames);
    requireAttacksNear(*first, attacks);
}

void testStrongCandidatesOutrankWeakDecoy() {
    constexpr std::size_t kFrames = 48000;
    std::vector<int16_t> samples(kFrames, int16_t{0});
    std::array<std::size_t, 11> strongAttacks{};
    for (std::size_t i = 0; i < strongAttacks.size(); ++i) {
        strongAttacks[i] = 2000U + i * 4000U;
        addBurst(samples, strongAttacks[i], 1U);
    }
    constexpr std::size_t kWeakDecoy = 4000;
    addBurst(samples, kWeakDecoy, 1U, false, int16_t{4000});

    const auto result = detectTransientBoundaries(makeView(samples));
    require(result.has_value(), "strong-candidate fixture must be analyzed");
    requireAttacksNear(*result, strongAttacks);
    const auto decoyBoundary = std::find_if(
        result->begin() + 1, result->end() - 1,
        [](std::size_t boundary) {
            const std::size_t distance = boundary > kWeakDecoy
                                             ? boundary - kWeakDecoy
                                             : kWeakDecoy - boundary;
            return distance <= kToleranceFrames;
        });
    require(decoyBoundary == result->end() - 1,
            "a weak decoy must not displace one of eleven stronger attacks");
}

void testSilenceUsesDeterministicFallback() {
    constexpr std::size_t kFrames = 12001;
    std::vector<int16_t> silence(kFrames, int16_t{0});
    const auto first = detectTransientBoundaries(makeView(silence));
    const auto second = detectTransientBoundaries(makeView(silence));
    require(first.has_value() && second.has_value(), "silence still needs twelve slices");
    require(*first == *second, "fallback must be deterministic");
    requireValidBoundaries(*first, kFrames);
}

void testInvalidAndShortViewsAreRejected() {
    std::array<int16_t, 24> samples{};
    const std::array<PcmView, 8> invalid{{
        {},
        {0U, 1U, samples.data(), samples.size()},
        {kSampleRate, 0U, samples.data(), samples.size()},
        {kSampleRate, 1U, nullptr, samples.size()},
        {kSampleRate, 2U, samples.data(), 23U},
        {kSampleRate, 3U, samples.data(), samples.size()},
        {kSampleRate, std::numeric_limits<uint16_t>::max(), samples.data(),
         std::numeric_limits<std::size_t>::max()},
        {kSampleRate, 1U, samples.data(), 11U},
    }};
    for (const PcmView view : invalid) {
        require(!detectTransientBoundaries(view).has_value(),
                "invalid, unsupported, or short PCM must be rejected before indexing");
    }
}

void testLongInputHasStableBoundedAnalysis() {
    constexpr std::size_t kFrames = 3000000;
    std::vector<int16_t> samples(kFrames, int16_t{0});
    for (std::size_t frame = 100000U; frame < kFrames; frame += 200000U) {
        addBurst(samples, frame, 1U);
    }
    const auto first = detectTransientBoundaries(makeView(samples));
    const auto second = detectTransientBoundaries(makeView(samples));
    require(first.has_value() && second.has_value(), "long PCM must be supported");
    require(*first == *second, "long-input analysis must remain deterministic");
    requireValidBoundaries(*first, kFrames);
}

void testPadAssignmentIntegrationSharesPcm() {
    constexpr std::size_t kFrames = 24000;
    std::vector<int16_t> samples(kFrames, int16_t{0});
    for (std::size_t frame = 1500U; frame < 23000U; frame += 2000U) {
        addBurst(samples, frame, 1U);
    }
    const PcmView pcm = makeView(samples);
    const auto boundaries = detectTransientBoundaries(pcm);
    require(boundaries.has_value(), "integration fixture must produce boundaries");
    const auto plan = buildBoundaryAssignment(pcm, *boundaries);
    require(plan.has_value(), "detected boundaries must satisfy pad assignment validation");
    require(plan->pcmView().samples == samples.data(), "assignment must share borrowed PCM");
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const auto range = plan->range(pad);
        require(range.has_value(), "all twelve pads must have ranges");
        require(range->startFrame == (*boundaries)[pad] &&
                    range->endFrame == (*boundaries)[pad + 1U],
                "assignment ranges must match detector boundaries contiguously");
    }
}

}  // namespace

int main() {
    testRegularMonoImpulsesAndInputUnchanged();
    testIrregularStereoAntiPhaseAndSilence();
    testStrongCandidatesOutrankWeakDecoy();
    testSilenceUsesDeterministicFallback();
    testInvalidAndShortViewsAreRejected();
    testLongInputHasStableBoundedAnalysis();
    testPadAssignmentIntegrationSharesPcm();
    std::cout << "All transient detector tests passed\n";
    return 0;
}
