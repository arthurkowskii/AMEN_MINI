#pragma once

#include "diatonic_scales.h"
#include "musical_presets.h"

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

class SimpleMidiController {
public:
    static constexpr uint8_t kKeyCount = 12;
    static constexpr uint8_t kHarmonyStartKey = 12;
    static constexpr uint8_t kHarmonyKeyCount = kHarmonySlotCount;
    static constexpr uint8_t kShiftKey = kHarmonyStartKey + kHarmonyKeyCount;
    static constexpr uint8_t kChannel = 1;
    static constexpr uint8_t kVelocity = 100;
    static constexpr uint8_t kMaxChordVoices = kMaxRecipeVoices;
    static constexpr uint8_t kMaxEventsPerAction = 128;

    uint8_t press(uint8_t key, MidiCommand* out, uint8_t capacity) noexcept {
        return applyAction(key, true, out, capacity);
    }

    uint8_t release(uint8_t key, MidiCommand* out, uint8_t capacity) noexcept {
        return applyAction(key, false, out, capacity);
    }

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
        const int64_t wrapped = ((value % count) + count) % count;
        return static_cast<uint8_t>(wrapped);
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

    uint8_t soundingTarget(const DegreeState& degree, int16_t* target) const noexcept {
        if (harmonyCount_ == 0) {
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
        for (const DegreeState& degree : degrees_) {
            if (!degree.held) continue;
            int16_t target[kMaxChordVoices];
            const uint8_t targetCount = soundingTarget(degree, target);
            for (uint8_t i = 0; i < targetCount; ++i) sounding[target[i]] = true;
        }
    }

    uint8_t applyAction(uint8_t key, bool down, MidiCommand* out, uint8_t capacity) noexcept {
        if (out == nullptr || capacity == 0 || key >= kShiftKey) return 0;
        SimpleMidiController candidate = *this;
        if (key < kKeyCount) {
            if (down) candidate.pressDegree(key);
            else candidate.releaseDegree(key);
        } else {
            if (down) candidate.pressHarmony(key);
            else candidate.releaseHarmony(key);
        }
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

    std::array<DegreeState, kKeyCount> degrees_{};
    std::array<uint8_t, kKeyCount> heldOrder_{};
    std::array<uint8_t, kHarmonyKeyCount> harmonyStack_{};
    uint8_t harmonyCount_{};
    int8_t octave_{};
    uint8_t rootPitchClass_{};
    uint8_t heldCount_{};
    MusicalPreset preset_{MusicalPreset::Major};
};

}
