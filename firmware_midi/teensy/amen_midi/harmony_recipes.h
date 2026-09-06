#pragma once

#include <array>
#include <cstdint>

namespace amen {

struct ChordRecipe {
    const char* name;
    uint8_t voiceCount;
    std::array<uint8_t, 5> degrees;
    std::array<int8_t, 5> octaveDisplacements;
};

static constexpr uint8_t kHarmonySlotCount = 8;
static constexpr uint8_t kMaxRecipeVoices = 5;

static constexpr std::array<ChordRecipe, 26> kChordRecipes{{
    {"TRIAD", 3, {{0, 2, 4}}, {}},
    {"SEVENTH", 4, {{0, 2, 4, 6}}, {}},
    {"NINTH", 5, {{0, 2, 4, 6, 8}}, {}},
    {"ADD9", 4, {{0, 2, 4, 8}}, {}},
    {"SUS2", 3, {{0, 1, 4}}, {}},
    {"SUS4", 3, {{0, 3, 4}}, {}},
    {"SIXTH", 4, {{0, 2, 4, 5}}, {}},
    {"SIX9", 5, {{0, 2, 4, 5, 8}}, {}},
    {"OPEN TRIAD", 3, {{0, 4, 2}}, {{0, 0, 12}}},
    {"OPEN7", 4, {{0, 4, 2, 6}}, {{0, 0, 12, 0}}},
    {"OPEN9", 5, {{0, 4, 6, 8, 2}}, {{0, 0, 0, 0, 12}}},
    {"FIFTH", 2, {{0, 4}}, {}},
    {"SUS9", 3, {{0, 4, 8}}, {}},
    {"QUARTAL", 4, {{0, 3, 6, 9}}, {}},
    {"QUINTAL", 5, {{0, 4, 8, 12, 16}}, {}},
    {"OPEN69", 5, {{0, 4, 5, 8, 2}}, {{0, 0, 0, 0, 12}}},
    {"ADD2", 4, {{0, 1, 2, 4}}, {}},
    {"CLUSTER", 3, {{0, 1, 2}}, {}},
    {"TRIAD", 3, {{0, 4, 7}}, {}},
    {"SEVENTH", 4, {{0, 4, 7, 11}}, {}},
    {"NINTH", 5, {{0, 4, 7, 11, 14}}, {}},
    {"ADD9", 4, {{0, 4, 7, 14}}, {}},
    {"SUS2", 3, {{0, 2, 7}}, {}},
    {"SUS4", 3, {{0, 5, 7}}, {}},
    {"SIXTH", 4, {{0, 4, 7, 9}}, {}},
    {"SIX9", 5, {{0, 4, 7, 9, 14}}, {}},
}};

}
