#pragma once

#include "pcm_view.h"

#include <array>
#include <cstddef>
#include <optional>

constexpr std::size_t kPadCount = 12;

struct PadRange {
    std::size_t startFrame = 0;
    std::size_t endFrame = 0;
};

class PadAssignmentPlan {
public:
    PcmView pcm() const noexcept { return pcm_; }
    PadRange range(std::size_t padIndex) const noexcept;

private:
    PadAssignmentPlan(PcmView pcm,
                      const std::array<PadRange, kPadCount>& ranges) noexcept;

    friend std::optional<PadAssignmentPlan> buildBoundaryAssignment(
        PcmView pcm,
        const std::array<std::size_t, kPadCount + 1>& boundaries) noexcept;
    friend std::optional<PadAssignmentPlan> assignWholeFileToPad(
        const PadAssignmentPlan& previous,
        std::size_t padIndex) noexcept;

    PcmView pcm_;
    std::array<PadRange, kPadCount> ranges_;
};

// Builds twelve contiguous, non-empty ranges covering exactly [0, frameCount).
// Failure is represented without constructing a PadAssignmentPlan.
std::optional<PadAssignmentPlan> buildBoundaryAssignment(
    PcmView pcm,
    const std::array<std::size_t, kPadCount + 1>& boundaries) noexcept;

// Returns a plan in which only padIndex spans the whole shared PCM view.
// Returning a value makes input/output aliasing impossible.
std::optional<PadAssignmentPlan> assignWholeFileToPad(
    const PadAssignmentPlan& previous,
    std::size_t padIndex) noexcept;
