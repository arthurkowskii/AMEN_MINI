#include "pad_assignment.h"

namespace {

bool validPcm(PcmView pcm) noexcept {
    return pcm.valid() && pcm.sampleCount % pcm.channels == 0;
}

bool validRange(PadRange range, std::size_t frameCount) noexcept {
    return range.startFrame < range.endFrame && range.endFrame <= frameCount;
}

}  // namespace

PadAssignmentPlan::PadAssignmentPlan(
    PcmView pcm,
    const std::array<PadRange, kPadCount>& ranges) noexcept
    : pcm_(pcm), ranges_(ranges) {}

std::optional<PadRange> PadAssignmentPlan::range(
    std::size_t padIndex) const noexcept {
    if (padIndex >= kPadCount) return std::nullopt;
    return ranges_[padIndex];
}

std::optional<PadAssignmentPlan> buildBoundaryAssignment(
    PcmView pcm,
    const std::array<std::size_t, kPadCount + 1>& boundaries) noexcept {
    if (!validPcm(pcm) || pcm.frameCount() < kPadCount ||
        boundaries.front() != 0 || boundaries.back() != pcm.frameCount()) {
        return std::nullopt;
    }

    std::array<PadRange, kPadCount> ranges{};
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const PadRange range{boundaries[pad], boundaries[pad + 1]};
        if (!validRange(range, pcm.frameCount())) return std::nullopt;
        ranges[pad] = range;
    }

    return PadAssignmentPlan{pcm, ranges};
}

std::optional<PadAssignmentPlan> assignWholeFileToPad(
    const PadAssignmentPlan& previous,
    std::size_t padIndex) noexcept {
    if (padIndex >= kPadCount) return std::nullopt;

    auto ranges = previous.ranges_;
    ranges[padIndex] = {0, previous.pcm_.frameCount()};
    return PadAssignmentPlan{previous.pcm_, ranges};
}
