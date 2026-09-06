#pragma once

#include "diatonic_scales.h"

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
    static constexpr uint8_t kChannel = 1;
    static constexpr uint8_t kVelocity = 100;

    SimpleMidiController() noexcept {
        activeNotes_.fill(-1);
    }

    MidiCommand press(uint8_t key) noexcept {
        if (key >= kKeyCount || activeNotes_[key] >= 0) return {};
        const uint8_t note = noteForKey(key);
        activeNotes_[key] = note;
        activeSpellings_[key] = spellScaleDegree(rootPitchClass_, mode_, key);
        heldOrder_[heldCount_++] = key;
        return {MidiCommandType::NoteOn, note, kVelocity};
    }

    MidiCommand release(uint8_t key) noexcept {
        if (key >= kKeyCount || activeNotes_[key] < 0) return {};
        const uint8_t note = static_cast<uint8_t>(activeNotes_[key]);
        activeNotes_[key] = -1;
        removeHeldKey(key);
        return {MidiCommandType::NoteOff, note, 0};
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

    bool turnMode(int delta) noexcept {
        const uint8_t current = static_cast<uint8_t>(mode_);
        const uint8_t next = wrap(current, delta, kDiatonicModeCount);
        if (next == current) return false;
        mode_ = static_cast<DiatonicMode>(next);
        return true;
    }

    int octave() const noexcept { return octave_; }
    uint8_t octaveNumber() const noexcept { return static_cast<uint8_t>(octave_ - kMinOctave); }
    uint8_t rootPitchClass() const noexcept { return rootPitchClass_; }
    DiatonicMode mode() const noexcept { return mode_; }
    uint8_t rootNote() const noexcept { return noteForKey(0); }
    uint8_t highestNote() const noexcept { return noteForKey(kKeyCount - 1); }
    int16_t activeNote(uint8_t key) const noexcept {
        return key < kKeyCount ? activeNotes_[key] : -1;
    }
    uint8_t heldCount() const noexcept { return heldCount_; }
    const char* currentNoteName() const noexcept {
        if (heldCount_ == 0) return "";
        return activeSpellings_[heldOrder_[heldCount_ - 1]].text.data();
    }

private:
    static constexpr int kMinOctave = -5;
    static constexpr int kMaxOctave = 3;

    static uint8_t wrap(uint8_t current, int delta, uint8_t count) noexcept {
        const int64_t value = static_cast<int64_t>(current) + delta;
        const int64_t wrapped = ((value % count) + count) % count;
        return static_cast<uint8_t>(wrapped);
    }

    uint8_t noteForKey(uint8_t key) const noexcept {
        return static_cast<uint8_t>(60 + octave_ * 12 + rootPitchClass_ + scaleDegreeOffset(mode_, key));
    }

    void removeHeldKey(uint8_t key) noexcept {
        uint8_t index = 0;
        while (index < heldCount_ && heldOrder_[index] != key) ++index;
        if (index == heldCount_) return;
        for (; index + 1 < heldCount_; ++index) heldOrder_[index] = heldOrder_[index + 1];
        --heldCount_;
    }

    std::array<int16_t, kKeyCount> activeNotes_{};
    std::array<NoteSpelling, kKeyCount> activeSpellings_{};
    std::array<uint8_t, kKeyCount> heldOrder_{};
    int8_t octave_{};
    uint8_t rootPitchClass_{};
    uint8_t heldCount_{};
    DiatonicMode mode_{DiatonicMode::Ionian};
};

}
