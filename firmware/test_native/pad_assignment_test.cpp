#include "pad_assignment.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <std::size_t SampleCount>
PcmView makePcm(const std::array<int16_t, SampleCount>& samples,
                uint16_t channels = 1) {
    return {48000, channels, samples.data(), samples.size()};
}

std::array<std::size_t, kPadCount + 1> unitBoundaries() {
    std::array<std::size_t, kPadCount + 1> boundaries{};
    for (std::size_t i = 0; i < boundaries.size(); ++i) boundaries[i] = i;
    return boundaries;
}

PadAssignmentPlan requirePlan(std::optional<PadAssignmentPlan> result,
                              const char* message) {
    require(result.has_value(), message);
    return *result;
}

bool sameRange(std::optional<PadRange> actual,
               std::optional<PadRange> expected) {
    if (actual.has_value() != expected.has_value()) return false;
    return !actual.has_value() ||
           (actual->startFrame == expected->startFrame &&
            actual->endFrame == expected->endFrame);
}

void requireSamePlan(const PadAssignmentPlan& actual,
                     const PadAssignmentPlan& expected,
                     const char* message) {
    require(actual.pcmView().samples == expected.pcmView().samples, message);
    require(actual.pcmView().sampleRate == expected.pcmView().sampleRate, message);
    require(actual.pcmView().channels == expected.pcmView().channels, message);
    require(actual.pcmView().sampleCount == expected.pcmView().sampleCount, message);
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        require(sameRange(actual.range(pad), expected.range(pad)), message);
    }
}

PadRange requireRange(const PadAssignmentPlan& plan, std::size_t padIndex,
                      const char* message) {
    const auto result = plan.range(padIndex);
    require(result.has_value(), message);
    return *result;
}

void testBoundaryAssignmentCoversSharedPcm() {
    std::array<int16_t, 24> samples{};
    const PcmView pcm = makePcm(samples, 2);
    const auto boundaries = unitBoundaries();
    const PadAssignmentPlan plan = requirePlan(
        buildBoundaryAssignment(pcm, boundaries),
        "13 valid boundaries must build an assignment");

    require(plan.pcmView().samples == samples.data(),
            "the plan must reference PCM without copying samples");
    require(plan.pcmView().sampleRate == pcm.sampleRate &&
                plan.pcmView().channels == pcm.channels &&
                plan.pcmView().sampleCount == pcm.sampleCount,
            "all PCM metadata must be preserved");
    require(requireRange(plan, 0, "pad zero must have a range").startFrame == 0,
            "the first range must start at frame zero");
    require(requireRange(plan, kPadCount - 1, "last pad must have a range")
                .endFrame == pcm.frameCount(),
            "the final range must end at frameCount");

    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const PadRange range =
            requireRange(plan, pad, "valid pads must have ranges");
        require(range.startFrame < range.endFrame,
                "every pad range must be non-empty");
        if (pad > 0) {
            require(requireRange(plan, pad - 1, "valid pads must have ranges")
                        .endFrame == range.startFrame,
                    "boundary-built ranges must be contiguous");
        }
        require(plan.pcmView().samples == pcm.samples,
                "every range must use the plan's shared PCM view");
    }

    require(!plan.range(kPadCount).has_value(),
            "an out-of-range pad index must not look like a valid empty range");
}

void testWholeFileAssignmentChangesOnlySpecifiedPad() {
    std::array<int16_t, 24> samples{};
    const PcmView pcm = makePcm(samples);
    std::array<std::size_t, kPadCount + 1> boundaries{};
    for (std::size_t i = 0; i < boundaries.size(); ++i) boundaries[i] = i * 2;

    const PadAssignmentPlan previous = requirePlan(
        buildBoundaryAssignment(pcm, boundaries),
        "fixture assignment must build");
    const PadAssignmentPlan candidate = requirePlan(
        assignWholeFileToPad(previous, 5),
        "a valid pad must accept the whole file");

    const PadRange assigned =
        requireRange(candidate, 5, "assigned pad must have a range");
    require(assigned.startFrame == 0 && assigned.endFrame == pcm.frameCount(),
            "the selected pad must span the complete file");
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        if (pad == 5) continue;
        require(sameRange(candidate.range(pad), previous.range(pad)),
                "whole-file assignment must preserve every other pad range");
    }
    require(candidate.pcmView().samples == previous.pcmView().samples,
            "whole-file assignment must retain the borrowed PCM pointer");
}

void testInvalidPcmAndTooShortPcmAreRejected() {
    std::array<int16_t, 12> sentinelSamples{};

    const std::array<PcmView, 5> invalidPcm{{
        {},
        {0, 1, sentinelSamples.data(), sentinelSamples.size()},
        {48000, 0, sentinelSamples.data(), sentinelSamples.size()},
        {48000, 1, nullptr, sentinelSamples.size()},
        {48000, 2, sentinelSamples.data(), 11},
    }};
    for (const PcmView pcm : invalidPcm) {
        const auto rejected = buildBoundaryAssignment(pcm, unitBoundaries());
        require(!rejected.has_value(), "invalid PCM must be rejected");
    }

    std::array<int16_t, kPadCount - 1> shortSamples{};
    const auto rejected =
        buildBoundaryAssignment(makePcm(shortSamples), unitBoundaries());
    require(!rejected.has_value(),
            "PCM shorter than twelve frames cannot make twelve ranges");
}

void testMalformedBoundariesAreRejected() {
    std::array<int16_t, 24> samples{};
    const PcmView pcm = makePcm(samples);

    const auto valid = [] {
        std::array<std::size_t, kPadCount + 1> boundaries{};
        for (std::size_t i = 0; i < boundaries.size(); ++i) boundaries[i] = i * 2;
        return boundaries;
    }();

    std::array<std::array<std::size_t, kPadCount + 1>, 5> malformed{};
    malformed.fill(valid);
    malformed[0][0] = 1;
    malformed[1][kPadCount] = pcm.frameCount() - 1;
    malformed[2][4] = malformed[2][3];
    malformed[3][6] = malformed[3][5] - 1;
    malformed[4][7] = pcm.frameCount() + 1;

    for (const auto& boundaries : malformed) {
        const auto rejected = buildBoundaryAssignment(pcm, boundaries);
        require(!rejected.has_value(), "malformed boundaries must be rejected");
    }
}

void testInvalidPadPreservesExistingPlan() {
    std::array<int16_t, 12> samples{};
    const PadAssignmentPlan previous = requirePlan(
        buildBoundaryAssignment(makePcm(samples), unitBoundaries()),
        "fixture assignment must build");
    const PadAssignmentPlan snapshot = previous;

    const auto rejected = assignWholeFileToPad(previous, kPadCount);
    require(!rejected.has_value(), "pad index twelve must be rejected");
    requireSamePlan(previous, snapshot,
                    "rejecting an invalid update must preserve its input plan");
}

}  // namespace

int main() {
    static_assert(kPadCount == 12, "the assignment model must expose twelve pads");
    static_assert(!std::is_default_constructible<PadAssignmentPlan>::value,
                  "invalid pad assignment plans must not be publicly constructible");
    testBoundaryAssignmentCoversSharedPcm();
    testWholeFileAssignmentChangesOnlySpecifiedPad();
    testInvalidPcmAndTooShortPcmAreRejected();
    testMalformedBoundariesAreRejected();
    testInvalidPadPreservesExistingPlan();
    std::cout << "All pad assignment tests passed\n";
    return 0;
}
