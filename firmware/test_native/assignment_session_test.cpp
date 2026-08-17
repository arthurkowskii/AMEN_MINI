#include "assignment_session.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Counts stopAll() calls so tests can prove a rejected assignment never
// silences the voices and a successful one does it exactly once.
class CountingVoiceStopper final : public VoiceStopper {
public:
    void stopAll() override { ++calls; }
    std::size_t calls = 0;
};

constexpr std::size_t kFrames = 1200;

WavData makeWav(std::size_t frameCount = kFrames) {
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 1;
    wav.samples.assign(frameCount, int16_t{0});
    return wav;
}

TransientBoundaries makeBoundaries() {
    TransientBoundaries boundaries{};
    for (std::size_t i = 0; i < boundaries.size(); ++i) boundaries[i] = i * 100;
    return boundaries;
}

bool sameRange(std::optional<PadRange> actual,
               std::optional<PadRange> expected) {
    if (actual.has_value() != expected.has_value()) return false;
    return !actual.has_value() ||
           (actual->startFrame == expected->startFrame &&
            actual->endFrame == expected->endFrame);
}

void requireSameRanges(const PadAssignmentPlan& actual,
                       const PadAssignmentPlan& expected,
                       const char* message) {
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

void testApplyTransientPublishesStablePlanAndConsumesOwnership() {
    WavData loaded = makeWav();
    const int16_t* originalBuffer = loaded.samples.data();
    const TransientBoundaries boundaries = makeBoundaries();
    CountingVoiceStopper stopper;
    AssignmentSession session;

    require(session.applyTransient(std::move(loaded), boundaries, stopper),
            "a valid transient assignment must succeed");
    require(stopper.calls == 1,
            "a successful assignment must stop the voices exactly once");
    require(loaded.samples.empty(),
            "the moved-in WavData must be cleared after a successful move");
    require(!loaded.valid(), "the moved-from WavData must no longer be valid");
    require(session.currentWav() != nullptr && session.currentWav()->valid(),
            "the session must own a valid WavData after a success");
    require(session.plan() != nullptr, "the session must publish a plan");
    require(session.plan()->pcmView().samples ==
                session.currentWav()->samples.data(),
            "the plan must borrow the session-owned WavData buffer");
    require(session.plan()->pcmView().samples == originalBuffer,
            "the vector move must keep the sample buffer address stable");
    require(session.currentWav()->samples.size() == kFrames,
            "the session must own every sample of the moved-in file");

    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const PadRange range =
            requireRange(*session.plan(), pad, "every pad must have a range");
        require(range.startFrame == boundaries[pad] &&
                    range.endFrame == boundaries[pad + 1],
                "each pad range must match its transient boundaries");
    }

    // A second successful assignment must retire the first plan and repoint
    // it at the new buffer.
    WavData second = makeWav();
    const int16_t* secondBuffer = second.samples.data();
    require(session.applyTransient(std::move(second), boundaries, stopper),
            "a replacement transient assignment must succeed");
    require(stopper.calls == 2, "each success must stop the voices once");
    require(session.plan()->pcmView().samples == secondBuffer,
            "the replacement plan must borrow the new buffer");
    require(session.plan()->pcmView().samples ==
                session.currentWav()->samples.data(),
            "the replacement plan must alias the session audio");
}

void testApplyTransientFailureLeavesStateAndStopperUntouched() {
    WavData first = makeWav();
    const int16_t* firstBuffer = first.samples.data();
    CountingVoiceStopper stopper;
    AssignmentSession session;
    require(session.applyTransient(std::move(first), makeBoundaries(), stopper),
            "fixture assignment must succeed");
    require(stopper.calls == 1, "fixture assignment must stop voices once");
    const PadAssignmentPlan snapshot = *session.plan();
    const int16_t* planBuffer = session.plan()->pcmView().samples;

    // Boundaries that do not cover the file must be rejected atomically.
    WavData replacement = makeWav();
    TransientBoundaries bad = makeBoundaries();
    bad[kPadCount] = kFrames - 1;
    require(!session.applyTransient(std::move(replacement), bad, stopper),
            "boundaries that do not cover the file must be rejected");
    require(stopper.calls == 1,
            "a rejected assignment must not stop the voices");
    require(!replacement.samples.empty(),
            "a rejected assignment must not consume the WavData");
    require(session.currentWav()->samples.data() == firstBuffer,
            "a rejected assignment must leave the audio untouched");
    require(session.plan()->pcmView().samples == planBuffer,
            "a rejected assignment must leave the plan untouched");
    requireSameRanges(*session.plan(), snapshot,
                      "a rejected assignment must leave the ranges untouched");

    // Invalid PCM with valid boundaries must be rejected atomically too.
    WavData invalid = makeWav(0);
    require(!session.applyTransient(std::move(invalid), makeBoundaries(),
                                    stopper),
            "invalid PCM must be rejected");
    require(stopper.calls == 1,
            "a rejected assignment must not stop the voices");
    require(session.currentWav()->samples.data() == firstBuffer,
            "a rejected assignment must leave the audio untouched");
    requireSameRanges(*session.plan(), snapshot,
                      "a rejected assignment must leave the ranges untouched");

    // A fresh session must stay empty when the first assignment fails.
    AssignmentSession fresh;
    require(!fresh.applyTransient(makeWav(0), makeBoundaries(), stopper),
            "a failing first assignment must be rejected");
    require(fresh.plan() == nullptr,
            "a failed first assignment must not publish a plan");
    require(!fresh.currentWav()->valid(),
            "a failed first assignment must leave the session empty");
    require(stopper.calls == 1,
            "a failed first assignment must not stop the voices");
}

void testWholeFileAssignmentPreservesOtherRangesAndSharedPointer() {
    AssignmentSession session;
    CountingVoiceStopper stopper;
    WavData first = makeWav();
    const int16_t* firstBuffer = first.samples.data();
    require(session.applyTransient(std::move(first), makeBoundaries(), stopper),
            "fixture assignment must succeed");
    const PadAssignmentPlan snapshot = *session.plan();
    const int16_t* planBuffer = session.plan()->pcmView().samples;

    WavData reload = makeWav();
    require(session.applyWholeFileToPad(std::move(reload), 5),
            "a whole-file assignment must succeed");
    require(session.plan()->pcmView().samples == planBuffer,
            "the rebuilt plan must share the previous plan's PCM pointer");
    require(session.plan()->pcmView().samples ==
                session.currentWav()->samples.data(),
            "the rebuilt plan must alias the session-owned audio");
    require(session.currentWav()->samples.data() == firstBuffer,
            "the whole-file action must not replace the session audio");

    const PadRange whole =
        requireRange(*session.plan(), 5, "pad five must have a range");
    require(whole.startFrame == 0 && whole.endFrame == kFrames,
            "pad five must span the complete file");
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        if (pad == 5) continue;
        require(sameRange(session.plan()->range(pad), snapshot.range(pad)),
                "whole-file assignment must preserve every other pad range");
    }
}

void testWholeFileAssignmentChainsAcrossPads() {
    AssignmentSession session;
    CountingVoiceStopper stopper;
    require(session.applyTransient(makeWav(), makeBoundaries(), stopper),
            "fixture assignment must succeed");
    const PadAssignmentPlan snapshot = *session.plan();

    require(session.applyWholeFileToPad(makeWav(), 5),
            "the first whole-file assignment must succeed");
    require(session.applyWholeFileToPad(makeWav(), 9),
            "a second whole-file assignment must succeed");

    for (const std::size_t pad : {std::size_t{5}, std::size_t{9}}) {
        const PadRange whole =
            requireRange(*session.plan(), pad, "whole-file pads must have ranges");
        require(whole.startFrame == 0 && whole.endFrame == kFrames,
                "every whole-file pad must span the complete file");
    }
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        if (pad == 5 || pad == 9) continue;
        require(sameRange(session.plan()->range(pad), snapshot.range(pad)),
                "chained whole-file assignments must preserve the other pads");
    }
}

void testWholeFileBeforeAnyPlanIsRejected() {
    AssignmentSession session;
    WavData loaded = makeWav();
    require(!session.applyWholeFileToPad(std::move(loaded), 0),
            "whole-file assignment must require an existing plan");
    require(!loaded.samples.empty(),
            "a rejected call must not consume the WavData");
    require(session.plan() == nullptr,
            "a rejected call must not publish a plan");
    require(!session.currentWav()->valid(),
            "a rejected call must leave the session empty");
}

void testWholeFileInvalidPadAndInvalidPcmAreRejectedUntouched() {
    AssignmentSession session;
    CountingVoiceStopper stopper;
    WavData first = makeWav();
    const int16_t* firstBuffer = first.samples.data();
    require(session.applyTransient(std::move(first), makeBoundaries(), stopper),
            "fixture assignment must succeed");
    const PadAssignmentPlan snapshot = *session.plan();
    const int16_t* planBuffer = session.plan()->pcmView().samples;

    WavData valid = makeWav();
    require(!session.applyWholeFileToPad(std::move(valid), kPadCount),
            "pad index twelve must be rejected");
    require(!valid.samples.empty(),
            "a rejected call must not consume the WavData");

    WavData invalid = makeWav(0);
    require(!session.applyWholeFileToPad(std::move(invalid), 0),
            "invalid PCM must be rejected");

    require(session.currentWav()->samples.data() == firstBuffer,
            "rejected calls must leave the audio untouched");
    require(session.plan()->pcmView().samples == planBuffer,
            "rejected calls must leave the plan untouched");
    requireSameRanges(*session.plan(), snapshot,
                      "rejected calls must leave the ranges untouched");
}

}  // namespace

int main() {
    static_assert(kPadCount == 12, "the assignment model must expose twelve pads");
    static_assert(kTransientBoundaryCount == kPadCount + 1,
                  "transient boundaries must cover twelve ranges");
    testApplyTransientPublishesStablePlanAndConsumesOwnership();
    testApplyTransientFailureLeavesStateAndStopperUntouched();
    testWholeFileAssignmentPreservesOtherRangesAndSharedPointer();
    testWholeFileAssignmentChainsAcrossPads();
    testWholeFileBeforeAnyPlanIsRejected();
    testWholeFileInvalidPadAndInvalidPcmAreRejectedUntouched();
    std::cout << "All assignment session tests passed\n";
    return 0;
}
