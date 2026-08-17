#include "pad_assignment.h"

namespace {

bool validPcm(PcmView pcm) noexcept {
    return pcm.valid() && pcm.sampleCount % pcm.channels == 0;
}

bool validRange(PadRange range, std::size_t frameCount) noexcept {
    return range.startFrame < range.endFrame && range.endFrame <= frameCount;
}

}  // namespace

bool PadAssignmentPlan::valid() const noexcept {
    if (!validPcm(pcm_)) return false;

    const std::size_t frames = pcm_.frameCount();
    for (const PadRange range : ranges_) {
        if (!validRange(range, frames)) return false;
    }
    return true;
}

PadRange PadAssignmentPlan::range(std::size_t padIndex) const noexcept {
    return padIndex < kPadCount ? ranges_[padIndex] : PadRange{};
}

bool buildBoundaryAssignment(
    PcmView pcm,
    const std::array<std::size_t, kPadCount + 1>& boundaries,
    PadAssignmentPlan& output) noexcept {
    if (!validPcm(pcm) || pcm.frameCount() < kPadCount ||
        boundaries.front() != 0 || boundaries.back() != pcm.frameCount()) {
        return false;
    }

    PadAssignmentPlan candidate;
    candidate.pcm_ = pcm;
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const PadRange range{boundaries[pad], boundaries[pad + 1]};
        if (!validRange(range, pcm.frameCount())) return false;
        candidate.ranges_[pad] = range;
    }

    if (!candidate.valid()) return false;
    output = candidate;
    return true;
}

bool assignWholeFileToPad(const PadAssignmentPlan& previous,
                          std::size_t padIndex,
                          PadAssignmentPlan& output) noexcept {
    if (!previous.valid() || padIndex >= kPadCount) return false;

    PadAssignmentPlan candidate = previous;
    candidate.ranges_[padIndex] = {0, candidate.pcm_.frameCount()};
    if (!candidate.valid()) return false;

    output = candidate;
    return true;
}
