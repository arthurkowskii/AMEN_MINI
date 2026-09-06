#pragma once

#include "diatonic_scales.h"
#include "musical_presets.h"
#include "run_pattern.h"

#include <array>
#include <cstdint>

namespace amen {

enum class MidiCommandType : uint8_t {
    None,
    NoteOn,
    NoteOff
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

    uint8_t press(uint8_t key, MidiCommand* out, uint8_t capacity) noexcept {
        return press(key, 0, out, capacity);
    }

    uint8_t press(uint8_t key, uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        return applyAction(key, true, now, out, capacity);
    }

    uint8_t release(uint8_t key, MidiCommand* out, uint8_t capacity) noexcept {
        return applyAction(key, false, 0, out, capacity);
    }

    uint8_t tick(uint32_t now, MidiCommand* out, uint8_t capacity) noexcept {
        SimpleMidiController candidate = *this;
        candidate.run_.tick(now);
        if (!candidate.run_.active() && candidate.runSourceKey_ != kNoRunSource)
            candidate.runSourceKey_ = kNoRunSource;
        return commit(candidate, out, capacity);
    }

    uint8_t cancelRun(MidiCommand* out, uint8_t capacity) noexcept {
        SimpleMidiController candidate = *this;
        candidate.run_.cancel();
        candidate.runSourceKey_ = kNoRunSource;
        return commit(candidate, out, capacity);
    }

    uint8_t togglePage(MidiCommand* out, uint8_t capacity) noexcept {
        SimpleMidiController candidate = *this;
        if (candidate.page_ == PerformancePage::Harmony) candidate.page_ = PerformancePage::Pattern;
        else if (candidate.page_ == PerformancePage::Pattern) candidate.page_ = PerformancePage::None;
        else candidate.page_ = PerformancePage::Harmony;
        if (candidate.page_ != PerformancePage::Pattern) {
            candidate.run_.cancel();
            candidate.runSourceKey_ = kNoRunSource;
        }
        return commit(candidate, out, capacity);
    }

    bool turnStep(int delta) noexcept {
        const int64_t next = static_cast<int64_t>(stepMs_) + static_cast<int64_t>(delta) * 5;
        const uint16_t clamped = next < 30 ? 30 : (next > 200 ? 200 : static_cast<uint16_t>(next));
        if (clamped == stepMs_) return false;
        stepMs_ = clamped;
        return true;
    }

    bool turnPattern(int delta) noexcept {
        if (page_ != PerformancePage::Pattern) return false;
        const uint8_t key = heldPatternKey();
        if (key == kNoRunSource) return false;
        const uint8_t slot = key - kHarmonyStartKey;
        const uint8_t current = static_cast<uint8_t>(patternAssign_[slot]);
        const uint8_t next = wrap(current, delta, kRunShapeCount);
        if (next == current) return false;
        patternAssign_[slot] = static_cast<RunShape>(next);
        return true;
    }

    PerformancePage page() const noexcept { return page_; }
    uint16_t stepMs() const noexcept { return stepMs_; }
    bool runActive() const noexcept { return run_.active(); }
    int16_t runNote() const noexcept { return run_.note(); }
    uint32_t runFinalOff() const noexcept { return run_.finalOff(); }
    RunShape runShape() const noexcept { return run_.shape(); }
    bool patternHeld() const noexcept { return patternCount_ > 0; }
    RunShape currentPattern() const noexcept {
        const uint8_t key = heldPatternKey();
        return key == kNoRunSource ? patternAssign_[0] : patternAssign_[key - kHarmonyStartKey];
    }
    RunShape slotAssignment(uint8_t slot) const noexcept { return patternAssign_[slot]; }
    uint8_t patternSlot() const noexcept {
        const uint8_t key = heldPatternKey();
        return key == kNoRunSource ? 0 : key - kHarmonyStartKey;
    }
    uint8_t patternStackSize() const noexcept { return patternCount_; }
    uint8_t runSourceKey() const noexcept { return runSourceKey_; }
    const char* runShapeName() const noexcept { return amen::runShapeName(run_.active() ? run_.shape() : currentPattern()); }

    bool turnOctave(int delta) noexcept {
        const int64_t next = static_cast<int64_t>(octave_) + delta;
        const int clamped = next < kMinOctave ? kMinOctave : (next > kMaxOctave ? kMaxOctave : static_cast<int>(next));
        if (clamped == octave_) return false;
        octave_ = static_cast<int8_t>(clamped);
        return true;
    }

    bool turnRoot(int delta) noexcept {
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
        return degrees_[heldOrder_[heldCount_ - 1]].spelling.text.data();
    }

private:
    static constexpr int kMinOctave = -5;
    static constexpr int kMaxOctave = 3;

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
        degree.spelling = spellScaleDegree(rootPitchClass_, degree.scale, key);
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

    void startRun(uint8_t sourceKey, RunShape shape, uint32_t now) noexcept {
        const DegreeState& degree = degrees_[sourceKey];
        run_.start(degree.rootNote, degree.scale, sourceKey, shape, stepMs_, now);
        runSourceKey_ = run_.active() ? sourceKey : kNoRunSource;
    }

    uint8_t soundingTarget(const DegreeState& degree, int16_t* target) const noexcept {
        if (harmonyCount_ == 0 || roles_[degree.key] == PadRole::ManualDegree) {
            target[0] = degree.rootNote;
            return 1;
        }
        const uint8_t slot = static_cast<uint8_t>(harmonyStack_[harmonyCount_ - 1] - kHarmonyStartKey);
        const ChordRecipe& recipe = *harmonySlotRecipe(degree.preset, slot);
        for (uint8_t i = 0; i < recipe.voiceCount; ++i)
            target[i] = foldNote(static_cast<int16_t>(degree.rootNote +
                scaleDegreeOffset(degree.scale, degree.key + recipe.degrees[i]) -
                scaleDegreeOffset(degree.scale, degree.key) + recipe.octaveDisplacements[i]));
        sortNotes(target, recipe.voiceCount);
        return recipe.voiceCount;
    }

    void soundingUnion(std::array<bool, 128>& sounding) const noexcept {
        if (run_.active()) sounding[static_cast<uint8_t>(run_.note())] = true;
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
        if (key >= kShiftKey) return 0;
        SimpleMidiController candidate = *this;
        PadRole& role = candidate.roles_[key];
        if (down) {
            if (role != PadRole::None) return 0;
            if (isLower(key)) {
                candidate.pressDegree(key);
                role = page_ == PerformancePage::Harmony ? PadRole::HarmonyDegree : PadRole::ManualDegree;
                if (page_ == PerformancePage::Pattern && candidate.patternCount_ > 0)
                    candidate.startRun(key, candidate.currentPattern(), now);
            } else if (isUpper(key)) {
                if (page_ == PerformancePage::Harmony) {
                    role = PadRole::HarmonySlot;
                    candidate.pressHarmony(key);
                } else if (page_ == PerformancePage::Pattern) {
                    role = PadRole::PatternSlot;
                    candidate.pressPattern(key);
                    const uint8_t lower = candidate.lowerHeldKey();
                    if (lower != kNoRunSource) candidate.startRun(lower, candidate.currentPattern(), now);
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
            if (candidate.runSourceKey_ == key) candidate.runSourceKey_ = kNoRunSource;
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
        if (required > capacity) return 0;
        uint8_t count = 0;
        for (uint16_t note = 0; note < 128; ++note)
            if (oldSounding[note] && !newSounding[note])
                out[count++] = {MidiCommandType::NoteOff, static_cast<uint8_t>(note), 0};
        for (uint16_t note = 0; note < 128; ++note)
            if (!oldSounding[note] && newSounding[note])
                out[count++] = {MidiCommandType::NoteOn, static_cast<uint8_t>(note), kVelocity};
        *this = candidate;
        return count;
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
    std::array<RunShape, kHarmonyKeyCount> patternAssign_{{
        RunShape::RunUp, RunShape::RunDown, RunShape::UpDown, RunShape::DownUp,
        RunShape::ThirdsUp, RunShape::ThirdsDown, RunShape::ArpUp, RunShape::ArpDown
    }};
    int8_t octave_{};
    uint8_t rootPitchClass_{};
    uint8_t heldCount_{};
    uint8_t runSourceKey_{kNoRunSource};
    MusicalPreset preset_{MusicalPreset::Major};
    PerformancePage page_{PerformancePage::Harmony};
    uint16_t stepMs_{80};
    std::array<PadRole, kShiftKey> roles_{};
    RunPattern run_{};
};

}
