#pragma once

#include "pcm_view.h"

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>

constexpr std::size_t kPadCount = 12;

struct PadRange {
    std::size_t startFrame = 0;
    std::size_t endFrame = 0;
};

// Fixed-size value type: stores one borrowed PCM view and twelve ranges, with no
// allocation or sample ownership. Copying a plan copies only that metadata;
// value-return copies are deliberate control-path work.
class PadAssignmentPlan {
public:
    // Borrowed/non-owning. The PCM address must remain stable and its storage
    // must outlive this plan, every copy/optional containing it, and all
    // playback that uses its ranges. Moving, resizing, or reusing the backing
    // WavData invalidates existing plans: build and publish a replacement plan
    // before freeing or repurposing the old storage.
    PcmView pcmView() const noexcept { return pcm_; }

    // Returns no value when padIndex is outside [0, kPadCount).
    std::optional<PadRange> range(std::size_t padIndex) const noexcept;

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

static_assert(std::is_trivially_copyable<PadAssignmentPlan>::value,
              "pad plans must remain fixed-size metadata values");
static_assert(sizeof(PadAssignmentPlan) ==
                  sizeof(PcmView) + sizeof(std::array<PadRange, kPadCount>),
              "pad plans must not gain hidden storage");

// Builds twelve contiguous, non-empty ranges covering exactly [0, frameCount).
// Failure is represented without constructing a PadAssignmentPlan.
// Control-path builder: never call from the audio callback.
std::optional<PadAssignmentPlan> buildBoundaryAssignment(
    PcmView pcm,
    const std::array<std::size_t, kPadCount + 1>& boundaries) noexcept;

// Returns a plan in which only padIndex spans the whole borrowed PCM view.
// Returning a value makes input/output aliasing impossible.
// Control-path builder: never call from the audio callback.
std::optional<PadAssignmentPlan> assignWholeFileToPad(
    const PadAssignmentPlan& previous,
    std::size_t padIndex) noexcept;
