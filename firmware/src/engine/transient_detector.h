#pragma once

#include "pcm_view.h"

#include <array>
#include <cstddef>
#include <optional>

constexpr std::size_t kTransientBoundaryCount = 13;
using TransientBoundaries = std::array<std::size_t, kTransientBoundaryCount>;

// Absolute salience floor for candidate onsets, expressed in the detector's
// mean-energy units (window energy divided by window frames). Equivalent to
// per-window energy of windowFrames * 128 * 128, i.e. a sustained mono
// amplitude of 128/32768 (~ -48 dBFS). An onset must exceed this fixed floor
// in addition to the adaptive relative threshold, so low-level noise or
// dither (~ -84 dBFS, ~ 2 LSB) can never consume internal boundaries.
constexpr uint64_t kTransientNoiseFloorMeanEnergy = 128U * 128U;

// Deterministically finds eleven internal slice boundaries in borrowed PCM.
//
// V1 analyzes 5 ms windows at a 2.5 ms hop (integer frame rounding, minimum one
// frame), with a 20 ms minimum attack separation. Windows are capped at 4096
// frames and candidates at 256, so working storage is fixed and independent of
// file length (~4.4 KiB of stack for the candidate and history arrays).
// Stereo is folded to the signed channel sample having the largest magnitude
// (left wins ties), avoiding anti-phase cancellation; int32_t/int64_t
// intermediates make INT16_MIN safe. Candidates must exceed both an adaptive
// relative onset threshold and the absolute noise floor above; weak/missing
// attacks are completed by repeatedly bisecting the longest interval.
//
// Returns no value for invalid PCM, channel counts other than mono/stereo,
// non-integral interleaving, or fewer than twelve frames. Otherwise returns
// exactly [0, eleven strictly increasing internals, frameCount].
// Control-path builder: never call from the audio callback.
std::optional<TransientBoundaries> detectTransientBoundaries(PcmView pcm) noexcept;
