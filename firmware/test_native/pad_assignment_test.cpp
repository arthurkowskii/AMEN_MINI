#include "pad_assignment.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

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

void requireSamePlan(const PadAssignmentPlan& actual,
                     const PadAssignmentPlan& expected,
                     const char* message) {
    require(actual.valid() == expected.valid(), message);
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
    PadAssignmentPlan plan;

    require(buildBoundaryAssignment(pcm, boundaries, plan),
            "13 valid boundaries must build an assignment");
    require(plan.valid(), "a successfully built assignment must be valid");
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

    PadAssignmentPlan previous;
    require(buildBoundaryAssignment(pcm, boundaries, previous),
            "fixture assignment must build");
    PadAssignmentPlan candidate;
    require(assignWholeFileToPad(previous, 5, candidate),
            "a valid pad must accept the whole file");
    require(candidate.valid(), "whole-file candidate must remain valid");
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

void testInvalidPcmAndTooShortPcmAreTransactional() {
    std::array<int16_t, 12> sentinelSamples{};
    PadAssignmentPlan sentinel;
    require(buildBoundaryAssignment(makePcm(sentinelSamples), unitBoundaries(),
                                    sentinel),
            "sentinel assignment must build");

    const std::array<PcmView, 5> invalidPcm{{
        {},
        {0, 1, sentinelSamples.data(), sentinelSamples.size()},
        {48000, 0, sentinelSamples.data(), sentinelSamples.size()},
        {48000, 1, nullptr, sentinelSamples.size()},
        {48000, 2, sentinelSamples.data(), 11},
    }};
    for (const PcmView pcm : invalidPcm) {
        PadAssignmentPlan output = sentinel;
        require(!buildBoundaryAssignment(pcm, unitBoundaries(), output),
                "invalid PCM must be rejected");
        requireSamePlan(output, sentinel,
                        "invalid PCM must leave the output plan unchanged");
    }

    std::array<int16_t, kPadCount - 1> shortSamples{};
    PadAssignmentPlan output = sentinel;
    require(!buildBoundaryAssignment(makePcm(shortSamples), unitBoundaries(),
                                     output),
            "PCM shorter than twelve frames cannot make twelve ranges");
    requireSamePlan(output, sentinel,
                    "too-short PCM must leave the output plan unchanged");
}

void testMalformedBoundariesAreTransactional() {
    std::array<int16_t, 24> samples{};
    const PcmView pcm = makePcm(samples);
    std::array<int16_t, 12> sentinelSamples{};
    PadAssignmentPlan sentinel;
    require(buildBoundaryAssignment(makePcm(sentinelSamples), unitBoundaries(),
                                    sentinel),
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
        PadAssignmentPlan output = sentinel;
        require(!buildBoundaryAssignment(pcm, boundaries, output),
                "malformed boundaries must be rejected");
        requireSamePlan(output, sentinel,
                        "malformed boundaries must leave output unchanged");
    }
}

void testInvalidPadAndInvalidPreviousPlanAreTransactional() {
    std::array<int16_t, 12> samples{};
    PadAssignmentPlan previous;
    require(buildBoundaryAssignment(makePcm(samples), unitBoundaries(), previous),
            "fixture assignment must build");
    PadAssignmentPlan output = previous;

    require(!assignWholeFileToPad(previous, kPadCount, output),
            "pad index twelve must be rejected");
    requireSamePlan(output, previous,
                    "invalid pad must leave output unchanged");

    const PadAssignmentPlan invalidPrevious;
    require(!assignWholeFileToPad(invalidPrevious, 0, output),
            "an invalid previous plan must be rejected");
    requireSamePlan(output, previous,
                    "invalid previous plan must leave output unchanged");
}

}  // namespace

int main() {
    static_assert(kPadCount == 12, "the assignment model must expose twelve pads");
    testBoundaryAssignmentCoversSharedPcm();
    testWholeFileAssignmentChangesOnlySpecifiedPad();
    testInvalidPcmAndTooShortPcmAreTransactional();
    testMalformedBoundariesAreTransactional();
    testInvalidPadAndInvalidPreviousPlanAreTransactional();
    std::cout << "All pad assignment tests passed\n";
    return 0;
}
