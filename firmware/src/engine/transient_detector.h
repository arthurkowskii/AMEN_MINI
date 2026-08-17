#pragma once

#include "pcm_view.h"

#include <array>
#include <cstddef>
#include <optional>

constexpr std::size_t kTransientBoundaryCount = 13;
using TransientBoundaries = std::array<std::size_t, kTransientBoundaryCount>;

// Deterministically finds eleven internal slice boundaries in borrowed PCM.
//
// V1 analyzes 5 ms windows at a 2.5 ms hop (integer frame rounding, minimum one
// frame), with a 20 ms minimum attack separation. Windows are capped at 4096
// frames and candidates at 256, so working storage is fixed and independent of
// file length. Stereo is folded to the signed channel sample having the largest
// magnitude (left wins ties), avoiding anti-phase cancellation; int32_t/int64_t
// intermediates make INT16_MIN safe.
//
// Returns no value for invalid PCM, channel counts other than mono/stereo,
// non-integral interleaving, or fewer than twelve frames. Otherwise returns
// exactly [0, eleven strictly increasing internals, frameCount]. Weak/missing
// attacks are completed by repeatedly bisecting the longest interval.
// Control-path builder: never call from the audio callback.
std::optional<TransientBoundaries> detectTransientBoundaries(PcmView pcm) noexcept;
