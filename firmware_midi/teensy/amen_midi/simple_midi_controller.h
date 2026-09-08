#pragma once

#include "diatonic_scales.h"
#include "musical_presets.h"
#include "run_pattern.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace amen {

enum class MidiCommandType : uint8_t {
    None,
    NoteOn,
    NoteOff,
    ControlChange
};

struct MidiCommand {
    MidiCommandType type{MidiCommandType::None};
    uint8_t note{};
    uint8_t velocity{};
};

enum class PerformancePage : uint8_t {
    Harmony,
    Pattern,
    None
};

enum class ClockMode : uint8_t {
    Tempo,
    Frequency
};

enum class ShiftMode : uint8_t {
    Hold,
    Mod,
    OctaveUp,
    SemitoneUp,
    OctaveDown,
    SemitoneDown
};

enum class PatternBank : uint8_t {
    Orchestral,
    FutureArp,
    FuturePattern,
    Kawaii,
    Repeat
};

enum class RateDivision : uint8_t {
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
    QuarterTriplet,
    EighthTriplet,
    SixteenthTriplet,
    ThirtySecondTriplet
};

struct PatternAssignment {
    RunShape shape;
    RateDivision division;
};

class SimpleMidiController {
public:
    static constexpr uint8_t kKeyCount = 12;
    static constexpr uint8_t kHarmonyStartKey = 12;
    static constexpr uint8_t kHarmonyKeyCount = kHarmonySlotCount;
    static constexpr uint8_t kNoteKeyCount = kKeyCount + kHarmonyKeyCount;
    static constexpr uint8_t kShiftKey = kHarmonyStartKey + kHarmonyKeyCount;
    static constexpr uint8_t kChannel = 1;
    static constexpr uint8_t kVelocity = 100;
    static constexpr uint8_t kMaxChordVoices = kMaxRecipeVoices;
    static constexpr uint8_t kMaxEventsPerAction = 128;
    static constexpr uint8_t kNoRunSource = 255;
    static constexpr uint8_t kDrumChannel = 10;

    static constexpr bool isDrumPreset(MusicalPreset preset) noexcept {
        return preset == MusicalPreset::GmKit;
    }

    uint8_t press(uint8_t key, MidiCommand* out, uint8_t capacity) noexcept {
        return press(key, 0, out, capacity);
    }

    uint8_t press(uint8_t key, uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        return applyAction(key, true, now, out, capacity);
    }

    uint8_t release(uint8_t key, MidiCommand* out, uint8_t capacity) noexcept {
        return applyAction(key, false, 0, out, capacity);
    }

    uint8_t release(uint8_t key, uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        return applyAction(key, false, now, out, capacity);
    }

    uint8_t tick(uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        SimpleMidiController candidate = *this;
        candidate.run_.tick(now);
        candidate.advanceModulation(now);
        if (!candidate.run_.active()) {
            candidate.runSourceKey_ = kNoRunSource;
            candidate.runPatternKey_ = kNoRunSource;
        }
        return commit(candidate, out, capacity);
    }

    uint8_t cancelRun(MidiCommand* out, uint8_t capacity) noexcept {
        SimpleMidiController candidate = *this;
        candidate.run_.cancel();
        candidate.runSourceKey_ = kNoRunSource;
        candidate.runPatternKey_ = kNoRunSource;
        return commit(candidate, out, capacity);
    }

    uint8_t togglePage(MidiCommand* out, uint8_t capacity) noexcept {
        return turnPage(1, out, capacity);
    }

    uint8_t turnPage(int delta, MidiCommand* out, uint8_t capacity) noexcept {
        SimpleMidiController candidate = *this;
        const uint8_t current = static_cast<uint8_t>(candidate.page_);
        candidate.page_ = static_cast<PerformancePage>(wrap(current, delta, 3));
        if (candidate.page_ == page_) return 0;
        if (candidate.page_ != PerformancePage::Pattern) {
            candidate.run_.cancel();
            candidate.runSourceKey_ = kNoRunSource;
            candidate.runPatternKey_ = kNoRunSource;
        }
        return commit(candidate, out, capacity);
    }

    bool nextPatternBank() noexcept {
        if (page_ != PerformancePage::Pattern) return false;
        patternBank_ = static_cast<PatternBank>(wrap(static_cast<uint8_t>(patternBank_), 1, 5));
        return true;
    }

    bool toggleClockMode(uint32_t now) noexcept {
        clockMode_ = clockMode_ == ClockMode::Tempo ? ClockMode::Frequency : ClockMode::Tempo;
        run_.setStepDuration(currentStepDurationUs(), now);
        return true;
    }

    bool turnTempo(int delta, uint32_t now) noexcept {
        const int64_t next = static_cast<int64_t>(tempo_) + delta;
        const uint16_t clamped = next < 20 ? 20 : (next > 300 ? 300 : static_cast<uint16_t>(next));
        if (clamped == tempo_ && clockMode_ == ClockMode::Tempo) return false;
        tempo_ = clamped;
        clockMode_ = ClockMode::Tempo;
        run_.setStepDuration(currentStepDurationUs(), now);
        return true;
    }

    bool turnFrequency(int delta, uint32_t now) noexcept {
        const int64_t next = static_cast<int64_t>(frequencyIndex_) + delta;
        const uint8_t clamped = next < 0 ? 0 : (next > kMaxFrequencyIndex
            ? kMaxFrequencyIndex : static_cast<uint8_t>(next));
        if (clamped == frequencyIndex_ && clockMode_ == ClockMode::Frequency) return false;
        frequencyIndex_ = clamped;
        clockMode_ = ClockMode::Frequency;
        run_.setStepDuration(currentStepDurationUs(), now);
        return true;
    }

    bool turnPattern(int delta) noexcept {
        if (page_ != PerformancePage::Pattern) return false;
        const uint8_t key = heldPatternKey();
        if (key == kNoRunSource) return false;
        const uint8_t slot = key - kHarmonyStartKey;
        PatternAssignment& assignment = patternAssign_[static_cast<uint8_t>(patternBank_)][slot];
        if (patternBank_ == PatternBank::Repeat) {
            const uint8_t current = static_cast<uint8_t>(assignment.division);
            const uint8_t next = wrap(current, delta, 8);
            if (next == current) return false;
            assignment.division = static_cast<RateDivision>(next);
            return true;
        }
        const uint8_t first = patternBank_ == PatternBank::Orchestral ? 0
            : (patternBank_ == PatternBank::FutureArp ? 12
            : (patternBank_ == PatternBank::FuturePattern ? 20 : 28));
        const uint8_t count = patternBank_ == PatternBank::Orchestral ? 12 : 8;
        const uint8_t current = static_cast<uint8_t>(assignment.shape) - first;
        const uint8_t next = wrap(current, delta, count);
        if (next == current) return false;
        assignment.shape = static_cast<RunShape>(first + next);
        return true;
    }

    uint8_t turnShiftMode(int delta, uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        if (out == nullptr || capacity == 0 || delta == 0) return 0;
        SimpleMidiController candidate = *this;
        candidate.advanceModulation(now);
        candidate.setShiftModeActive(false, now);
        candidate.shiftMode_ = static_cast<ShiftMode>(
            wrap(static_cast<uint8_t>(candidate.shiftMode_), delta, 6));
        candidate.setShiftModeActive(candidate.shiftHeld_, now);
        return commit(candidate, out, capacity);
    }

    PerformancePage page() const noexcept { return page_; }
    uint16_t tempo() const noexcept { return tempo_; }
    ClockMode clockMode() const noexcept { return clockMode_; }
    uint16_t frequencyTenths() const noexcept {
        return static_cast<uint16_t>((frequencyHundredths() + 5U) / 10U);
    }
    bool runActive() const noexcept { return run_.active(); }
    int16_t runNote() const noexcept { return run_.note(); }
    RunShape runShape() const noexcept { return run_.shape(); }
    bool patternHeld() const noexcept { return patternCount_ > 0; }
    RunShape currentPattern() const noexcept {
        const uint8_t key = heldPatternKey();
        return currentAssignment(key == kNoRunSource ? 0 : key - kHarmonyStartKey).shape;
    }
    RunShape slotAssignment(uint8_t slot) const noexcept { return currentAssignment(slot).shape; }
    RateDivision slotDivision(uint8_t slot) const noexcept { return currentAssignment(slot).division; }
    PatternBank patternBank() const noexcept { return patternBank_; }
    const char* patternBankName() const noexcept {
        switch (patternBank_) {
            case PatternBank::Orchestral: return "ORCH";
            case PatternBank::FutureArp: return "FUT. ARP";
            case PatternBank::FuturePattern: return "FUT. PATTERN";
            case PatternBank::Kawaii: return "KAWAII";
            case PatternBank::Repeat: return "REPEAT";
        }
        return "";
    }
    const char* currentDivisionName() const noexcept {
        static constexpr const char* names[]{"1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T"};
        const uint8_t key = heldPatternKey();
        return names[static_cast<uint8_t>(currentAssignment(key == kNoRunSource ? 0 : key - kHarmonyStartKey).division)];
    }
    uint8_t patternSlot() const noexcept {
        const uint8_t key = heldPatternKey();
        return key == kNoRunSource ? 0 : key - kHarmonyStartKey;
    }
    uint8_t patternStackSize() const noexcept { return patternCount_; }
    uint8_t runSourceKey() const noexcept { return runSourceKey_; }
    const char* runShapeName() const noexcept { return amen::runShapeName(run_.active() ? run_.shape() : currentPattern()); }

    bool turnOctave(int delta) noexcept {
        if (isDrumPreset(preset_)) return false;
        const int64_t next = static_cast<int64_t>(octave_) + delta;
        const int clamped = next < kMinOctave ? kMinOctave : (next > kMaxOctave ? kMaxOctave : static_cast<int>(next));
        if (clamped == octave_) return false;
        octave_ = static_cast<int8_t>(clamped);
        return true;
    }

    bool turnRoot(int delta) noexcept {
        if (isDrumPreset(preset_)) return false;
        const uint8_t next = wrap(rootPitchClass_, delta, 12);
        if (next == rootPitchClass_) return false;
        rootPitchClass_ = next;
        return true;
    }

    bool turnPreset(int delta) noexcept {
        const uint8_t current = static_cast<uint8_t>(preset_);
        const uint8_t next = wrap(current, delta, kMusicalPresetCount);
        if (next == current) return false;
        preset_ = static_cast<MusicalPreset>(next);
        return true;
    }

    int octave() const noexcept { return octave_; }
    uint8_t octaveNumber() const noexcept { return static_cast<uint8_t>(octave_ - kMinOctave); }
    uint8_t rootPitchClass() const noexcept { return rootPitchClass_; }
    MusicalPreset preset() const noexcept { return preset_; }
    const char* presetName() const noexcept { return musicalPreset(preset_).name; }
    uint8_t midiChannel() const noexcept {
        return isDrumPreset(preset_) ? kDrumChannel : kChannel;
    }
    bool drums() const noexcept { return isDrumPreset(preset_); }
    bool shiftHeld() const noexcept { return shiftHeld_; }
    ShiftMode shiftMode() const noexcept { return shiftMode_; }
    const char* shiftModeName() const noexcept {
        switch (shiftMode_) {
            case ShiftMode::Hold: return "HOLD";
            case ShiftMode::Mod: return "MOD";
            case ShiftMode::OctaveUp: return "+1 OCT";
            case ShiftMode::SemitoneUp: return "+1 ST";
            case ShiftMode::OctaveDown: return "-1 OCT";
            case ShiftMode::SemitoneDown: return "-1 ST";
        }
        return "";
    }
    DiatonicMode scale() const noexcept { return musicalPreset(preset_).scale; }
    bool hasHeldPresetMismatch() const noexcept {
        for (const DegreeState& degree : degrees_)
            if (degree.held && degree.preset != preset_) return true;
        return false;
    }
    uint8_t rootNote() const noexcept { return noteForKey(0); }
    uint8_t highestNote() const noexcept { return noteForKey(kKeyCount - 1); }

    bool harmonyActive() const noexcept { return harmonyCount_ > 0; }
    uint8_t harmonyStackSize() const noexcept { return harmonyCount_; }
    const char* harmonyName() const noexcept {
        if (harmonyCount_ == 0) return "";
        return harmonySlotName(preset_, harmonyStack_[harmonyCount_ - 1] - kHarmonyStartKey);
    }

    uint8_t heldCount() const noexcept { return heldCount_; }
    const char* currentNoteName() const noexcept {
        if (heldCount_ == 0) return "";
        if (isDrumPreset(preset_))
            return gmDrumLabel(degrees_[heldOrder_[heldCount_ - 1]].key);
        return degrees_[heldOrder_[heldCount_ - 1]].spelling.text.data();
    }

private:
    static constexpr int kMinOctave = -5;
    static constexpr int kMaxOctave = 3;
    static constexpr uint8_t kMaxFrequencyIndex = 48;

    enum class PadRole : uint8_t {
        None,
        HarmonyDegree,
        ManualDegree,
        HarmonySlot,
        PatternSlot
    };

    struct DegreeState {
        int16_t rootNote{-1};
        NoteSpelling spelling{};
        DiatonicMode scale{};
        MusicalPreset preset{};
        uint8_t key{};
        int8_t transpose{};
        bool held{};
    };

    static uint8_t wrap(uint8_t current, int delta, uint8_t count) noexcept {
        const int64_t value = static_cast<int64_t>(current) + delta;
        return static_cast<uint8_t>(((value % count) + count) % count);
    }

    static int16_t foldNote(int16_t note) noexcept {
        while (note > 127) note -= 12;
        while (note < 0) note += 12;
        return note;
    }

    static void sortNotes(int16_t* notes, uint8_t count) noexcept {
        for (uint8_t i = 1; i < count; ++i) {
            const int16_t value = notes[i];
            uint8_t j = i;
            while (j > 0 && notes[j - 1] > value) {
                notes[j] = notes[j - 1];
                --j;
            }
            notes[j] = value;
        }
    }

    uint8_t noteForKey(uint8_t key) const noexcept {
        if (isDrumPreset(preset_)) return kGmDrumNotes[key % kGmDrumNotes.size()];
        const int note = 60 + octave_ * 12 + rootPitchClass_ + scaleDegreeOffset(scale(), key);
        return note > 127 ? 127 : static_cast<uint8_t>(note);
    }

    void pressDegree(uint8_t key) noexcept {
        DegreeState& degree = degrees_[key];
        if (degree.held) return;
        degree.held = true;
        degree.rootNote = static_cast<int16_t>(noteForKey(key));
        degree.scale = scale();
        degree.preset = preset_;
        degree.key = key;
        degree.transpose = currentTranspose();
        degree.spelling = isDrumPreset(preset_)
            ? NoteSpelling{{static_cast<char>(key + 'A')}}
            : spellScaleDegree(rootPitchClass_, degree.scale, key);
        heldOrder_[heldCount_++] = key;
    }

    void releaseDegree(uint8_t key) noexcept {
        DegreeState& degree = degrees_[key];
        if (!degree.held) return;
        degree.held = false;
        degree.rootNote = -1;
        removeHeldKey(key);
    }

    void pressHarmony(uint8_t key) noexcept {
        if (harmonyCount_ >= kHarmonyKeyCount) return;
        for (uint8_t i = 0; i < harmonyCount_; ++i)
            if (harmonyStack_[i] == key) return;
        harmonyStack_[harmonyCount_++] = key;
    }

    void releaseHarmony(uint8_t key) noexcept {
        uint8_t index = 0;
        while (index < harmonyCount_ && harmonyStack_[index] != key) ++index;
        if (index == harmonyCount_) return;
        for (uint8_t i = index; i + 1 < harmonyCount_; ++i) harmonyStack_[i] = harmonyStack_[i + 1];
        --harmonyCount_;
    }

    void pressPattern(uint8_t key) noexcept {
        if (patternCount_ >= kHarmonyKeyCount) return;
        for (uint8_t i = 0; i < patternCount_; ++i)
            if (patternStack_[i] == key) return;
        patternStack_[patternCount_++] = key;
    }

    void releasePattern(uint8_t key) noexcept {
        uint8_t index = 0;
        while (index < patternCount_ && patternStack_[index] != key) ++index;
        if (index == patternCount_) return;
        for (uint8_t i = index; i + 1 < patternCount_; ++i) patternStack_[i] = patternStack_[i + 1];
        --patternCount_;
    }

    uint8_t heldPatternKey() const noexcept {
        return patternCount_ == 0 ? kNoRunSource : patternStack_[patternCount_ - 1];
    }

    uint8_t lowerHeldKey() const noexcept {
        return heldCount_ == 0 ? kNoRunSource : heldOrder_[heldCount_ - 1];
    }

    static constexpr bool isLower(uint8_t key) noexcept { return key < kKeyCount; }
    static constexpr bool isUpper(uint8_t key) noexcept {
        return key >= kHarmonyStartKey && key < kShiftKey;
    }

    uint16_t frequencyHundredths() const noexcept {
        if (frequencyIndex_ == 0) return 50;
        if (frequencyIndex_ == kMaxFrequencyIndex) return 5000;
        return static_cast<uint16_t>(std::lround(
            50.0 * std::pow(100.0, static_cast<double>(frequencyIndex_) / kMaxFrequencyIndex)));
    }

    uint32_t stepDurationUs() const noexcept {
        if (clockMode_ == ClockMode::Tempo)
            return static_cast<uint32_t>((15000000ULL + tempo_ / 2U) / tempo_);
        const uint16_t hundredths = frequencyHundredths();
        return static_cast<uint32_t>((100000000ULL + hundredths / 2U) / hundredths);
    }

    static uint32_t applyDivision(uint32_t sixteenthUs, RateDivision division) noexcept {
        static constexpr uint8_t numerators[]{4, 2, 1, 1, 8, 4, 2, 1};
        static constexpr uint8_t denominators[]{1, 1, 1, 2, 3, 3, 3, 3};
        const uint8_t index = static_cast<uint8_t>(division);
        return static_cast<uint32_t>(static_cast<uint64_t>(sixteenthUs) * numerators[index] / denominators[index]);
    }

    uint32_t currentStepDurationUs() const noexcept {
        if (run_.active()) return applyDivision(stepDurationUs(), runDivision_);
        const uint8_t key = heldPatternKey();
        const uint8_t slot = key == kNoRunSource ? 0 : key - kHarmonyStartKey;
        return applyDivision(stepDurationUs(), currentAssignment(slot).division);
    }

    void startRun(uint8_t sourceKey, uint8_t patternKey, RunShape shape, uint32_t now) noexcept {
        const DegreeState& degree = degrees_[sourceKey];
        const PatternAssignment& assignment = currentAssignment(patternKey - kHarmonyStartKey);
        const RunShape targetShape = isDrumPreset(preset_) ? RunShape::Repeat : shape;
        const uint32_t duration = applyDivision(stepDurationUs(), assignment.division);
        if (run_.active() && runSourceKey_ == sourceKey && run_.shape() == RunShape::Repeat && targetShape == RunShape::Repeat)
            run_.setStepDuration(duration, now);
        else
            run_.start(static_cast<int16_t>(degree.rootNote + degree.transpose), degree.scale,
                       sourceKey, targetShape, duration, now);
        runSourceKey_ = run_.active() ? sourceKey : kNoRunSource;
        runPatternKey_ = run_.active() ? patternKey : kNoRunSource;
        runDivision_ = assignment.division;
    }

    uint8_t soundingTarget(const DegreeState& degree, int16_t* target) const noexcept {
        if (harmonyCount_ == 0 || roles_[degree.key] == PadRole::ManualDegree) {
            const int16_t note = static_cast<int16_t>(degree.rootNote + degree.transpose);
            if (note < 0 || note > 127) return 0;
            target[0] = note;
            return 1;
        }
        const uint8_t slot = static_cast<uint8_t>(harmonyStack_[harmonyCount_ - 1] - kHarmonyStartKey);
        const ChordRecipe& recipe = *harmonySlotRecipe(degree.preset, slot);
        uint8_t count = 0;
        for (uint8_t i = 0; i < recipe.voiceCount; ++i) {
            const int16_t note = static_cast<int16_t>(foldNote(static_cast<int16_t>(degree.rootNote +
                (recipe.chromaticIntervals
                    ? recipe.degrees[i]
                    : scaleDegreeOffset(degree.scale, degree.key + recipe.degrees[i]) -
                      scaleDegreeOffset(degree.scale, degree.key)) +
                recipe.octaveDisplacements[i])) + degree.transpose);
            if (note >= 0 && note <= 127) target[count++] = note;
        }
        sortNotes(target, count);
        return count;
    }

    void soundingUnion(std::array<bool, 128>& sounding) const noexcept {
        for (uint8_t voice = 0; voice < run_.soundingCount(); ++voice) {
            const int16_t runNote = run_.soundingNote(voice);
            if (runNote >= 0) sounding[static_cast<uint8_t>(runNote)] = true;
        }
        for (const DegreeState& degree : degrees_) {
            if (!degree.held) continue;
            if (degree.key == runSourceKey_) continue;
            int16_t target[kMaxChordVoices];
            const uint8_t targetCount = soundingTarget(degree, target);
            for (uint8_t i = 0; i < targetCount; ++i) sounding[target[i]] = true;
        }
    }

    uint8_t applyAction(uint8_t key, bool down, uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        if (out == nullptr || capacity == 0) return 0;
        if (key > kShiftKey) return 0;
        if (key == kShiftKey) {
            if (down == shiftHeld_) return 0;
            SimpleMidiController candidate = *this;
            candidate.advanceModulation(now);
            candidate.shiftHeld_ = down;
            candidate.setShiftModeActive(down, now);
            return commit(candidate, out, capacity);
        }
        SimpleMidiController candidate = *this;
        PadRole& role = candidate.roles_[key];
        if (down) {
            if (role != PadRole::None) return 0;
            if (isLower(key)) {
                candidate.pressDegree(key);
                role = page_ == PerformancePage::Harmony ? PadRole::HarmonyDegree : PadRole::ManualDegree;
                if (page_ == PerformancePage::Pattern && candidate.patternCount_ > 0)
                    candidate.startRun(key, candidate.heldPatternKey(), candidate.currentPattern(), now);
            } else if (isUpper(key)) {
                if (page_ == PerformancePage::Harmony) {
                    role = PadRole::HarmonySlot;
                    candidate.pressHarmony(key);
                } else if (page_ == PerformancePage::Pattern) {
                    role = PadRole::PatternSlot;
                    candidate.pressPattern(key);
                    const uint8_t lower = candidate.lowerHeldKey();
                    if (lower != kNoRunSource) candidate.startRun(lower, key, candidate.currentPattern(), now);
                } else {
                    role = PadRole::ManualDegree;
                    candidate.pressDegree(key);
                }
            }
        } else {
            if (role == PadRole::HarmonyDegree || role == PadRole::ManualDegree)
                candidate.releaseDegree(key);
            else if (role == PadRole::HarmonySlot) candidate.releaseHarmony(key);
            else if (role == PadRole::PatternSlot) candidate.releasePattern(key);
            const bool restoresRepeat = candidate.run_.active() && candidate.runPatternKey_ == key &&
                candidate.run_.shape() == RunShape::Repeat && candidate.patternCount_ > 0 &&
                candidate.runSourceKey_ != kNoRunSource && candidate.degrees_[candidate.runSourceKey_].held;
            if (restoresRepeat)
                candidate.startRun(candidate.runSourceKey_, candidate.heldPatternKey(), candidate.currentPattern(), now);
            const bool stopsRun = candidate.run_.active() &&
                (candidate.runSourceKey_ == key || candidate.runPatternKey_ == key);
            if (stopsRun) {
                candidate.run_.cancel();
                candidate.runSourceKey_ = kNoRunSource;
                candidate.runPatternKey_ = kNoRunSource;
            } else if (candidate.runSourceKey_ == key) candidate.runSourceKey_ = kNoRunSource;
            role = PadRole::None;
        }
        return commit(candidate, out, capacity);
    }

    uint8_t commit(const SimpleMidiController& candidate, MidiCommand* out, uint8_t capacity) noexcept {
        if (out == nullptr || capacity == 0) return 0;
        std::array<bool, 128> oldSounding{};
        std::array<bool, 128> newSounding{};
        soundingUnion(oldSounding);
        candidate.soundingUnion(newSounding);
        uint16_t required = 0;
        for (uint16_t note = 0; note < 128; ++note)
            if (oldSounding[note] != newSounding[note]) ++required;
        if (sustainValue_ != candidate.sustainValue_) ++required;
        if (modulationValue_ != candidate.modulationValue_) ++required;
        if (required > capacity) return 0;
        uint8_t count = 0;
        for (uint16_t note = 0; note < 128; ++note)
            if (oldSounding[note] && !newSounding[note])
                out[count++] = {MidiCommandType::NoteOff, static_cast<uint8_t>(note), 0};
        for (uint16_t note = 0; note < 128; ++note)
            if (!oldSounding[note] && newSounding[note])
                out[count++] = {MidiCommandType::NoteOn, static_cast<uint8_t>(note), kVelocity};
        if (sustainValue_ != candidate.sustainValue_)
            out[count++] = {MidiCommandType::ControlChange, 64, candidate.sustainValue_};
        if (modulationValue_ != candidate.modulationValue_)
            out[count++] = {MidiCommandType::ControlChange, 1, candidate.modulationValue_};
        *this = candidate;
        return count;
    }

    int8_t currentTranspose() const noexcept {
        if (!shiftHeld_) return 0;
        switch (shiftMode_) {
            case ShiftMode::OctaveUp: return 12;
            case ShiftMode::SemitoneUp: return 1;
            case ShiftMode::OctaveDown: return -12;
            case ShiftMode::SemitoneDown: return -1;
            default: return 0;
        }
    }

    void setShiftModeActive(bool active, uint32_t now) noexcept {
        if (shiftMode_ == ShiftMode::Hold) sustainValue_ = active ? 127 : 0;
        if (shiftMode_ == ShiftMode::Mod) setModulationTarget(active ? 127 : 0, now);
    }

    void setModulationTarget(uint8_t target, uint32_t now) noexcept {
        advanceModulation(now);
        modulationStartValue_ = modulationValue_;
        modulationTarget_ = target;
        modulationStartedAt_ = now;
    }

    void advanceModulation(uint32_t now) noexcept {
        if (modulationValue_ == modulationTarget_) return;
        constexpr uint32_t rampUs = 1000000U;
        const uint32_t elapsed = now - modulationStartedAt_;
        if (elapsed >= rampUs) {
            modulationValue_ = modulationTarget_;
            return;
        }
        const int32_t start = modulationStartValue_;
        const int32_t distance = static_cast<int32_t>(modulationTarget_) - start;
        modulationValue_ = static_cast<uint8_t>(start + distance * static_cast<int32_t>(elapsed) /
                                                static_cast<int32_t>(rampUs));
    }

    void removeHeldKey(uint8_t key) noexcept {
        uint8_t index = 0;
        while (index < heldCount_ && heldOrder_[index] != key) ++index;
        if (index == heldCount_) return;
        for (; index + 1 < heldCount_; ++index) heldOrder_[index] = heldOrder_[index + 1];
        --heldCount_;
    }

    std::array<DegreeState, kNoteKeyCount> degrees_{};
    std::array<uint8_t, kNoteKeyCount> heldOrder_{};
    std::array<uint8_t, kHarmonyKeyCount> harmonyStack_{};
    uint8_t harmonyCount_{};
    std::array<uint8_t, kHarmonyKeyCount> patternStack_{};
    uint8_t patternCount_{};
    const PatternAssignment& currentAssignment(uint8_t slot) const noexcept {
        return patternAssign_[static_cast<uint8_t>(patternBank_)][slot];
    }

    std::array<std::array<PatternAssignment, kHarmonyKeyCount>, 5> patternAssign_{{
        {{{RunShape::RunUp, RateDivision::Sixteenth}, {RunShape::RunDown, RateDivision::Sixteenth},
          {RunShape::UpDown, RateDivision::Sixteenth}, {RunShape::DownUp, RateDivision::Sixteenth},
          {RunShape::ThirdsUp, RateDivision::Sixteenth}, {RunShape::ThirdsDown, RateDivision::Sixteenth},
          {RunShape::ArpUp, RateDivision::Sixteenth}, {RunShape::ArpDown, RateDivision::Sixteenth}}},
        {{{RunShape::FutureLift, RateDivision::Sixteenth}, {RunShape::FutureBounce, RateDivision::Sixteenth},
          {RunShape::FutureWide, RateDivision::Sixteenth}, {RunShape::FutureSeven, RateDivision::Sixteenth},
          {RunShape::FutureNine, RateDivision::Sixteenth}, {RunShape::FutureSparkle, RateDivision::Sixteenth},
          {RunShape::FutureRise, RateDivision::Sixteenth}, {RunShape::FutureFall, RateDivision::Sixteenth}}},
        {{{RunShape::FuturePulse, RateDivision::Sixteenth}, {RunShape::FutureOffbeat, RateDivision::Sixteenth},
          {RunShape::FutureDouble, RateDivision::Sixteenth}, {RunShape::FuturePush, RateDivision::Sixteenth},
          {RunShape::FutureSync, RateDivision::Sixteenth}, {RunShape::FutureHold, RateDivision::Sixteenth},
          {RunShape::FutureSevenChop, RateDivision::Sixteenth}, {RunShape::FutureNineChop, RateDivision::Sixteenth}}},
        {{{RunShape::KawaiiAdd9Arp, RateDivision::Sixteenth}, {RunShape::KawaiiSevenBounce, RateDivision::Sixteenth},
          {RunShape::KawaiiNineChop, RateDivision::Sixteenth}, {RunShape::KawaiiSixNinePulse, RateDivision::Sixteenth},
          {RunShape::KawaiiDom7Arp, RateDivision::Sixteenth}, {RunShape::KawaiiDomFlatNineChop, RateDivision::Sixteenth},
          {RunShape::KawaiiMin9Arp, RateDivision::Sixteenth}, {RunShape::KawaiiMin6Pulse, RateDivision::Sixteenth}}},
        {{{RunShape::Repeat, RateDivision::Quarter}, {RunShape::Repeat, RateDivision::Eighth},
          {RunShape::Repeat, RateDivision::Sixteenth}, {RunShape::Repeat, RateDivision::ThirtySecond},
          {RunShape::Repeat, RateDivision::QuarterTriplet}, {RunShape::Repeat, RateDivision::EighthTriplet},
          {RunShape::Repeat, RateDivision::SixteenthTriplet}, {RunShape::Repeat, RateDivision::ThirtySecondTriplet}}}
    }};
    int8_t octave_{};
    uint8_t rootPitchClass_{};
    uint8_t heldCount_{};
    uint8_t runSourceKey_{kNoRunSource};
    uint8_t runPatternKey_{kNoRunSource};
    MusicalPreset preset_{MusicalPreset::Major};
    PerformancePage page_{PerformancePage::Harmony};
    uint16_t tempo_{120};
    uint8_t frequencyIndex_{29};
    ClockMode clockMode_{ClockMode::Tempo};
    PatternBank patternBank_{PatternBank::Orchestral};
    RateDivision runDivision_{RateDivision::Sixteenth};
    std::array<PadRole, kShiftKey> roles_{};
    RunPattern run_{};
    ShiftMode shiftMode_{ShiftMode::Hold};
    bool shiftHeld_{};
    uint8_t sustainValue_{};
    uint8_t modulationValue_{};
    uint8_t modulationStartValue_{};
    uint8_t modulationTarget_{};
    uint32_t modulationStartedAt_{};
};

}
