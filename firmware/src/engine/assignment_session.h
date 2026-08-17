#pragma once

#include "pad_assignment.h"
#include "transient_detector.h"
#include "wav_loader.h"

#include <cstddef>
#include <optional>

// Interface the assignment session calls to silence every playing voice just
// before it publishes a new assignment, so no voice keeps rendering stale pad
// ranges against the new plan. Implemented by the voice layer; the session
// itself stays decoupled from concrete voice types. Engine control path only:
// the call sites are never in the audio callback.
class VoiceStopper {
public:
    virtual ~VoiceStopper() = default;
    virtual void stopAll() = 0;
};

// Control-path object that owns the currently assigned break and its pad
// plan. Portable C++17, no Arduino dependencies, and no allocation in the
// assignment path itself: the moved-in WavData owns the samples on the heap,
// and the plan is fixed-size borrowed metadata. Never call apply* from the
// audio callback.
//
// Lifetime contract
// -----------------
// The published plan borrows the sample buffer of the session's current
// WavData. Moving a WavData moves its vector, and a vector move steals the
// buffer: the address returned by samples.data() is unchanged by the move.
// applyTransient() therefore takes the PcmView of `loaded` BEFORE moving it
// in, validates and builds the candidate plan against that view, and only
// then stops the voices, moves `loaded` into currentWav_, and publishes the
// plan. This ordering guarantees that, after every successful apply* call,
// plan()->pcmView().samples == currentWav()->samples.data().
//
// Every successful apply* call retires the previous plan: any plan pointer or
// copy obtained before the call must not be dereferenced afterwards, because
// the session may have moved a new buffer into place.
//
// Atomicity contract
// ------------------
// Both apply* methods validate everything before mutating anything. A failed
// call returns false and leaves the session's audio and plan untouched, never
// calls the VoiceStopper, and never consumes (moves from) `loaded`; ownership
// of `loaded` then remains with the caller.
class AssignmentSession {
public:
    // Replaces the session's audio with `loaded` and publishes a twelve-pad
    // plan built from `boundaries` over it. Returns false - without touching
    // the session, without calling the stopper, and without consuming
    // `loaded` - when the PCM is invalid or the boundaries cannot cover the
    // file with twelve non-empty ranges.
    bool applyTransient(WavData&& loaded, TransientBoundaries boundaries,
                        VoiceStopper& stopper) noexcept;

    // Re-assigns `padIndex` to span the whole of the session's current audio,
    // preserving the other eleven pads' ranges, via assignWholeFileToPad().
    // Requires an existing plan (from a previous successful applyTransient or
    // applyWholeFileToPad call). Returns false, leaving the session untouched
    // and `loaded` unconsumed, when no plan exists yet, when padIndex is
    // outside [0, kPadCount), or when `loaded` is not a valid PCM view.
    //
    // `loaded` is the caller's prospective audio for the assignment. Its
    // ownership is NOT transferred on this path: the engine's whole-file
    // builder keeps the previous plan's borrowed PCM pointer, so a rebuilt
    // plan can only reference the audio buffer the session already owns.
    // Callers re-assign the audio itself with applyTransient().
    bool applyWholeFileToPad(WavData&& loaded,
                             std::size_t padIndex) noexcept;

    // Always non-null; invalid() (empty, no samples) until the first
    // successful apply* call.
    const WavData* currentWav() const noexcept { return &currentWav_; }

    // The published plan, or nullptr before the first successful apply* call.
    const PadAssignmentPlan* plan() const noexcept {
        return plan_ ? &*plan_ : nullptr;
    }

private:
    WavData currentWav_;
    std::optional<PadAssignmentPlan> plan_;
};
