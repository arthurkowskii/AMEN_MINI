#pragma once

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
    static constexpr uint8_t kKeyCount = 20;
    static constexpr uint8_t kChannel = 1;
    static constexpr uint8_t kVelocity = 100;

    SimpleMidiController() noexcept {
        activeNotes_.fill(-1);
    }

    MidiCommand press(uint8_t key) noexcept {
        if (key >= kKeyCount || activeNotes_[key] >= 0) return {};
        const uint8_t note = static_cast<uint8_t>(baseNote() + key);
        activeNotes_[key] = note;
        return {MidiCommandType::NoteOn, note, kVelocity};
    }

    MidiCommand release(uint8_t key) noexcept {
        if (key >= kKeyCount || activeNotes_[key] < 0) return {};
        const uint8_t note = static_cast<uint8_t>(activeNotes_[key]);
        activeNotes_[key] = -1;
        return {MidiCommandType::NoteOff, note, 0};
    }

    bool turnOctave(int delta) noexcept {
        const int next = octave_ + delta;
        const int clamped = next < kMinOctave ? kMinOctave : (next > kMaxOctave ? kMaxOctave : next);
        if (clamped == octave_) return false;
        octave_ = static_cast<int8_t>(clamped);
        return true;
    }

    int octave() const noexcept { return octave_; }
    uint8_t baseNote() const noexcept {
        return static_cast<uint8_t>(60 + octave_ * 12);
    }

private:
    static constexpr int kMinOctave = -5;
    static constexpr int kMaxOctave = 4;
    std::array<int16_t, kKeyCount> activeNotes_{};
    int8_t octave_{};
};

}
