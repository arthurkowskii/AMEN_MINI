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

void requireSamePlan(const PadAssignmentPlan& actual,
                     const PadAssignmentPlan& expected,
                     const char* message) {
    require(actual.pcm().samples == expected.pcm().samples, message);
    require(actual.pcm().sampleRate == expected.pcm().sampleRate, message);
    require(actual.pcm().channels == expected.pcm().channels, message);
    require(actual.pcm().sampleCount == expected.pcm().sampleCount, message);
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        require(actual.range(pad).startFrame == expected.range(pad).startFrame,
                message);
        require(actual.range(pad).endFrame == expected.range(pad).endFrame,
                message);
    }
}

void testBoundaryAssignmentCoversSharedPcm() {
    std::array<int16_t, 24> samples{};
    const PcmView pcm = makePcm(samples, 2);
    const auto boundaries = unitBoundaries();
    const PadAssignmentPlan plan = requirePlan(
        buildBoundaryAssignment(pcm, boundaries),
        "13 valid boundaries must build an assignment");

    require(plan.pcm().samples == samples.data(),
            "the plan must reference PCM without copying samples");
    require(plan.pcm().sampleRate == pcm.sampleRate &&
                plan.pcm().channels == pcm.channels &&
                plan.pcm().sampleCount == pcm.sampleCount,
            "all PCM metadata must be preserved");
    require(plan.range(0).startFrame == 0,
            "the first range must start at frame zero");
    require(plan.range(kPadCount - 1).endFrame == pcm.frameCount(),
            "the final range must end at frameCount");

    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const PadRange range = plan.range(pad);
        require(range.startFrame < range.endFrame,
                "every pad range must be non-empty");
        if (pad > 0) {
            require(plan.range(pad - 1).endFrame == range.startFrame,
                    "boundary-built ranges must be contiguous");
        }
        require(plan.pcm().samples == pcm.samples,
                "every range must use the plan's shared PCM view");
    }
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

    require(candidate.range(5).startFrame == 0 &&
                candidate.range(5).endFrame == pcm.frameCount(),
            "the selected pad must span the complete file");
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        if (pad == 5) continue;
        require(candidate.range(pad).startFrame == previous.range(pad).startFrame &&
                    candidate.range(pad).endFrame == previous.range(pad).endFrame,
                "whole-file assignment must preserve every other pad range");
    }
    require(candidate.pcm().samples == previous.pcm().samples,
            "whole-file assignment must retain the shared PCM pointer");
    require(previous.range(5).startFrame == 10 &&
                previous.range(5).endFrame == 12,
            "building a candidate must not mutate the previous plan");
}

void testInvalidPcmAndTooShortPcmDoNotMutateExistingPlan() {
    std::array<int16_t, 12> sentinelSamples{};
    const PadAssignmentPlan sentinel = requirePlan(
        buildBoundaryAssignment(makePcm(sentinelSamples), unitBoundaries()),
        "sentinel assignment must build");

    const std::array<PcmView, 5> invalidPcm{{
        {},
        {0, 1, sentinelSamples.data(), sentinelSamples.size()},
        {48000, 0, sentinelSamples.data(), sentinelSamples.size()},
        {48000, 1, nullptr, sentinelSamples.size()},
        {48000, 2, sentinelSamples.data(), 11},
    }};
    for (const PcmView pcm : invalidPcm) {
        PadAssignmentPlan existing = sentinel;
        const auto rejected = buildBoundaryAssignment(pcm, unitBoundaries());
        require(!rejected.has_value(), "invalid PCM must be rejected");
        requireSamePlan(existing, sentinel,
                        "failed value factories cannot mutate an existing plan");
    }

    std::array<int16_t, kPadCount - 1> shortSamples{};
    PadAssignmentPlan existing = sentinel;
    const auto rejected =
        buildBoundaryAssignment(makePcm(shortSamples), unitBoundaries());
    require(!rejected.has_value(),
            "PCM shorter than twelve frames cannot make twelve ranges");
    requireSamePlan(existing, sentinel,
                    "too-short PCM must not mutate an existing plan");
}

void testMalformedBoundariesDoNotMutateExistingPlan() {
    std::array<int16_t, 24> samples{};
    const PcmView pcm = makePcm(samples);
    std::array<int16_t, 12> sentinelSamples{};
    const PadAssignmentPlan sentinel = requirePlan(
        buildBoundaryAssignment(makePcm(sentinelSamples), unitBoundaries()),
        "sentinel assignment must build");

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
        PadAssignmentPlan existing = sentinel;
        const auto rejected = buildBoundaryAssignment(pcm, boundaries);
        require(!rejected.has_value(), "malformed boundaries must be rejected");
        requireSamePlan(existing, sentinel,
                        "failed value factories cannot mutate an existing plan");
    }
}

void testInvalidPadDoesNotMutateExistingPlan() {
    std::array<int16_t, 12> samples{};
    const PadAssignmentPlan previous = requirePlan(
        buildBoundaryAssignment(makePcm(samples), unitBoundaries()),
        "fixture assignment must build");
    PadAssignmentPlan existing = previous;

    const auto rejected = assignWholeFileToPad(previous, kPadCount);
    require(!rejected.has_value(), "pad index twelve must be rejected");
    requireSamePlan(existing, previous,
                    "failed value factories cannot mutate an existing plan");
}

}  // namespace

int main() {
    static_assert(kPadCount == 12, "the assignment model must expose twelve pads");
    static_assert(!std::is_default_constructible<PadAssignmentPlan>::value,
                  "invalid pad assignment plans must not be publicly constructible");
    testBoundaryAssignmentCoversSharedPcm();
    testWholeFileAssignmentChangesOnlySpecifiedPad();
    testInvalidPcmAndTooShortPcmDoNotMutateExistingPlan();
    testMalformedBoundariesDoNotMutateExistingPlan();
    testInvalidPadDoesNotMutateExistingPlan();
    std::cout << "All pad assignment tests passed\n";
    return 0;
}
