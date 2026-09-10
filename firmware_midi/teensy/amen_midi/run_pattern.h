#pragma once

#include "diatonic_scales.h"
#include "harmony_recipes.h"

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
    ArpDown,
    Repeat,
    Arp7,
    Arp9,
    Arp69,
    FutureLift,
    FutureBounce,
    FutureWide,
    FutureSeven,
    FutureNine,
    FutureSparkle,
    FutureRise,
    FutureFall,
    FuturePulse,
    FutureOffbeat,
    FutureDouble,
    FuturePush,
    FutureSync,
    FutureHold,
    FutureSevenChop,
    FutureNineChop,
    KawaiiAdd9Arp,
    KawaiiSevenBounce,
    KawaiiNineChop,
    KawaiiSixNinePulse,
    KawaiiDom7Arp,
    KawaiiDomFlatNineChop,
    KawaiiMin9Arp,
    KawaiiMin6Pulse
};

static constexpr uint8_t kRunShapeCount = 36;
static constexpr uint8_t kMaxPatternVoices = 5;

struct PolyPatternStep {
    uint8_t voiceCount;
    std::array<int8_t, kMaxPatternVoices> degrees;
};

struct RunPatternDefinition {
    const char* name;
    const int8_t* degrees;
    uint8_t degreeCount;
    const PolyPatternStep* steps{};
    uint8_t stepCount{};
    const ChordRecipe* chordRecipe{};
};

static constexpr int8_t kRunUpDegrees[]{0, 1, 2, 3, 4, 5, 6, 7};
static constexpr int8_t kRunDownDegrees[]{0, -1, -2, -3, -4, -5, -6, -7};
static constexpr int8_t kUpDownDegrees[]{0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1};
static constexpr int8_t kDownUpDegrees[]{0, -1, -2, -3, -4, -5, -6, -7, -6, -5, -4, -3, -2, -1};
static constexpr int8_t kThirdsUpDegrees[]{0, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7};
static constexpr int8_t kThirdsDownDegrees[]{0, -2, -1, -3, -2, -4, -3, -5, -4, -6, -5, -7};
static constexpr int8_t kArpUpDegrees[]{0, 2, 4, 7};
static constexpr int8_t kArpDownDegrees[]{0, -3, -5, -7};
static constexpr int8_t kRepeatDegrees[]{0};
static constexpr int8_t kArp7Degrees[]{0, 2, 4, 6};
static constexpr int8_t kArp9Degrees[]{0, 2, 4, 6, 8};
static constexpr int8_t kArp69Degrees[]{0, 2, 4, 5, 8};
static constexpr int8_t kFutureLiftDegrees[]{0, 2, 4, 8, 7, 4, 2, 4};
static constexpr int8_t kFutureBounceDegrees[]{0, 4, 2, 4, 7, 4, 2, 4};
static constexpr int8_t kFutureWideDegrees[]{0, 7, 2, 9, 4, 11, 2, 9};
static constexpr int8_t kFutureSevenDegrees[]{0, 2, 4, 6, 7, 6, 4, 2};
static constexpr int8_t kFutureNineDegrees[]{0, 4, 8, 6, 4, 2, 4, 6};
static constexpr int8_t kFutureSparkleDegrees[]{0, 4, 8, 11, 8, 4, 2, 4};
static constexpr int8_t kFutureRiseDegrees[]{0, 2, 7, 4, 9, 7, 11, 14};
static constexpr int8_t kFutureFallDegrees[]{14, 11, 7, 9, 4, 7, 2, 0};

static constexpr PolyPatternStep kFuturePulseSteps[]{
    {3, {{0, 2, 4}}}, {0, {}}, {3, {{0, 2, 4}}}, {0, {}}};
static constexpr PolyPatternStep kFutureOffbeatSteps[]{
    {0, {}}, {3, {{0, 2, 4}}}, {0, {}}, {3, {{0, 2, 4}}}};
static constexpr PolyPatternStep kFutureDoubleSteps[]{
    {3, {{0, 2, 4}}}, {0, {}}, {3, {{0, 2, 4}}}, {0, {}}, {0, {}}, {3, {{0, 2, 4}}}, {0, {}}, {0, {}}};
static constexpr PolyPatternStep kFuturePushSteps[]{
    {4, {{0, 2, 4, 8}}}, {0, {}}, {0, {}}, {4, {{0, 2, 4, 8}}}, {0, {}}, {4, {{0, 2, 4, 8}}}, {0, {}}, {0, {}}};
static constexpr PolyPatternStep kFutureSyncSteps[]{
    {0, {}}, {4, {{0, 2, 4, 6}}}, {0, {}}, {0, {}}, {4, {{0, 2, 4, 6}}}, {0, {}}, {4, {{0, 2, 4, 6}}}, {0, {}}};
static constexpr PolyPatternStep kFutureHoldSteps[]{
    {4, {{0, 2, 4, 8}}}, {4, {{0, 2, 4, 8}}}, {4, {{0, 2, 4, 8}}}, {0, {}}};
static constexpr PolyPatternStep kFutureSevenChopSteps[]{
    {4, {{0, 2, 4, 6}}}, {0, {}}, {0, {}}, {4, {{0, 2, 4, 6}}}, {0, {}}, {0, {}}, {4, {{0, 2, 4, 6}}}, {0, {}}};
static constexpr PolyPatternStep kFutureNineChopSteps[]{
    {5, {{0, 2, 4, 6, 8}}}, {0, {}}, {5, {{0, 2, 4, 6, 8}}}, {0, {}},
    {0, {}}, {5, {{0, 2, 4, 6, 8}}}, {0, {}}, {5, {{0, 2, 4, 6, 8}}}};

static constexpr PolyPatternStep kKawaiiAdd9ArpSteps[]{
    {1, {{0}}}, {1, {{1}}}, {1, {{2}}}, {1, {{3}}}, {1, {{2}}}, {1, {{1}}}};
static constexpr PolyPatternStep kKawaiiSevenBounceSteps[]{
    {1, {{0}}}, {1, {{2}}}, {1, {{1}}}, {1, {{2}}}, {1, {{3}}}, {1, {{2}}}, {1, {{1}}}, {1, {{2}}}};
static constexpr PolyPatternStep kKawaiiNineChopSteps[]{
    {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {5, {{0, 1, 2, 3, 4}}}, {0, {}},
    {0, {}}, {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {0, {}}};
static constexpr PolyPatternStep kKawaiiSixNinePulseSteps[]{
    {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {0, {}}, {5, {{0, 1, 2, 3, 4}}},
    {0, {}}, {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {0, {}}};
static constexpr PolyPatternStep kKawaiiDom7ArpSteps[]{
    {1, {{0}}}, {1, {{1}}}, {1, {{2}}}, {1, {{3}}}, {1, {{2}}}, {1, {{1}}}};
static constexpr PolyPatternStep kKawaiiDomFlatNineChopSteps[]{
    {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {0, {}}, {5, {{0, 1, 2, 3, 4}}},
    {0, {}}, {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {0, {}}};
static constexpr PolyPatternStep kKawaiiMin9ArpSteps[]{
    {1, {{0}}}, {1, {{1}}}, {1, {{2}}}, {1, {{3}}}, {1, {{4}}}, {1, {{3}}}, {1, {{2}}}, {1, {{1}}}};
static constexpr PolyPatternStep kKawaiiMin6PulseSteps[]{
    {4, {{0, 1, 2, 3}}}, {0, {}}, {4, {{0, 1, 2, 3}}}, {0, {}},
    {0, {}}, {4, {{0, 1, 2, 3}}}, {0, {}}, {0, {}}};

static constexpr std::array<RunPatternDefinition, kRunShapeCount> kRunShapes{{
    {"RUN UP", kRunUpDegrees, 8},
    {"RUN DOWN", kRunDownDegrees, 8},
    {"UP DOWN", kUpDownDegrees, 14},
    {"DOWN UP", kDownUpDegrees, 14},
    {"THIRDS UP", kThirdsUpDegrees, 12},
    {"THIRDS DN", kThirdsDownDegrees, 12},
    {"ARP UP", kArpUpDegrees, 4},
    {"ARP DOWN", kArpDownDegrees, 4},
    {"REPEAT", kRepeatDegrees, 1},
    {"ARP 7", kArp7Degrees, 4},
    {"ARP 9", kArp9Degrees, 5},
    {"ARP 69", kArp69Degrees, 5},
    {"FUT LIFT", kFutureLiftDegrees, 8},
    {"FUT BOUNCE", kFutureBounceDegrees, 8},
    {"FUT WIDE", kFutureWideDegrees, 8},
    {"FUT 7", kFutureSevenDegrees, 8},
    {"FUT 9", kFutureNineDegrees, 8},
    {"FUT SPARK", kFutureSparkleDegrees, 8},
    {"FUT RISE", kFutureRiseDegrees, 8},
    {"FUT FALL", kFutureFallDegrees, 8},
    {"FUT PULSE", nullptr, 0, kFuturePulseSteps, 4},
    {"FUT OFF", nullptr, 0, kFutureOffbeatSteps, 4},
    {"FUT DOUBLE", nullptr, 0, kFutureDoubleSteps, 8},
    {"FUT PUSH", nullptr, 0, kFuturePushSteps, 8},
    {"FUT SYNC", nullptr, 0, kFutureSyncSteps, 8},
    {"FUT HOLD", nullptr, 0, kFutureHoldSteps, 4},
    {"FUT 7CHOP", nullptr, 0, kFutureSevenChopSteps, 8},
    {"FUT 9CHOP", nullptr, 0, kFutureNineChopSteps, 8},
    {"K ADD9", nullptr, 0, kKawaiiAdd9ArpSteps, 6, &kChordRecipes[3]},
    {"K 7 BOUNCE", nullptr, 0, kKawaiiSevenBounceSteps, 8, &kChordRecipes[1]},
    {"K 9 CHOP", nullptr, 0, kKawaiiNineChopSteps, 8, &kChordRecipes[2]},
    {"K 69 PULSE", nullptr, 0, kKawaiiSixNinePulseSteps, 8, &kChordRecipes[7]},
    {"K DOM7", nullptr, 0, kKawaiiDom7ArpSteps, 6, &kChordRecipes[27]},
    {"K 7b9", nullptr, 0, kKawaiiDomFlatNineChopSteps, 8, &kChordRecipes[28]},
    {"K MIN9", nullptr, 0, kKawaiiMin9ArpSteps, 8, &kChordRecipes[29]},
    {"K MIN6", nullptr, 0, kKawaiiMin6PulseSteps, 8, &kChordRecipes[30]},
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
    static constexpr uint8_t kMaxRunSteps = 15;

    void start(int16_t baseNote, DiatonicMode scale, int sourceDegree, RunShape shape,
               uint32_t stepDurationUs, uint32_t now) noexcept {
        const RunPatternDefinition& definition = kRunShapes[static_cast<uint8_t>(shape)];
        shape_ = shape;
        const int base = baseNote - signedScaleDegreeOffset(scale, sourceDegree);
        voiceCounts_.fill(0);
        count_ = 0;
        if (definition.steps == nullptr) {
            for (uint8_t step = 0; step < definition.degreeCount; ++step) {
                const int note = base + signedScaleDegreeOffset(scale, sourceDegree + definition.degrees[step]);
                if (note < 0 || note > 127) break;
                notes_[count_][0] = static_cast<int16_t>(note);
                voiceCounts_[count_++] = 1;
            }
        } else {
            count_ = definition.stepCount;
            for (uint8_t step = 0; step < count_; ++step) {
                for (uint8_t voice = 0; voice < definition.steps[step].voiceCount; ++voice) {
                    const int value = definition.steps[step].degrees[voice];
                    const int note = definition.chordRecipe == nullptr
                        ? base + signedScaleDegreeOffset(scale, sourceDegree + value)
                        : recipeNote(baseNote, scale, sourceDegree, *definition.chordRecipe,
                                     static_cast<uint8_t>(value));
                    if (note < 0 || note > 127) continue;
                    notes_[step][voiceCounts_[step]++] = static_cast<int16_t>(note);
                }
            }
        }
        stepStartedAt_ = now;
        stepDurationUs_ = stepDurationUs;
        active_ = count_ != 0 && stepDurationUs_ != 0;
        sounding_ = active_ && voiceCounts_[0] != 0;
        index_ = 0;
    }

    void tick(uint32_t now) noexcept {
        if (!active_) return;
        const uint32_t elapsed = now - stepStartedAt_;
        if (shape_ == RunShape::Repeat) {
            const uint32_t elapsedSteps = elapsed / stepDurationUs_;
            stepStartedAt_ += elapsedSteps * stepDurationUs_;
            sounding_ = now - stepStartedAt_ < gateDurationUs();
            return;
        }
        const uint32_t elapsedSteps = elapsed / stepDurationUs_;
        if (elapsedSteps == 0) return;
        index_ = static_cast<uint8_t>((index_ + elapsedSteps % count_) % count_);
        stepStartedAt_ += elapsedSteps * stepDurationUs_;
        sounding_ = voiceCounts_[index_] != 0;
    }

    void setStepDuration(uint32_t stepDurationUs, uint32_t now) noexcept {
        if (stepDurationUs == 0 || stepDurationUs == stepDurationUs_) return;
        if (active_) {
            const uint32_t elapsed = (now - stepStartedAt_) % stepDurationUs_;
            const uint32_t scaledElapsed = static_cast<uint32_t>(
                static_cast<uint64_t>(elapsed) * stepDurationUs / stepDurationUs_);
            stepStartedAt_ = now - scaledElapsed;
        }
        stepDurationUs_ = stepDurationUs;
    }

    void cancel() noexcept {
        active_ = false;
        sounding_ = false;
    }
    bool active() const noexcept { return active_; }
    RunShape shape() const noexcept { return shape_; }
    int16_t note() const noexcept { return soundingCount() == 0 ? -1 : notes_[index_][0]; }
    uint8_t count() const noexcept { return count_; }
    int16_t noteAt(uint8_t index) const noexcept { return index < count_ && voiceCounts_[index] != 0 ? notes_[index][0] : -1; }
    uint8_t soundingCount() const noexcept { return active_ && sounding_ ? voiceCounts_[index_] : 0; }
    int16_t soundingNote(uint8_t voice) const noexcept {
        return voice < soundingCount() ? notes_[index_][voice] : -1;
    }

private:
    static int recipeNote(int16_t baseNote, DiatonicMode scale, int sourceDegree,
                          const ChordRecipe& recipe, uint8_t voice) noexcept {
        if (voice >= recipe.voiceCount) return -1;
        const int offset = recipe.chromaticIntervals
            ? recipe.degrees[voice]
            : signedScaleDegreeOffset(scale, sourceDegree + recipe.degrees[voice]) -
              signedScaleDegreeOffset(scale, sourceDegree);
        return baseNote + offset + recipe.octaveDisplacements[voice];
    }

    uint32_t gateDurationUs() const noexcept {
        return stepDurationUs_ * 3U / 4U;
    }

    std::array<std::array<int16_t, kMaxPatternVoices>, kMaxRunSteps> notes_{};
    std::array<uint8_t, kMaxRunSteps> voiceCounts_{};
    uint32_t stepStartedAt_{};
    uint32_t stepDurationUs_{};
    RunShape shape_{RunShape::RunUp};
    uint8_t count_{};
    uint8_t index_{};
    bool active_{};
    bool sounding_{};
};

}
