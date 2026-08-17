#include "transient_detector.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

constexpr std::size_t kPadCount = kTransientBoundaryCount - 1U;
constexpr std::size_t kInternalBoundaryCount = kPadCount - 1U;
constexpr std::size_t kMaxWindowFrames = 4096;
constexpr std::size_t kMaxCandidates = 256;
constexpr std::size_t kThresholdHistory = 16;

struct Candidate {
    std::size_t frame = 0;
    uint64_t strength = 0;
};

std::size_t framesForMilliseconds(uint32_t sampleRate,
                                  uint32_t milliseconds) noexcept {
    const uint64_t frames =
        (static_cast<uint64_t>(sampleRate) * milliseconds + 999U) / 1000U;
    const uint64_t sizeMax = static_cast<uint64_t>(
        std::numeric_limits<std::size_t>::max());
    return static_cast<std::size_t>(std::min(frames, sizeMax));
}

int32_t monoSample(PcmView pcm, std::size_t frame) noexcept {
    const std::size_t base = frame * static_cast<std::size_t>(pcm.channels);
    int32_t selected = pcm.samples[base];
    int32_t selectedMagnitude = selected < 0 ? -selected : selected;
    for (std::size_t channel = 1; channel < pcm.channels; ++channel) {
        const int32_t value = pcm.samples[base + channel];
        const int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > selectedMagnitude) {
            selected = value;
            selectedMagnitude = magnitude;
        }
    }
    return selected;
}

uint64_t sampleEnergy(PcmView pcm, std::size_t frame) noexcept {
    const int64_t sample = monoSample(pcm, frame);
    return static_cast<uint64_t>(sample * sample);
}

bool stronger(const Candidate& lhs, const Candidate& rhs) noexcept {
    return lhs.strength > rhs.strength ||
           (lhs.strength == rhs.strength && lhs.frame < rhs.frame);
}

void retainCandidate(std::array<Candidate, kMaxCandidates>& candidates,
                     std::size_t& candidateCount,
                     Candidate incoming,
                     std::size_t minimumSeparation) noexcept {
    for (std::size_t i = 0; i < candidateCount; ++i) {
        const std::size_t distance = candidates[i].frame > incoming.frame
                                         ? candidates[i].frame - incoming.frame
                                         : incoming.frame - candidates[i].frame;
        if (distance < minimumSeparation) {
            if (stronger(incoming, candidates[i])) candidates[i] = incoming;
            return;
        }
    }

    if (candidateCount < candidates.size()) {
        candidates[candidateCount] = incoming;
        ++candidateCount;
        return;
    }

    std::size_t weakest = 0;
    for (std::size_t i = 1; i < candidateCount; ++i) {
        if (stronger(candidates[weakest], candidates[i])) weakest = i;
    }
    if (stronger(incoming, candidates[weakest])) candidates[weakest] = incoming;
}

void collectCandidates(PcmView pcm,
                       std::array<Candidate, kMaxCandidates>& candidates,
                       std::size_t& candidateCount) noexcept {
    const std::size_t frameCount = pcm.frameCount();
    const std::size_t requestedWindow = framesForMilliseconds(pcm.sampleRate, 5U);
    const std::size_t window = std::max<std::size_t>(
        1U, std::min(requestedWindow, kMaxWindowFrames));
    if (frameCount < window) return;
    const std::size_t hop = std::max<std::size_t>(1U, window / 2U);
    const std::size_t minimumSeparation = std::max<std::size_t>(
        1U, framesForMilliseconds(pcm.sampleRate, 20U));

    uint64_t energy = 0;
    for (std::size_t frame = 0; frame < window; ++frame) {
        energy += sampleEnergy(pcm, frame);
    }
    uint64_t previousEnergy = energy / window;
    std::array<uint64_t, kThresholdHistory> history{};
    std::size_t historyCount = 0;
    std::size_t historyIndex = 0;
    uint64_t historySum = 0;

    std::size_t windowStart = 0;
    while (windowStart <= frameCount - window &&
           hop <= frameCount - window - windowStart) {
        const std::size_t nextStart = windowStart + hop;
        for (std::size_t offset = 0; offset < hop; ++offset) {
            energy -= sampleEnergy(pcm, windowStart + offset);
            energy += sampleEnergy(pcm, windowStart + window + offset);
        }
        windowStart = nextStart;
        const uint64_t meanEnergy = energy / window;
        const uint64_t onset =
            meanEnergy > previousEnergy ? meanEnergy - previousEnergy : 0U;
        const uint64_t localMean = historyCount == 0U ? 0U : historySum / historyCount;
        const uint64_t threshold = localMean + localMean / 2U;
        if (onset > threshold && onset > 0U) {
            const std::size_t estimatedAttack = std::min(windowStart + window, frameCount - 1U);
            if (estimatedAttack > 0U) {
                retainCandidate(candidates, candidateCount,
                                {estimatedAttack, onset}, minimumSeparation);
            }
        }

        if (historyCount < history.size()) {
            history[historyIndex] = onset;
            historySum += onset;
            ++historyCount;
        } else {
            historySum -= history[historyIndex];
            history[historyIndex] = onset;
            historySum += onset;
        }
        historyIndex = (historyIndex + 1U) % history.size();
        previousEnergy = meanEnergy;
    }
}

void insertSorted(TransientBoundaries& boundaries,
                  std::size_t& count,
                  std::size_t boundary) noexcept {
    std::size_t position = count;
    while (position > 0U && boundaries[position - 1U] > boundary) {
        boundaries[position] = boundaries[position - 1U];
        --position;
    }
    boundaries[position] = boundary;
    ++count;
}

void fillLongestIntervals(TransientBoundaries& boundaries,
                          std::size_t& count) noexcept {
    while (count < boundaries.size()) {
        std::size_t longestIndex = 0;
        std::size_t longestLength = 0;
        for (std::size_t i = 0; i + 1U < count; ++i) {
            const std::size_t length = boundaries[i + 1U] - boundaries[i];
            if (length > longestLength) {
                longestLength = length;
                longestIndex = i;
            }
        }
        const std::size_t midpoint =
            boundaries[longestIndex] + longestLength / 2U;
        insertSorted(boundaries, count, midpoint);
    }
}

}  // namespace

std::optional<TransientBoundaries> detectTransientBoundaries(PcmView pcm) noexcept {
    if (!pcm.valid() || (pcm.channels != 1U && pcm.channels != 2U) ||
        pcm.sampleCount % pcm.channels != 0U || pcm.frameCount() < kPadCount) {
        return std::nullopt;
    }

    std::array<Candidate, kMaxCandidates> candidates{};
    std::size_t candidateCount = 0;
    collectCandidates(pcm, candidates, candidateCount);
    std::sort(candidates.begin(), candidates.begin() + candidateCount, stronger);

    TransientBoundaries boundaries{};
    boundaries[0] = 0U;
    boundaries[1] = pcm.frameCount();
    std::size_t boundaryCount = 2;
    const std::size_t selectedCount =
        std::min(candidateCount, kInternalBoundaryCount);
    for (std::size_t i = 0; i < selectedCount; ++i) {
        insertSorted(boundaries, boundaryCount, candidates[i].frame);
    }
    fillLongestIntervals(boundaries, boundaryCount);
    return boundaries;
}
