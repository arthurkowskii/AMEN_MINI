#pragma once

#include <array>
#include <cstdint>

namespace amen {

enum class DiatonicMode : uint8_t {
    Ionian,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Aeolian,
    Locrian
};

static constexpr uint8_t kDiatonicModeCount = 7;

static constexpr std::array<std::array<uint8_t, 7>, kDiatonicModeCount> kDiatonicIntervals{{
    {{0, 2, 4, 5, 7, 9, 11}},
    {{0, 2, 3, 5, 7, 9, 10}},
    {{0, 1, 3, 5, 7, 8, 10}},
    {{0, 2, 4, 6, 7, 9, 11}},
    {{0, 2, 4, 5, 7, 9, 10}},
    {{0, 2, 3, 5, 7, 8, 10}},
    {{0, 1, 3, 5, 6, 8, 10}},
}};

constexpr uint8_t scaleDegreeOffset(DiatonicMode mode, uint8_t degree) noexcept {
    const uint8_t index = static_cast<uint8_t>(mode);
    if (index >= kDiatonicModeCount) return 0;
    return static_cast<uint8_t>(12 * (degree / 7) + kDiatonicIntervals[index][degree % 7]);
}

constexpr const char* modeName(DiatonicMode mode) noexcept {
    switch (mode) {
        case DiatonicMode::Ionian: return "IONIAN";
        case DiatonicMode::Dorian: return "DORIAN";
        case DiatonicMode::Phrygian: return "PHRYGIAN";
        case DiatonicMode::Lydian: return "LYDIAN";
        case DiatonicMode::Mixolydian: return "MIXOLYDIAN";
        case DiatonicMode::Aeolian: return "AEOLIAN";
        case DiatonicMode::Locrian: return "LOCRIAN";
    }
    return "";
}

constexpr const char* modeShortName(DiatonicMode mode) noexcept {
    switch (mode) {
        case DiatonicMode::Ionian: return "ION";
        case DiatonicMode::Dorian: return "DOR";
        case DiatonicMode::Phrygian: return "PHR";
        case DiatonicMode::Lydian: return "LYD";
        case DiatonicMode::Mixolydian: return "MIX";
        case DiatonicMode::Aeolian: return "AEO";
        case DiatonicMode::Locrian: return "LOC";
    }
    return "";
}

constexpr const char* modeDescription(DiatonicMode mode) noexcept {
    switch (mode) {
        case DiatonicMode::Ionian: return "(MAJOR)";
        case DiatonicMode::Dorian: return "(MINOR +6)";
        case DiatonicMode::Phrygian: return "(MINOR b2)";
        case DiatonicMode::Lydian: return "(MAJOR #4)";
        case DiatonicMode::Mixolydian: return "(MAJOR b7)";
        case DiatonicMode::Aeolian: return "(NAT MINOR)";
        case DiatonicMode::Locrian: return "(MINOR b2 b5)";
    }
    return "";
}

constexpr const char* pitchClassName(uint8_t pitchClass) noexcept {
    constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[pitchClass % 12];
}

}
