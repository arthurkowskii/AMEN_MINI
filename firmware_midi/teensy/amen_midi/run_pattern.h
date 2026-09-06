#pragma once

#include "diatonic_scales.h"

#include <array>
#include <cstdint>

namespace amen {

enum class RunShape : uint8_t {
    RunUp,
    RunDown,
    UpDown,
    DownUp,
    ThirdsUp,
    ThirdsDown,
    ArpUp,
    ArpDown
};

static constexpr uint8_t kRunShapeCount = 8;

struct RunPatternDefinition {
    const char* name;
    const int8_t* degrees;
    uint8_t degreeCount;
};

static constexpr int8_t kRunUpDegrees[]{0, 1, 2, 3, 4, 5, 6, 7};
static constexpr int8_t kRunDownDegrees[]{0, -1, -2, -3, -4, -5, -6, -7};
static constexpr int8_t kUpDownDegrees[]{0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0};
static constexpr int8_t kDownUpDegrees[]{0, -1, -2, -3, -4, -5, -6, -7, -6, -5, -4, -3, -2, -1, 0};
static constexpr int8_t kThirdsUpDegrees[]{0, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7};
static constexpr int8_t kThirdsDownDegrees[]{0, -2, -1, -3, -2, -4, -3, -5, -4, -6, -5, -7};
static constexpr int8_t kArpUpDegrees[]{0, 2, 4, 7};
static constexpr int8_t kArpDownDegrees[]{0, -3, -5, -7};

static constexpr std::array<RunPatternDefinition, kRunShapeCount> kRunShapes{{
    {"RUN UP", kRunUpDegrees, 8},
    {"RUN DOWN", kRunDownDegrees, 8},
    {"UP DOWN", kUpDownDegrees, 15},
    {"DOWN UP", kDownUpDegrees, 15},
    {"THIRDS UP", kThirdsUpDegrees, 12},
    {"THIRDS DN", kThirdsDownDegrees, 12},
    {"ARP UP", kArpUpDegrees, 4},
    {"ARP DOWN", kArpDownDegrees, 4},
}};

constexpr const char* runShapeName(RunShape shape) noexcept {
    if (static_cast<uint8_t>(shape) >= kRunShapeCount) return "";
    return kRunShapes[static_cast<uint8_t>(shape)].name;
}

inline int16_t signedScaleDegreeOffset(DiatonicMode mode, int degree) noexcept {
    if (mode == DiatonicMode::Chromatic) return static_cast<int16_t>(degree);
    int octave = degree / 7;
    int step = degree % 7;
    if (step < 0) {
        step += 7;
        --octave;
    }
    return static_cast<int16_t>(12 * octave + scaleDegreeOffset(mode, static_cast<uint8_t>(step)));
}

class RunPattern {
public:
    static constexpr uint8_t kMaxRunNotes = 15;

    void start(int16_t baseNote, DiatonicMode scale, int sourceDegree, RunShape shape,
               uint16_t stepMs, uint32_t now) noexcept {
        const RunPatternDefinition& definition = kRunShapes[static_cast<uint8_t>(shape)];
        shape_ = shape;
        const int base = baseNote - signedScaleDegreeOffset(scale, sourceDegree);
        count_ = 0;
        for (uint8_t step = 0; step < definition.degreeCount; ++step) {
            const int note = base + signedScaleDegreeOffset(scale, sourceDegree + definition.degrees[step]);
            if (note < 0 || note > 127) break;
            notes_[count_++] = static_cast<int16_t>(note);
        }
        startedAt_ = now;
        stepMs_ = stepMs;
        finalOff_ = now + static_cast<uint32_t>(count_) * stepMs_;
        active_ = count_ != 0 && stepMs_ != 0;
        index_ = 0;
    }

    void tick(uint32_t now) noexcept {
        if (!active_) return;
        const uint32_t elapsed = now - startedAt_;
        if (elapsed >= static_cast<uint32_t>(count_) * stepMs_) active_ = false;
        else index_ = static_cast<uint8_t>(elapsed / stepMs_);
    }

    void cancel() noexcept { active_ = false; }
    bool active() const noexcept { return active_; }
    RunShape shape() const noexcept { return shape_; }
    int16_t note() const noexcept { return active_ ? notes_[index_] : -1; }
    uint8_t count() const noexcept { return count_; }
    int16_t noteAt(uint8_t index) const noexcept { return index < count_ ? notes_[index] : -1; }
    uint32_t finalOff() const noexcept { return finalOff_; }

private:
    std::array<int16_t, kMaxRunNotes> notes_{};
    uint32_t startedAt_{};
    uint32_t finalOff_{};
    uint16_t stepMs_{};
    RunShape shape_{RunShape::RunUp};
    uint8_t count_{};
    uint8_t index_{};
    bool active_{};
};

}
