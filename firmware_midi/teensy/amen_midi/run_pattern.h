#pragma once

#include "diatonic_scales.h"
#include "harmony_recipes.h"

#include <array>
#include <cstdint>

namespace amen {

enum class RampShape : uint8_t {
    Rise,
    RiseHold,
    Swell,
    Fall
};

static constexpr uint8_t kRampShapeCount = 4;
static constexpr uint8_t kNeutralVelocity = 100;

constexpr const char* rampShapeName(RampShape shape) noexcept {
    switch (shape) {
        case RampShape::Rise: return "RISE";
        case RampShape::RiseHold: return "R-HOLD";
        case RampShape::Swell: return "SWELL";
        case RampShape::Fall: return "FALL";
    }
    return "";
}

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
    KawaiiMin6Pulse,
    NoirPulse,
    NoirTrill,
    NoirBuild,
    NoirCrawl,
    NoirStab,
    NoirLurch,
    NoirMarch,
    NoirChime
};

static constexpr uint8_t kRunShapeCount = 44;
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
    const uint8_t* stepVelocities{};
    uint8_t gateSixteenths{4};
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

static constexpr int8_t kNoirPulseDegrees[]{0, 0, 0, 0, 0, 0, 0, 0};
static constexpr uint8_t kNoirPulseVelocities[]{127, 86, 110, 86, 127, 86, 110, 86};
static constexpr int8_t kNoirTrillDegrees[]{0, 1, 0, 1, 0, 1, 0, 1};
static constexpr uint8_t kNoirTrillVelocities[]{70, 100, 74, 104, 78, 108, 82, 112};
static constexpr int8_t kNoirBuildDegrees[]{0, 1, 2, 3, 4, 5, 6, 7};
static constexpr uint8_t kNoirBuildVelocities[]{40, 52, 64, 76, 88, 100, 112, 124};
static constexpr int8_t kNoirCrawlDegrees[]{0, -1, -2, -3, -4, -5, -6, -7};
static constexpr uint8_t kNoirCrawlVelocities[]{110, 100, 92, 86, 80, 74, 68, 62};
static constexpr PolyPatternStep kNoirStabSteps[]{
    {3, {{0, 2, 4}}}, {0, {}}, {3, {{0, 2, 4}}}, {0, {}},
    {0, {}}, {3, {{0, 2, 4}}}, {0, {}}, {0, {}}};
static constexpr uint8_t kNoirStabVelocities[]{127, 0, 110, 0, 0, 86, 0, 0};
static constexpr PolyPatternStep kNoirLurchSteps[]{
    {1, {{0}}}, {0, {}}, {0, {}}, {1, {{0}}}, {0, {}}, {0, {}}, {1, {{0}}}, {0, {}}};
static constexpr uint8_t kNoirLurchVelocities[]{127, 0, 0, 96, 0, 0, 112, 0};
static constexpr int8_t kNoirMarchDegrees[]{0, 4, 2, 4};
static constexpr uint8_t kNoirMarchVelocities[]{127, 72, 88, 72};
static constexpr PolyPatternStep kNoirChimeSteps[]{
    {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {0, {}}, {0, {}},
    {5, {{0, 1, 2, 3, 4}}}, {0, {}}, {0, {}}, {0, {}}};
static constexpr uint8_t kNoirChimeVelocities[]{70, 0, 0, 0, 88, 0, 0, 0};

static constexpr std::array<RunPatternDefinition, kRunShapeCount> kRunShapes{{
    {"RUN UP", kRunUpDegrees, 8},
    {"RUN DOWN", kRunDownDegrees, 8},
    {"UP DOWN", kUpDownDegrees, 14},
    {"DOWN UP", kDownUpDegrees, 14},
    {"THIRDS UP", kThirdsUpDegrees, 12},
    {"THIRDS DN", kThirdsDownDegrees, 12},
    {"ARP UP", kArpUpDegrees, 4},
    {"ARP DOWN", kArpDownDegrees, 4},
    {"REPEAT", kRepeatDegrees, 1, nullptr, 0, nullptr, nullptr, 3},
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
    {"N PULSE", kNoirPulseDegrees, 8, nullptr, 0, nullptr, kNoirPulseVelocities, 3},
    {"N TRILL", kNoirTrillDegrees, 8, nullptr, 0, nullptr, kNoirTrillVelocities, 3},
    {"N 16 BUILD", kNoirBuildDegrees, 8, nullptr, 0, nullptr, kNoirBuildVelocities, 2},
    {"N CRAWL", kNoirCrawlDegrees, 8, nullptr, 0, nullptr, kNoirCrawlVelocities, 4},
    {"N STAB", nullptr, 0, kNoirStabSteps, 8, &kChordRecipes[33], kNoirStabVelocities, 1},
    {"N LURCH", nullptr, 0, kNoirLurchSteps, 8, nullptr, kNoirLurchVelocities, 2},
    {"N MARCH", kNoirMarchDegrees, 4, nullptr, 0, nullptr, kNoirMarchVelocities, 3},
    {"N CHIME", nullptr, 0, kNoirChimeSteps, 8, &kChordRecipes[20], kNoirChimeVelocities, 2},
}};

static constexpr uint8_t kPatternBankCount = 6;
static constexpr std::array<uint8_t, kPatternBankCount> kPatternBankFirst{{0, 12, 20, 28, 0, 36}};
static constexpr std::array<uint8_t, kPatternBankCount> kPatternBankShapeCount{{12, 8, 8, 8, 1, 8}};

constexpr const char* runShapeName(RunShape shape) noexcept {
    if (static_cast<uint8_t>(shape) >= kRunShapeCount) return "";
    return kRunShapes[static_cast<uint8_t>(shape)].name;
}

inline int16_t signedScaleDegreeOffset(DiatonicMode mode, int degree) noexcept {
    const int steps = static_cast<int>(scaleStepCount(mode));
    int octave = degree / steps;
    int step = degree % steps;
    if (step < 0) {
        step += steps;
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
        stepVelocities_.fill(100);
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
        if (definition.stepVelocities != nullptr)
            for (uint8_t step = 0; step < count_; ++step)
                stepVelocities_[step] = definition.stepVelocities[step];
        gateSixteenths_ = definition.gateSixteenths < 1 ? 1
            : (definition.gateSixteenths > 4 ? 4 : definition.gateSixteenths);
        stepStartedAt_ = now;
        stepDurationUs_ = stepDurationUs;
        rampSteps_ = 0;
        active_ = count_ != 0 && stepDurationUs_ != 0;
        sounding_ = active_ && voiceCounts_[0] != 0;
        index_ = 0;
    }

    void tick(uint32_t now) noexcept {
        if (!active_) return;
        const uint32_t elapsed = now - stepStartedAt_;
        const uint32_t elapsedSteps = elapsed / stepDurationUs_;
        if (elapsedSteps != 0) {
            if (shape_ != RunShape::Repeat)
                index_ = static_cast<uint8_t>((index_ + elapsedSteps % count_) % count_);
            stepStartedAt_ += elapsedSteps * stepDurationUs_;
            rampSteps_ += elapsedSteps;
        }
        sounding_ = voiceCounts_[index_] != 0 && (now - stepStartedAt_) < gateDurationUs();
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
    uint8_t stepVelocity() const noexcept { return stepVelocities_[index_]; }

    void setRamp(RampShape shape, uint8_t depth, uint8_t lengthSteps) noexcept {
        rampShape_ = shape;
        rampDepth_ = depth > 127 ? 127 : depth;
        rampLength_ = lengthSteps == 0 ? 1 : lengthSteps;
    }

    uint8_t currentVelocity() const noexcept {
        const int32_t value = static_cast<int32_t>(rampValue()) +
            (static_cast<int32_t>(stepVelocities_[index_]) - static_cast<int32_t>(kNeutralVelocity));
        if (value < 1) return 1;
        if (value > 127) return 127;
        return static_cast<uint8_t>(value);
    }

    uint8_t rampValue() const noexcept {
        if (rampDepth_ == 0) return kNeutralVelocity;
        const uint8_t floorVelocity = static_cast<uint8_t>(127 - rampDepth_);
        const uint8_t span = static_cast<uint8_t>(127 - floorVelocity);
        const uint32_t cycle = rampSteps_ % rampLength_;
        const uint32_t linear = cycle * 255U / rampLength_;
        const uint32_t monotonic = (rampSteps_ < rampLength_ ? rampSteps_ : rampLength_) * 255U / rampLength_;
        uint32_t progress = linear;
        switch (rampShape_) {
            case RampShape::Rise: break;
            case RampShape::RiseHold: progress = monotonic; break;
            case RampShape::Swell: progress = linear < 128U ? linear * 2U : (255U - linear) * 2U; break;
            case RampShape::Fall: progress = 255U - linear; break;
        }
        return static_cast<uint8_t>(floorVelocity + span * progress / 255U);
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
        return stepDurationUs_ * gateSixteenths_ / 4U;
    }

    std::array<std::array<int16_t, kMaxPatternVoices>, kMaxRunSteps> notes_{};
    std::array<uint8_t, kMaxRunSteps> voiceCounts_{};
    std::array<uint8_t, kMaxRunSteps> stepVelocities_{};
    uint32_t stepStartedAt_{};
    uint32_t stepDurationUs_{};
    RunShape shape_{RunShape::RunUp};
    uint8_t count_{};
    uint8_t index_{};
    uint8_t gateSixteenths_{4};
    RampShape rampShape_{RampShape::Rise};
    uint8_t rampDepth_{};
    uint8_t rampLength_{4};
    uint32_t rampSteps_{};
    bool active_{};
    bool sounding_{};
};

}
