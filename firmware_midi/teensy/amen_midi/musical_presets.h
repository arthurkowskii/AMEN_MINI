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
    GmKit
};

struct PresetDefinition {
    const char* name;
    DiatonicMode scale;
    uint8_t palette;
};

static constexpr uint8_t kMusicalPresetCount = 7;
static constexpr std::array<std::array<uint8_t, kHarmonySlotCount>, 4> kHarmonyPalettes{{
    {{0, 1, 2, 3, 4, 5, 6, 7}},
    {{8, 9, 10, 11, 12, 13, 14, 15}},
    {{0, 1, 2, 16, 17, 4, 13, 11}},
    {{18, 19, 20, 21, 22, 23, 24, 25}},
}};
static constexpr std::array<PresetDefinition, kMusicalPresetCount> kMusicalPresets{{
    {"MAJOR", DiatonicMode::Ionian, 0},
    {"MINOR", DiatonicMode::Aeolian, 0},
    {"HARM MIN", DiatonicMode::HarmonicMinor, 0},
    {"CINEMA", DiatonicMode::Lydian, 1},
    {"DARK", DiatonicMode::Phrygian, 2},
    {"CHROMATIC", DiatonicMode::Chromatic, 3},
    {"GM KIT", DiatonicMode::Chromatic, 0},
}};

constexpr const PresetDefinition& musicalPreset(MusicalPreset preset) noexcept {
    return kMusicalPresets[static_cast<uint8_t>(preset)];
}

constexpr const ChordRecipe* harmonySlotRecipe(MusicalPreset preset, uint8_t slot) noexcept {
    if (static_cast<uint8_t>(preset) >= kMusicalPresetCount || slot >= kHarmonySlotCount) return nullptr;
    static_assert(kHarmonyPalettes.size() <= 4, "palette index must stay inside kHarmonyPalettes");
    const uint8_t palette = musicalPreset(preset).palette;
    if (palette >= kHarmonyPalettes.size()) return nullptr;
    return &kChordRecipes[kHarmonyPalettes[palette][slot]];
}

constexpr const char* harmonySlotName(MusicalPreset preset, uint8_t slot) noexcept {
    const ChordRecipe* recipe = harmonySlotRecipe(preset, slot);
    return recipe ? recipe->name : "";
}

}
