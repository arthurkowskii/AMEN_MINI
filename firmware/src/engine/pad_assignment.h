#pragma once

#include "pcm_view.h"

#include <array>
#include <cstddef>

constexpr std::size_t kPadCount = 12;

struct PadRange {
    std::size_t startFrame = 0;
    std::size_t endFrame = 0;
};

class PadAssignmentPlan {
public:
    bool valid() const noexcept;
    PcmView pcm() const noexcept { return pcm_; }
    PadRange range(std::size_t padIndex) const noexcept;

private:
    friend bool buildBoundaryAssignment(
        PcmView pcm,
        const std::array<std::size_t, kPadCount + 1>& boundaries,
        PadAssignmentPlan& output) noexcept;
    friend bool assignWholeFileToPad(const PadAssignmentPlan& previous,
                                     std::size_t padIndex,
                                     PadAssignmentPlan& output) noexcept;

    PcmView pcm_{};
    std::array<PadRange, kPadCount> ranges_{};
};

// Builds twelve contiguous, non-empty ranges covering exactly [0, frameCount).
// output is modified only when the complete candidate is valid.
bool buildBoundaryAssignment(
    PcmView pcm,
    const std::array<std::size_t, kPadCount + 1>& boundaries,
    PadAssignmentPlan& output) noexcept;

// Returns a candidate in which only padIndex spans the whole shared PCM view.
// previous and output are unchanged on failure.
bool assignWholeFileToPad(const PadAssignmentPlan& previous,
                          std::size_t padIndex,
                          PadAssignmentPlan& output) noexcept;
