#pragma once

#include "diatonic_scales.h"
#include "gm_drum_kit.h"
#include "harmony_recipes.h"

namespace amen {

enum class MusicalPreset : uint8_t {
    Major,
    Minor,
    HarmonicMinor,
    Cinema,
    Dark,
    Chromatic,
    GmKit,
    PrismMajor,
    PrismMinor,
    Kawaii,
    NoirHarmonicMinor,
    NoirPhrygianDominant,
    NoirMelodicMinor,
    NoirOctatonic,
    NoirWholeTone
};

struct PresetDefinition {
    const char* name;
    DiatonicMode scale;
    uint8_t palette;
};

static constexpr uint8_t kMusicalPresetCount = 15;
static constexpr uint8_t kHarmonyPaletteCount = 11;
static constexpr std::array<std::array<uint8_t, kHarmonySlotCount>, kHarmonyPaletteCount> kHarmonyPalettes{{
    {{0, 1, 2, 3, 4, 5, 6, 7}},
    {{8, 9, 10, 31, 12, 13, 14, 15}},
    {{0, 1, 2, 16, 17, 4, 13, 11}},
    {{18, 19, 20, 21, 22, 23, 24, 25}},
    {{11, 12, 3, 9, 2, 10, 15, 26}},
    {{3, 1, 2, 7, 27, 28, 29, 30}},
    {{1, 2, 30, 0, 34, 33, 26, 17}},
    {{1, 0, 5, 32, 29, 33, 35, 39}},
    {{1, 2, 6, 24, 20, 36, 34, 38}},
    {{1, 2, 0, 16, 13, 19, 20, 39}},
    {{36, 35, 22, 11, 12, 38, 19, 39}},
}};
static constexpr std::array<PresetDefinition, kMusicalPresetCount> kMusicalPresets{{
    {"MAJOR", DiatonicMode::Ionian, 0},
    {"MINOR", DiatonicMode::Aeolian, 0},
    {"HARM MIN", DiatonicMode::HarmonicMinor, 0},
    {"CINEMA", DiatonicMode::Lydian, 1},
    {"DARK", DiatonicMode::Phrygian, 2},
    {"CHROMATIC", DiatonicMode::Chromatic, 3},
    {"GM KIT", DiatonicMode::Chromatic, 0},
    {"PRISM MAJ", DiatonicMode::Ionian, 4},
    {"PRISM MIN", DiatonicMode::Aeolian, 4},
    {"KAWAII", DiatonicMode::Ionian, 5},
    {"NOIR HM", DiatonicMode::HarmonicMinor, 6},
    {"NOIR HM5", DiatonicMode::PhrygianDominant, 7},
    {"NOIR JZ", DiatonicMode::MelodicMinor, 8},
    {"NOIR DIM", DiatonicMode::Octatonic, 9},
    {"NOIR WT", DiatonicMode::WholeTone, 10},
}};

constexpr const PresetDefinition& musicalPreset(MusicalPreset preset) noexcept {
    return kMusicalPresets[static_cast<uint8_t>(preset)];
}

constexpr const ChordRecipe* harmonySlotRecipe(MusicalPreset preset, uint8_t slot) noexcept {
    if (static_cast<uint8_t>(preset) >= kMusicalPresetCount || slot >= kHarmonySlotCount) return nullptr;
    const uint8_t palette = musicalPreset(preset).palette;
    if (palette >= kHarmonyPaletteCount) return nullptr;
    return &kChordRecipes[kHarmonyPalettes[palette][slot]];
}

constexpr const char* harmonySlotName(MusicalPreset preset, uint8_t slot) noexcept {
    const ChordRecipe* recipe = harmonySlotRecipe(preset, slot);
    return recipe ? recipe->name : "";
}

}
