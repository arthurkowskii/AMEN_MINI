#include "assignment_session.h"

bool AssignmentSession::applyTransient(WavData&& loaded,
                                       TransientBoundaries boundaries,
                                       VoiceStopper& stopper) noexcept {
    // Take the borrowed view BEFORE the move: the candidate plan references
    // loaded's buffer, and the vector move that follows steals that buffer,
    // so the address in the view stays valid after loaded becomes
    // currentWav_. See the lifetime contract in the header.
    const PcmView pcm = loaded.view();
    std::optional<PadAssignmentPlan> built =
        buildBoundaryAssignment(pcm, boundaries);
    if (!built) return false;

    stopper.stopAll();
    currentWav_ = std::move(loaded);
    plan_ = std::move(built);
    return true;
}

bool AssignmentSession::applyWholeFileToPad(WavData&& loaded,
                                            std::size_t padIndex) noexcept {
    if (!plan_) return false;
    if (padIndex >= kPadCount) return false;
    if (!loaded.view().valid()) return false;

    // Keeps the previous plan's borrowed PCM pointer and every other pad
    // range; only padIndex is re-spanned to the whole file. The checks above
    // make this unconditionally successful, but the engine reports failure
    // through its return value, so honour it defensively.
    std::optional<PadAssignmentPlan> built =
        assignWholeFileToPad(*plan_, padIndex);
    if (!built) return false;

    plan_ = std::move(built);
    return true;
}
