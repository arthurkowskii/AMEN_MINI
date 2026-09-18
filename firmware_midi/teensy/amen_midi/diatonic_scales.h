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
    Locrian,
    HarmonicMinor,
    MelodicMinor,
    PhrygianDominant,
    Octatonic,
    WholeTone,
    Chromatic
};

static constexpr uint8_t kDiatonicModeCount = 13;
static constexpr uint8_t kMaxScaleSteps = 12;

struct NoteSpelling {
    std::array<char, 4> text{};
};

static constexpr std::array<std::array<uint8_t, kMaxScaleSteps>, kDiatonicModeCount> kDiatonicIntervals{{
    {{0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}},
    {{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}},
    {{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {{0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0}},
    {{0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0}},
    {{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {{0, 1, 3, 5, 6, 8, 10, 0, 0, 0, 0, 0}},
    {{0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}},
    {{0, 2, 3, 5, 7, 9, 11, 0, 0, 0, 0, 0}},
    {{0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {{0, 1, 3, 4, 6, 7, 9, 10, 0, 0, 0, 0}},
    {{0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0}},
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
}};

static constexpr std::array<uint8_t, kDiatonicModeCount> kScaleStepCounts{{
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 6, 12
}};

constexpr uint8_t scaleStepCount(DiatonicMode mode) noexcept {
    const uint8_t index = static_cast<uint8_t>(mode);
    return index < kDiatonicModeCount ? kScaleStepCounts[index] : 7;
}

constexpr bool isHeptatonic(DiatonicMode mode) noexcept { return scaleStepCount(mode) == 7; }

constexpr uint8_t scaleDegreeOffset(DiatonicMode mode, uint8_t degree) noexcept {
    const uint8_t index = static_cast<uint8_t>(mode);
    if (index >= kDiatonicModeCount) return 0;
    const uint8_t steps = kScaleStepCounts[index];
    return static_cast<uint8_t>(12 * (degree / steps) + kDiatonicIntervals[index][degree % steps]);
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
        case DiatonicMode::HarmonicMinor: return "HARM MIN";
        case DiatonicMode::MelodicMinor: return "MEL MIN";
        case DiatonicMode::PhrygianDominant: return "PHRYG DOM";
        case DiatonicMode::Octatonic: return "OCTATONIC";
        case DiatonicMode::WholeTone: return "WHOLE TONE";
        case DiatonicMode::Chromatic: return "CHROMATIC";
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
        case DiatonicMode::HarmonicMinor: return "HMIN";
        case DiatonicMode::MelodicMinor: return "MELM";
        case DiatonicMode::PhrygianDominant: return "PHRD";
        case DiatonicMode::Octatonic: return "OCT";
        case DiatonicMode::WholeTone: return "WT";
        case DiatonicMode::Chromatic: return "CHRO";
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
        case DiatonicMode::HarmonicMinor: return "(MINOR #7)";
        case DiatonicMode::MelodicMinor: return "(JAZZ MINOR)";
        case DiatonicMode::PhrygianDominant: return "(HM b2)";
        case DiatonicMode::Octatonic: return "(HW DIM)";
        case DiatonicMode::WholeTone: return "(6 EQUAL)";
        case DiatonicMode::Chromatic: return "(12 SEMITONES)";
    }
    return "";
}

constexpr const char* pitchClassName(uint8_t pitchClass) noexcept {
    constexpr const char* names[] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return names[pitchClass % 12];
}

constexpr NoteSpelling spellScaleDegree(uint8_t rootPitchClass, DiatonicMode mode, uint8_t degree) noexcept {
    constexpr std::array<char, 7> letters{{'C', 'D', 'E', 'F', 'G', 'A', 'B'}};
    constexpr std::array<uint8_t, 7> naturalPitchClasses{{0, 2, 4, 5, 7, 9, 11}};
    constexpr std::array<uint8_t, 12> rootLetters{{0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6}};

    const uint8_t root = rootPitchClass % 12;
    if (!isHeptatonic(mode)) {
        const uint8_t semitone = static_cast<uint8_t>((root + scaleDegreeOffset(mode, degree)) % 12);
        const uint8_t letter = rootLetters[semitone];
        int accidental = static_cast<int>(semitone) - naturalPitchClasses[letter];
        while (accidental > 6) accidental -= 12;
        while (accidental < -6) accidental += 12;

        NoteSpelling spelling{};
        spelling.text[0] = letters[letter];
        const char accidentalCharacter = accidental < 0 ? 'b' : '#';
        const int accidentalCount = accidental < 0 ? -accidental : accidental;
        for (int index = 0; index < accidentalCount && index < 2; ++index)
            spelling.text[static_cast<std::size_t>(index + 1)] = accidentalCharacter;
        return spelling;
    }

    const uint8_t letter = static_cast<uint8_t>((rootLetters[root] + degree % 7) % 7);
    const int target = (root + scaleDegreeOffset(mode, degree)) % 12;
    int accidental = target - naturalPitchClasses[letter];
    while (accidental > 6) accidental -= 12;
    while (accidental < -6) accidental += 12;

    NoteSpelling spelling{};
    spelling.text[0] = letters[letter];
    const char accidentalCharacter = accidental < 0 ? 'b' : '#';
    const int accidentalCount = accidental < 0 ? -accidental : accidental;
    for (int index = 0; index < accidentalCount && index < 2; ++index)
        spelling.text[static_cast<std::size_t>(index + 1)] = accidentalCharacter;
    return spelling;
}

}
